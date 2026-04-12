export type PageId = 'system' | 'operate' | 'integrate' | 'update' | 'history'
export type TransportMode = 'browser-mock' | 'serial' | 'wireless'
export type TransportStatus = 'disconnected' | 'connecting' | 'connected' | 'error'
export type PowerTier = 'unknown' | 'programming_only' | 'insufficient' | 'reduced' | 'full'
export type RuntimeMode = 'binary_trigger' | 'modulated_host'
export type Severity = 'ok' | 'info' | 'warn' | 'critical'
export type Tone = 'neutral' | 'ok' | 'warn' | 'critical'
export type EventSource = 'firmware' | 'host' | 'derived'
export type CommandRisk = 'read' | 'write' | 'service' | 'firmware'
export type BusKind = 'i2c' | 'spi'
export type WirelessMode = 'softap' | 'station'
export type DeploymentTargetMode = 'temp' | 'lambda'
export type DacReferenceMode = 'internal' | 'external'
export type DacSyncMode = 'async' | 'sync'
export type HapticMode =
  | 'internal_trigger'
  | 'external_edge'
  | 'external_level'
  | 'pwm_analog'
  | 'audio_to_vibe'
  | 'rtp'
  | 'diagnostics'
  | 'auto_cal'
export type HapticActuator = 'erm' | 'lra'
export type ModuleKey =
  | 'imu'
  | 'dac'
  | 'haptic'
  | 'tof'
  | 'buttons'
  | 'pd'
  | 'laserDriver'
  | 'tec'
export type SystemState =
  | 'BOOT_INIT'
  | 'PROGRAMMING_ONLY'
  | 'SAFE_IDLE'
  | 'POWER_NEGOTIATION'
  | 'LIMITED_POWER_IDLE'
  | 'TEC_WARMUP'
  | 'TEC_SETTLING'
  | 'READY_ALIGNMENT'
  | 'READY_NIR'
  | 'ALIGNMENT_ACTIVE'
  | 'NIR_ACTIVE'
  | 'FAULT_LATCHED'
  | 'SERVICE_MODE'

export interface PortInfo {
  name: string
  label: string
}

export interface CommandEnvelope {
  id: number
  cmd: string
  args?: Record<string, unknown>
}

export interface CommandLogEntry {
  id: number
  cmd: string
  note: string
  atIso: string
  status: 'queued' | 'sent' | 'ok' | 'error'
  detail: string
  risk: CommandRisk
  module: string
}

export interface EventEntry {
  id: string
  atIso: string
  category: string
  title: string
  detail: string
  tone: Tone
  severity: Severity
  source: EventSource
  module: string
  summary?: string
  bus?: BusKind
  device?: string
  operation?: string
  addressHex?: string
  registerHex?: string
  registerName?: string
  valueHex?: string
  decodedDetail?: string
}

export interface WirelessNetwork {
  ssid: string
  rssiDbm: number
  channel: number
  secure: boolean
}

export interface GpioPin {
  gpioNum: number
  modulePin: number
  outputCapable: boolean
  inputEnabled: boolean
  outputEnabled: boolean
  openDrainEnabled: boolean
  pullupEnabled: boolean
  pulldownEnabled: boolean
  levelHigh: boolean
  overrideActive: boolean
  overrideMode: 'firmware' | 'input' | 'output'
  overrideLevelHigh: boolean
  overridePullupEnabled: boolean
  overridePulldownEnabled: boolean
}

export interface ModuleState {
  expectedPresent: boolean
  debugEnabled: boolean
  detected: boolean
  healthy: boolean
}

export interface PdSinkProfile {
  enabled: boolean
  voltageV: number
  currentA: number
}

export interface FirmwareSignature {
  schemaVersion: number
  productName: string
  projectName: string
  boardName: string
  protocolVersion: string
  hardwareScope: string
  firmwareVersion: string
  buildUtc: string
  payloadSha256Hex: string
  verified?: boolean
}

export interface FirmwareSegment {
  name: string
  fileName: string
  path: string
  offsetHex: string
  bytes: number
  sha256: string
  role: 'app' | 'bootloader' | 'partition-table' | 'data' | 'unknown'
}

