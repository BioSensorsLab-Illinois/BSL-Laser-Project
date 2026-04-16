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
  lastUpdatedMs: number
  snapshotFresh: boolean
  source: 'none' | 'boot_reconcile' | 'integrate_refresh' | 'cached'
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

/**
 * SBDN pin physical drive state on the ATLS6A214 laser driver (GPIO13).
 * Mirrors `laser_controller_sbdn_state_t` in firmware.
 *   - 'off'     → GPIO drive LOW, fast 20us shutdown
 *   - 'on'      → GPIO drive HIGH, driver operate
 *   - 'standby' → GPIO input / Hi-Z, external divider holds ~2.25V standby
 */
export type SbdnState = 'off' | 'on' | 'standby'

/**
 * Firmware-computed reason why a host-control action cannot be requested
 * right now. Produced on every bench status frame. Green alignment is
 * always 'none' (ungated per user directive 2026-04-14).
 *
 * Must stay in sync with `laser_controller_comms_nir_blocked_reason`
 * and `laser_controller_comms_led_blocked_reason` in firmware.
 */
export type NirBlockedReason =
  | 'none'
  | 'not-connected'
  | 'fault-latched'
  | 'deployment-off'
  | 'checklist-running'
  | 'checklist-not-ready'
  | 'ready-not-idle'
  | 'not-modulated-host'
  | 'power-not-full'
  | 'rail-not-good'
  | 'tec-not-settled'

export type LedBlockedReason =
  | 'none'
  | 'not-connected'
  | 'deployment-off'
  | 'checklist-running'

export interface HostControlReadiness {
  nirBlockedReason: NirBlockedReason
  alignmentBlockedReason: 'none'
  ledBlockedReason: LedBlockedReason
  sbdnState: SbdnState
}

/**
 * USB-Debug Mock Layer status. Mirrors the firmware module
 * `laser_controller_usb_debug_mock_status_t` and is published in every
 * bench status frame (`bench.usbDebugMock`).
 *
 * `active` true means the controller is synthesizing TEC/LD rail PGOOD
 * and telemetry to allow online testing from a USB-only session. The
 * GUI MUST render a loud, app-wide banner whenever this is true and
 * MUST flag every TEC/LD readout as synthesized. Failing to surface
 * the mock state is a safety-visibility regression (see AGENT.md).
 *
 * `pdConflictLatched` true means real PD power was detected while the
 * mock was active and the firmware latched the
 * `usb_debug_mock_pd_conflict` SYSTEM_MAJOR fault. The operator must
 * clear faults explicitly before the mock can be re-enabled.
 */
export interface UsbDebugMockStatus {
  active: boolean
  pdConflictLatched: boolean
  enablePending: boolean
  activatedAtMs: number
  deactivatedAtMs: number
  lastDisableReason: string
}

export interface BenchControlStatus {
  targetMode: BenchTargetMode
  runtimeMode: RuntimeMode
  runtimeModeSwitchAllowed: boolean
  runtimeModeLockReason: string
  requestedAlignmentEnabled: boolean
  appliedAlignmentEnabled: boolean
  requestedNirEnabled: boolean
  requestedCurrentA: number
  requestedLedEnabled: boolean
  requestedLedDutyCyclePct: number
  appliedLedOwner: 'none' | 'integrate_service' | 'operate_runtime' | 'deployment' | 'button_trigger'
  appliedLedPinHigh: boolean
  illuminationEnabled: boolean
  illuminationDutyCyclePct: number
  illuminationFrequencyHz: number
  modulationEnabled: boolean
  modulationFrequencyHz: number
  modulationDutyCyclePct: number
  lowStateCurrentA: number
  hostControlReadiness: HostControlReadiness
  usbDebugMock: UsbDebugMockStatus
}

export interface DeploymentStep {
  key: string
  label: string
  status: DeploymentStepStatus
  startedAtMs: number
  completedAtMs: number
}

export interface DeploymentSecondaryEffect {
  code: string
  reason: string
  atMs: number
}

