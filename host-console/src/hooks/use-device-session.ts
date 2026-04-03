import { useCallback, useEffect, useEffectEvent, useMemo, useRef, useState } from 'react'

import { makeDefaultBringupStatus } from '../lib/bringup'
import { makeDefaultGpioInspectorStatus } from '../lib/gpio-layout'
import { mergeGpioInspector } from '../lib/gpio-inspector'
import { makeDefaultBenchControlStatus } from '../lib/bench-model'
import { annotateSessionEvent } from '../lib/event-decode'
import { createRealtimeTelemetryStore } from '../lib/live-telemetry'
import {
  downloadSessionArchive,
  makeSessionArchivePayload,
} from '../lib/session-archive'
import {
  clearSessionAutosaveHandle,
  describeSessionAutosaveError,
  ensureSessionAutosavePermission,
  loadSessionAutosaveHandle,
  pickSessionAutosaveHandle,
  saveSessionAutosaveHandle,
  supportsSessionAutosave,
  writeSessionAutosaveFile,
} from '../lib/session-autosave'
import { MockTransport } from '../lib/mock-transport'
import { WebSerialTransport } from '../lib/web-serial-transport'
import { WebSocketTransport } from '../lib/websocket-transport'
import type {
  CommandEnvelope,
  CommandHistoryEntry,
  CommandRisk,
  DeviceSnapshot,
  DeviceTransport,
  FirmwarePackageDescriptor,
  FirmwareTransferProgress,
  SessionArchivePayload,
  SessionAutosaveStatus,
  SessionEvent,
  TransportKind,
  TransportMessage,
  TransportStatus,
} from '../types'

const HOST_TRANSPORT_KIND_STORAGE_KEY = 'bsl-host-transport-kind'
const HOST_SERIAL_RECONNECT_STORAGE_KEY = 'bsl-host-serial-reconnect'
const HOST_WIFI_URL_STORAGE_KEY = 'bsl-host-wifi-url'
const DEFAULT_WIFI_WS_URL = 'ws://192.168.4.1/ws'
const COMMAND_ACK_TIMEOUT_MS = 2600
const LINK_IDLE_PROBE_DELAY_MS = 1000
const LINK_IDLE_PROBE_TIMEOUT_MS = 1000
const WIFI_LINK_IDLE_PROBE_DELAY_MS = 6000
const WIFI_LINK_IDLE_PROBE_TIMEOUT_MS = 5000
const SESSION_AUTOSAVE_FLUSH_MS = 2500

type LinkLivenessProbe = {
  commandId: number
  sentAt: number
}

function linkIdleProbeDelayMs(kind: TransportKind): number {
  return kind === 'wifi' ? WIFI_LINK_IDLE_PROBE_DELAY_MS : LINK_IDLE_PROBE_DELAY_MS
}

function linkIdleProbeTimeoutMs(kind: TransportKind): number {
  return kind === 'wifi' ? WIFI_LINK_IDLE_PROBE_TIMEOUT_MS : LINK_IDLE_PROBE_TIMEOUT_MS
}

function makeEventId(suffix: string): string {
  return `${Date.now()}-${Math.random().toString(16).slice(2, 8)}-${suffix}`
}

function moduleFromCommand(cmd: string): string {
  if (cmd.includes('gpio')) {
    return 'service'
  }

  if (cmd.startsWith('i2c_') || cmd.startsWith('spi_')) {
    return 'bus'
  }

  if (cmd.startsWith('set_target_') || cmd.includes('tec')) {
    return 'tec'
  }

  if (cmd.includes('pd') || cmd === 'refresh_pd_status') {
    return 'pd'
  }

  if (cmd.includes('imu') || cmd === 'spi_read' || cmd === 'spi_write') {
    return 'imu'
  }

  if (cmd.includes('tof')) {
    return 'tof'
  }

  if (cmd.includes('dac')) {
    return 'dac'
  }

  if (cmd.includes('haptic')) {
    return 'haptic'
  }

  if (cmd.includes('alignment') || cmd.includes('laser') || cmd.includes('modulation')) {
    return 'laser'
  }

  if (cmd.includes('service') || cmd.includes('fault') || cmd.includes('interlock') || cmd === 'reboot') {
    return 'service'
  }

  return 'system'
}

function noteFromSnapshotForCommand(
  cmd: string,
  snapshot: DeviceSnapshot,
): string | null {
  if (cmd === 'i2c_scan') {
    return snapshot.bringup.tools.lastI2cScan.trim().length > 0
      ? snapshot.bringup.tools.lastI2cScan
      : null
  }

  if (cmd === 'i2c_read' || cmd === 'i2c_write') {
    return snapshot.bringup.tools.lastI2cOp.trim().length > 0
      ? snapshot.bringup.tools.lastI2cOp
      : null
  }

  if (cmd === 'spi_read' || cmd === 'spi_write') {
    return snapshot.bringup.tools.lastSpiOp.trim().length > 0
      ? snapshot.bringup.tools.lastSpiOp
      : null
  }

  if (cmd === 'refresh_pd_status') {
    return `PD refresh -> ${snapshot.pd.sourceVoltageV.toFixed(1)} V, ${snapshot.pd.sourceCurrentA.toFixed(2)} A, ${snapshot.pd.negotiatedPowerW.toFixed(1)} W`
  }

  if (cmd === 'set_gpio_override') {
    return snapshot.gpioInspector.anyOverrideActive
      ? `${snapshot.gpioInspector.activeOverrideCount} GPIO override${snapshot.gpioInspector.activeOverrideCount === 1 ? '' : 's'} active.`
      : 'Selected GPIO returned to firmware ownership.'
  }

  if (cmd === 'clear_gpio_overrides') {
    return 'All GPIO overrides cleared; original firmware logic owns every pin again.'
  }

  if (cmd === 'tof_illumination_set') {
    return snapshot.bringup.illumination.tof.enabled
      ? `Front illumination active on GPIO6 at ${snapshot.bringup.illumination.tof.dutyCyclePct}% duty and ${snapshot.bringup.illumination.tof.frequencyHz} Hz.`
      : 'Front illumination disabled and GPIO6 returned low.'
  }

  if (cmd === 'enable_alignment') {
    if (snapshot.laser.alignmentEnabled) {
      return 'Green alignment laser request is staged and the output is live.'
    }
    if (snapshot.bench.requestedAlignmentEnabled) {
      return 'Green alignment laser request is staged, but output is still waiting on firmware gates.'
    }
    return 'Green alignment laser request was accepted.'
  }

  if (cmd === 'disable_alignment') {
    return snapshot.bench.requestedAlignmentEnabled
      ? 'Green alignment request is still staged.'
      : 'Green alignment laser request cleared.'
  }

  if (cmd === 'set_interlocks_disabled') {
    return snapshot.bringup.interlocksDisabled
      ? 'All controller beam interlocks are disabled under explicit bench override.'
      : 'Normal controller interlock supervision restored.'
  }

  return null
}