export interface FirmwareTransferSupport {
  supported: boolean
  note: string
}

export interface FirmwareInspection {
  path: string
  fileName: string
  packageName: string
  version: string
  board: string
  note: string
  bytes: number
  sha256: string
  extension: string
  format: 'binary' | 'manifest'
  rawBinary: boolean
  flashOffsetHex: string
  signature: FirmwareSignature | null
  segments: FirmwareSegment[]
  transfer: FirmwareTransferSupport
}

export interface FlashProgress {
  phase: string
  percent: number
  detail: string
}

export interface SessionAutosaveStatus {
  supported: boolean
  enabled: boolean
  targetPath: string
  lastWriteAtIso: string | null
  lastError: string
}

export interface Snapshot {
  identity: {
    label: string
    firmwareVersion: string
    hardwareRevision: string
    serialNumber: string
    protocolVersion: string
    boardName: string
    buildUtc: string
  }
  session: {
    uptimeSeconds: number
    state: SystemState
    powerTier: PowerTier
    bootReason: string
  }
  transport: {
    mode: TransportMode
    status: TransportStatus
    detail: string
    serialPort: string
    wirelessUrl: string
  }
  wireless: {
    started: boolean
    mode: WirelessMode
    apReady: boolean
    stationConfigured: boolean
    stationConnecting: boolean
    stationConnected: boolean
    clientCount: number
    ssid: string
    stationSsid: string
    stationRssiDbm: number
    stationChannel: number
    scanInProgress: boolean
    scannedNetworks: WirelessNetwork[]
    ipAddress: string
    wsUrl: string
    lastError: string
  }
  pd: {
    contractValid: boolean
    negotiatedPowerW: number
    sourceVoltageV: number
    sourceCurrentA: number
    operatingCurrentA: number
    contractObjectPosition: number
    sinkProfileCount: number
    sinkProfiles: PdSinkProfile[]
    sourceIsHostOnly: boolean
  }
  rails: {
    ld: { enabled: boolean; pgood: boolean }
    tec: { enabled: boolean; pgood: boolean }
  }
  imu: {
    valid: boolean
    fresh: boolean
    beamPitchDeg: number
    beamRollDeg: number
    beamYawDeg: number
    beamYawRelative: boolean
    beamPitchLimitDeg: number
  }
  tof: {
    valid: boolean
    fresh: boolean
    distanceM: number
    minRangeM: number
    maxRangeM: number
  }
  buttons: {
    stage1Pressed: boolean
    stage2Pressed: boolean
  }
  laser: {
    alignmentEnabled: boolean
    nirEnabled: boolean
    driverStandby: boolean
    telemetryValid: boolean
    commandVoltageV: number
    commandedCurrentA: number
    currentMonitorVoltageV: number
    measuredCurrentA: number
    loopGood: boolean
    driverTempVoltageV: number
    driverTempC: number
  }
  tec: {
    targetTempC: number
    targetLambdaNm: number
    actualLambdaNm: number
    telemetryValid: boolean
    commandVoltageV: number
    tempGood: boolean
    tempC: number
    tempAdcVoltageV: number
    currentA: number
    voltageV: number
    settlingSecondsRemaining: number
  }
  safety: {
    allowAlignment: boolean
    allowNir: boolean
    horizonBlocked: boolean
    distanceBlocked: boolean
    lambdaDriftBlocked: boolean
    tecTempAdcBlocked: boolean
    horizonThresholdDeg: number
    horizonHysteresisDeg: number
    tofMinRangeM: number
    tofMaxRangeM: number
    tofHysteresisM: number
    imuStaleMs: number
    tofStaleMs: number
    railGoodTimeoutMs: number
    lambdaDriftLimitNm: number
    lambdaDriftHysteresisNm: number
    lambdaDriftHoldMs: number
    ldOvertempLimitC: number
    tecTempAdcTripV: number
    tecTempAdcHysteresisV: number
    tecTempAdcHoldMs: number
    tecMinCommandC: number
    tecMaxCommandC: number
    tecReadyToleranceC: number
    maxLaserCurrentA: number
    actualLambdaNm: number
    targetLambdaNm: number
    lambdaDriftNm: number
    tempAdcVoltageV: number
  }
  deployment: {
    active: boolean
    running: boolean
    ready: boolean
    failed: boolean
    currentStep: string
    lastCompletedStep: string
    failureCode: string
    failureReason: string
    targetMode: DeploymentTargetMode
    targetTempC: number
    targetLambdaNm: number
    maxLaserCurrentA: number
    maxOpticalPowerW: number
    steps: Array<{ key: string; label: string; status: 'inactive' | 'pending' | 'in_progress' | 'passed' | 'failed' }>
  }
  operate: {
    targetMode: DeploymentTargetMode
    runtimeMode: RuntimeMode
    runtimeModeSwitchAllowed: boolean
    runtimeModeLockReason: string
    requestedAlignmentEnabled: boolean
    requestedNirEnabled: boolean
    modulationEnabled: boolean
    modulationFrequencyHz: number
    modulationDutyCyclePct: number
    lowStateCurrentA: number
  }
  integrate: {
    serviceModeRequested: boolean
    serviceModeActive: boolean
    interlocksDisabled: boolean
    profileName: string
    profileRevision: number
    persistenceDirty: boolean
    persistenceAvailable: boolean
    lastSaveOk: boolean
    power: {
      ldRequested: boolean
      tecRequested: boolean
    }
    illumination: {
      tofEnabled: boolean
      tofDutyCyclePct: number
      tofFrequencyHz: number
    }
    modules: Record<ModuleKey, ModuleState>
    tuning: {
      dacLdChannelV: number
      dacTecChannelV: number
      dacReferenceMode: DacReferenceMode
      dacGain2x: boolean
      dacRefDiv: boolean
      dacSyncMode: DacSyncMode
      imuOdrHz: number
      imuAccelRangeG: number
      imuGyroRangeDps: number
      imuGyroEnabled: boolean
      imuLpf2Enabled: boolean
      imuTimestampEnabled: boolean
      imuBduEnabled: boolean
      imuIfIncEnabled: boolean
      imuI2cDisabled: boolean
      tofMinRangeM: number
      tofMaxRangeM: number
      tofStaleTimeoutMs: number
      pdProfiles: PdSinkProfile[]
      pdProgrammingOnlyMaxW: number
      pdReducedModeMinW: number
      pdReducedModeMaxW: number
      pdFullModeMinW: number
      pdFirmwarePlanEnabled: boolean
      hapticEffectId: number
      hapticMode: HapticMode
      hapticLibrary: number
      hapticActuator: HapticActuator
      hapticRtpLevel: number
    }
    tools: {
      lastI2cScan: string
      lastI2cOp: string
      lastSpiOp: string
      lastAction: string
    }
  }
  gpioInspector: {
    anyOverrideActive: boolean
    activeOverrideCount: number
    pins: GpioPin[]
  }
  fault: {
    latched: boolean
    activeCode: string
    activeCount: number
    tripCounter: number
    lastFaultAtIso: string | null
  }
}