export interface DeploymentReadyTruth {
  tecRailPgoodRaw: boolean
  tecRailPgoodFiltered: boolean
  tecTempGood: boolean
  tecAnalogPlausible: boolean
  ldRailPgoodRaw: boolean
  ldRailPgoodFiltered: boolean
  driverLoopGood: boolean
  sbdnHigh: boolean
  pcnLow: boolean
  idleBiasCurrentA: number
}

export interface DeploymentStatus {
  active: boolean
  running: boolean
  ready: boolean
  readyIdle: boolean
  readyQualified: boolean
  readyInvalidated: boolean
  failed: boolean
  phase: 'inactive' | 'entry' | 'checklist' | 'ready_idle' | 'failed'
  sequenceId: number
  currentStep: string
  currentStepIndex: number
  lastCompletedStep: string
  lastCompletedStepKey: string
  failureCode: string
  failureReason: string
  primaryFailureCode: string
  primaryFailureReason: string
  secondaryEffects: DeploymentSecondaryEffect[]
  targetMode: DeploymentTargetMode
  targetTempC: number
  targetLambdaNm: number
  maxLaserCurrentA: number
  maxOpticalPowerW: number
  readyTruth: DeploymentReadyTruth
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

/*
 * VL53L1X runtime calibration + ROI. Persisted in a dedicated NVS blob
 * ("tof_cal") on the controller; survives reboot and is re-applied on
 * every ToF init. Editable via `integrate.tof.set_calibration`.
 */
export type TofDistanceMode = 'short' | 'medium' | 'long'

export interface TofCalibration {
  distanceMode: TofDistanceMode
  timingBudgetMs: 20 | 33 | 50 | 100 | 200
  roiWidthSpads: number   // 4..16
  roiHeightSpads: number  // 4..16
  roiCenterSpad: number   // 0..255, default 199 = grid centre
  offsetMm: number        // signed mm
  xtalkCps: number        // unsigned counts/sec
  xtalkEnabled: boolean
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
  tofCalibration: TofCalibration
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
  /*
   * Side buttons on the MCP23017 expander GPA2/GPA3. Used by the firmware
   * binary-trigger policy to step the front LED brightness +/- 10 %. No
   * safety implication on side buttons.
   */
  side1Pressed: boolean
  side2Pressed: boolean
  side1Edge: boolean
  side2Edge: boolean
  /*
   * `boardReachable` is TRUE when the MCP23017 button expander has been
   * probed and configured. When FALSE, the firmware drops all pressed
   * state to inactive and the host should disable any "switch to
   * binary_trigger" UI.
   */
  boardReachable: boolean
  /*
   * Monotonic count of GPIO7 INTA ISR fires since boot. Useful for
   * verifying the interrupt line in service mode without physical button
   * presses.
   */
  isrFireCount: number
}

/*
 * RGB status LED on the button board (TLC59116 channels 0/1/2 wired
 * B/R/G respectively). Mirrors `laser_controller_rgb_led_state_t` in
 * firmware. `enabled=false` means the LED is dark regardless of R/G/B.
 * `blink=true` engages the firmware-configured 1 Hz / 50% group blink.
 */
export interface RgbLedState {
  r: number
  g: number
  b: number
  blink: boolean
  enabled: boolean
}

/*
 * Trigger phase exposed by the firmware so the host doesn't need to
 * recompute the RGB policy. Mirrors the helper in
 * components/laser_controller/src/laser_controller_comms.c
 * (laser_controller_comms_trigger_phase). Keep in lockstep with both
 * the firmware helper and the mock helper in mock-transport.ts.
 *
 *   off          - outside deployment-ready-idle, LED dark
 *   ready        - deployment ready, awaiting trigger (solid blue)
 *   armed        - stage1 pressed + LD_GOOD + TEC_GOOD (solid green)
 *   firing       - NIR currently emitting (solid red)
 *   interlock    - recoverable interlock active (flashing orange)
 *   lockout      - press-and-hold lockout latched (flashing orange)
 *   unrecoverable - SYSTEM_MAJOR fault or button board lost (flashing red)
 */
export type TriggerPhase =
  | 'off'
  | 'ready'
  | 'armed'
  | 'firing'
  | 'interlock'
  | 'lockout'
  | 'unrecoverable'

/*
 * Top-level button-board telemetry. Published in every snapshot under
 * `DeviceSnapshot.buttonBoard`. The MCP23017 + TLC59116 reachability and
 * the firmware-computed RGB / trigger-phase state. Four-place sync
 * target: firmware comms.c, mock-transport.ts, docs/protocol-spec.md.
 */
export interface ButtonBoardStatus {
  mcpAddr: string
  tlcAddr: string
  mcpReachable: boolean
  mcpConfigured: boolean
  mcpLastError: number
  mcpConsecFailures: number
  tlcReachable: boolean
  tlcConfigured: boolean
  tlcLastError: number
  isrFireCount: number
  rgb: RgbLedState & { testActive: boolean }
  ledBrightnessPct: number
  ledOwned: boolean
  triggerLockout: boolean
  triggerPhase: TriggerPhase
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

/**
 * Frozen at-trip diagnostic frame for a fault.
 *
 * Currently populated by firmware ONLY for `LD_OVERTEMP` (added
 * 2026-04-15 late, after operators saw spurious overtemp trips
 * with no visibility into the ADC reading or rail settle state).
 * The shape is general so future faults (loop-bad, rail-bad,
 * unexpected-current) can reuse it without a schema migration.
 *
 * The firmware captures this ONCE on the rising edge of the fault
 * and never overwrites it while the fault stays latched. Cleared
 * only by `clear_faults`.
 */
export interface FaultTriggerDiag {
  code: string
  measuredC: number
  measuredVoltageV: number
  limitC: number
  ldPgoodForMs: number
  sbdnNotOffForMs: number
  expr: string
}

export interface FaultSummary {
  latched: boolean
  activeCode: string
  activeClass: string
  latchedCode: string
  latchedClass: string
  activeCount: number
  tripCounter: number
  lastFaultAtIso: string | null
  triggerDiag: FaultTriggerDiag | null
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
  lastUpdatedMs: number
  snapshotFresh: boolean
  source: PdStatus['source']
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
  readyIdle: boolean
  readyQualified: boolean
  readyInvalidated: boolean
  failed: boolean
  phase: DeploymentStatus['phase']
  sequenceId: number
  currentStep: string
  currentStepIndex: number
  lastCompletedStep: string
  lastCompletedStepKey: string
  failureCode: string
  failureReason: string
  primaryFailureCode: string
  primaryFailureReason: string
  secondaryEffects: DeploymentSecondaryEffect[]
  targetMode: DeploymentTargetMode
  targetTempC: number
  targetLambdaNm: number
  maxLaserCurrentA: number
  maxOpticalPowerW: number
  readyTruth: DeploymentReadyTruth
  steps: DeploymentStep[]
}

export interface RealtimeFaultSummary {
  latched: boolean
  activeCode: string
  activeClass: string
  latchedCode: string
  latchedClass: string
  activeCount: number
  tripCounter: number
  triggerDiag: FaultTriggerDiag | null
}

type DeepPartial<T> = {
  [K in keyof T]?: T[K] extends object ? DeepPartial<T[K]> : T[K]
}

export interface RealtimeTelemetry {
  session: RealtimeSessionStatus
  pd: RealtimePdStatus
  bench: BenchControlStatus
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
  buttonBoard: ButtonBoardStatus
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
  offCurrentThresholdA: number
  /*
   * Hard safety cap on the GPIO6 ToF-board front LED duty cycle,
   * integer percent 0..100. Enforced in firmware at every illumination
   * entry point (service + runtime). Default 50 per user directive
   * 2026-04-15. Editable via `integrate.set_safety` while in service mode.
   */
  maxTofLedDutyCyclePct: number
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
  buttonBoard: ButtonBoardStatus
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