function isBusCommand(cmd: string): boolean {
  return (
    cmd === 'i2c_scan' ||
    cmd === 'i2c_read' ||
    cmd === 'i2c_write' ||
    cmd === 'spi_read' ||
    cmd === 'spi_write'
  )
}

function isSlowWirelessCommand(cmd: string): boolean {
  return (
    cmd === 'get_status' ||
    cmd === 'get_bringup_profile' ||
    cmd === 'set_runtime_safety' ||
    cmd.includes('service_mode') ||
    cmd.startsWith('set_supply_') ||
    cmd.startsWith('set_haptic_') ||
    cmd.startsWith('set_gpio_') ||
    cmd === 'clear_gpio_overrides' ||
    isBusCommand(cmd) ||
    cmd.includes('haptic') ||
    cmd.includes('dac') ||
    cmd.includes('pd') ||
    cmd.includes('imu') ||
    cmd.includes('tof')
  )
}

function effectiveCommandAckTimeoutMs(
  cmd: string,
  timeoutMs: number,
  transportKind: TransportKind,
): number {
  if (transportKind !== 'wifi' || cmd === 'ping') {
    return timeoutMs
  }

  return isSlowWirelessCommand(cmd)
    ? Math.max(timeoutMs, 6500)
    : Math.max(timeoutMs, 4500)
}

function deriveSnapshotEvents(
  previous: DeviceSnapshot,
  next: DeviceSnapshot,
): SessionEvent[] {
  const events: SessionEvent[] = []

  if (previous.bringup.serviceModeActive !== next.bringup.serviceModeActive) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('service-mode'),
        atIso: new Date().toISOString(),
        severity: next.bringup.serviceModeActive ? 'ok' : 'info',
        category: 'service',
        title: next.bringup.serviceModeActive ? 'Service mode active' : 'Service mode closed',
        detail: next.bringup.serviceModeActive
          ? 'Guarded maintenance writes are now permitted by the controller.'
          : 'Controller returned to normal safe supervision.',
        module: 'service',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.interlocksDisabled !== next.bringup.interlocksDisabled) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('interlocks-override'),
        atIso: new Date().toISOString(),
        severity: next.bringup.interlocksDisabled ? 'critical' : 'ok',
        category: 'safety',
        title: next.bringup.interlocksDisabled
          ? 'All interlocks disabled'
          : 'Interlocks restored',
        detail: next.bringup.interlocksDisabled
          ? 'The controller is running under an explicit bench override. Treat all optical safety gates as defeated until restored.'
          : 'Normal controller interlock supervision has been restored.',
        module: 'service',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastI2cScan !== next.bringup.tools.lastI2cScan) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('i2c-scan'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'I2C scan updated',
        detail: next.bringup.tools.lastI2cScan,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastI2cOp !== next.bringup.tools.lastI2cOp) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('i2c-op'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'I2C transaction',
        detail: next.bringup.tools.lastI2cOp,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastSpiOp !== next.bringup.tools.lastSpiOp) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('spi-op'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'SPI transaction',
        detail: next.bringup.tools.lastSpiOp,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (
    previous.bringup.tools.lastAction !== next.bringup.tools.lastAction &&
    next.bringup.tools.lastAction.trim().length > 0
  ) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('service-action'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'service',
        title: 'Bring-up action',
        detail: next.bringup.tools.lastAction,
        module: 'service',
        source: 'derived',
      }),
    )
  }

  return events
}

type CommandAckResult = {
  ok: boolean
  note: string
}

type PendingCommandAck = {
  resolve: (result: CommandAckResult) => void
  timeoutId: number
}

function readStoredTransportKind(): TransportKind {
  if (typeof window === 'undefined') {
    return 'mock'
  }

  const stored = window.localStorage.getItem(HOST_TRANSPORT_KIND_STORAGE_KEY)
  return stored === 'serial' || stored === 'wifi' ? stored : 'mock'
}

function readStoredSerialReconnect(): boolean {
  if (typeof window === 'undefined') {
    return false
  }

  return window.localStorage.getItem(HOST_SERIAL_RECONNECT_STORAGE_KEY) === 'true'
}

function writeStoredTransportKind(kind: TransportKind): void {
  if (typeof window === 'undefined') {
    return
  }

  window.localStorage.setItem(HOST_TRANSPORT_KIND_STORAGE_KEY, kind)
}

function readStoredWifiUrl(): string {
  if (typeof window === 'undefined') {
    return DEFAULT_WIFI_WS_URL
  }

  const stored = window.localStorage.getItem(HOST_WIFI_URL_STORAGE_KEY)
  return stored !== null && stored.trim().length > 0 ? stored : DEFAULT_WIFI_WS_URL
}

function writeStoredWifiUrl(url: string): void {
  if (typeof window === 'undefined') {
    return
  }

  window.localStorage.setItem(HOST_WIFI_URL_STORAGE_KEY, url)
}

function writeStoredSerialReconnect(enabled: boolean): void {
  if (typeof window === 'undefined') {
    return
  }

  window.localStorage.setItem(
    HOST_SERIAL_RECONNECT_STORAGE_KEY,
    enabled ? 'true' : 'false',
  )
}