export interface WorkbenchState {
  page: PageId
  snapshot: Snapshot
  ports: PortInfo[]
  firmware: FirmwareInspection | null
  flash: FlashProgress | null
  commands: CommandLogEntry[]
  events: EventEntry[]
  rawFeed: string[]
  sessionAutosave: SessionAutosaveStatus
}

export interface ProtocolOutcome {
  snapshot?: DeepPartial<Snapshot>
  event?: EventEntry
  commandAck?: {
    id: number
    ok: boolean
    note: string
  }
}

export interface BridgeEvent {
  kind: 'transport' | 'protocol-line' | 'flash-progress'
  channel: string
  status?: TransportStatus
  detail?: string
  line?: string
  phase?: string
  percent?: number
}

export type DeepPartial<T> = {
  [K in keyof T]?: T[K] extends Array<infer U>
    ? Array<DeepPartial<U>>
    : T[K] extends object
      ? DeepPartial<T[K]>
      : T[K]
}

export function severityToTone(severity: Severity): Tone {
  if (severity === 'critical') return 'critical'
  if (severity === 'warn') return 'warn'
  if (severity === 'ok') return 'ok'
  return 'neutral'
}

export function makeEmptySnapshot(): Snapshot {
  return {
    identity: {
      label: 'BSL Console V2',
      firmwareVersion: 'unavailable',
      hardwareRevision: 'rev-A',
      serialNumber: 'UNPROVISIONED',
      protocolVersion: 'host-v1',
      boardName: 'esp32s3',
      buildUtc: '',
    },
    session: {
      uptimeSeconds: 0,
      state: 'BOOT_INIT',
      powerTier: 'unknown',
      bootReason: 'power_on_reset',
    },
    transport: {
      mode: 'browser-mock',
      status: 'connected',
      detail: 'Browser inspection mode uses a local deterministic dataset.',
      serialPort: '',
      wirelessUrl: 'ws://192.168.4.1/ws',
    },
    wireless: {
      started: true,
      mode: 'softap',
      apReady: true,
      stationConfigured: false,
      stationConnecting: false,
      stationConnected: false,
      clientCount: 0,
      ssid: 'BSL-HTLS-Bench',
      stationSsid: '',
      stationRssiDbm: -58,
      stationChannel: 6,
      scanInProgress: false,
      scannedNetworks: [],
      ipAddress: '192.168.4.1',
      wsUrl: 'ws://192.168.4.1/ws',
      lastError: '',
    },
    pd: {
      contractValid: true,
      negotiatedPowerW: 2.5,
      sourceVoltageV: 5,
      sourceCurrentA: 0.5,
      operatingCurrentA: 0.5,
      contractObjectPosition: 0,
      sinkProfileCount: 3,
      sinkProfiles: [
        { enabled: true, voltageV: 5, currentA: 0.5 },
        { enabled: true, voltageV: 9, currentA: 2 },
        { enabled: true, voltageV: 12, currentA: 1.5 },
      ],
      sourceIsHostOnly: true,
    },
    rails: {
      ld: { enabled: false, pgood: false },
      tec: { enabled: false, pgood: false },
    },
    imu: {
      valid: true,
      fresh: true,
      beamPitchDeg: -4.2,
      beamRollDeg: 2.1,
      beamYawDeg: 12.8,
      beamYawRelative: false,
      beamPitchLimitDeg: 0,
    },
    tof: {
      valid: true,
      fresh: true,
      distanceM: 0.38,
      minRangeM: 0.21,
      maxRangeM: 0.95,
    },
    buttons: {
      stage1Pressed: false,
      stage2Pressed: false,
    },
    laser: {
      alignmentEnabled: false,
      nirEnabled: false,
      driverStandby: true,
      telemetryValid: false,
      commandVoltageV: 0,
      commandedCurrentA: 0,
      currentMonitorVoltageV: 0,
      measuredCurrentA: 0,
      loopGood: false,
      driverTempVoltageV: 0,
      driverTempC: 0,
    },
    tec: {
      targetTempC: 25,
      targetLambdaNm: 778.56,
      actualLambdaNm: 778.56,
      telemetryValid: false,
      commandVoltageV: 0.962,
      tempGood: false,
      tempC: 0,
      tempAdcVoltageV: 0,
      currentA: 0,
      voltageV: 0,
      settlingSecondsRemaining: 1,
    },
    safety: {
      allowAlignment: false,
      allowNir: false,
      horizonBlocked: false,
      distanceBlocked: false,
      lambdaDriftBlocked: false,
      tecTempAdcBlocked: false,
      horizonThresholdDeg: 0,
      horizonHysteresisDeg: 3,
      tofMinRangeM: 0.21,
      tofMaxRangeM: 0.95,
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
      actualLambdaNm: 778.56,
      targetLambdaNm: 778.56,
      lambdaDriftNm: 0,
      tempAdcVoltageV: 0,
    },
    deployment: {
      active: false,
      running: false,
      ready: false,
      failed: false,
      currentStep: 'none',
      lastCompletedStep: 'none',
      failureCode: 'none',
      failureReason: '',
      targetMode: 'lambda',
      targetTempC: 25,
      targetLambdaNm: 778.56,
      maxLaserCurrentA: 5,
      maxOpticalPowerW: 5,
      steps: [],
    },
    operate: {
      targetMode: 'lambda',
      runtimeMode: 'modulated_host',
      runtimeModeSwitchAllowed: true,
      runtimeModeLockReason: '',
      requestedAlignmentEnabled: false,
      requestedNirEnabled: false,
      modulationEnabled: false,
      modulationFrequencyHz: 2000,
      modulationDutyCyclePct: 50,
      lowStateCurrentA: 0,
    },
    integrate: {
      serviceModeRequested: false,
      serviceModeActive: false,
      interlocksDisabled: false,
      profileName: 'precision bench',
      profileRevision: 8,
      persistenceDirty: false,
      persistenceAvailable: true,
      lastSaveOk: true,
      power: {
        ldRequested: false,
        tecRequested: false,
      },
      illumination: {
        tofEnabled: false,
        tofDutyCyclePct: 0,
        tofFrequencyHz: 20000,
      },
      modules: {
        imu: { expectedPresent: true, debugEnabled: true, detected: true, healthy: true },
        dac: { expectedPresent: true, debugEnabled: true, detected: true, healthy: true },
        haptic: { expectedPresent: true, debugEnabled: true, detected: true, healthy: true },
        tof: { expectedPresent: true, debugEnabled: true, detected: true, healthy: true },
        buttons: { expectedPresent: false, debugEnabled: false, detected: false, healthy: false },
        pd: { expectedPresent: true, debugEnabled: true, detected: true, healthy: true },
        laserDriver: { expectedPresent: true, debugEnabled: true, detected: false, healthy: false },
        tec: { expectedPresent: true, debugEnabled: true, detected: false, healthy: false },
      },
      tuning: {
        dacLdChannelV: 0,
        dacTecChannelV: 0.962,
        dacReferenceMode: 'internal',
        dacGain2x: false,
        dacRefDiv: false,
        dacSyncMode: 'async',
        imuOdrHz: 208,
        imuAccelRangeG: 4,
        imuGyroRangeDps: 250,
        imuGyroEnabled: true,
        imuLpf2Enabled: false,
        imuTimestampEnabled: false,
        imuBduEnabled: true,
        imuIfIncEnabled: true,
        imuI2cDisabled: false,
        tofMinRangeM: 0.21,
        tofMaxRangeM: 0.95,
        tofStaleTimeoutMs: 100,
        pdProfiles: [
          { enabled: true, voltageV: 5, currentA: 0.5 },
          { enabled: true, voltageV: 9, currentA: 2 },
          { enabled: true, voltageV: 12, currentA: 1.5 },
        ],
        pdProgrammingOnlyMaxW: 2.5,
        pdReducedModeMinW: 9,
        pdReducedModeMaxW: 18,
        pdFullModeMinW: 24,
        pdFirmwarePlanEnabled: true,
        hapticEffectId: 1,
        hapticMode: 'internal_trigger',
        hapticLibrary: 1,
        hapticActuator: 'erm',
        hapticRtpLevel: 64,
      },
      tools: {
        lastI2cScan: '0x28 0x29 0x48 0x5A',
        lastI2cOp: 'read 0x48 reg 0x07 -> 0x00',
        lastSpiOp: 'read imu reg 0x0F -> 0x6C',
        lastAction: 'Inspection-mode dataset loaded.',
      },
    },
    gpioInspector: {
      anyOverrideActive: false,
      activeOverrideCount: 0,
      pins: [],
    },
    fault: {
      latched: false,
      activeCode: 'none',
      activeCount: 0,
      tripCounter: 0,
      lastFaultAtIso: null,
    },
  }
}

export function makeDefaultWorkbenchState(): WorkbenchState {
  return {
    page: 'system',
    snapshot: makeEmptySnapshot(),
    ports: [],
    firmware: null,
    flash: null,
    commands: [],
    events: [],
    rawFeed: [],
    sessionAutosave: {
      supported: true,
      enabled: false,
      targetPath: '',
      lastWriteAtIso: null,
      lastError: '',
    },
  }
}
