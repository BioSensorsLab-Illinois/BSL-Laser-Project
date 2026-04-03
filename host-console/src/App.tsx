import { startTransition, useEffect, useMemo, useState } from 'react'
import { AnimatePresence, motion } from 'framer-motion'
import {
  Activity,
  Box,
  Cable,
  ChevronDown,
  ChevronUp,
  Command,
  Cpu,
  FlaskConical,
  Shield,
  Wifi,
  Zap,
} from 'lucide-react'

import { BringupWorkbench } from './components/BringupWorkbench'
import { BusTrafficViewer } from './components/BusTrafficViewer'
import { CommandDeck } from './components/CommandDeck'
import { ControlWorkbench } from './components/ControlWorkbench'
import { EventTimeline } from './components/EventTimeline'
import { FirmwareWorkbench } from './components/FirmwareWorkbench'
import { InspectorRail } from './components/InspectorRail'
import { ModuleReadinessPanel } from './components/ModuleReadinessPanel'
import { ProgressMeter } from './components/ProgressMeter'
import { SafetyMatrix } from './components/SafetyMatrix'
import { StatusRail } from './components/StatusRail'
import { deriveBenchEstimate } from './lib/bench-model'
import { parseFirmwareFile } from './lib/firmware'
import { formatNumber, formatRelativeUptime } from './lib/format'
import { useDeviceSession } from './hooks/use-device-session'
import { useGlobalHoverHelp } from './hooks/use-global-hover-help'
import {
  buildSafetyChecks,
  formatEnumLabel,
  formatTofValidityLabel,
  formatTofWindowSummary,
  getTofDisplayDistanceM,
  summarizeSafetyChecks,
  toneFromSystemState,
} from './lib/presentation'
import {
  controllerBenchAp,
  preferredWirelessUrl,
  sortWirelessScanNetworks,
  transportLabel,
  wirelessConnectionStateLabel,
  wirelessModeLabel,
  wirelessSignalLabel,
  wirelessSignalPercent,
} from './lib/wireless'
import type {
  FirmwarePackageDescriptor,
  Severity,
  ThemeMode,
  TransportKind,
  WirelessStatus,
} from './types'

const HOST_THEME_STORAGE_KEY = 'bsl-host-theme'

type AppView = 'overview' | 'control' | 'bringup' | 'events' | 'firmware' | 'service'
type EventLogView = 'system' | 'comms'
type WirelessAction = 'scan' | 'join' | 'restore'
type WirelessFeedbackTone = 'steady' | 'warning' | 'critical'

const navItems: Array<{
  id: AppView
  label: string
  icon: typeof Shield
  detail: string
}> = [
  {
    id: 'overview',
    label: 'Overview',
    icon: Shield,
    detail: 'State, rails, interlocks',
  },
  {
    id: 'control',
    label: 'Control',
    icon: FlaskConical,
    detail: 'Laser, TEC, PWM',
  },
  {
    id: 'bringup',
    label: 'Bring-up',
    icon: Cpu,
    detail: 'Modules and probes',
  },
  {
    id: 'events',
    label: 'Events',
    icon: Activity,
    detail: 'Logs and faults',
  },
  {
    id: 'firmware',
    label: 'Firmware',
    icon: Box,
    detail: 'Load and update',
  },
  {
    id: 'service',
    label: 'Tools',
    icon: Command,
    detail: 'Bus lab and utility',
  },
]

function connectStepsForTransport(
  kind: TransportKind,
  wireless: WirelessStatus,
): string[] {
  if (kind === 'wifi') {
    if (wireless.mode === 'station' || wireless.stationConfigured) {
      return [
        'Use Web Serial when staging controller network changes. Switching Wi-Fi mode can drop the current wireless socket immediately.',
        wireless.stationConnected
          ? `Controller joined existing Wi-Fi SSID ${wireless.ssid}. Keep the laptop on that same network and connect to ${preferredWirelessUrl(wireless)}.`
          : wireless.stationConfigured
            ? `Controller is configured for existing Wi-Fi SSID ${wireless.stationSsid}. Power it, let DHCP complete, then use the reported controller URL. Only 2.4 GHz Wi-Fi is supported.`
            : 'Enter an SSID and password in the wireless settings panel to move the controller onto your existing network.',
        'Station mode keeps the laptop on normal internet while the ESP32 publishes the same bench WebSocket protocol.',
        'Only 2.4 GHz Wi-Fi networks are supported by the ESP32 radio. 5 GHz SSIDs will not work.',
        'If station association fails, reconnect over Web Serial and restore the bench AP.',
        'Firmware flashing still requires Web Serial.',
      ]
    }

    return [
      `Power the controller from USB-PD, then join Wi-Fi SSID ${controllerBenchAp.ssid}.`,
      `Use password ${controllerBenchAp.password}. Keep this host GUI running locally on the laptop.`,
      `Choose Wireless and connect to ${controllerBenchAp.wsUrl}.`,
      'Wireless uses the same controller JSON protocol as USB, including service-mode gating and logs.',
      'The laptop will usually lose normal internet while joined to the controller AP. That is expected during bench operation.',
      'Wireless is for monitoring and bench control. Firmware flashing still requires Web Serial.',
    ]
  }

  if (kind === 'serial') {
    return [
      'Open in Chrome or Edge.',
      'Plug in the ESP32-S3 and choose the USB serial device.',
      'Enter service mode from Bring-up or Firmware. Do not press RST for service mode.',
      'Firmware can flash a raw app binary over Web Serial; use BOOT + RST only if the browser reset sequence misses the bootloader.',
      'After page refresh or board reboot, the app will try to reopen the last approved serial port automatically.',
      'If you press RST or BOOT + RST, the USB link will drop. Click Connect board after the port comes back.',
    ]
  }

  return [
    'Mock rig auto-connects.',
    'Use Bring-up first, then Control.',
    'Switch to Web Serial only with a terminated bench unit attached.',
  ]
}

function readStoredTheme(): ThemeMode {
  if (typeof window === 'undefined') {
    return 'dark'
  }

  const stored = window.localStorage.getItem(HOST_THEME_STORAGE_KEY)
  return stored === 'light' ? 'light' : 'dark'
}

