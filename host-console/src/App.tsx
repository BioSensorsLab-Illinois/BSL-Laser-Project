import { startTransition, useEffect, useMemo, useState } from 'react'
import { AnimatePresence, motion } from 'framer-motion'
import {
  Activity,
  Box,
  Cable,
  Command,
  Cpu,
  FlaskConical,
  Shield,
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
import {
  buildSafetyChecks,
  formatEnumLabel,
  formatTofValidityLabel,
  formatTofWindowSummary,
  getTofDisplayDistanceM,
  summarizeSafetyChecks,
  toneFromSystemState,
} from './lib/presentation'
import { controllerBenchAp, transportLabel } from './lib/wireless'
import type {
  FirmwarePackageDescriptor,
  Severity,
  ThemeMode,
  TransportKind,
} from './types'

const HOST_THEME_STORAGE_KEY = 'bsl-host-theme'

type AppView = 'overview' | 'control' | 'bringup' | 'events' | 'firmware' | 'service'
type EventLogView = 'system' | 'comms'

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

function connectStepsForTransport(kind: TransportKind): string[] {
  if (kind === 'wifi') {
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

  const {
    transportKind,
    wifiUrl,
    setWifiUrl,
    setTransportKind,
    transportStatus,
    transportDetail,
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
        label: 'Wireless AP',
        value: snapshot.wireless.apReady ? snapshot.wireless.ssid : 'Offline',
        detail: snapshot.wireless.apReady
          ? `${snapshot.wireless.wsUrl} · ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}`
          : 'SoftAP not advertising yet',
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
      snapshot.wireless.ssid,
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

  const connectSteps = connectStepsForTransport(transportKind)
  const connectionHealthy = transportStatus === 'connected'
  const deviceLinkLost = !connectionHealthy
  const serviceModeLabel = snapshot.bringup.serviceModeActive
    ? 'Service active'
    : snapshot.bringup.serviceModeRequested
      ? 'Service requesting'
      : 'Service off'
  const compactConnectionDetail =
    transportKind === 'mock'
      ? 'Mock rig linked. Use the right rail for transport and service controls.'
      : transportKind === 'wifi'
        ? snapshot.wireless.apReady
          ? `Wireless link active. Bench AP ${snapshot.wireless.ssid} is up at ${snapshot.wireless.wsUrl} with ${snapshot.wireless.clientCount} client${snapshot.wireless.clientCount === 1 ? '' : 's'}.`
          : 'Wireless link active. USB can stay dedicated to PD power while the laptop talks over Wi-Fi.'
        : 'Web Serial linked. Use the right rail for reconnect, disconnect, and service mode.'

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

                  {transportKind === 'wifi' ? (
                    <label className="field-block">
                      <span>Wireless controller URL</span>
                      <input
                        type="text"
                        value={wifiUrl}
                        onChange={(event) => setWifiUrl(event.target.value)}
                        placeholder={controllerBenchAp.wsUrl}
                      />
                      <small>
                        Bench AP: <code>{controllerBenchAp.ssid}</code> /
                        <code>{controllerBenchAp.password}</code>
                      </small>
                      <small>Default endpoint: <code>{controllerBenchAp.wsUrl}</code></small>
                    </label>
                  ) : null}

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
            </div>

            <div className={deviceLinkLost ? 'hero-facts offline-dim' : 'hero-facts'}>
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
                <ModuleReadinessPanel modules={snapshot.bringup.modules} />

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

                <SafetyMatrix snapshot={snapshot} />

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
                onIssueCommand={issueCommand}
              />
            ) : null}

            {activeView === 'bringup' ? (
              <BringupWorkbench
                snapshot={snapshot}
                telemetryStore={telemetryStore}
                transportStatus={transportStatus}
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
      />
    </div>
  )
}

export default App
