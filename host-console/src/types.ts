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

export type PowerTier =
  | 'unknown'
  | 'programming_only'
  | 'insufficient'
  | 'reduced'
  | 'full'

export type Severity = 'ok' | 'info' | 'warn' | 'critical'
export type ThemeMode = 'dark' | 'light'
export type EventSource = 'firmware' | 'host' | 'derived'
export type EventBus = 'i2c' | 'spi'
export type RuntimeMode = 'binary_trigger' | 'modulated_host'

export type TransportKind = 'mock' | 'serial' | 'wifi'
export type WirelessMode = 'softap' | 'station'
export type DeploymentTargetMode = 'temp' | 'lambda'
export type DeploymentStepStatus =
  | 'inactive'
  | 'pending'
  | 'in_progress'
  | 'passed'
  | 'failed'

export type TransportStatus = 'disconnected' | 'connecting' | 'connected' | 'error'

export interface WirelessScanNetwork {
  ssid: string
  rssiDbm: number
  channel: number
  secure: boolean
}

export type CommandRisk = 'read' | 'write' | 'service' | 'firmware'
export type BenchTargetMode = 'temp' | 'lambda'
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
export type GpioOverrideMode = 'firmware' | 'input' | 'output'
export type ModuleKey =
  | 'imu'
  | 'dac'
  | 'haptic'
  | 'tof'
  | 'buttons'
  | 'pd'
  | 'laserDriver'
  | 'tec'

export interface RailState {
  enabled: boolean
  pgood: boolean
}