function makeSeedSnapshot(): DeviceSnapshot {
  const now = new Date().toISOString()
  return {
    identity: {
      label: 'BSL-HTLS Gen2',
      firmwareVersion: 'unavailable',
      hardwareRevision: 'rev-?',
      serialNumber: 'pending',
      protocolVersion: 'host-v1',
    },
    session: {
      uptimeSeconds: 0,
      state: 'BOOT_INIT',
      powerTier: 'unknown',
      bootReason: 'unknown',
      connectedAtIso: now,
    },
    wireless: {
      started: false,
      mode: 'softap',
      apReady: false,
      stationConfigured: false,
      stationConnecting: false,
      stationConnected: false,
      clientCount: 0,
      ssid: 'BSL-HTLS-Bench',
      stationSsid: '',
      stationRssiDbm: 0,
      stationChannel: 0,
      scanInProgress: false,
      scannedNetworks: [],
      ipAddress: '192.168.4.1',
      wsUrl: DEFAULT_WIFI_WS_URL,
      lastError: '',
    },
    pd: {
      contractValid: false,
      negotiatedPowerW: 0,
      sourceVoltageV: 0,
      sourceCurrentA: 0,
      operatingCurrentA: 0,
      contractObjectPosition: 0,
      sinkProfileCount: 0,
      sinkProfiles: [],
      sourceIsHostOnly: false,
    },
    rails: {
      ld: { enabled: false, pgood: false },
      tec: { enabled: false, pgood: false },
    },
    imu: {
      valid: false,
      fresh: false,
      beamPitchDeg: 0,
      beamRollDeg: 0,
      beamYawDeg: 0,
      beamYawRelative: true,
      beamPitchLimitDeg: 0,
    },
    tof: {
      valid: false,
      fresh: false,
      distanceM: 0,
      minRangeM: 0.2,
      maxRangeM: 1,
    },
    laser: {
      alignmentEnabled: false,
      nirEnabled: false,
      driverStandby: true,
      commandVoltageV: 0,
      measuredCurrentA: 0,
      commandedCurrentA: 0,
      currentMonitorVoltageV: 0,
      loopGood: false,
      driverTempVoltageV: 0,
      driverTempC: 0,
    },
    tec: {
      targetTempC: 0,
      targetLambdaNm: 785,
      actualLambdaNm: 785,
      commandVoltageV: 0,
      tempGood: false,
      tempC: 0,
      tempAdcVoltageV: 0,
      currentA: 0,
      voltageV: 0,
      settlingSecondsRemaining: 0,
    },
    buttons: {
      stage1Pressed: false,
      stage2Pressed: false,
      stage1Edge: false,
      stage2Edge: false,
    },
    peripherals: {
      dac: {
        reachable: false,
        configured: false,
        refAlarm: false,
        syncReg: 0,
        configReg: 0,
        gainReg: 0,
        statusReg: 0,
        dataAReg: 0,
        dataBReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      pd: {
        reachable: false,
        attached: false,
        ccStatusReg: 0,
        pdoCountReg: 0,
        rdoStatusRaw: 0,
      },
      imu: {
        reachable: false,
        configured: false,
        whoAmI: 0,
        statusReg: 0,
        ctrl1XlReg: 0,
        ctrl2GReg: 0,
        ctrl3CReg: 0,
        ctrl4CReg: 0,
        ctrl10CReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      haptic: {
        reachable: false,
        enablePinHigh: false,
        triggerPinHigh: false,
        modeReg: 0,
        libraryReg: 0,
        goReg: 0,
        feedbackReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      tof: {
        reachable: false,
        configured: false,
        interruptLineHigh: false,
        ledCtrlAsserted: false,
        dataReady: false,
        bootState: 0,
        rangeStatus: 0,
        sensorId: 0,
        distanceMm: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
    },
    gpioInspector: makeDefaultGpioInspectorStatus(),
    bench: makeDefaultBenchControlStatus(),
    safety: {
      allowAlignment: false,
      allowNir: false,
      horizonBlocked: false,
      distanceBlocked: false,
      lambdaDriftBlocked: false,
      tecTempAdcBlocked: false,
      horizonThresholdDeg: 0,
      horizonHysteresisDeg: 3,
      tofMinRangeM: 0.2,
      tofMaxRangeM: 1,
      tofHysteresisM: 0.02,
      imuStaleMs: 50,
      tofStaleMs: 100,
      railGoodTimeoutMs: 250,
      lambdaDriftLimitNm: 5,
      lambdaDriftHysteresisNm: 0.5,
      lambdaDriftHoldMs: 2000,
      ldOvertempLimitC: 55,
      tecTempAdcTripV: 2.45,
      tecTempAdcHysteresisV: 0.05,
      tecTempAdcHoldMs: 2000,
      tecMinCommandC: 15,
      tecMaxCommandC: 35,
      tecReadyToleranceC: 0.25,
      maxLaserCurrentA: 5,
      actualLambdaNm: 785,
      targetLambdaNm: 785,
      lambdaDriftNm: 0,
      tempAdcVoltageV: 0,
    },
    bringup: makeDefaultBringupStatus(),
    deployment: {
      active: false,
      running: false,
      ready: false,
      failed: false,
      currentStep: 'none',
      lastCompletedStep: 'none',
      failureCode: 'none',
      failureReason: '',
      targetMode: 'temp',
      targetTempC: 25,
      targetLambdaNm: 785,
      maxLaserCurrentA: 5,
      maxOpticalPowerW: 5,
      steps: [],
    },
    fault: {
      latched: false,
      activeCode: 'none',
      activeCount: 0,
      tripCounter: 0,
      lastFaultAtIso: null,
    },
    counters: {
      commsTimeouts: 0,
      watchdogTrips: 0,
      brownouts: 0,
    },
  }
}

function mergeSnapshot(
  current: DeviceSnapshot,
  incoming: DeviceSnapshot,
): DeviceSnapshot {
  return {
    ...current,
    ...incoming,
    identity: { ...current.identity, ...incoming.identity },
    session: { ...current.session, ...incoming.session },
    wireless: { ...current.wireless, ...incoming.wireless },
    pd: { ...current.pd, ...incoming.pd },
    rails: {
      ld: { ...current.rails.ld, ...(incoming.rails?.ld ?? {}) },
      tec: { ...current.rails.tec, ...(incoming.rails?.tec ?? {}) },
    },
    imu: { ...current.imu, ...incoming.imu },
    tof: { ...current.tof, ...incoming.tof },
    laser: { ...current.laser, ...incoming.laser },
    tec: { ...current.tec, ...incoming.tec },
    buttons: { ...current.buttons, ...(incoming.buttons ?? {}) },
    peripherals: {
      dac: { ...current.peripherals.dac, ...(incoming.peripherals?.dac ?? {}) },
      pd: { ...current.peripherals.pd, ...(incoming.peripherals?.pd ?? {}) },
      imu: { ...current.peripherals.imu, ...(incoming.peripherals?.imu ?? {}) },
      haptic: { ...current.peripherals.haptic, ...(incoming.peripherals?.haptic ?? {}) },
      tof: { ...current.peripherals.tof, ...(incoming.peripherals?.tof ?? {}) },
    },
    gpioInspector: mergeGpioInspector(current.gpioInspector, incoming.gpioInspector),
    bench: { ...current.bench, ...incoming.bench },
    safety: { ...current.safety, ...incoming.safety },
    bringup: {
      ...current.bringup,
      ...(incoming.bringup ?? {}),
      power: {
        ...current.bringup.power,
        ...(incoming.bringup?.power ?? {}),
      },
      illumination: {
        ...current.bringup.illumination,
        ...(incoming.bringup?.illumination ?? {}),
        tof: {
          ...current.bringup.illumination.tof,
          ...(incoming.bringup?.illumination?.tof ?? {}),
        },
      },
      modules: {
        ...current.bringup.modules,
        ...(incoming.bringup?.modules ?? {}),
      },
      tuning: {
        ...current.bringup.tuning,
        ...(incoming.bringup?.tuning ?? {}),
      },
      tools: {
        ...current.bringup.tools,
        ...(incoming.bringup?.tools ?? {}),
      },
    },
    deployment: {
      ...current.deployment,
      ...(incoming.deployment ?? {}),
      steps:
        incoming.deployment?.steps !== undefined
          ? incoming.deployment.steps.map((step) => ({ ...step }))
          : current.deployment.steps,
    },
    fault: { ...current.fault, ...incoming.fault },
    counters: { ...current.counters, ...incoming.counters },
  }
}

export function useDeviceSession() {
  const [transportKind, setTransportKindState] = useState<TransportKind>(() =>
    readStoredTransportKind(),
  )
  const [transportStatus, setTransportStatus] = useState<TransportStatus>('disconnected')
  const [transportDetail, setTransportDetail] = useState('No active device link.')
  const [transportRecovering, setTransportRecovering] = useState(false)
  const [wifiUrl, setWifiUrlState] = useState(() => readStoredWifiUrl())
  const [snapshot, setSnapshot] = useState<DeviceSnapshot>(makeSeedSnapshot)
  const [events, setEvents] = useState<SessionEvent[]>([])
  const [commands, setCommands] = useState<CommandHistoryEntry[]>([])
  const [firmwareProgress, setFirmwareProgress] = useState<FirmwareTransferProgress | null>(null)
  const [sessionAutosave, setSessionAutosave] = useState<SessionAutosaveStatus>(() => ({
    supported: supportsSessionAutosave(),
    armed: false,
    fileName: null,
    saving: false,
    lastSavedAtIso: null,
    error: null,
  }))
  const [serialReconnectCycle, setSerialReconnectCycle] = useState(0)
  const [serialReconnectEnabled, setSerialReconnectEnabled] = useState<boolean>(() =>
    readStoredSerialReconnect() || readStoredTransportKind() === 'serial',
  )
  const [wifiReconnectEnabled, setWifiReconnectEnabled] = useState<boolean>(
    () => readStoredTransportKind() === 'wifi',
  )

  const commandCounterRef = useRef(1)
  const transportRef = useRef<DeviceTransport | null>(null)
  const snapshotRef = useRef<DeviceSnapshot>(makeSeedSnapshot())
  const telemetryStoreRef = useRef<ReturnType<typeof createRealtimeTelemetryStore> | null>(null)
  if (telemetryStoreRef.current === null) {
    telemetryStoreRef.current = createRealtimeTelemetryStore(snapshotRef.current)
  }
  const transportKindRef = useRef<TransportKind>(transportKind)
  const transportStatusRef = useRef<TransportStatus>(transportStatus)
  const eventsRef = useRef<SessionEvent[]>(events)
  const commandsRef = useRef<CommandHistoryEntry[]>(commands)
  const firmwareProgressRef = useRef<FirmwareTransferProgress | null>(firmwareProgress)
  const serialReconnectAttemptRef = useRef(false)
  const serialManualDisconnectRef = useRef(false)
  const wifiManualDisconnectRef = useRef(false)
  const protocolReadyRef = useRef(false)
  const flashRecoveryUntilRef = useRef(0)
  const lastInboundMessageAtRef = useRef(0)
  const livenessProbeRef = useRef<LinkLivenessProbe | null>(null)
  const pendingAcksRef = useRef(new Map<number, PendingCommandAck>())
  const commandDispatchChainRef = useRef<Promise<void>>(Promise.resolve())
  const sessionAutosaveHandleRef = useRef<FileSystemFileHandle | null>(null)
  const sessionArchiveRevisionRef = useRef(0)
  const sessionArchiveRef = useRef<SessionArchivePayload>(
    makeSessionArchivePayload({
      transportKind,
      snapshot: snapshotRef.current,
      events,
      commands,
      firmwareProgress,
    }),
  )
  const autosaveDirtyRef = useRef(false)
  const autosaveWriteInFlightRef = useRef(false)

  const transport = useMemo<DeviceTransport>(() => {
    if (transportKind === 'serial') {
      return new WebSerialTransport()
    }

    if (transportKind === 'wifi') {
      return new WebSocketTransport(wifiUrl)
    }

    return new MockTransport()
  }, [transportKind, wifiUrl])

  const appendEvent = useCallback((event: SessionEvent) => {
    setEvents((current) => [annotateSessionEvent(event), ...current].slice(0, 250))
  }, [])

  const runSerializedControllerOperation = useCallback(
    <T,>(operation: () => Promise<T>): Promise<T> => {
      if (transportKindRef.current !== 'serial' && transportKindRef.current !== 'wifi') {
        return operation()
      }

      const nextOperation = commandDispatchChainRef.current
        .catch(() => undefined)
        .then(operation)

      commandDispatchChainRef.current = nextOperation
        .then(() => undefined)
        .catch(() => undefined)

      return nextOperation
    },
    [],
  )

  const rebuildSessionArchive = useCallback(() => {
    const payload = makeSessionArchivePayload({
      transportKind: transportKindRef.current,
      snapshot: snapshotRef.current,
      events: eventsRef.current,
      commands: commandsRef.current,
      firmwareProgress: firmwareProgressRef.current,
    })

    sessionArchiveRef.current = payload
    sessionArchiveRevisionRef.current += 1
    autosaveDirtyRef.current = true
  }, [])

  const flushSessionAutosave = useCallback(
    async (interactivePermission = false) => {
      const handle = sessionAutosaveHandleRef.current

      if (handle === null || !supportsSessionAutosave()) {
        return
      }

      if (autosaveWriteInFlightRef.current) {
        autosaveDirtyRef.current = true
        return
      }

      autosaveWriteInFlightRef.current = true
      const revisionAtStart = sessionArchiveRevisionRef.current

      setSessionAutosave((current) => ({
        ...current,
        saving: true,
        error: null,
      }))

      try {
        const permissionGranted = await ensureSessionAutosavePermission(
          handle,
          interactivePermission,
        )

        if (!permissionGranted) {
          throw new DOMException(
            'Browser write permission for the autosave file was not granted.',
            'NotAllowedError',
          )
        }

        const payload = makeSessionArchivePayload({
          transportKind: transportKindRef.current,
          snapshot: snapshotRef.current,
          events: eventsRef.current,
          commands: commandsRef.current,
          firmwareProgress: firmwareProgressRef.current,
        })
        sessionArchiveRef.current = payload
        await writeSessionAutosaveFile(handle, payload)

        if (sessionArchiveRevisionRef.current === revisionAtStart) {
          autosaveDirtyRef.current = false
        }

        setSessionAutosave((current) => ({
          ...current,
          armed: true,
          fileName: handle.name,
          saving: false,
          lastSavedAtIso: payload.exportedAtIso,
          error: null,
        }))
      } catch (error) {
        setSessionAutosave((current) => ({
          ...current,
          armed: handle !== null,
          fileName: handle.name,
          saving: false,
          error: describeSessionAutosaveError(error),
        }))
      } finally {
        autosaveWriteInFlightRef.current = false
      }
    },
    [],
  )

  const clearPendingAcks = useCallback((message: string) => {
    for (const [commandId, pending] of pendingAcksRef.current) {
      window.clearTimeout(pending.timeoutId)
      pending.resolve({
        ok: false,
        note: message,
      })
      pendingAcksRef.current.delete(commandId)
    }
  }, [])

  const resetLiveSnapshot = useCallback(() => {
    const nextSnapshot = makeSeedSnapshot()
    snapshotRef.current = nextSnapshot
    telemetryStoreRef.current?.reset(nextSnapshot)
    setSnapshot(nextSnapshot)
  }, [])

  const markTransportUnhealthy = useCallback(
    (reason: string, module = 'transport') => {
      if (
        transportKindRef.current === 'wifi' &&
        wifiReconnectEnabled &&
        !wifiManualDisconnectRef.current
      ) {
        livenessProbeRef.current = null
        transportStatusRef.current = 'disconnected'
        setTransportStatus('disconnected')
        setTransportDetail(reason)
        setTransportRecovering(true)
        clearPendingAcks(reason)
        appendEvent({
          id: makeEventId('transport-health'),
          atIso: new Date().toISOString(),
          severity: 'warn',
          category: 'transport',
          title: 'Wireless link recovering',
          detail: reason,
          module,
          source: 'host',
        })
        void transportRef.current?.disconnect().catch(() => undefined)
        return
      }

      livenessProbeRef.current = null
      transportStatusRef.current = 'error'
      setTransportStatus('error')
      setTransportDetail(reason)
      setTransportRecovering(false)
      clearPendingAcks(reason)
      resetLiveSnapshot()
      appendEvent({
        id: makeEventId('transport-health'),
        atIso: new Date().toISOString(),
        severity: 'critical',
        category: 'transport',
        title: 'Controller link unhealthy',
        detail: reason,
        module,
        source: 'host',
      })
      void transportRef.current?.disconnect().catch(() => undefined)
    },
    [appendEvent, clearPendingAcks, resetLiveSnapshot, wifiReconnectEnabled],
  )

  const noteInboundControllerTraffic = useCallback(() => {
    lastInboundMessageAtRef.current = Date.now()
    livenessProbeRef.current = null

    if (
      (transportKindRef.current === 'serial' || transportKindRef.current === 'wifi') &&
      transportStatusRef.current === 'connecting' &&
      !protocolReadyRef.current
    ) {
      setTransportDetail('Controller port active. Waiting for firmware protocol handshake…')
    }
  }, [])

  const issueLinkLivenessProbe = useCallback(async () => {
    if (
      transportKindRef.current !== 'serial' &&
      transportKindRef.current !== 'wifi'
    ) {
      return
    }

    if (
      transportStatusRef.current !== 'connected' &&
      transportStatusRef.current !== 'connecting'
    ) {
      return
    }

    if (pendingAcksRef.current.size > 0) {
      return
    }

    if (transportRef.current === null || livenessProbeRef.current !== null) {
      return
    }

    const commandId = commandCounterRef.current++
    livenessProbeRef.current = {
      commandId,
      sentAt: Date.now(),
    }

    try {
      await transportRef.current.sendCommand({
        id: commandId,
        type: 'cmd',
        cmd: 'ping',
      })

      if (
        transportStatusRef.current === 'connecting' &&
        !protocolReadyRef.current
      ) {
        setTransportDetail('Controller went quiet. Sending a protocol checkup…')
      }
    } catch (error) {
      livenessProbeRef.current = null
      markTransportUnhealthy(
        error instanceof Error
          ? error.message
          : 'The controller liveness probe could not be sent.',
      )
    }
  }, [markTransportUnhealthy])

  const handleMessage = useEffectEvent((message: TransportMessage) => {
    if (message.kind === 'transport') {
      if (
        (transportKind === 'serial' || transportKind === 'wifi') &&
        (transportKind === 'serial'
          ? serialManualDisconnectRef.current
          : wifiManualDisconnectRef.current) &&
        (message.status === 'connected' || message.status === 'connecting')
      ) {
        void transportRef.current?.disconnect().catch(() => undefined)
        return
      }

      if (
        message.status === 'connected' &&
        (transportKind === 'serial' || transportKind === 'wifi')
      ) {
        lastInboundMessageAtRef.current = Date.now()
        livenessProbeRef.current = null
        protocolReadyRef.current = false
        transportStatusRef.current = 'connecting'
        setTransportStatus('connecting')
        setTransportRecovering(false)
        setTransportDetail(
          transportKind === 'serial'
            ? 'Serial port opened. Waiting for controller firmware handshake…'
            : 'Wireless socket open. Waiting for controller firmware handshake…',
        )
        return
      }

      if (
        transportKind === 'wifi' &&
        (message.status === 'disconnected' || message.status === 'error') &&
        wifiReconnectEnabled &&
        !wifiManualDisconnectRef.current
      ) {
        protocolReadyRef.current = false
        livenessProbeRef.current = null
        transportStatusRef.current = 'disconnected'
        setTransportStatus('disconnected')
        setTransportDetail(message.detail ?? 'Wireless link dropped. Trying to reconnect…')
        setTransportRecovering(true)
        lastInboundMessageAtRef.current = 0
        clearPendingAcks(message.detail ?? 'Wireless transport became unavailable.')
        return
      }

      protocolReadyRef.current = false
      livenessProbeRef.current = null
      transportStatusRef.current = message.status
      setTransportStatus(message.status)
      setTransportDetail(message.detail ?? '')
      setTransportRecovering(false)

      if (message.status === 'disconnected' || message.status === 'error') {
        lastInboundMessageAtRef.current = 0
        clearPendingAcks(message.detail ?? 'Transport became unavailable.')
        resetLiveSnapshot()
      }
      return
    }

    if (message.kind === 'snapshot') {
      noteInboundControllerTraffic()
      protocolReadyRef.current = true
      flashRecoveryUntilRef.current = 0
      if (
        (transportKind === 'serial' || transportKind === 'wifi') &&
        transportStatus !== 'connected'
      ) {
        transportStatusRef.current = 'connected'
        setTransportStatus('connected')
        setTransportRecovering(false)
        setTransportDetail('Controller protocol active.')
      }
      const merged = mergeSnapshot(snapshotRef.current, message.snapshot)
      const derivedEvents = deriveSnapshotEvents(snapshotRef.current, merged)
      snapshotRef.current = merged
      telemetryStoreRef.current?.syncFromSnapshot(merged)
      setSnapshot(merged)

      if (derivedEvents.length > 0) {
        setEvents((existing) => [...derivedEvents.reverse(), ...existing].slice(0, 250))
      }
      return
    }

    if (message.kind === 'telemetry') {
      noteInboundControllerTraffic()
      protocolReadyRef.current = true
      flashRecoveryUntilRef.current = 0
      if (
        (transportKind === 'serial' || transportKind === 'wifi') &&
        transportStatus !== 'connected'
      ) {
        transportStatusRef.current = 'connected'
        setTransportStatus('connected')
        setTransportRecovering(false)
        setTransportDetail('Controller protocol active.')
      }
      telemetryStoreRef.current?.publish(message.telemetry)
      return
    }

    if (message.kind === 'event') {
      noteInboundControllerTraffic()
      appendEvent(message.event)
      return
    }

    if (message.kind === 'commandAck') {
      noteInboundControllerTraffic()
      protocolReadyRef.current = true
      flashRecoveryUntilRef.current = 0
      if (
        (transportKind === 'serial' || transportKind === 'wifi') &&
        transportStatus !== 'connected'
      ) {
        transportStatusRef.current = 'connected'
        setTransportStatus('connected')
        setTransportRecovering(false)
        setTransportDetail('Controller protocol active.')
      }
      const pending = pendingAcksRef.current.get(message.commandId)

      if (pending !== undefined) {
        window.clearTimeout(pending.timeoutId)
        pending.resolve({
          ok: message.ok,
          note: message.note,
        })
        pendingAcksRef.current.delete(message.commandId)
      }

      setCommands((current) =>
        current.map((entry) =>
          entry.id === message.commandId
            ? {
                ...entry,
                status: message.ok ? 'ack' : 'error',
                note: message.note,
              }
            : entry,
        ),
      )
      return
    }
  })

  useEffect(() => {
    transportKindRef.current = transportKind
    transportStatusRef.current = transportStatus
    eventsRef.current = events
    commandsRef.current = commands
    firmwareProgressRef.current = firmwareProgress
    rebuildSessionArchive()
  }, [commands, events, firmwareProgress, rebuildSessionArchive, transportKind, transportStatus])

  useEffect(() => {
    snapshotRef.current = snapshot
    rebuildSessionArchive()
  }, [rebuildSessionArchive, snapshot])

  useEffect(() => {
    if (!supportsSessionAutosave()) {
      return
    }

    let cancelled = false

    void loadSessionAutosaveHandle()
      .then((handle) => {
        if (cancelled || handle === null) {
          return
        }

        sessionAutosaveHandleRef.current = handle
        setSessionAutosave((current) => ({
          ...current,
          armed: true,
          fileName: handle.name,
          error: null,
        }))
      })
      .catch((error) => {
        if (cancelled) {
          return
        }

        setSessionAutosave((current) => ({
          ...current,
          error: describeSessionAutosaveError(error),
        }))
      })

    return () => {
      cancelled = true
    }
  }, [])

  useEffect(() => {
    if (!sessionAutosave.armed || sessionAutosaveHandleRef.current === null) {
      return
    }

    const intervalId = window.setInterval(() => {
      if (!autosaveDirtyRef.current || autosaveWriteInFlightRef.current) {
        return
      }

      void flushSessionAutosave(false)
    }, SESSION_AUTOSAVE_FLUSH_MS)

    return () => {
      window.clearInterval(intervalId)
    }
  }, [flushSessionAutosave, sessionAutosave.armed])

  useEffect(() => {
    transportRef.current = transport
    const unsubscribe = transport.subscribe((message) => {
      handleMessage(message)
    })

    return () => {
      clearPendingAcks('Transport changed before the command completed.')
      unsubscribe()
      void transport.disconnect()
    }
  }, [clearPendingAcks, transport])

  const waitForCommandAck = useCallback((commandId: number, timeoutMs: number) => {
    return new Promise<CommandAckResult>((resolve) => {
      const timeoutId = window.setTimeout(() => {
        pendingAcksRef.current.delete(commandId)
        resolve({
          ok: false,
          note: 'Controller acknowledgement timed out.',
        })
      }, timeoutMs)

      pendingAcksRef.current.set(commandId, {
        resolve,
        timeoutId,
      })
    })
  }, [])

  const connect = useCallback(async () => {
    if (transportStatus === 'connected' || transportStatus === 'connecting') {
      return
    }

    if (transportKind === 'serial') {
      serialManualDisconnectRef.current = false
      setSerialReconnectEnabled(true)
      setSerialReconnectCycle(0)
      writeStoredSerialReconnect(true)
    } else if (transportKind === 'wifi') {
      wifiManualDisconnectRef.current = false
      setWifiReconnectEnabled(true)
    }

    lastInboundMessageAtRef.current = Date.now()
    livenessProbeRef.current = null
    transportStatusRef.current = 'connecting'
    setTransportRecovering(false)
    setTransportDetail('Connecting…')
    await transportRef.current?.connect()
  }, [transportKind, transportStatus])

  useEffect(() => {
    if (transportKind !== 'mock') {
      return
    }

    if (transportStatus !== 'disconnected') {
      return
    }

    const timerId = window.setTimeout(() => {
      void connect()
    }, 0)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [connect, transportKind, transportStatus])

  useEffect(() => {
    if (
      transportKind !== 'wifi' ||
      !wifiReconnectEnabled ||
      wifiManualDisconnectRef.current
    ) {
      return
    }

    if (firmwareProgress !== null && firmwareProgress.percent < 100) {
      return
    }

    if (transportStatus === 'connected' || transportStatus === 'connecting') {
      return
    }

    const timerId = window.setTimeout(() => {
      if (!wifiManualDisconnectRef.current) {
        void connect()
      }
    }, 900)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [connect, firmwareProgress, transportKind, transportStatus, wifiReconnectEnabled, wifiUrl])

  useEffect(() => {
    writeStoredTransportKind(transportKind)
  }, [transportKind])

  useEffect(() => {
    writeStoredSerialReconnect(serialReconnectEnabled)
  }, [serialReconnectEnabled])

  useEffect(() => {
    writeStoredWifiUrl(wifiUrl)
  }, [wifiUrl])

  useEffect(() => {
    if (transportKind !== 'serial' && transportKind !== 'wifi') {
      return
    }

    if (transportStatus !== 'connected' && transportStatus !== 'connecting') {
      return
    }

    if (firmwareProgress !== null && firmwareProgress.percent < 100) {
      return
    }

    const intervalId = window.setInterval(() => {
      const now = Date.now()
      const probe = livenessProbeRef.current

      if (pendingAcksRef.current.size > 0) {
        return
      }

      if (probe !== null) {
        if (lastInboundMessageAtRef.current > probe.sentAt) {
          livenessProbeRef.current = null
          return
        }

        if ((now - probe.sentAt) < linkIdleProbeTimeoutMs(transportKindRef.current)) {
          return
        }

        const recoveringFromFlash = flashRecoveryUntilRef.current > now
        markTransportUnhealthy(
          transportStatusRef.current === 'connecting'
            ? recoveringFromFlash && transportKindRef.current === 'serial'
              ? 'Serial port reopened after browser flash, but the controller stayed silent after a liveness probe. The board may still be rebooting, sitting in bootloader mode, or have failed to leave the flasher cleanly.'
              : transportKindRef.current === 'serial'
                ? 'Serial port opened, then stayed silent for several seconds and ignored a controller checkup. The board may still be rebooting, sitting in bootloader mode, or running incompatible firmware.'
                : 'Wireless link opened, then stayed silent for several seconds and ignored a controller checkup. Verify the laptop joined the controller AP and the ESP32 is running the wireless bench image.'
            : 'Controller traffic stopped for several seconds and the keepalive probe was not answered. Treating the active link as lost.',
        )
        return
      }

      if (lastInboundMessageAtRef.current === 0) {
        return
      }

      if ((now - lastInboundMessageAtRef.current) >=
        linkIdleProbeDelayMs(transportKindRef.current)) {
        void issueLinkLivenessProbe()
      }
    }, 200)

    return () => {
      window.clearInterval(intervalId)
    }
  }, [firmwareProgress, issueLinkLivenessProbe, markTransportUnhealthy, transportKind, transportStatus])

  useEffect(() => {
    if (transportKind !== 'serial' || !serialReconnectEnabled || serialManualDisconnectRef.current) {
      return
    }

    if (firmwareProgress !== null && firmwareProgress.percent < 100) {
      return
    }

    if (transportStatus === 'connected' || transportStatus === 'connecting') {
      return
    }

    if (serialReconnectAttemptRef.current) {
      return
    }

    const reconnect = transportRef.current?.reconnectSilently

    if (reconnect === undefined) {
      return
    }

    const timerId = window.setTimeout(() => {
      if (serialManualDisconnectRef.current) {
        return
      }

      serialReconnectAttemptRef.current = true
      void reconnect.call(transportRef.current)
        .then((result) => {
          if (serialManualDisconnectRef.current) {
            return
          }

          if (result !== 'connected' && serialReconnectEnabled) {
            setSerialReconnectCycle((current) => current + 1)
          }
        })
        .finally(() => {
          serialReconnectAttemptRef.current = false
        })
    }, 1400)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [firmwareProgress, serialReconnectCycle, serialReconnectEnabled, transportKind, transportStatus])

  const disconnect = useCallback(async () => {
    if (transportKind === 'serial') {
      serialManualDisconnectRef.current = true
      setSerialReconnectEnabled(false)
      setSerialReconnectCycle(0)
      writeStoredSerialReconnect(false)
    } else if (transportKind === 'wifi') {
      wifiManualDisconnectRef.current = true
      setWifiReconnectEnabled(false)
    }

    if (transportStatus === 'disconnected') {
      setTransportRecovering(false)
      setFirmwareProgress(null)
      return
    }

    setTransportRecovering(false)
    setFirmwareProgress(null)
    await transportRef.current?.disconnect()
  }, [transportKind, transportStatus])

  const setTransportKind = useCallback(
    async (nextKind: TransportKind) => {
      if (nextKind === transportKind) {
        return
      }

      const currentTransport = transportRef.current

      setFirmwareProgress(null)

      if (currentTransport !== null) {
        try {
          await currentTransport.disconnect()
        } catch (error) {
        appendEvent({
          id: makeEventId('transport-switch'),
          atIso: new Date().toISOString(),
          severity: 'warn',
          category: 'transport',
            title: 'Transport disconnect warning',
            detail:
              error instanceof Error
                ? error.message
                : 'Previous transport did not disconnect cleanly during handoff.',
          })
        }
      }

      transportStatusRef.current = 'disconnected'
      lastInboundMessageAtRef.current = 0
      livenessProbeRef.current = null
      setTransportStatus('disconnected')
      setTransportDetail(
        nextKind === 'serial'
          ? 'Previous transport disconnected. Web Serial selected.'
          : nextKind === 'wifi'
            ? 'Previous transport disconnected. Wireless selected.'
            : 'Previous transport disconnected. Mock rig selected.',
      )
      setTransportRecovering(false)
      const nextSnapshot = makeSeedSnapshot()
      snapshotRef.current = nextSnapshot
      setSnapshot(nextSnapshot)
      serialManualDisconnectRef.current = false
      wifiManualDisconnectRef.current = false
      setSerialReconnectEnabled(nextKind === 'serial')
      setWifiReconnectEnabled(nextKind === 'wifi')
      setSerialReconnectCycle(0)
      setTransportKindState(nextKind)
    },
    [appendEvent, transportKind],
  )

  const issueCommand = useCallback(
    async (
      cmd: string,
      risk: CommandRisk,
      note: string,
      args?: Record<string, number | string | boolean>,
    ) => {
      const command: CommandEnvelope = {
        id: commandCounterRef.current++,
        type: 'cmd',
        cmd,
        args,
      }

      const historyEntry: CommandHistoryEntry = {
        id: command.id,
        cmd,
        risk,
        args,
        issuedAtIso: new Date().toISOString(),
        status: 'queued',
        note,
      }

      setCommands((current) => [
        historyEntry,
        ...current,
      ].slice(0, 40))

      await runSerializedControllerOperation(async () => {
        try {
          await transportRef.current?.sendCommand(command)
          setCommands((current) =>
            current.map((entry) =>
              entry.id === command.id
                ? {
                    ...entry,
                    status: 'sent',
                  }
                : entry,
            ),
          )
        } catch (error) {
          const message =
            error instanceof Error ? error.message : 'Command transport failed.'
          setCommands((current) =>
            current.map((entry) =>
              entry.id === command.id
                ? {
                    ...entry,
                    status: 'error',
                    note: message,
                  }
                : entry,
            ),
          )
          appendEvent({
            id: makeEventId('command-error'),
            atIso: new Date().toISOString(),
            severity: 'critical',
            category: 'transport',
            title: 'Command delivery failed',
            detail: message,
            module: moduleFromCommand(cmd),
            source: 'host',
          })
        }
      })
    },
    [appendEvent, runSerializedControllerOperation],
  )

  const issueCommandAwaitAck = useCallback(
    async (
      cmd: string,
      risk: CommandRisk,
      note: string,
      args?: Record<string, number | string | boolean>,
      options?: {
        logHistory?: boolean
        timeoutMs?: number
      },
    ): Promise<CommandAckResult> => {
      const logHistory = options?.logHistory ?? true
      const timeoutMs = effectiveCommandAckTimeoutMs(
        cmd,
        options?.timeoutMs ?? COMMAND_ACK_TIMEOUT_MS,
        transportKind,
      )
      const command: CommandEnvelope = {
        id: commandCounterRef.current++,
        type: 'cmd',
        cmd,
        args,
      }

      if (logHistory) {
        const historyEntry: CommandHistoryEntry = {
          id: command.id,
          cmd,
          risk,
          args,
          issuedAtIso: new Date().toISOString(),
          status: 'queued',
          note,
        }

        setCommands((current) => [historyEntry, ...current].slice(0, 40))
      }

      return runSerializedControllerOperation(async () => {
        const ackPromise = waitForCommandAck(command.id, timeoutMs)

        try {
          await transportRef.current?.sendCommand(command)

          if (logHistory) {
            setCommands((current) =>
              current.map((entry) =>
                entry.id === command.id
                  ? {
                      ...entry,
                      status: 'sent',
                    }
                  : entry,
              ),
            )
          }

          const ack = await ackPromise
          const successNote =
            ack.ok
              ? noteFromSnapshotForCommand(cmd, snapshotRef.current) ?? ack.note
              : ack.note

          if (!ack.ok) {
            if (logHistory) {
              setCommands((current) =>
                current.map((entry) =>
                  entry.id === command.id
                    ? {
                        ...entry,
                        status: 'error',
                        note: ack.note,
                      }
                    : entry,
                ),
              )
            }

            appendEvent({
              id: makeEventId('command-timeout'),
              atIso: new Date().toISOString(),
              severity: 'warn',
              category: 'transport',
              title: ack.note.toLowerCase().includes('timed out')
                ? 'Command timed out'
                : 'Command rejected',
              detail: `${cmd}: ${ack.note}`,
              module: moduleFromCommand(cmd),
              source: 'host',
            })

            if (
              (transportKind === 'serial' || transportKind === 'wifi') &&
              ack.note.toLowerCase().includes('timed out')
            ) {
              const quietForMs = Date.now() - lastInboundMessageAtRef.current
              if (
                quietForMs >= linkIdleProbeDelayMs(transportKind) &&
                livenessProbeRef.current === null
              ) {
                void issueLinkLivenessProbe()
              }
            }
          }

          if (ack.ok && logHistory && successNote !== ack.note) {
            setCommands((current) =>
              current.map((entry) =>
                entry.id === command.id
                  ? {
                      ...entry,
                      status: 'ack',
                      note: successNote,
                    }
                  : entry,
              ),
            )
          }

          if (ack.ok && isBusCommand(cmd)) {
            appendEvent({
              id: makeEventId('bus-command-result'),
              atIso: new Date().toISOString(),
              severity: 'info',
              category: 'bus',
              title:
                cmd === 'i2c_scan'
                  ? 'I2C scan completed'
                  : cmd.startsWith('i2c_')
                    ? 'I2C transaction completed'
                    : 'SPI transaction completed',
              detail: successNote,
              module: 'bus',
              source: 'host',
            })
          }

          return {
            ok: ack.ok,
            note: successNote,
          }
        } catch (error) {
          const message =
            error instanceof Error ? error.message : 'Command transport failed.'
          const pending = pendingAcksRef.current.get(command.id)

          if (pending !== undefined) {
            window.clearTimeout(pending.timeoutId)
            pending.resolve({
              ok: false,
              note: message,
            })
            pendingAcksRef.current.delete(command.id)
          }

          if (logHistory) {
            setCommands((current) =>
              current.map((entry) =>
                entry.id === command.id
                  ? {
                      ...entry,
                      status: 'error',
                      note: message,
                    }
                  : entry,
              ),
            )
          }

          appendEvent({
            id: makeEventId('command-error'),
            atIso: new Date().toISOString(),
            severity: 'critical',
            category: 'transport',
            title: 'Command delivery failed',
            detail: message,
            module: moduleFromCommand(cmd),
            source: 'host',
          })

          if (
            (transportKind === 'serial' || transportKind === 'wifi') &&
            message.toLowerCase().includes('not connected')
          ) {
            markTransportUnhealthy(message, 'transport')
          }

          return {
            ok: false,
            note: message,
          }
        }
      })
    },
    [
      appendEvent,
      issueLinkLivenessProbe,
      markTransportUnhealthy,
      runSerializedControllerOperation,
      transportKind,
      waitForCommandAck,
    ],
  )

  const beginFirmwareTransfer = useCallback(
    async (pkg: FirmwarePackageDescriptor) => {
      const currentTransport = transportRef.current

      if (currentTransport?.beginFirmwareTransfer === undefined) {
        appendEvent({
          id: makeEventId('firmware-unsupported'),
          atIso: new Date().toISOString(),
          severity: 'warn',
          category: 'firmware',
          title: 'Direct transfer unavailable',
          detail:
            'This transport can stage packages and run preflight, but it does not implement a flashing pipeline yet.',
          module: 'firmware',
          source: 'host',
          operation: 'transfer',
        })
        return
      }

      setFirmwareProgress({
        phase: 'queue',
        percent: 0,
        detail: 'Waiting for transport handoff…',
      })
      flashRecoveryUntilRef.current = Date.now() + 15000

      try {
        await currentTransport.beginFirmwareTransfer(pkg, (progress) => {
          setFirmwareProgress(progress)
        })
      } catch (error) {
        const message =
          error instanceof Error ? error.message : 'Browser flash failed.'
        appendEvent({
          id: makeEventId('firmware-error'),
          atIso: new Date().toISOString(),
          severity: 'critical',
          category: 'firmware',
          title: 'Browser flash failed',
          detail: message,
          module: 'firmware',
          source: 'host',
          operation: 'transfer',
        })
        setFirmwareProgress({
          phase: 'error',
          percent: 100,
          detail: message,
        })
      }
    },
    [appendEvent],
  )

  const configureSessionAutosave = useCallback(async () => {
    if (!supportsSessionAutosave()) {
      setSessionAutosave({
        supported: false,
        armed: false,
        fileName: null,
        saving: false,
        lastSavedAtIso: null,
        error:
          'This browser does not support file-backed autosave. Use Chrome or Edge for a persistent session file.',
      })
      return
    }

    try {
      const handle = await pickSessionAutosaveHandle()
      const permissionGranted = await ensureSessionAutosavePermission(handle, true)

      if (!permissionGranted) {
        throw new DOMException(
          'Browser write permission for the autosave file was not granted.',
          'NotAllowedError',
        )
      }

      sessionAutosaveHandleRef.current = handle
      await saveSessionAutosaveHandle(handle)
      setSessionAutosave((current) => ({
        ...current,
        supported: true,
        armed: true,
        fileName: handle.name,
        error: null,
      }))
      autosaveDirtyRef.current = true
      await flushSessionAutosave(true)
    } catch (error) {
      setSessionAutosave((current) => ({
        ...current,
        error: describeSessionAutosaveError(error),
      }))
    }
  }, [flushSessionAutosave])

  const disableSessionAutosave = useCallback(async () => {
    sessionAutosaveHandleRef.current = null

    try {
      await clearSessionAutosaveHandle()
    } catch (error) {
      setSessionAutosave((current) => ({
        ...current,
        error: describeSessionAutosaveError(error),
      }))
      return
    }

    setSessionAutosave((current) => ({
      ...current,
      armed: false,
      fileName: null,
      saving: false,
      error: null,
    }))
  }, [])

  const clearSessionHistory = useCallback(() => {
    setEvents([])
    setCommands([])
  }, [])

  const exportSession = useCallback(() => {
    downloadSessionArchive(
      makeSessionArchivePayload({
        transportKind: transportKindRef.current,
        snapshot: snapshotRef.current,
        events: eventsRef.current,
        commands: commandsRef.current,
        firmwareProgress: firmwareProgressRef.current,
      }),
    )
  }, [])

  return {
    transportKind,
    wifiUrl,
    setWifiUrl: setWifiUrlState,
    setTransportKind,
    transportStatus,
    transportDetail,
    transportRecovering,
    snapshot,
    telemetryStore: telemetryStoreRef.current,
    events,
    commands,
    firmwareProgress,
    sessionAutosave,
    supportsFirmwareTransfer: transport.supportsFirmwareTransfer,
    connect,
    disconnect,
    issueCommand,
    issueCommandAwaitAck,
    beginFirmwareTransfer,
    exportSession,
    clearSessionHistory,
    configureSessionAutosave,
    disableSessionAutosave,
  }
}