function App() {
  const [activeView, setActiveView] = useState<AppView>('overview')
  const [eventQuery, setEventQuery] = useState('')
  const [eventSeverity, setEventSeverity] = useState<Severity | 'all'>('all')
  const [eventModuleFilter, setEventModuleFilter] = useState('all')
  const [eventLogView, setEventLogView] = useState<EventLogView>('system')
  const [packageDescriptor, setPackageDescriptor] = useState<FirmwarePackageDescriptor | null>(null)
  const [themeMode, setThemeMode] = useState<ThemeMode>(() => readStoredTheme())
  const [wirelessStationSsidDraft, setWirelessStationSsidDraft] = useState('')
  const [wirelessStationSsidDraftTouched, setWirelessStationSsidDraftTouched] = useState(false)
  const [wirelessStationPasswordDraft, setWirelessStationPasswordDraft] = useState('')
  const [wirelessReuseSavedPassword, setWirelessReuseSavedPassword] = useState(false)
  const [wirelessActionPending, setWirelessActionPending] = useState<WirelessAction | null>(null)
  const [wirelessActionStartedAtMs, setWirelessActionStartedAtMs] = useState<number | null>(null)
  const [wirelessActionError, setWirelessActionError] = useState<string | null>(null)
  const [wirelessPanelExpanded, setWirelessPanelExpanded] = useState(false)

  const {
    transportKind,
    wifiUrl,
    setWifiUrl,
    setTransportKind,
    transportStatus,
    transportDetail,
    transportRecovering,
    snapshot,
    telemetryStore,
    events,
    commands,
    firmwareProgress,
    sessionAutosave,
    supportsFirmwareTransfer,
    connect,
    disconnect,
    issueCommand,
    issueCommandAwaitAck,
    beginFirmwareTransfer,
    exportSession,
    clearSessionHistory,
    configureSessionAutosave,
    disableSessionAutosave,
  } = useDeviceSession()
  useGlobalHoverHelp()

  const stateTone = toneFromSystemState(snapshot.session.state)
  const benchEstimate = useMemo(() => deriveBenchEstimate(snapshot), [snapshot])
  const safetyChecks = useMemo(() => buildSafetyChecks(snapshot), [snapshot])
  const safetySummary = useMemo(() => summarizeSafetyChecks(safetyChecks), [safetyChecks])
  const tofDisplayDistance = useMemo(() => getTofDisplayDistanceM(snapshot), [snapshot])
  const heroKpis = useMemo(
    () => [
      {
        label: 'Controller state',
        value: formatEnumLabel(snapshot.session.state),
        detail: `uptime ${formatRelativeUptime(snapshot.session.uptimeSeconds)}`,
        tone: stateTone,
      },
      {
        label: 'Power posture',
        value: `${formatNumber(snapshot.pd.negotiatedPowerW, 1)} W`,
        detail: `${formatEnumLabel(snapshot.session.powerTier)} • ${formatNumber(benchEstimate.totalEstimatedInputPowerW, 1)} W draw`,
        tone:
          snapshot.session.powerTier === 'full'
            ? 'steady'
            : snapshot.session.powerTier === 'reduced'
              ? 'warning'
              : 'critical',
      },
      {
        label: 'Lambda lock',
        value: `${formatNumber(snapshot.tec.targetLambdaNm, 1)} nm`,
        detail: `${formatNumber(snapshot.tec.tempC, 1)} °C live / ${snapshot.tec.tempGood ? 'settled' : 'settling'}`,
        tone: snapshot.tec.tempGood ? 'steady' : 'warning',
      },
      {
        label: 'Optical output',
        value: `${formatNumber(benchEstimate.averageOpticalPowerW, 2)} W`,
        detail: snapshot.laser.nirEnabled ? 'beam active under gate' : 'output path safe',
        tone: snapshot.laser.nirEnabled ? 'critical' : snapshot.laser.alignmentEnabled ? 'warning' : 'steady',
      },
    ],
    [benchEstimate, snapshot, stateTone],
  )
  const scannedWirelessNetworks = useMemo(
    () => sortWirelessScanNetworks(snapshot.wireless.scannedNetworks),
    [snapshot.wireless.scannedNetworks],
  )
  const displayedWirelessStationSsidDraft =
    wirelessStationSsidDraftTouched
      ? wirelessStationSsidDraft
      : snapshot.wireless.stationSsid
  const selectedScannedWirelessNetwork = useMemo(
    () =>
      scannedWirelessNetworks.find(
        (network) => network.ssid === displayedWirelessStationSsidDraft.trim(),
      ) ?? null,
    [displayedWirelessStationSsidDraft, scannedWirelessNetworks],
  )
  const requestedStationSsid = displayedWirelessStationSsidDraft.trim()
  const savedStationSsid = snapshot.wireless.stationSsid.trim()
  const reusingSavedStationSsid =
    requestedStationSsid.length > 0 &&
    savedStationSsid.length > 0 &&
    requestedStationSsid === savedStationSsid
  const canReuseSavedPassword =
    snapshot.wireless.stationConfigured && reusingSavedStationSsid
  const wirelessSignalLevel = useMemo(
    () =>
      snapshot.wireless.mode === 'station'
        ? wirelessSignalPercent(snapshot.wireless.stationRssiDbm)
        : snapshot.wireless.apReady
          ? 100
          : 0,
    [
      snapshot.wireless.apReady,
      snapshot.wireless.mode,
      snapshot.wireless.stationRssiDbm,
    ],
  )
  const wirelessSignalTone = wirelessSignalLevel >= 70
    ? 'steady'
    : wirelessSignalLevel >= 35
      ? 'warning'
      : 'critical'
  const wirelessStatusCards = useMemo(
    () => [
      {
        label: 'Mode',
        value: wirelessModeLabel(snapshot.wireless),
        detail: wirelessConnectionStateLabel(snapshot.wireless),
      },
      {
        label: 'Endpoint',
        value: snapshot.wireless.ipAddress || 'Awaiting IP',
        detail: preferredWirelessUrl(snapshot.wireless),
      },
      {
        label: 'Signal',
        value: wirelessSignalLabel(snapshot.wireless),
        detail:
          snapshot.wireless.mode === 'station'
            ? snapshot.wireless.stationConnected
              ? `${snapshot.wireless.ssid} · channel ${snapshot.wireless.stationChannel || '?'}`
              : snapshot.wireless.lastError || 'No active station link yet'
            : `${snapshot.wireless.ssid} · ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}`,
      },
      {
        label: 'Nearby SSIDs',
        value: snapshot.wireless.scanInProgress
          ? 'Scanning…'
          : `${scannedWirelessNetworks.length} found`,
        detail:
          scannedWirelessNetworks.length > 0
            ? scannedWirelessNetworks
              .slice(0, 2)
              .map((network) => network.ssid)
              .join(' · ')
            : 'Run a controller Wi-Fi scan from the active link.',
      },
    ],
    [
      scannedWirelessNetworks,
      snapshot.wireless,
    ],
  )
  const wirelessPanelSummary = useMemo(() => {
    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConnected) {
      return {
        label: 'Joined existing Wi-Fi',
        detail: `${snapshot.wireless.ssid} • ${snapshot.wireless.ipAddress || 'Awaiting IP'}`,
      }
    }

    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConfigured) {
      return {
        label: 'Station saved',
        detail: `${snapshot.wireless.stationSsid || 'Saved SSID'} • bench AP still available`,
      }
    }

    return {
      label: 'Bench AP ready',
      detail: `${controllerBenchAp.ssid} • ${controllerBenchAp.wsUrl}`,
    }
  }, [snapshot.wireless])
  useEffect(() => {
    if (
      snapshot.wireless.mode === 'station' &&
      snapshot.wireless.stationConnected &&
      snapshot.wireless.wsUrl.trim().length > 0 &&
      wifiUrl.trim().length === 0
    ) {
      setWifiUrl(snapshot.wireless.wsUrl)
    }
  }, [
    snapshot.wireless.mode,
    snapshot.wireless.stationConnected,
    snapshot.wireless.wsUrl,
    setWifiUrl,
    wifiUrl,
  ])

  useEffect(() => {
    if (wirelessActionPending === null) {
      return
    }

    if (
      wirelessActionPending === 'scan' &&
      wirelessActionStartedAtMs !== null &&
      window.performance.now() - wirelessActionStartedAtMs > 1200 &&
      !snapshot.wireless.scanInProgress
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      return
    }

    if (wirelessActionPending === 'join' && snapshot.wireless.stationConnected) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(null)
      return
    }

    if (
      wirelessActionPending === 'restore' &&
      snapshot.wireless.mode === 'softap' &&
      snapshot.wireless.apReady
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(null)
      return
    }

    if (
      wirelessActionPending === 'join' &&
      wirelessActionStartedAtMs !== null &&
      window.performance.now() - wirelessActionStartedAtMs > 1400 &&
      snapshot.wireless.mode === 'station' &&
      !snapshot.wireless.stationConnected &&
      !snapshot.wireless.stationConnecting &&
      snapshot.wireless.lastError.trim().length > 0
    ) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(snapshot.wireless.lastError)
    }
  }, [
    snapshot.wireless.apReady,
    snapshot.wireless.lastError,
    snapshot.wireless.mode,
    snapshot.wireless.scanInProgress,
    snapshot.wireless.stationConnected,
    snapshot.wireless.stationConnecting,
    wirelessActionPending,
    wirelessActionStartedAtMs,
  ])
  useEffect(() => {
    if (!(activeView === 'overview' && (transportKind === 'serial' || transportKind === 'wifi'))) {
      setWirelessPanelExpanded(false)
    }
  }, [activeView, transportKind])

  const sessionFacts = useMemo(
    () => [
      {
        label: 'Firmware build',
        value: snapshot.identity.firmwareVersion,
        detail: `Protocol ${snapshot.identity.protocolVersion}`,
      },
      {
        label: 'Hardware rev',
        value: snapshot.identity.hardwareRevision,
        detail: `Serial ${snapshot.identity.serialNumber}`,
      },
      {
        label: 'Boot reason',
        value: formatEnumLabel(snapshot.session.bootReason),
        detail: `Uptime ${formatRelativeUptime(snapshot.session.uptimeSeconds)}`,
      },
      {
        label: 'Power source',
        value: snapshot.pd.sourceIsHostOnly ? 'USB host only' : formatEnumLabel(snapshot.session.powerTier),
        detail: `${formatNumber(snapshot.pd.sourceVoltageV, 1)} V · ${formatNumber(snapshot.pd.sourceCurrentA, 2)} A`,
      },
      {
        label: 'Controller network',
        value:
          snapshot.wireless.mode === 'station'
            ? snapshot.wireless.stationConnected
              ? snapshot.wireless.ssid
              : snapshot.wireless.stationConfigured
                ? snapshot.wireless.stationSsid
                : 'Station unset'
            : snapshot.wireless.apReady
              ? snapshot.wireless.ssid
              : 'Bench AP idle',
        detail:
          snapshot.wireless.mode === 'station'
            ? snapshot.wireless.stationConnected
              ? `${snapshot.wireless.ipAddress} · ${snapshot.wireless.wsUrl}`
              : snapshot.wireless.lastError || 'Waiting for station association'
            : snapshot.wireless.apReady
              ? `${snapshot.wireless.wsUrl} · ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}`
              : snapshot.wireless.lastError || 'SoftAP not advertising yet',
      },
    ],
    [
      snapshot.identity.firmwareVersion,
      snapshot.identity.hardwareRevision,
      snapshot.identity.protocolVersion,
      snapshot.identity.serialNumber,
      snapshot.pd.sourceCurrentA,
      snapshot.pd.sourceIsHostOnly,
      snapshot.pd.sourceVoltageV,
      snapshot.session.bootReason,
      snapshot.session.powerTier,
      snapshot.session.uptimeSeconds,
      snapshot.wireless.apReady,
      snapshot.wireless.clientCount,
      snapshot.wireless.ipAddress,
      snapshot.wireless.lastError,
      snapshot.wireless.mode,
      snapshot.wireless.ssid,
      snapshot.wireless.stationConfigured,
      snapshot.wireless.stationConnected,
      snapshot.wireless.stationSsid,
      snapshot.wireless.wsUrl,
    ],
  )
  const workflowSteps = useMemo(
    () => [
      {
        label: 'Link live',
        pass: transportStatus === 'connected',
        detail:
          transportStatus === 'connected'
            ? transportKind === 'mock'
              ? 'Mock rig active'
              : transportKind === 'wifi'
                ? 'Wireless controller link active'
                : 'Web Serial attached'
            : 'Start or connect the selected transport',
      },
      {
        label: 'Outputs safe',
        pass: !snapshot.laser.nirEnabled && !snapshot.laser.alignmentEnabled,
        detail:
          !snapshot.laser.nirEnabled && !snapshot.laser.alignmentEnabled
            ? 'No optical output requested'
            : snapshot.laser.nirEnabled
              ? 'NIR laser request is active'
              : 'Green alignment laser is active',
      },
      {
        label: 'Fault state',
        pass: !snapshot.fault.latched,
        detail: snapshot.fault.latched ? snapshot.fault.activeCode : 'No latched fault',
      },
      {
        label: 'Gate health',
        pass: safetySummary.percent >= 75,
        detail: `${safetySummary.passCount}/${safetySummary.total} interlocks passing`,
      },
    ],
    [safetySummary, snapshot.fault.activeCode, snapshot.fault.latched, snapshot.laser.alignmentEnabled, snapshot.laser.nirEnabled, transportKind, transportStatus],
  )

  async function handlePickFirmware(file: File) {
    const parsed = await parseFirmwareFile(file)
    setPackageDescriptor(parsed)
  }

  async function handlePrepareDevice() {
    if (snapshot.session.state !== 'SERVICE_MODE') {
      await issueCommand(
        'enter_service_mode',
        'service',
        'Preparing controller for maintenance or firmware work.',
      )
      return
    }

    await issueCommand('get_status', 'read', 'Refreshing state after service-mode request.')
  }

  async function handleBeginTransfer() {
    if (packageDescriptor === null) {
      return
    }

    await beginFirmwareTransfer(packageDescriptor)
  }

  const connectSteps = connectStepsForTransport(transportKind, snapshot.wireless)
  const connectionHealthy = transportStatus === 'connected'
  const deviceLinkLost = !connectionHealthy
  const showControllerWirelessPanel =
    activeView === 'overview' && (transportKind === 'serial' || transportKind === 'wifi')
  const controllerWirelessPanelExpanded =
    showControllerWirelessPanel &&
    (wirelessPanelExpanded ||
      wirelessActionPending !== null ||
      (wirelessActionError !== null && wirelessActionError.trim().length > 0))
  const canConfigureControllerWireless = transportStatus === 'connected'
  const wirelessFeedback = useMemo<{
    tone: WirelessFeedbackTone
    label: string
    detail: string
  }>(() => {
    if (wirelessActionError !== null && wirelessActionError.trim().length > 0) {
      return {
        tone: 'critical',
        label: 'Controller Wi-Fi update failed',
        detail: wirelessActionError,
      }
    }

    if (wirelessActionPending === 'scan' || snapshot.wireless.scanInProgress) {
      return {
        tone: 'warning',
        label: 'Scanning nearby Wi-Fi',
        detail: 'Controller radio is scanning nearby 2.4 GHz Wi-Fi networks. Results will appear below when the scan completes.',
      }
    }

    if (wirelessActionPending === 'join') {
      if (snapshot.wireless.stationConnected) {
        return {
          tone: 'steady',
          label: 'Joined existing Wi-Fi',
          detail: snapshot.wireless.apReady
            ? `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress || 'the reported IP address'}. Bench AP stays online for management, so you do not need to abandon ${controllerBenchAp.ssid} to change networks later.`
            : `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress || 'the reported IP address'} and is ready for wireless reconnection.`,
        }
      }

      if (snapshot.wireless.stationConnecting) {
        return {
          tone: 'warning',
          label: 'Joining existing Wi-Fi',
          detail: `Controller is associating with ${snapshot.wireless.stationSsid || displayedWirelessStationSsidDraft || 'the requested SSID'}. Only 2.4 GHz Wi-Fi is supported.`,
        }
      }

      return {
        tone: 'warning',
        label: 'Waiting for controller network join',
        detail: `Station credentials were sent. Keep USB connected while the controller joins ${snapshot.wireless.stationSsid || displayedWirelessStationSsidDraft || 'the requested SSID'} and waits for DHCP.`,
      }
    }

    if (wirelessActionPending === 'restore') {
      if (snapshot.wireless.mode === 'softap' && snapshot.wireless.apReady) {
        return {
          tone: 'steady',
          label: 'Bench AP restored',
          detail: `${snapshot.wireless.ssid} is back online at ${preferredWirelessUrl(snapshot.wireless)}.`,
        }
      }

      return {
        tone: 'warning',
        label: 'Restoring bench AP',
        detail: 'Controller is switching back to its bench access point. The wireless socket may drop briefly during the transition.',
      }
    }

    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConnected) {
      return {
        tone: 'steady',
        label: 'Existing Wi-Fi linked',
        detail: snapshot.wireless.apReady
          ? `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}. Bench AP ${controllerBenchAp.ssid} stays up for stable management and future network changes.`
          : `Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}. Only 2.4 GHz Wi-Fi is supported.`,
      }
    }

    if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConfigured) {
      return {
        tone: 'warning',
        label: 'Existing Wi-Fi configured',
        detail: `Controller is staged for ${snapshot.wireless.stationSsid || 'the saved SSID'} but is not connected yet. Only 2.4 GHz Wi-Fi is supported.`,
      }
    }

    if (snapshot.wireless.mode === 'softap' && snapshot.wireless.apReady) {
      return {
        tone: 'steady',
        label: 'Bench AP ready',
        detail: `${snapshot.wireless.ssid} is online at ${preferredWirelessUrl(snapshot.wireless)} for local bench access.`,
      }
    }

    return {
      tone: 'warning',
      label: 'Wireless ready for setup',
      detail: 'Use Web Serial to stage controller Wi-Fi changes safely. The ESP32 radio supports 2.4 GHz Wi-Fi only.',
    }
  }, [displayedWirelessStationSsidDraft, snapshot.wireless, wirelessActionError, wirelessActionPending])
  const serviceModeLabel = snapshot.bringup.serviceModeActive
    ? 'Service active'
    : snapshot.bringup.serviceModeRequested
      ? 'Service requesting'
      : 'Service off'
  const compactConnectionDetail =
    transportKind === 'mock'
      ? 'Mock rig linked. Use the right rail for transport and service controls.'
      : transportKind === 'wifi'
        ? snapshot.wireless.mode === 'station'
          ? snapshot.wireless.stationConnected
            ? `Wireless link active. Controller joined ${snapshot.wireless.ssid} at ${snapshot.wireless.wsUrl}, so the laptop can stay on normal internet.`
            : `Wireless transport selected. Controller is trying to join ${snapshot.wireless.stationSsid || 'the configured Wi-Fi network'}.`
          : snapshot.wireless.apReady
            ? `Wireless link active. Bench AP ${snapshot.wireless.ssid} is up at ${snapshot.wireless.wsUrl} with ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}.`
            : 'Wireless link active. USB can stay dedicated to PD power while the laptop talks over the controller AP.'
        : 'Web Serial linked. Use the right rail for reconnect, disconnect, and service mode.'

  async function handleConfigureControllerStationMode() {
    setWirelessActionError(null)
    const nextStationSsid = displayedWirelessStationSsidDraft.trim()

    if (nextStationSsid.length === 0) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError('Enter the target Wi-Fi SSID before asking the controller to join.')
      return {
        ok: false,
        note: 'Enter the target Wi-Fi SSID before asking the controller to join.',
      }
    }

    setWirelessActionPending('join')
    setWirelessActionStartedAtMs(window.performance.now())
    const args: Record<string, string> = { mode: 'station' }
    args.ssid = nextStationSsid

    if (!(wirelessReuseSavedPassword && canReuseSavedPassword)) {
      args.password = wirelessStationPasswordDraft
    }

    const result = await issueCommandAwaitAck(
      'configure_wireless',
      'write',
      'Configure the controller to join an existing Wi-Fi network.',
      args,
      { timeoutMs: 5000 },
    )

    if (!result.ok) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(result.note)
      return result
    }

    if (transportKind === 'serial' && transportStatus === 'connected') {
      void issueCommandAwaitAck(
        'get_status',
        'read',
        'Refresh controller wireless status after staging station-mode credentials.',
        undefined,
        {
          logHistory: false,
          timeoutMs: 2200,
        },
      )
    }

    return result
  }

  async function handleRestoreControllerBenchAp() {
    setWirelessActionError(null)
    setWirelessActionPending('restore')
    setWirelessActionStartedAtMs(window.performance.now())
    const result = await issueCommandAwaitAck(
      'configure_wireless',
      'write',
      'Restore the controller bench SoftAP network.',
      { mode: 'softap' },
      { timeoutMs: 5000 },
    )

    if (result.ok) {
      setWifiUrl(controllerBenchAp.wsUrl)
      if (transportKind === 'serial' && transportStatus === 'connected') {
        void issueCommandAwaitAck(
          'get_status',
          'read',
          'Refresh controller wireless status after restoring the bench access point.',
          undefined,
          {
            logHistory: false,
            timeoutMs: 2200,
          },
        )
      }
      return
    }

    setWirelessActionPending(null)
    setWirelessActionStartedAtMs(null)
    setWirelessActionError(result.note)
  }

  async function handleScanControllerWirelessNetworks() {
    setWirelessActionError(null)
    setWirelessActionPending('scan')
    setWirelessActionStartedAtMs(window.performance.now())
    const result = await issueCommandAwaitAck(
      'scan_wireless_networks',
      'read',
      'Ask the controller to scan nearby Wi-Fi networks for station-mode setup.',
      undefined,
      { timeoutMs: 9000 },
    )

    if (!result.ok) {
      setWirelessActionPending(null)
      setWirelessActionStartedAtMs(null)
      setWirelessActionError(result.note)
    }
  }

  async function handleToggleServiceMode() {
    if (transportStatus !== 'connected') {
      return
    }

    await issueCommand(
      snapshot.bringup.serviceModeActive ? 'exit_service_mode' : 'enter_service_mode',
      'service',
      snapshot.bringup.serviceModeActive
        ? 'Leaving guarded write session.'
        : 'Entering guarded write session with outputs held safe.',
    )
  }

  async function handleSetInterlocksDisabled(enabled: boolean) {
    if (transportStatus !== 'connected') {
      return
    }

    await issueCommandAwaitAck(
      'set_interlocks_disabled',
      'service',
      enabled
        ? 'Disabling all beam interlocks under explicit bench override.'
        : 'Restoring normal controller interlock supervision.',
      { enabled },
      { timeoutMs: 3200 },
    )
  }

  useEffect(() => {
    document.documentElement.dataset.theme = themeMode
    window.localStorage.setItem(HOST_THEME_STORAGE_KEY, themeMode)
  }, [themeMode])

  return (
    <div className={deviceLinkLost ? 'app-shell is-offline' : 'app-shell'}>
      <aside className={deviceLinkLost ? 'sidebar offline-dim' : 'sidebar'}>
        <div className="sidebar__brand">
          <div className="brand-mark">
            <Shield size={18} />
          </div>
          <div>
            <p className="eyebrow">BioSensors Lab @ UIUC</p>
            <h1>BSL-HTLS Gen2</h1>
          </div>
        </div>

        <p className="sidebar__lede">
          770-790 nm, 5 W tunable handheld laser for F-IGS.
        </p>

        <nav className="nav">
          {navItems.map((item) => {
            const Icon = item.icon
            return (
              <button
                key={item.id}
                type="button"
                className={activeView === item.id ? 'nav-link is-active' : 'nav-link'}
                onClick={() => startTransition(() => setActiveView(item.id))}
              >
                <Icon size={16} />
                <span>{item.label}</span>
                <small>{item.detail}</small>
              </button>
            )
          })}
        </nav>

        <div className="sidebar__footer">
          <div className="segmented">
            <button
              type="button"
              className={themeMode === 'dark' ? 'segmented__button is-active' : 'segmented__button'}
              onClick={() => setThemeMode('dark')}
            >
              Dark
            </button>
            <button
              type="button"
              className={themeMode === 'light' ? 'segmented__button is-active' : 'segmented__button'}
              onClick={() => setThemeMode('light')}
            >
              Light
            </button>
          </div>
          <div className={`state-pill is-${stateTone}`}>
            <Zap size={14} />
            <span>{snapshot.session.state.replaceAll('_', ' ')}</span>
          </div>
          <p>Launcher: <code>./start-host-console.command</code></p>
        </div>
      </aside>

      <main className="workspace">
        {deviceLinkLost ? (
          <section className="offline-banner">
            <div>
              <p className="eyebrow">Controller link lost</p>
              <strong>
                {transportStatus === 'connecting'
                  ? 'Waiting for the ESP32 protocol handshake.'
                  : 'Live telemetry and bench controls are muted until the ESP32 reconnects.'}
              </strong>
            </div>
            <span className={`transport-chip is-${transportStatus}`}>
              <Cable size={14} />
              <span>{formatEnumLabel(transportStatus)}</span>
            </span>
          </section>
        ) : null}

        <header className={`hero-panel tone-${stateTone}`}>
          <div className={deviceLinkLost ? 'hero-panel__main offline-dim' : 'hero-panel__main'}>
            <motion.div
              initial={{ opacity: 0, y: 16 }}
              animate={{ opacity: 1, y: 0 }}
              transition={{ duration: 0.4 }}
            >
              <p className="eyebrow">Live session</p>
              <h2>{snapshot.identity.label}</h2>
              <p className="hero-copy">
                770-790 nm, 5 W tunable handheld laser for F-IGS. Developed by BioSensors Lab @ UIUC.
              </p>
            </motion.div>

            <div className="hero-summary" data-tone={safetySummary.tone}>
              <div className="hero-summary__head">
                <div>
                  <p className="eyebrow">Readiness</p>
                  <strong>{safetySummary.label}</strong>
                </div>
                <div className={`state-pill is-${safetySummary.tone}`}>
                  <span>{safetySummary.percent}% ready</span>
                </div>
              </div>
              <ProgressMeter value={safetySummary.percent} tone={safetySummary.tone} />
              <div className="hero-workflow">
                {workflowSteps.map((step) => (
                  <div
                    key={step.label}
                    className={step.pass ? 'hero-workflow__step is-pass' : 'hero-workflow__step is-fail'}
                  >
                    <span>{step.pass ? 'OK' : 'HOLD'}</span>
                    <div>
                      <strong>{step.label}</strong>
                      <small>{step.detail}</small>
                    </div>
                  </div>
                ))}
              </div>
            </div>

            <div className="hero-kpis hero-kpis--grid">
              {heroKpis.map((kpi) => (
                <div key={kpi.label} className="hero-kpi" data-tone={kpi.tone}>
                  <span>{kpi.label}</span>
                  <strong>{kpi.value}</strong>
                  <small>{kpi.detail}</small>
                </div>
              ))}
            </div>
          </div>

          <div className="hero-panel__side">
            <div className={connectionHealthy ? 'hero-connect is-compact' : 'hero-connect'} data-link-surface="active">
              <div className="cutout-head">
                <div>
                  <p className="eyebrow">Connection</p>
                  <strong>{connectionHealthy ? 'Link healthy' : 'Connect fast'}</strong>
                </div>
                <div className={`transport-chip is-${transportStatus}`}>
                  <Cable size={14} />
                  <span>{formatEnumLabel(transportStatus)}</span>
                </div>
              </div>

              {connectionHealthy ? (
                <div className="hero-connect__compact">
                  <div className="hero-connect__status-row">
                    <span className="inline-token">
                      {transportLabel(transportKind)}
                    </span>
                    <span
                      className={
                        snapshot.bringup.serviceModeActive
                          ? 'status-badge is-on'
                          : snapshot.bringup.serviceModeRequested
                            ? 'status-badge is-warn'
                            : 'status-badge is-off'
                      }
                    >
                      {serviceModeLabel}
                    </span>
                  </div>
                  <p className="inline-help">{compactConnectionDetail}</p>
                </div>
              ) : (
                <>
                  <div className="segmented">
                    <button
                      type="button"
                      className={transportKind === 'mock' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => {
                        void setTransportKind('mock')
                      }}
                    >
                      Mock rig
                    </button>
                    <button
                      type="button"
                      className={transportKind === 'serial' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => {
                        void setTransportKind('serial')
                      }}
                    >
                      Web Serial
                    </button>
                    <button
                      type="button"
                      className={transportKind === 'wifi' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => {
                        void setTransportKind('wifi')
                      }}
                    >
                      Wireless
                    </button>
                  </div>

                  <div className="button-row">
                    <button
                      type="button"
                      className="action-button is-inline is-accent"
                      onClick={() => connect()}
                    >
                      {transportKind === 'mock'
                        ? 'Start mock rig'
                        : transportKind === 'wifi'
                          ? 'Connect wireless'
                          : 'Connect board'}
                    </button>
                    <button
                      type="button"
                      className="action-button is-inline"
                      onClick={() => disconnect()}
                    >
                      Disconnect
                    </button>
                  </div>

                  <p className="inline-help">{transportDetail}</p>

                  <div className="connect-steps">
                    {connectSteps.map((step, index) => (
                      <div key={step} className="connect-step">
                        <span>{index + 1}</span>
                        <p>{step}</p>
                      </div>
                    ))}
                  </div>
                </>
              )}

              {showControllerWirelessPanel ? (
                <div className="controller-wireless-shell">
                  <div className="controller-wireless-shell__summary">
                    <div className="controller-wireless-shell__identity">
                      <div className="cutout-head">
                        <Wifi size={16} />
                        <strong>Controller Wi-Fi settings</strong>
                      </div>
                      <p className="inline-help">
                        {wirelessPanelSummary.label} • {wirelessPanelSummary.detail}
                      </p>
                    </div>
                    <button
                      type="button"
                      className="action-button is-inline"
                      aria-expanded={controllerWirelessPanelExpanded}
                      onClick={() => setWirelessPanelExpanded((current) => !current)}
                    >
                      {controllerWirelessPanelExpanded ? (
                        <>
                          <ChevronUp size={16} />
                          <span>Fold Wi-Fi</span>
                        </>
                      ) : (
                        <>
                          <ChevronDown size={16} />
                          <span>Show Wi-Fi</span>
                        </>
                      )}
                    </button>
                  </div>

                  {controllerWirelessPanelExpanded ? (
                    <div className="field-stack">
                      {transportKind === 'wifi' ? (
                        <label className="field-block">
                          <span>Wireless controller URL</span>
                          <input
                            type="text"
                            value={wifiUrl}
                            onChange={(event) => setWifiUrl(event.target.value)}
                            placeholder={preferredWirelessUrl(snapshot.wireless)}
                          />
                          <small>
                            Controller network: <code>{wirelessModeLabel(snapshot.wireless)}</code>
                            {' '}• endpoint <code>{preferredWirelessUrl(snapshot.wireless)}</code>
                          </small>
                          <small>
                            {snapshot.wireless.mode === 'station'
                              ? snapshot.wireless.stationConnected
                                ? `Joined ${snapshot.wireless.ssid} at ${snapshot.wireless.ipAddress}`
                                : snapshot.wireless.lastError || 'Waiting for station association'
                              : `Bench AP: ${controllerBenchAp.ssid} / ${controllerBenchAp.password}`}
                          </small>
                        </label>
                      ) : null}

                      <div className="panel-cutout panel-cutout--compact">
                        <div className="cutout-head">
                          <strong>Controller Wi-Fi mode</strong>
                        </div>
                        <p className="panel-note">
                          {transportKind === 'serial'
                            ? 'Use the live Web Serial link to stage controller network changes safely, then switch the transport to Wireless once the controller endpoint is ready.'
                            : 'Switching controller network mode can intentionally drop the current wireless socket before the browser sees the acknowledgement.'}
                        </p>
                        <div
                          className={`controller-wireless-feedback is-${wirelessFeedback.tone}`}
                          data-hover-help={wirelessFeedback.detail}
                        >
                          <div>
                            <span>Wi-Fi transition</span>
                            <strong>{wirelessFeedback.label}</strong>
                            <small>{wirelessFeedback.detail}</small>
                          </div>
                          <div className={`state-pill is-${wirelessFeedback.tone}`}>
                            <span>{wirelessFeedback.label}</span>
                          </div>
                        </div>
                        <div className="controller-wireless-status-grid">
                          {wirelessStatusCards.map((card) => (
                            <div
                              key={card.label}
                              className="controller-wireless-status-card"
                              data-hover-help={`${card.label}. ${card.detail}`}
                            >
                              <span>{card.label}</span>
                              <strong>{card.value}</strong>
                              <small>{card.detail}</small>
                            </div>
                          ))}
                        </div>
                        <div className="controller-wireless-meter">
                          <div className="controller-wireless-meter__label">
                            <span>Signal / endpoint confidence</span>
                            <strong>
                              {snapshot.wireless.mode === 'station'
                                ? `${wirelessSignalLevel}%`
                                : snapshot.wireless.apReady
                                  ? 'AP ready'
                                  : 'AP idle'}
                            </strong>
                          </div>
                          <ProgressMeter value={wirelessSignalLevel} tone={wirelessSignalTone} compact />
                        </div>
                        <div className="field-grid field-grid--two">
                          {scannedWirelessNetworks.length > 0 ? (
                            <label className="field-block field-block--compact">
                              <span>Nearby SSIDs</span>
                              <select
                                value={
                                  selectedScannedWirelessNetwork?.ssid ??
                                  (displayedWirelessStationSsidDraft.trim().length > 0 ? '__manual__' : '')
                                }
                                onChange={(event) => {
                                  const nextValue = event.target.value
                                  if (nextValue === '__manual__' || nextValue === '') {
                                    return
                                  }
                                  setWirelessStationSsidDraftTouched(true)
                                  setWirelessStationSsidDraft(nextValue)
                                  setWirelessReuseSavedPassword(false)
                                }}
                              >
                                <option value="">Select scanned network</option>
                                {selectedScannedWirelessNetwork === null &&
                                displayedWirelessStationSsidDraft.trim().length > 0 ? (
                                  <option value="__manual__">
                                    Manual SSID: {displayedWirelessStationSsidDraft.trim()}
                                  </option>
                                ) : null}
                                {scannedWirelessNetworks.map((network) => (
                                  <option
                                    key={`${network.ssid}-${network.channel}`}
                                    value={network.ssid}
                                  >
                                    {network.ssid} · {network.rssiDbm} dBm · ch {network.channel}
                                    {network.secure ? '' : ' · open'}
                                  </option>
                                ))}
                              </select>
                              <small>
                                {snapshot.wireless.scanInProgress
                                  ? 'Controller scan is in progress.'
                                  : 'Pick a scanned SSID or type one manually below. Only 2.4 GHz networks are supported.'}
                              </small>
                            </label>
                          ) : null}
                          <label className="field-block field-block--compact">
                            <span>Existing Wi-Fi SSID</span>
                            <input
                              type="text"
                              value={displayedWirelessStationSsidDraft}
                              onChange={(event) => {
                                setWirelessStationSsidDraftTouched(true)
                                setWirelessStationSsidDraft(event.target.value)
                                setWirelessReuseSavedPassword(false)
                              }}
                              placeholder={snapshot.wireless.stationSsid || 'Lab network'}
                            />
                          </label>
                          <label className="field-block field-block--compact">
                            <span>Password</span>
                            <input
                              type="text"
                              value={wirelessStationPasswordDraft}
                              onChange={(event) => {
                                setWirelessStationPasswordDraft(event.target.value)
                                if (event.target.value.length > 0) {
                                  setWirelessReuseSavedPassword(false)
                                }
                              }}
                              placeholder={
                                canReuseSavedPassword
                                  ? 'Enter a replacement password or enable reuse below'
                                  : 'Enter Wi-Fi password or leave blank for an open network'
                              }
                            />
                            <small>
                              {wirelessReuseSavedPassword && canReuseSavedPassword
                                ? `Controller will reuse the saved password for ${savedStationSsid}.`
                                : selectedScannedWirelessNetwork === null
                                  ? canReuseSavedPassword
                                    ? `Blank means open-network join unless you enable saved-password reuse for ${savedStationSsid}.`
                                    : 'Password is always visible here so you can confirm exactly what will be sent.'
                                  : selectedScannedWirelessNetwork.secure
                                    ? `Selected network ${selectedScannedWirelessNetwork.ssid} is secured. Enter its password or explicitly reuse the saved password for ${savedStationSsid}.`
                                    : `Selected network ${selectedScannedWirelessNetwork.ssid} is open; password can stay blank.`}
                            </small>
                          </label>
                        </div>
                        {canReuseSavedPassword ? (
                          <label className="field-inline-toggle">
                            <input
                              type="checkbox"
                              checked={wirelessReuseSavedPassword}
                              onChange={(event) =>
                                setWirelessReuseSavedPassword(event.target.checked)
                              }
                            />
                            <span>
                              Reuse the controller’s saved password for {savedStationSsid}
                            </span>
                          </label>
                        ) : null}
                        <p className="controller-wireless-note">
                          ESP32 Wi-Fi supports 2.4 GHz networks only. 5 GHz SSIDs will not work.
                        </p>
                        <div className="button-row">
                          <button
                            type="button"
                            className="action-button is-inline"
                            disabled={
                              !canConfigureControllerWireless ||
                              snapshot.wireless.scanInProgress ||
                              wirelessActionPending === 'scan'
                            }
                            data-hover-help="Ask the controller radio to scan nearby 2.4 GHz Wi-Fi networks."
                            onClick={() => {
                              void handleScanControllerWirelessNetworks()
                            }}
                          >
                            {snapshot.wireless.scanInProgress || wirelessActionPending === 'scan'
                              ? 'Scanning…'
                              : 'Scan SSIDs'}
                          </button>
                          <button
                            type="button"
                            className="action-button is-inline is-accent"
                            disabled={
                              !canConfigureControllerWireless ||
                              wirelessActionPending === 'join' ||
                              wirelessActionPending === 'restore'
                            }
                            data-hover-help="Save the typed SSID and password into the controller, then ask it to join that 2.4 GHz Wi-Fi network."
                            onClick={() => {
                              void handleConfigureControllerStationMode()
                            }}
                          >
                            {wirelessActionPending === 'join' ? 'Saving and joining…' : 'Save and join Wi-Fi'}
                          </button>
                          <button
                            type="button"
                            className="action-button is-inline"
                            disabled={
                              !canConfigureControllerWireless ||
                              wirelessActionPending === 'join' ||
                              wirelessActionPending === 'restore'
                            }
                            data-hover-help="Switch the controller back to its built-in bench access point network and default WebSocket address."
                            onClick={() => {
                              void handleRestoreControllerBenchAp()
                            }}
                          >
                            {wirelessActionPending === 'restore' ? 'Restoring…' : 'Restore bench AP'}
                          </button>
                          <button
                            type="button"
                            className="action-button is-inline"
                            data-hover-help="Copy the controller-reported WebSocket URL into the Wireless controller URL field."
                            onClick={() => setWifiUrl(preferredWirelessUrl(snapshot.wireless))}
                          >
                            Use controller URL
                          </button>
                          {transportKind === 'wifi' ? (
                            <button
                              type="button"
                              className="action-button is-inline"
                              data-hover-help="Point the host back to the default bench access point URL so you can reconnect and manage Wi-Fi again."
                              onClick={() => setWifiUrl(controllerBenchAp.wsUrl)}
                            >
                              Use bench AP URL
                            </button>
                          ) : null}
                        </div>
                        {!canConfigureControllerWireless ? (
                          <p className="controller-wireless-note">
                            Connect to the controller first, then scan or save Wi-Fi settings. If the saved station endpoint is stale, switch Wireless controller URL back to <code>{controllerBenchAp.wsUrl}</code> and reconnect over the bench AP.
                          </p>
                        ) : null}
                      </div>
                    </div>
                  ) : null}
                </div>
              ) : null}
            </div>
          </div>

          <div className={deviceLinkLost ? 'hero-facts hero-facts--wide offline-dim' : 'hero-facts hero-facts--wide'}>
            <div className="cutout-head">
              <div>
                <p className="eyebrow">Bench facts</p>
                <strong>Live identity and boot</strong>
              </div>
            </div>
            <div className="hero-facts__grid">
              {sessionFacts.map((fact) => (
                <div key={fact.label} className="hero-facts__item">
                  <span>{fact.label}</span>
                  <strong>{fact.value}</strong>
                  <small>{fact.detail}</small>
                </div>
              ))}
            </div>
          </div>
        </header>

        <div className={deviceLinkLost ? 'offline-dim' : undefined}>
          <StatusRail snapshot={snapshot} telemetryStore={telemetryStore} />
        </div>

        <AnimatePresence mode="wait">
          <motion.div
            key={activeView}
            initial={{ opacity: 0, y: 16 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -12 }}
            transition={{ duration: 0.24 }}
            className={deviceLinkLost ? 'workspace__content offline-dim' : 'workspace__content'}
          >
            {activeView === 'overview' ? (
              <>
                <SafetyMatrix snapshot={snapshot} />

                <ModuleReadinessPanel snapshot={snapshot} telemetryStore={telemetryStore} />

                <section className="telemetry-grid">
                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                          <p className="eyebrow">Laser path</p>
                          <h2>Beam status</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>Green alignment laser</dt>
                        <dd>{snapshot.laser.alignmentEnabled ? 'On' : 'Safe off'}</dd>
                      </div>
                      <div>
                        <dt>NIR laser</dt>
                        <dd>{snapshot.laser.nirEnabled ? 'On' : 'Safe off'}</dd>
                      </div>
                      <div>
                        <dt>Driver standby</dt>
                        <dd>{snapshot.laser.driverStandby ? 'asserted' : 'released'}</dd>
                      </div>
                      <div>
                        <dt>Measured current</dt>
                        <dd>{formatNumber(snapshot.laser.measuredCurrentA, 3)} A</dd>
                      </div>
                      <div>
                        <dt>Commanded current</dt>
                        <dd>{formatNumber(snapshot.laser.commandedCurrentA, 3)} A</dd>
                      </div>
                      <div>
                        <dt>Optical estimate</dt>
                        <dd>{formatNumber(benchEstimate.averageOpticalPowerW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Power and rails</p>
                        <h2>Power</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>PD contract</dt>
                        <dd>{snapshot.pd.contractValid ? 'Negotiated' : 'Unavailable'}</dd>
                      </div>
                      <div>
                        <dt>Source</dt>
                        <dd>
                          {formatNumber(snapshot.pd.sourceVoltageV, 1)} V / {formatNumber(snapshot.pd.sourceCurrentA, 1)} A
                        </dd>
                      </div>
                      <div>
                        <dt>TEC rail</dt>
                        <dd>{snapshot.rails.tec.enabled ? 'enabled' : 'off'} · {snapshot.rails.tec.pgood ? 'pgood' : 'bad'}</dd>
                      </div>
                      <div>
                        <dt>LD rail</dt>
                        <dd>{snapshot.rails.ld.enabled ? 'enabled' : 'off'} · {snapshot.rails.ld.pgood ? 'pgood' : 'bad'}</dd>
                      </div>
                      <div>
                        <dt>Total estimated draw</dt>
                        <dd>{formatNumber(benchEstimate.totalEstimatedInputPowerW, 2)} W</dd>
                      </div>
                      <div>
                        <dt>PD headroom</dt>
                        <dd>{formatNumber(benchEstimate.pdHeadroomW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Thermal loop</p>
                        <h2>TEC and lambda</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>Target temp</dt>
                        <dd>{formatNumber(snapshot.tec.targetTempC, 1)} °C</dd>
                      </div>
                      <div>
                        <dt>Actual temp</dt>
                        <dd>{formatNumber(snapshot.tec.tempC, 2)} °C</dd>
                      </div>
                      <div>
                        <dt>Target wavelength</dt>
                        <dd>{formatNumber(snapshot.tec.targetLambdaNm, 1)} nm</dd>
                      </div>
                      <div>
                        <dt>TEC voltage</dt>
                        <dd>{formatNumber(snapshot.tec.voltageV, 2)} V</dd>
                      </div>
                      <div>
                        <dt>TEC current</dt>
                        <dd>{formatNumber(snapshot.tec.currentA, 2)} A</dd>
                      </div>
                      <div>
                        <dt>Cooling estimate</dt>
                        <dd>{formatNumber(benchEstimate.tecCoolingPowerW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Sensors</p>
                        <h2>IMU and range</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>IMU freshness</dt>
                        <dd>{snapshot.imu.valid && snapshot.imu.fresh ? 'Fresh and valid' : 'Stale or invalid'}</dd>
                      </div>
                      <div>
                        <dt>Beam pitch</dt>
                        <dd>{formatNumber(snapshot.imu.beamPitchDeg, 1)}°</dd>
                      </div>
                      <div>
                        <dt>Pitch limit</dt>
                        <dd>{formatNumber(snapshot.imu.beamPitchLimitDeg, 1)}°</dd>
                      </div>
                      <div>
                        <dt>ToF validity</dt>
                        <dd>{formatTofValidityLabel(snapshot)}</dd>
                      </div>
                      <div>
                        <dt>Distance</dt>
                        <dd>
                          {tofDisplayDistance !== null
                            ? `${formatNumber(tofDisplayDistance, 2)} m`
                            : 'No live sample'}
                        </dd>
                      </div>
                      <div>
                        <dt>Allowed window</dt>
                        <dd>{formatTofWindowSummary(snapshot)}</dd>
                      </div>
                    </dl>
                  </article>
                </section>

                <EventTimeline
                  events={events}
                  commands={commands}
                  query={eventQuery}
                  severity={eventSeverity}
                  moduleFilter={eventModuleFilter}
                  onQueryChange={setEventQuery}
                  onSeverityChange={setEventSeverity}
                  onModuleFilterChange={setEventModuleFilter}
                  compact
                />
              </>
            ) : null}

            {activeView === 'control' ? (
              <ControlWorkbench
                snapshot={snapshot}
                telemetryStore={telemetryStore}
                transportKind={transportKind}
                transportStatus={transportStatus}
                transportRecovering={transportRecovering}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}

            {activeView === 'bringup' ? (
              <BringupWorkbench
                snapshot={snapshot}
                telemetryStore={telemetryStore}
                transportStatus={transportStatus}
                transportRecovering={transportRecovering}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}

            {activeView === 'events' ? (
              <>
                <section className="panel-section log-workspace-switcher">
                  <div className="panel-section__head">
                    <div>
                      <p className="eyebrow">Log workspace</p>
                      <h2>Separate system and comms views</h2>
                    </div>
                    <p className="panel-note">
                      Switch between controller/system history and the SPI or I2C traffic stream instead of stacking both on one page.
                    </p>
                  </div>
                  <div className="segmented log-workspace-switcher__tabs">
                    <button
                      type="button"
                      className={eventLogView === 'system' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setEventLogView('system')}
                    >
                      System log
                    </button>
                    <button
                      type="button"
                      className={eventLogView === 'comms' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setEventLogView('comms')}
                    >
                      Comms log
                    </button>
                  </div>
                </section>

                {eventLogView === 'system' ? (
                  <EventTimeline
                    events={events}
                    commands={commands}
                    query={eventQuery}
                    severity={eventSeverity}
                    moduleFilter={eventModuleFilter}
                    onQueryChange={setEventQuery}
                  onSeverityChange={setEventSeverity}
                  onModuleFilterChange={setEventModuleFilter}
                  onClearSessionHistory={clearSessionHistory}
                  mode="system"
                />
              ) : (
                  <BusTrafficViewer events={events} commands={commands} />
                )}
              </>
            ) : null}

            {activeView === 'firmware' ? (
              <FirmwareWorkbench
                snapshot={snapshot}
                packageDescriptor={packageDescriptor}
                firmwareProgress={firmwareProgress}
                connected={transportStatus === 'connected'}
                supportsFirmwareTransfer={supportsFirmwareTransfer}
                transportKind={transportKind}
                onPickFile={handlePickFirmware}
                onBeginTransfer={handleBeginTransfer}
                onPrepareDevice={handlePrepareDevice}
              />
            ) : null}

            {activeView === 'service' ? (
              <CommandDeck
                snapshot={snapshot}
                transportKind={transportKind}
                transportStatus={transportStatus}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}
          </motion.div>
        </AnimatePresence>
      </main>

      <InspectorRail
        snapshot={snapshot}
        commands={commands}
        transportKind={transportKind}
        transportStatus={transportStatus}
        transportDetail={transportDetail}
        wifiUrl={wifiUrl}
        firmwareProgress={firmwareProgress}
        sessionAutosave={sessionAutosave}
        deviceLinkLost={deviceLinkLost}
        onExportSession={exportSession}
        onClearSessionHistory={clearSessionHistory}
        onConfigureSessionAutosave={configureSessionAutosave}
        onDisableSessionAutosave={disableSessionAutosave}
        onSetWifiUrl={setWifiUrl}
        onSetTransportKind={setTransportKind}
        onConnect={connect}
        onDisconnect={disconnect}
        onToggleServiceMode={handleToggleServiceMode}
        onSetInterlocksDisabled={handleSetInterlocksDisabled}
      />
    </div>
  )
}

export default App