export interface PdStatus {
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

export interface WirelessStatus {
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
  scannedNetworks: WirelessScanNetwork[]
  ipAddress: string
  wsUrl: string
  lastError: string
}

export interface ImuStatus {
  valid: boolean
  fresh: boolean
  beamPitchDeg: number
  beamRollDeg: number
  beamYawDeg: number
  beamYawRelative: boolean
  beamPitchLimitDeg: number
}

export interface TofStatus {
  valid: boolean
  fresh: boolean
  distanceM: number
  minRangeM: number
  maxRangeM: number
}

export interface LaserStatus {
  alignmentEnabled: boolean
  nirEnabled: boolean
  driverStandby: boolean
  telemetryValid: boolean
  commandVoltageV: number
  measuredCurrentA: number
  commandedCurrentA: number
  currentMonitorVoltageV: number
  loopGood: boolean
  driverTempVoltageV: number
  driverTempC: number
}

export interface TecStatus {
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

export interface BenchControlStatus {
  targetMode: BenchTargetMode
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

export interface DeploymentStep {
  key: string
  label: string
  status: DeploymentStepStatus
}

export interface DeploymentStatus {
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
  steps: DeploymentStep[]
}

export interface BringupModuleStatus {
  expectedPresent: boolean
  debugEnabled: boolean
  detected: boolean
  healthy: boolean
}

export interface BringupModuleMap {
  imu: BringupModuleStatus
  dac: BringupModuleStatus
  haptic: BringupModuleStatus
  tof: BringupModuleStatus
  buttons: BringupModuleStatus
  pd: BringupModuleStatus
  laserDriver: BringupModuleStatus
  tec: BringupModuleStatus
}

export interface PdSinkProfile {
  enabled: boolean
  voltageV: number
  currentA: number
}

export interface BringupTuning {
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

export interface BringupTools {
  lastI2cScan: string
  lastI2cOp: string
  lastSpiOp: string
  lastAction: string
}

export interface TofIlluminationStatus {
  enabled: boolean
  dutyCyclePct: number
  frequencyHz: number
}

export interface ButtonRuntimeStatus {
  stage1Pressed: boolean
  stage2Pressed: boolean
  stage1Edge: boolean
  stage2Edge: boolean
}

export interface DacPeripheralReadback {
  reachable: boolean
  configured: boolean
  refAlarm: boolean
  syncReg: number
  configReg: number
  gainReg: number
  statusReg: number
  dataAReg: number
  dataBReg: number
  lastErrorCode: number
  lastError: string
}

export interface PdPeripheralReadback {
  reachable: boolean
  attached: boolean
  ccStatusReg: number
  pdoCountReg: number
  rdoStatusRaw: number
}

export interface ImuPeripheralReadback {
  reachable: boolean
  configured: boolean
  whoAmI: number
  statusReg: number
  ctrl1XlReg: number
  ctrl2GReg: number
  ctrl3CReg: number
  ctrl4CReg: number
  ctrl10CReg: number
  lastErrorCode: number
  lastError: string
}

export interface HapticPeripheralReadback {
  reachable: boolean
  enablePinHigh: boolean
  triggerPinHigh: boolean
  modeReg: number
  libraryReg: number
  goReg: number
  feedbackReg: number
  lastErrorCode: number
  lastError: string
}

export interface TofPeripheralReadback {
  reachable: boolean
  configured: boolean
  interruptLineHigh: boolean
  ledCtrlAsserted: boolean
  dataReady: boolean
  bootState: number
  rangeStatus: number
  sensorId: number
  distanceMm: number
  lastErrorCode: number
  lastError: string
}

export interface PeripheralReadback {
  dac: DacPeripheralReadback
  pd: PdPeripheralReadback
  imu: ImuPeripheralReadback
  haptic: HapticPeripheralReadback
  tof: TofPeripheralReadback
}

export interface GpioPinReadback {
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
  overrideMode: GpioOverrideMode
  overrideLevelHigh: boolean
  overridePullupEnabled: boolean
  overridePulldownEnabled: boolean
}

export interface GpioInspectorStatus {
  anyOverrideActive: boolean
  activeOverrideCount: number
  pins: GpioPinReadback[]
}

export interface BringupStatus {
  serviceModeRequested: boolean
  serviceModeActive: boolean
  interlocksDisabled: boolean
  persistenceDirty: boolean
  persistenceAvailable: boolean
  lastSaveOk: boolean
  profileRevision: number
  profileName: string
  power: {
    ldRequested: boolean
    tecRequested: boolean
  }
  illumination: {
    tof: TofIlluminationStatus
  }
  modules: BringupModuleMap
  tuning: BringupTuning
  tools: BringupTools
}

export interface FaultSummary {
  latched: boolean
  activeCode: string
  activeCount: number
  tripCounter: number
  lastFaultAtIso: string | null
}

export interface RealtimeSessionStatus {
  uptimeSeconds: number
  state: SystemState
  powerTier: PowerTier
}

export interface RealtimePdStatus {
  contractValid: boolean
  negotiatedPowerW: number
  sourceVoltageV: number
  sourceCurrentA: number
  operatingCurrentA: number
  sourceIsHostOnly: boolean
}

export interface RealtimeSafetyStatus {
  allowAlignment: boolean
  allowNir: boolean
  horizonBlocked: boolean
  distanceBlocked: boolean
  lambdaDriftBlocked: boolean
  tecTempAdcBlocked: boolean
  actualLambdaNm: number
  targetLambdaNm: number
  lambdaDriftNm: number
  tempAdcVoltageV: number
}

export interface RealtimeBringupStatus {
  serviceModeRequested: boolean
  serviceModeActive: boolean
  interlocksDisabled: boolean
  illumination: {
    tof: TofIlluminationStatus
  }
}

export interface RealtimeDeploymentStatus {
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
  steps: DeploymentStep[]
}

export interface RealtimeFaultSummary {
  latched: boolean
  activeCode: string
  activeCount: number
  tripCounter: number
}

type DeepPartial<T> = {
  [K in keyof T]?: T[K] extends object ? DeepPartial<T[K]> : T[K]
}

export interface RealtimeTelemetry {
  session: RealtimeSessionStatus
  pd: RealtimePdStatus
  rails: {
    ld: RailState
    tec: RailState
  }
  imu: ImuStatus
  tof: TofStatus
  laser: LaserStatus
  tec: TecStatus
  safety: RealtimeSafetyStatus
  buttons: ButtonRuntimeStatus
  bringup: RealtimeBringupStatus
  deployment: RealtimeDeploymentStatus
  fault: RealtimeFaultSummary
}

export type RealtimeTelemetryPatch = DeepPartial<RealtimeTelemetry>

export interface SafetyStatus {
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

export interface DeviceSnapshot {
  identity: {
    label: string
    firmwareVersion: string
    hardwareRevision: string
    serialNumber: string
    protocolVersion: string
  }
  session: {
    uptimeSeconds: number
    state: SystemState
    powerTier: PowerTier
    bootReason: string
    connectedAtIso: string
  }
  wireless: WirelessStatus
  pd: PdStatus
  rails: {
    ld: RailState
    tec: RailState
  }
  imu: ImuStatus
  tof: TofStatus
  laser: LaserStatus
  tec: TecStatus
  buttons: ButtonRuntimeStatus
  peripherals: PeripheralReadback
  gpioInspector: GpioInspectorStatus
  bench: BenchControlStatus
  safety: SafetyStatus
  bringup: BringupStatus
  deployment: DeploymentStatus
  fault: FaultSummary
  counters: {
    commsTimeouts: number
    watchdogTrips: number
    brownouts: number
  }
}

export interface SessionEvent {
  id: string
  atIso: string
  severity: Severity
  category: string
  title: string
  detail: string
  module?: string
  source?: EventSource
  bus?: EventBus
  operation?: 'scan' | 'read' | 'write' | 'status' | 'command' | 'transfer'
  device?: string
  addressHex?: string
  registerHex?: string
  registerName?: string
  valueHex?: string
  decodedDetail?: string
  summary?: string
}

export interface CommandEnvelope {
  id: number
  type: 'cmd'
  cmd: string
  args?: Record<string, number | string | boolean>
}

export interface CommandHistoryEntry {
  id: number
  cmd: string
  risk: CommandRisk
  args?: Record<string, number | string | boolean>
  issuedAtIso: string
  status: 'queued' | 'sent' | 'ack' | 'error'
  note: string
}

export interface FirmwareSegment {
  name: string
  address?: string
  bytes: number
  sha256?: string
}

export interface FirmwareFlashImage {
  name: string
  address: number
  bytes: number
  data?: Uint8Array
  sha256?: string
}

export interface FirmwareWebFlashPlan {
  supported: boolean
  note: string
  eraseAll: boolean
  images: FirmwareFlashImage[]
}

export interface FirmwareEmbeddedSignature {
  schemaVersion: number
  productName: string
  projectName: string
  boardName: string
  protocolVersion: string
  hardwareScope: string
  firmwareVersion: string
  buildUtc: string
  payloadSha256: string
  verified: boolean
  note: string
}

export interface FirmwarePackageDescriptor {
  fileName: string
  packageName: string
  version: string
  board: string
  format: 'manifest' | 'binary'
  bytes: number
  sha256: string
  segments: FirmwareSegment[]
  notes: string[]
  loadedAtIso: string
  signature: FirmwareEmbeddedSignature | null
  webFlash: FirmwareWebFlashPlan
}

export interface FirmwareTransferProgress {
  phase: string
  percent: number
  detail: string
}

export interface SessionArchivePayload {
  exportedAtIso: string
  transportKind: TransportKind
  snapshot: DeviceSnapshot
  events: SessionEvent[]
  commands: CommandHistoryEntry[]
  firmwareProgress: FirmwareTransferProgress | null
}

export interface SessionAutosaveStatus {
  supported: boolean
  armed: boolean
  fileName: string | null
  saving: boolean
  lastSavedAtIso: string | null
  error: string | null
}

export type TransportMessage =
  | {
      kind: 'transport'
      status: TransportStatus
      detail?: string
    }
  | {
      kind: 'snapshot'
      snapshot: DeviceSnapshot
    }
  | {
      kind: 'telemetry'
      telemetry: RealtimeTelemetryPatch
    }
  | {
      kind: 'event'
      event: SessionEvent
    }
  | {
      kind: 'commandAck'
      commandId: number
      ok: boolean
      note: string
    }

export interface DeviceTransport {
  readonly kind: TransportKind
  readonly label: string
  readonly supportsFirmwareTransfer: boolean
  connect(): Promise<void>
  disconnect(): Promise<void>
  reconnectSilently?(): Promise<'connected' | 'retry' | 'no_grant'>
  subscribe(listener: (message: TransportMessage) => void): () => void
  sendCommand(command: CommandEnvelope): Promise<void>
  beginFirmwareTransfer?(
    pkg: FirmwarePackageDescriptor,
    onProgress: (progress: FirmwareTransferProgress) => void,
  ): Promise<void>
}

export interface CommandTemplate {
  id: string
  label: string
  command: string
  risk: CommandRisk
  description: string
}
