import { makeDefaultBringupStatus } from './bringup'
import {
  currentFromOpticalPowerW,
  makeDefaultBenchControlStatus,
} from './bench-model'
import { makeDefaultGpioInspectorStatus } from './gpio-layout'
import { makeRealtimeTelemetryFromSnapshot } from './live-telemetry'
import {
  clampTecTempC,
  clampTecWavelengthNm,
  estimateTecVoltageFromTempC,
  estimateTempFromWavelengthNm,
  estimateWavelengthFromTempC,
} from './tec-calibration'
import type {
  BenchTargetMode,
  CommandEnvelope,
  DacReferenceMode,
  DacSyncMode,
  DeviceSnapshot,
  DeviceTransport,
  FirmwarePackageDescriptor,
  FirmwareTransferProgress,
  HapticActuator,
  HapticMode,
  ModuleKey,
  RuntimeMode,
  SessionEvent,
  TransportMessage,
} from '../types'

type MockState = {
  connected: boolean
  uptimeSeconds: number
  powerTier: DeviceSnapshot['session']['powerTier']
  systemState: DeviceSnapshot['session']['state']
  alignmentRequested: boolean
  laserRequested: boolean
  serviceMode: boolean
  tecSettlingTicks: number
  targetTempC: number
  targetLambdaNm: number
  targetMode: BenchTargetMode
  runtimeMode: RuntimeMode
  firmwareVersion: string
  activeFault: string
  faultLatched: boolean
  faultCount: number
  tripCounter: number
  lastFaultAtIso: string | null
  pdPowerW: number
  beamPitchDeg: number
  beamRollDeg: number
  beamYawDeg: number
  distanceM: number
  modulationEnabled: boolean
  modulationFrequencyHz: number
  modulationDutyCyclePct: number
  lowStateCurrentA: number
  laserHighCurrentA: number
  hapticDriverEnabled: boolean
  /*
   * USB-Debug Mock fields. Mirror firmware semantics: explicit opt-in,
   * auto-disable on real PD, latched fault on conflict. The mock NEVER
   * drives anything in the mock — it only flips snapshot fields.
   */
  usbDebugMockActive: boolean
  usbDebugMockPdConflictLatched: boolean
  usbDebugMockActivatedAtMs: number
  usbDebugMockDeactivatedAtMs: number
  usbDebugMockLastDisableReason: string
  /*
   * Button board mock state. Defaults: both chips reachable so the mock
   * lets the operator switch into binary_trigger mode and exercise the
   * trigger UI. Side-effect-free until commands or test scripts mutate.
   */
  mcpReachable: boolean
  tlcReachable: boolean
  buttonIsrFireCount: number
  buttonLedBrightnessPct: number
  buttonLedOwned: boolean
  buttonNirLockout: boolean
  rgbState: import('../types').RgbLedState
  rgbTestUntilMs: number
  safety: DeviceSnapshot['safety']
  bringup: DeviceSnapshot['bringup']
  deployment: DeviceSnapshot['deployment']
  deploymentStepIndex: number
  deploymentStepElapsedS: number
  deploymentPlannedFailure: 'none' | 'pd_insufficient'
  gpioInspector: DeviceSnapshot['gpioInspector']
  wireless: DeviceSnapshot['wireless']
}

const MOCK_TICK_MS = 100

function nowIso(): string {
  return new Date().toISOString()
}

function clamp(value: number, min: number, max: number): number {
  if (value < min) {
    return min
  }

  if (value > max) {
    return max
  }

  return value
}

function runtimeModeLockReason(state: MockState): string | null {
  if (!state.deployment.active) {
    return 'Enter deployment mode before changing runtime mode.'
  }

  if (state.deployment.running) {
    return 'Wait for the deployment checklist to stop before changing runtime mode.'
  }

  if (state.faultLatched) {
    return 'Clear the active fault before changing runtime mode.'
  }

  if (state.alignmentRequested) {
    return 'Clear the current alignment request before changing runtime mode.'
  }

  if (state.laserRequested) {
    return 'Clear the current NIR request before changing runtime mode.'
  }

  if (state.modulationEnabled) {
    return 'Disable PCN modulation before changing runtime mode.'
  }

  return null
}

/*
 * Keep these three helpers IN SYNC with the firmware equivalents in
 * components/laser_controller/src/laser_controller_comms.c
 * (laser_controller_comms_{nir,led}_blocked_reason, sbdn_state_name).
 * See .agent/skills/protocol-evolution/SKILL.md — the four-place rule.
 */
function mockNirBlockedReason(
  state: MockState,
): import('../types').NirBlockedReason {
  if (!state.connected) {
    return 'not-connected'
  }
  if (state.faultLatched) {
    return 'fault-latched'
  }
  if (!state.deployment.active) {
    return 'deployment-off'
  }
  if (state.deployment.running) {
    return 'checklist-running'
  }
  if (!state.deployment.ready) {
    return 'checklist-not-ready'
  }
  if (!state.deployment.readyIdle) {
    return 'ready-not-idle'
  }
  if (state.runtimeMode !== 'modulated_host') {
    return 'not-modulated-host'
  }
  if (state.powerTier !== 'full') {
    return 'power-not-full'
  }
  // Mock assumes rails are healthy whenever deployment.ready-idle is true.
  return 'none'
}

function mockLedBlockedReason(
  state: MockState,
): import('../types').LedBlockedReason {
  if (!state.connected) {
    return 'not-connected'
  }
  if (!state.deployment.active) {
    return 'deployment-off'
  }
  if (state.deployment.running) {
    return 'checklist-running'
  }
  return 'none'
}

/*
 * Mock trigger-phase helper. Mirrors
 * components/laser_controller/src/laser_controller_comms.c
 * (laser_controller_comms_trigger_phase). Keep IN SYNC with both the
 * firmware version and the host TriggerPhase union in types.ts —
 * see .agent/skills/protocol-evolution/SKILL.md (four-place rule).
 */
function mockTriggerPhase(
  state: MockState,
): import('../types').TriggerPhase {
  if (!state.connected) {
    return 'off'
  }
  if (state.faultLatched) {
    return 'unrecoverable'
  }
  if (state.buttonNirLockout) {
    return 'lockout'
  }
  if (
    !state.deployment.active ||
    !state.deployment.ready ||
    !state.deployment.readyIdle
  ) {
    return 'off'
  }
  if (state.laserRequested) {
    return 'firing'
  }
  if (state.runtimeMode === 'binary_trigger' && state.alignmentRequested) {
    return 'armed'
  }
  return 'ready'
}

function mockSbdnState(state: MockState): import('../types').SbdnState {
  // SBDN goes hard OFF on fault or when not in a ready state.
  if (state.faultLatched) {
    return 'off'
  }
  if (!state.deployment.active || !state.deployment.ready) {
    return 'off'
  }
  // When NIR is actively being driven, SBDN is ON.
  if (state.laserRequested && state.runtimeMode === 'modulated_host') {
    return 'on'
  }
  // Ready-idle with NIR off → STANDBY (Hi-Z). This is the new semantics the
  // user directed on 2026-04-14.
  return 'standby'
}

function makeDefaultDeploymentSteps(): DeviceSnapshot['deployment']['steps'] {
  return [
    'Deployment entry / ownership reclaim',
    'USB-PD inspect only',
    'Derive runtime power cap',
    'Confirm all controlled outputs off',
    '3V3 settle delay',
    'DAC init and safe zeroing',
    'Peripheral init and readback',
    'Rail sequencing',
    'TEC settle to deployment target',
    'Final ready posture',
  ].map((label, index) => ({
    key: `step_${index + 1}`,
    label,
    status: 'inactive',
    startedAtMs: 0,
    completedAtMs: 0,
  }))
}

function classifyPdPowerTier(
  negotiatedPowerW: number,
  hostOnly: boolean,
  tuning: DeviceSnapshot['bringup']['tuning'],
): DeviceSnapshot['session']['powerTier'] {
  const operationalMinW = Math.max(
    tuning.pdProgrammingOnlyMaxW,
    tuning.pdReducedModeMinW,
  )

  if (hostOnly) {
    return 'programming_only'
  }

  if (negotiatedPowerW < operationalMinW) {
    return 'insufficient'
  }

  if (negotiatedPowerW <= tuning.pdReducedModeMaxW) {
    return 'reduced'
  }

  if (negotiatedPowerW >= tuning.pdFullModeMinW) {
    return 'full'
  }

  return 'reduced'
}

function activeMockPdProfiles(
  tuning: DeviceSnapshot['bringup']['tuning'],
): DeviceSnapshot['pd']['sinkProfiles'] {
  return tuning.pdProfiles.map((profile, index) => {
    if (index === 0) {
      return {
        enabled: true,
        voltageV: 5,
        currentA: Math.max(profile.currentA || 0, 0.5),
      }
    }

    if (!profile.enabled) {
      return {
        enabled: false,
        voltageV: 0,
        currentA: 0,
      }
    }

    return {
      enabled: true,
      voltageV: profile.voltageV,
      currentA: profile.currentA,
    }
  })
}

function selectMockPdContract(
  negotiatedPowerW: number,
  hostOnly: boolean,
  tuning: DeviceSnapshot['bringup']['tuning'],
) {
  const sinkProfiles = activeMockPdProfiles(tuning)

  if (hostOnly) {
    return {
      sourceVoltageV: 5,
      sourceCurrentA: negotiatedPowerW > 0 ? negotiatedPowerW / 5 : 0.5,
      operatingCurrentA: negotiatedPowerW > 0 ? negotiatedPowerW / 5 : 0.5,
      contractObjectPosition: 0,
      sinkProfileCount: 1,
      sinkProfiles,
    }
  }

  for (let index = sinkProfiles.length - 1; index >= 0; index -= 1) {
    const profile = sinkProfiles[index]

    if (!profile.enabled) {
      continue
    }

    const requestedPowerW = profile.voltageV * profile.currentA
    if (requestedPowerW <= negotiatedPowerW + 0.01) {
      return {
        sourceVoltageV: profile.voltageV,
        sourceCurrentA: profile.currentA,
        operatingCurrentA: profile.currentA,
        contractObjectPosition: index + 1,
        sinkProfileCount: index + 1,
        sinkProfiles,
      }
    }
  }

  return {
    sourceVoltageV: 5,
    sourceCurrentA: Math.min(3, negotiatedPowerW > 0 ? negotiatedPowerW / 5 : 0),
    operatingCurrentA: Math.min(3, negotiatedPowerW > 0 ? negotiatedPowerW / 5 : 0),
    contractObjectPosition: 1,
    sinkProfileCount: 1,
    sinkProfiles,
  }
}

export class MockTransport implements DeviceTransport {
  readonly kind = 'mock'

  readonly label = 'Mock bench rig'

  readonly supportsFirmwareTransfer = true

  private listeners = new Set<(message: TransportMessage) => void>()

  private timer: number | null = null

  private state: MockState = {
    connected: false,
    uptimeSeconds: 0,
    powerTier: 'full',
    systemState: 'READY_NIR',
    alignmentRequested: false,
    laserRequested: false,
    serviceMode: false,
    tecSettlingTicks: 0,
    targetTempC: 58.7,
    targetLambdaNm: 786.1,
    targetMode: 'lambda',
    runtimeMode: 'binary_trigger',
    firmwareVersion: 'laser-fw-0.2.0-bench',
    activeFault: 'none',
    faultLatched: false,
    faultCount: 0,
    tripCounter: 2,
    lastFaultAtIso: null,
    pdPowerW: 45,
    beamPitchDeg: -14.2,
    beamRollDeg: 3.8,
    beamYawDeg: 18,
    distanceM: 0.42,
    modulationEnabled: false,
    modulationFrequencyHz: 2000,
    modulationDutyCyclePct: 50,
    lowStateCurrentA: 0,
    laserHighCurrentA: currentFromOpticalPowerW(2.8),
    hapticDriverEnabled: false,
    usbDebugMockActive: false,
    usbDebugMockPdConflictLatched: false,
    usbDebugMockActivatedAtMs: 0,
    usbDebugMockDeactivatedAtMs: 0,
    usbDebugMockLastDisableReason: '',
    mcpReachable: true,
    tlcReachable: true,
    buttonIsrFireCount: 0,
    buttonLedBrightnessPct: 20,
    buttonLedOwned: false,
    buttonNirLockout: false,
    rgbState: { r: 0, g: 0, b: 0, blink: false, enabled: false },
    rgbTestUntilMs: 0,
    safety: {
      allowAlignment: true,
      allowNir: true,
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
      maxLaserCurrentA: 5.2,
      offCurrentThresholdA: 0.2,
      maxTofLedDutyCyclePct: 50,
      lioVoltageOffsetV: 0.07,
      actualLambdaNm: 786.1,
      targetLambdaNm: 786.1,
      lambdaDriftNm: 0,
      tempAdcVoltageV: 2.182,
      interlocks: {
        horizonEnabled: true,
        distanceEnabled: true,
        lambdaDriftEnabled: true,
        tecTempAdcEnabled: true,
        imuInvalidEnabled: true,
        imuStaleEnabled: true,
        tofInvalidEnabled: true,
        tofStaleEnabled: true,
        ldOvertempEnabled: true,
        ldLoopBadEnabled: true,
        tofLowBoundOnly: false,
      },
    },
    bringup: makeDefaultBringupStatus(),
    deployment: {
      active: false,
      running: false,
      ready: false,
      readyIdle: false,
      readyQualified: false,
      readyInvalidated: false,
      failed: false,
      phase: 'inactive',
      sequenceId: 0,
      currentStep: 'none',
      currentStepIndex: 0,
      lastCompletedStep: 'none',
      lastCompletedStepKey: 'none',
      failureCode: 'none',
      failureReason: '',
      primaryFailureCode: 'none',
      primaryFailureReason: '',
      secondaryEffects: [],
      targetMode: 'temp',
      targetTempC: 25,
      targetLambdaNm: 785,
      maxLaserCurrentA: 5.2,
      maxOpticalPowerW: 5,
      readyTruth: {
        tecRailPgoodRaw: false,
        tecRailPgoodFiltered: false,
        tecTempGood: false,
        tecAnalogPlausible: false,
        ldRailPgoodRaw: false,
        ldRailPgoodFiltered: false,
        driverLoopGood: false,
        sbdnHigh: false,
        pcnLow: false,
        idleBiasCurrentA: 0,
      },
      steps: makeDefaultDeploymentSteps(),
    },
    deploymentStepIndex: -1,
    deploymentStepElapsedS: 0,
    deploymentPlannedFailure: 'none',
    gpioInspector: makeDefaultGpioInspectorStatus(),
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
      wsUrl: 'ws://192.168.4.1/ws',
      lastError: '',
    },
  }

  subscribe(listener: (message: TransportMessage) => void): () => void {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  async connect(): Promise<void> {
    if (this.state.connected) {
      this.emit({ kind: 'transport', status: 'connected', detail: this.label })
      this.emitRealtimeFrame(true)
      return
    }

    this.emit({ kind: 'transport', status: 'connecting', detail: 'Booting mock bench rig…' })

    this.state.connected = true

    this.timer = window.setInterval(() => {
      this.tick(MOCK_TICK_MS / 1000)
    }, MOCK_TICK_MS)

    this.emit({
      kind: 'event',
      event: this.makeEvent(
        'info',
        'transport',
        'Mock transport online',
        'Host console is attached to a deterministic simulation rig with writable bench controls.',
      ),
    })
    this.emit({ kind: 'transport', status: 'connected', detail: this.label })
    this.emitRealtimeFrame(true)

    void this.sendCommand({ id: -1, type: 'cmd', cmd: 'deployment.enter' })
    void this.sendCommand({ id: -2, type: 'cmd', cmd: 'deployment.run' })
  }

  async disconnect(): Promise<void> {
    if (!this.state.connected) {
      this.emit({ kind: 'transport', status: 'disconnected', detail: 'Mock rig detached.' })
      return
    }

    if (this.timer !== null) {
      window.clearInterval(this.timer)
      this.timer = null
    }

    this.state.connected = false
    this.emit({ kind: 'transport', status: 'disconnected', detail: 'Mock rig detached.' })
  }

  async sendCommand(command: CommandEnvelope): Promise<void> {
    if (!this.state.connected) {
      throw new Error('Mock transport is not connected.')
    }

    let ackOk = true
    let ackNote = 'Accepted by mock controller.'

    this.emit({
      kind: 'event',
      event: this.makeEvent(
        'info',
        'command',
        `Command sent: ${command.cmd}`,
        'The mock transport accepted the host request for bench evaluation.',
      ),
    })

    switch (command.cmd) {
      case 'get_status':
      case 'status.get':
      case 'get_faults':
        break
      case 'enter_deployment_mode':
      case 'deployment.enter':
        this.state.serviceMode = false
        this.state.bringup.serviceModeRequested = false
        this.state.bringup.serviceModeActive = false
        this.state.bringup.interlocksDisabled = false
        this.state.bringup.power.ldRequested = false
        this.state.bringup.power.tecRequested = false
        this.state.bringup.illumination.tof.enabled = false
        this.state.hapticDriverEnabled = false
        this.state.alignmentRequested = false
        this.state.laserRequested = false
        this.state.modulationEnabled = false
        this.state.runtimeMode = 'modulated_host'
        this.state.deployment = {
          ...this.state.deployment,
          active: true,
          running: false,
          ready: false,
          failed: false,
          currentStep: 'none',
          lastCompletedStep: 'none',
          failureCode: 'none',
          failureReason: '',
          steps: makeDefaultDeploymentSteps(),
        }
        this.state.deploymentStepIndex = -1
        this.state.deploymentStepElapsedS = 0
        this.state.deploymentPlannedFailure = 'none'
        break
      case 'set_runtime_mode':
      case 'operate.set_mode':
        if (
          command.args?.mode !== 'binary_trigger' &&
          command.args?.mode !== 'modulated_host'
        ) {
          ackOk = false
          ackNote = 'Unsupported runtime mode.'
          break
        }
        {
          const requestedMode = command.args.mode as RuntimeMode
          if (
            requestedMode === 'binary_trigger' &&
            !this.state.mcpReachable
          ) {
            ackOk = false
            ackNote =
              'Button board (MCP23017 @ 0x20) is not reachable. Confirm wiring before selecting binary_trigger.'
            break
          }
          if (
            requestedMode !== this.state.runtimeMode &&
            runtimeModeLockReason(this.state) !== null
          ) {
            ackOk = false
            ackNote = runtimeModeLockReason(this.state) ?? 'Runtime mode change rejected.'
            break
          }
          this.state.runtimeMode = requestedMode
        }
        this.state.alignmentRequested = false
        this.state.laserRequested = false
        if (this.state.runtimeMode === 'binary_trigger') {
          this.state.modulationEnabled = false
          this.state.lowStateCurrentA = 0
        }
        break
      case 'exit_deployment_mode':
      case 'deployment.exit':
        this.state.deployment = {
          ...this.state.deployment,
          active: false,
          running: false,
          ready: false,
          failed: false,
          currentStep: 'none',
          lastCompletedStep: 'none',
          failureCode: 'none',
          failureReason: '',
          steps: makeDefaultDeploymentSteps(),
        }
        this.state.deploymentStepIndex = -1
        this.state.deploymentStepElapsedS = 0
        this.state.deploymentPlannedFailure = 'none'
        break
      case 'run_deployment_sequence':
      case 'deployment.run': {
        const hostOnly = this.state.powerTier === 'programming_only' && this.state.pdPowerW <= 5.1
        this.state.deployment = {
          ...this.state.deployment,
          active: true,
          running: true,
          ready: false,
          failed: false,
          currentStep: 'ownership_reclaim',
          lastCompletedStep: 'none',
          failureCode: 'none',
          failureReason: '',
          maxLaserCurrentA: 0,
          maxOpticalPowerW: 0,
          steps: makeDefaultDeploymentSteps().map((step, index) => ({
            ...step,
            status: index === 0 ? 'in_progress' : 'pending',
          })),
        }
        this.state.deploymentStepIndex = 0
        this.state.deploymentStepElapsedS = 0
        this.state.deploymentPlannedFailure = hostOnly ? 'pd_insufficient' : 'none'
        break
      }
      case 'set_deployment_target':
      case 'deployment.set_target':
        if (typeof command.args?.temp_c === 'number') {
          this.state.targetTempC = clampTecTempC(command.args.temp_c)
          this.state.targetLambdaNm = estimateWavelengthFromTempC(this.state.targetTempC)
          this.state.targetMode = 'temp'
        }
        if (typeof command.args?.lambda_nm === 'number') {
          this.state.targetLambdaNm = clampTecWavelengthNm(command.args.lambda_nm)
          this.state.targetTempC = estimateTempFromWavelengthNm(this.state.targetLambdaNm)
          this.state.targetMode = 'lambda'
        }
        this.state.deployment.targetMode = this.state.targetMode
        this.state.deployment.targetTempC = this.state.targetTempC
        this.state.deployment.targetLambdaNm = this.state.targetLambdaNm
        break
      case 'configure_wireless':
        if (command.args?.mode === 'station') {
          const nextStationSsid =
            typeof command.args?.ssid === 'string' && command.args.ssid.trim().length > 0
              ? command.args.ssid.trim()
              : this.state.wireless.stationSsid

          if (nextStationSsid.length > 0) {
            this.state.wireless = {
              ...this.state.wireless,
              started: true,
              mode: 'station',
              apReady: false,
              stationConfigured: true,
              stationConnecting: false,
              stationConnected: true,
              ssid: nextStationSsid,
              stationSsid: nextStationSsid,
              stationRssiDbm: -54,
              stationChannel: 149,
              scanInProgress: false,
              ipAddress: '192.168.1.77',
              wsUrl: 'ws://192.168.1.77/ws',
              lastError: '',
            }
          } else {
            this.state.wireless = {
              ...this.state.wireless,
              lastError: 'Station mode needs an SSID.',
            }
          }
        } else {
          this.state.wireless = {
            ...this.state.wireless,
            started: true,
            mode: 'softap',
            apReady: true,
            stationConnecting: false,
            stationConnected: false,
            ssid: 'BSL-HTLS-Bench',
            stationRssiDbm: 0,
            stationChannel: 6,
            scanInProgress: false,
            ipAddress: '192.168.4.1',
            wsUrl: 'ws://192.168.4.1/ws',
            lastError: '',
          }
        }
        break
      case 'scan_wireless_networks':
        this.state.wireless = {
          ...this.state.wireless,
          scanInProgress: false,
          scannedNetworks: [
            { ssid: 'LabNet-5G', rssiDbm: -48, channel: 149, secure: true },
            { ssid: 'BioSensors-2G', rssiDbm: -61, channel: 11, secure: true },
            { ssid: 'Microscope-Guest', rssiDbm: -73, channel: 1, secure: false },
          ],
          lastError: '',
        }
        break
      case 'clear_faults':
        this.state.activeFault = 'none'
        this.state.faultLatched = false
        // Mirror firmware: clearing all faults also clears the
        // usb_debug_mock_pd_conflict latch so the mock can be re-enabled.
        this.state.usbDebugMockPdConflictLatched = false
        this.emit({
          kind: 'event',
          event: this.makeEvent(
            'ok',
            'fault',
            'Mock fault latch cleared',
            'The simulated controller returned to a non-latched state.',
          ),
        })
        break
      case 'enable_alignment':
        if (this.state.deployment.ready) {
          this.state.alignmentRequested = true
          this.state.laserRequested = false
        }
        break
      case 'disable_alignment':
        this.state.alignmentRequested = false
        break
      case 'operate.set_alignment':
        // Green alignment is UNGATED per user directive 2026-04-14.
        // Accept the request from any deployment state.
        if (typeof command.args?.enabled === 'boolean') {
          this.state.alignmentRequested = command.args.enabled
          if (this.state.alignmentRequested) {
            this.state.laserRequested = false
          }
        }
        break
      case 'set_target_temp':
        if (typeof command.args?.temp_c === 'number' && this.state.deployment.ready) {
          this.state.targetTempC = clampTecTempC(command.args.temp_c)
          this.state.targetLambdaNm = estimateWavelengthFromTempC(this.state.targetTempC)
          this.state.targetMode = 'temp'
          this.state.deployment.targetMode = 'temp'
          this.state.deployment.targetTempC = this.state.targetTempC
          this.state.deployment.targetLambdaNm = this.state.targetLambdaNm
          this.state.tecSettlingTicks = 8
        }
        if (
          typeof command.args?.temp_c === 'number' &&
          this.state.deployment.ready
        ) {
          break
        }
        break
      case 'operate.set_target':
        if (typeof command.args?.temp_c === 'number' && this.state.deployment.ready) {
          this.state.targetTempC = clampTecTempC(command.args.temp_c)
          this.state.targetLambdaNm = estimateWavelengthFromTempC(this.state.targetTempC)
          this.state.targetMode = 'temp'
          this.state.deployment.targetMode = 'temp'
          this.state.deployment.targetTempC = this.state.targetTempC
          this.state.deployment.targetLambdaNm = this.state.targetLambdaNm
          this.state.tecSettlingTicks = 8
        } else if (typeof command.args?.lambda_nm === 'number' && this.state.deployment.ready) {
          this.state.targetLambdaNm = clampTecWavelengthNm(command.args.lambda_nm)
          this.state.targetTempC = estimateTempFromWavelengthNm(this.state.targetLambdaNm)
          this.state.targetMode = 'lambda'
          this.state.deployment.targetMode = 'lambda'
          this.state.deployment.targetTempC = this.state.targetTempC
          this.state.deployment.targetLambdaNm = this.state.targetLambdaNm
          this.state.tecSettlingTicks = 10
        }
        break
      case 'set_target_lambda':
        if (typeof command.args?.lambda_nm === 'number' && this.state.deployment.ready) {
          this.state.targetLambdaNm = clampTecWavelengthNm(command.args.lambda_nm)
          this.state.targetTempC = estimateTempFromWavelengthNm(this.state.targetLambdaNm)
          this.state.targetMode = 'lambda'
          this.state.deployment.targetMode = 'lambda'
          this.state.deployment.targetTempC = this.state.targetTempC
          this.state.deployment.targetLambdaNm = this.state.targetLambdaNm
          this.state.tecSettlingTicks = 10
        }
        break
      case 'set_laser_power':
        if (this.state.runtimeMode !== 'modulated_host') {
          ackOk = false
          ackNote =
            'Host runtime output control is only available in modulated_host mode.'
          break
        }
        if (typeof command.args?.optical_power_w === 'number' && this.state.deployment.ready) {
          this.state.laserHighCurrentA = clamp(
            currentFromOpticalPowerW(command.args.optical_power_w),
            0,
            this.state.deployment.maxLaserCurrentA,
          )
        } else if (typeof command.args?.current_a === 'number' && this.state.deployment.ready) {
          this.state.laserHighCurrentA = clamp(
            command.args.current_a,
            0,
            this.state.deployment.maxLaserCurrentA,
          )
        }
        break
      case 'operate.set_output':
        if (this.state.runtimeMode !== 'modulated_host') {
          ackOk = false
          ackNote =
            'Host runtime output control is only available in modulated_host mode.'
          break
        }
        if (typeof command.args?.current_a === 'number' && this.state.deployment.ready) {
          this.state.laserHighCurrentA = clamp(
            command.args.current_a,
            0,
            this.state.deployment.maxLaserCurrentA || this.state.safety.maxLaserCurrentA,
          )
        }
        if (typeof command.args?.enabled === 'boolean') {
          this.state.laserRequested = command.args.enabled && this.state.deployment.ready
        }
        break
      case 'operate.set_led':
        if (typeof command.args?.enabled === 'boolean') {
          this.state.bringup.illumination.tof.enabled =
            command.args.enabled && this.state.deployment.ready
        }
        if (typeof command.args?.duty_cycle_pct === 'number') {
          this.state.bringup.illumination.tof.dutyCyclePct = clamp(
            command.args.duty_cycle_pct,
            0,
            100,
          )
        }
        if (typeof command.args?.frequency_hz === 'number') {
          this.state.bringup.illumination.tof.frequencyHz = clamp(
            command.args.frequency_hz,
            5000,
            100000,
          )
        }
        break
      case 'set_runtime_safety':
      case 'set_deployment_safety':
      case 'integrate.set_safety':
        if (typeof command.args?.horizon_threshold_deg === 'number') {
          this.state.safety.horizonThresholdDeg = command.args.horizon_threshold_deg
        }
        if (typeof command.args?.horizon_hysteresis_deg === 'number') {
          this.state.safety.horizonHysteresisDeg = command.args.horizon_hysteresis_deg
        }
        if (typeof command.args?.tof_min_range_m === 'number') {
          this.state.safety.tofMinRangeM = command.args.tof_min_range_m
        }
        if (typeof command.args?.tof_max_range_m === 'number') {
          this.state.safety.tofMaxRangeM = command.args.tof_max_range_m
        }
        if (typeof command.args?.tof_hysteresis_m === 'number') {
          this.state.safety.tofHysteresisM = command.args.tof_hysteresis_m
        }
        if (typeof command.args?.imu_stale_ms === 'number') {
          this.state.safety.imuStaleMs = command.args.imu_stale_ms
        }
        if (typeof command.args?.tof_stale_ms === 'number') {
          this.state.safety.tofStaleMs = command.args.tof_stale_ms
        }
        if (typeof command.args?.rail_good_timeout_ms === 'number') {
          this.state.safety.railGoodTimeoutMs = command.args.rail_good_timeout_ms
        }
        if (typeof command.args?.lambda_drift_limit_nm === 'number') {
          this.state.safety.lambdaDriftLimitNm = command.args.lambda_drift_limit_nm
        }
        if (typeof command.args?.lambda_drift_hysteresis_nm === 'number') {
          this.state.safety.lambdaDriftHysteresisNm = command.args.lambda_drift_hysteresis_nm
        }
        if (typeof command.args?.lambda_drift_hold_ms === 'number') {
          this.state.safety.lambdaDriftHoldMs = command.args.lambda_drift_hold_ms
        }
        if (typeof command.args?.ld_overtemp_limit_c === 'number') {
          this.state.safety.ldOvertempLimitC = command.args.ld_overtemp_limit_c
        }
        if (typeof command.args?.tec_temp_adc_trip_v === 'number') {
          this.state.safety.tecTempAdcTripV = command.args.tec_temp_adc_trip_v
        }
        if (typeof command.args?.tec_temp_adc_hysteresis_v === 'number') {
          this.state.safety.tecTempAdcHysteresisV = command.args.tec_temp_adc_hysteresis_v
        }
        if (typeof command.args?.tec_temp_adc_hold_ms === 'number') {
          this.state.safety.tecTempAdcHoldMs = command.args.tec_temp_adc_hold_ms
        }
        if (typeof command.args?.tec_min_command_c === 'number') {
          this.state.safety.tecMinCommandC = command.args.tec_min_command_c
        }
        if (typeof command.args?.tec_max_command_c === 'number') {
          this.state.safety.tecMaxCommandC = command.args.tec_max_command_c
        }
        if (typeof command.args?.tec_ready_tolerance_c === 'number') {
          this.state.safety.tecReadyToleranceC = command.args.tec_ready_tolerance_c
        }
        if (typeof command.args?.max_laser_current_a === 'number') {
          this.state.safety.maxLaserCurrentA = command.args.max_laser_current_a
          this.state.laserHighCurrentA = clamp(
            this.state.laserHighCurrentA,
            0,
            this.state.safety.maxLaserCurrentA,
          )
          this.state.lowStateCurrentA = clamp(
            this.state.lowStateCurrentA,
            0,
            this.state.laserHighCurrentA,
          )
        }
        if (this.state.deployment.active) {
          this.state.deployment.maxLaserCurrentA = Math.min(
            this.state.deployment.maxLaserCurrentA,
            this.state.safety.maxLaserCurrentA,
          )
          this.state.deployment.maxOpticalPowerW = this.state.deployment.maxLaserCurrentA
        }
        if (command.cmd === 'integrate.set_safety') {
          this.state.bringup.persistenceDirty = false
          this.state.bringup.lastSaveOk = true
        }
        break
      case 'pd_debug_config':
        if (typeof command.args?.programming_only_max_w === 'number') {
          this.state.bringup.tuning.pdProgrammingOnlyMaxW =
            command.args.programming_only_max_w
        }
        if (typeof command.args?.reduced_mode_min_w === 'number') {
          this.state.bringup.tuning.pdReducedModeMinW =
            command.args.reduced_mode_min_w
        }
        if (typeof command.args?.reduced_mode_max_w === 'number') {
          this.state.bringup.tuning.pdReducedModeMaxW =
            command.args.reduced_mode_max_w
        }
        if (typeof command.args?.full_mode_min_w === 'number') {
          this.state.bringup.tuning.pdFullModeMinW = command.args.full_mode_min_w
        }

        this.state.bringup.tuning.pdProfiles = this.state.bringup.tuning.pdProfiles.map(
          (profile, index) => {
            const slot = index + 1
            const enabled = command.args?.[`pdo${slot}_enabled`]
            const voltage = command.args?.[`pdo${slot}_voltage_v`]
            const current = command.args?.[`pdo${slot}_current_a`]

            return {
              enabled: typeof enabled === 'boolean' ? enabled : profile.enabled,
              voltageV: typeof voltage === 'number' ? voltage : profile.voltageV,
              currentA: typeof current === 'number' ? current : profile.currentA,
            }
          },
        )
        this.state.bringup.tuning.pdProfiles = this.state.bringup.tuning.pdProfiles.map(
          (profile, index, profiles) => {
            if (index === 0) {
              return {
                enabled: true,
                voltageV: 5,
                currentA: clamp(profile.currentA, 0.5, 5),
              }
            }

            if (index === 2 && !profiles[1].enabled) {
              return {
                ...profile,
                enabled: false,
              }
            }

            return {
              ...profile,
              currentA: clamp(profile.currentA, 0.5, 5),
              voltageV: clamp(profile.voltageV, 5, 20),
            }
          },
        )
        this.state.powerTier = classifyPdPowerTier(
          this.state.pdPowerW,
          this.state.pdPowerW <= 5.1,
          this.state.bringup.tuning,
        )
        // Mirror firmware guard: real PD power upgrade auto-disables the
        // mock and latches the SYSTEM_MAJOR fault.
        if (
          this.state.usbDebugMockActive &&
          this.state.powerTier !== 'programming_only'
        ) {
          this.state.usbDebugMockActive = false
          this.state.usbDebugMockPdConflictLatched = true
          this.state.usbDebugMockDeactivatedAtMs = Date.now()
          this.state.usbDebugMockLastDisableReason =
            'real PD power detected (auto-disable + fault latched)'
          this.state.activeFault = 'usb_debug_mock_pd_conflict'
          this.state.faultLatched = true
        }
        this.bumpBringupRevision('USB-PD sink planning updated.')
        break
      case 'pd_burn_nvm':
        this.state.bringup.tools.lastAction =
          'Mock STUSB4500 NVM burn simulated. The current PDO plan would become the startup default after reset.'
        this.emit({
          kind: 'event',
          event: this.makeEvent(
            'warn',
            'service',
            'Mock PD NVM burn',
            'Mock STUSB4500 NVM burn accepted. Real hardware endurance is finite, so this should only be used for final provisioning.',
          ),
        })
        break
      case 'pd_save_firmware_plan':
        if (typeof command.args?.programming_only_max_w === 'number') {
          this.state.bringup.tuning.pdProgrammingOnlyMaxW =
            command.args.programming_only_max_w
        }
        if (typeof command.args?.reduced_mode_min_w === 'number') {
          this.state.bringup.tuning.pdReducedModeMinW =
            command.args.reduced_mode_min_w
        }
        if (typeof command.args?.reduced_mode_max_w === 'number') {
          this.state.bringup.tuning.pdReducedModeMaxW =
            command.args.reduced_mode_max_w
        }
        if (typeof command.args?.full_mode_min_w === 'number') {
          this.state.bringup.tuning.pdFullModeMinW = command.args.full_mode_min_w
        }
        if (typeof command.args?.firmware_plan_enabled === 'boolean') {
          this.state.bringup.tuning.pdFirmwarePlanEnabled =
            command.args.firmware_plan_enabled
        }
        this.state.bringup.tuning.pdProfiles = this.state.bringup.tuning.pdProfiles.map(
          (profile, index) => {
            const slot = index + 1
            const enabled = command.args?.[`pdo${slot}_enabled`]
            const voltage = command.args?.[`pdo${slot}_voltage_v`]
            const current = command.args?.[`pdo${slot}_current_a`]

            return {
              enabled: typeof enabled === 'boolean' ? enabled : profile.enabled,
              voltageV: typeof voltage === 'number' ? voltage : profile.voltageV,
              currentA: typeof current === 'number' ? current : profile.currentA,
            }
          },
        )
        this.state.bringup.tuning.pdProfiles = this.state.bringup.tuning.pdProfiles.map(
          (profile, index, profiles) => {
            if (index === 0) {
              return {
                enabled: true,
                voltageV: 5,
                currentA: clamp(profile.currentA, 0.5, 5),
              }
            }

            if (index === 2 && !profiles[1].enabled) {
              return {
                ...profile,
                enabled: false,
              }
            }

            return {
              ...profile,
              currentA: clamp(profile.currentA, 0.5, 5),
              voltageV: clamp(profile.voltageV, 5, 20),
            }
          },
        )
        this.state.bringup.tools.lastAction = this.state.bringup.tuning.pdFirmwarePlanEnabled
          ? 'Mock controller firmware saved the PDO plan and would auto-reconcile it into STUSB runtime state on mismatch.'
          : 'Mock controller firmware saved the PDO plan with auto-reconcile disabled.'
        this.bumpBringupRevision('Firmware PDO plan updated.')
        break
      case 'laser_output_enable':
        if (this.state.runtimeMode !== 'modulated_host') {
          ackOk = false
          ackNote =
            'Host runtime output control is only available in modulated_host mode.'
          break
        }
        if (this.state.deployment.ready) {
          this.state.laserRequested = true
          this.state.alignmentRequested = false
        }
        break
      case 'laser_output_disable':
        if (this.state.runtimeMode !== 'modulated_host') {
          ackOk = false
          ackNote =
            'Host runtime output control is only available in modulated_host mode.'
          break
        }
        this.state.laserRequested = false
        break
      case 'configure_modulation':
      case 'operate.set_modulation':
        if (this.state.runtimeMode !== 'modulated_host') {
          ackOk = false
          ackNote =
            'Host runtime output control is only available in modulated_host mode.'
          break
        }
        if (!this.state.deployment.ready) {
          break
        }
        this.state.modulationEnabled = Boolean(command.args?.enabled)
        if (typeof command.args?.frequency_hz === 'number') {
          this.state.modulationFrequencyHz = clamp(command.args.frequency_hz, 0, 4000)
        }
        if (typeof command.args?.duty_cycle_pct === 'number') {
          this.state.modulationDutyCyclePct = clamp(command.args.duty_cycle_pct, 0, 100)
        }
        this.state.lowStateCurrentA = 0
        break
      case 'reboot':
        this.state.systemState = 'BOOT_INIT'
        this.state.alignmentRequested = false
        this.state.laserRequested = false
        this.state.tecSettlingTicks = 3
        this.state.bringup.serviceModeActive = false
        this.state.bringup.serviceModeRequested = false
        this.state.bringup.interlocksDisabled = false
        this.state.bringup.illumination.tof.enabled = false
        this.state.hapticDriverEnabled = false
        this.state.deployment = {
          ...this.state.deployment,
          active: false,
          running: false,
          ready: false,
          failed: false,
          currentStep: 'none',
          lastCompletedStep: 'none',
          failureCode: 'none',
          failureReason: '',
          steps: makeDefaultDeploymentSteps(),
        }
        this.emit({
          kind: 'event',
          event: this.makeEvent(
            'warn',
            'boot',
            'Controller reboot requested',
            'Outputs were dropped safe before the reboot sequence.',
          ),
        })
        break
      case 'enter_service_mode':
        if (!this.state.deployment.active) {
          this.state.serviceMode = true
          this.state.bringup.serviceModeRequested = true
          this.state.bringup.serviceModeActive = true
          this.state.systemState = 'SERVICE_MODE'
        }
        break
      case 'exit_service_mode':
        this.state.serviceMode = false
        this.state.bringup.serviceModeRequested = false
        this.state.bringup.serviceModeActive = false
        this.state.bringup.illumination.tof.enabled = false
        this.state.systemState = 'SAFE_IDLE'
        this.state.hapticDriverEnabled = false
        // Service-mode exit auto-disables the USB-Debug Mock (firmware
        // mirror).
        if (this.state.usbDebugMockActive) {
          this.state.usbDebugMockActive = false
          this.state.usbDebugMockDeactivatedAtMs = Date.now()
          this.state.usbDebugMockLastDisableReason = 'service mode exited'
        }
        break
      case 'service.usb_debug_mock_enable':
        // Mirror firmware guards: service mode + programming_only PD tier
        // + no latched PD-conflict fault.
        if (!this.state.serviceMode) {
          ackOk = false
          ackNote = 'Enter service mode before enabling the USB debug mock.'
          break
        }
        if (this.state.powerTier !== 'programming_only') {
          ackOk = false
          ackNote =
            'USB debug mock only engages when power_tier is programming_only.'
          break
        }
        if (this.state.usbDebugMockPdConflictLatched) {
          ackOk = false
          ackNote =
            'Clear the latched usb_debug_mock_pd_conflict fault before re-enabling.'
          break
        }
        this.state.usbDebugMockActive = true
        this.state.usbDebugMockActivatedAtMs = Date.now()
        this.state.usbDebugMockLastDisableReason = ''
        break
      case 'service.usb_debug_mock_disable':
        if (this.state.usbDebugMockActive) {
          this.state.usbDebugMockActive = false
          this.state.usbDebugMockDeactivatedAtMs = Date.now()
          this.state.usbDebugMockLastDisableReason = 'operator request'
        }
        break
      case 'integrate.rgb_led.set': {
        // Mirror firmware guards (comms.c handler):
        //   service mode active + no deployment + no fault latch.
        if (!this.state.serviceMode) {
          ackOk = false
          ackNote = 'Enter service mode before driving the RGB LED test.'
          break
        }
        if (this.state.deployment.active) {
          ackOk = false
          ackNote = 'Exit deployment mode before driving the RGB LED test.'
          break
        }
        if (this.state.faultLatched) {
          ackOk = false
          ackNote = 'Clear the latched fault before driving the RGB LED test.'
          break
        }
        const r = clamp(Number(command.args?.r ?? 0), 0, 255)
        const g = clamp(Number(command.args?.g ?? 0), 0, 255)
        const b = clamp(Number(command.args?.b ?? 0), 0, 255)
        const blink = Boolean(command.args?.blink)
        const requestedHold = Number(command.args?.hold_ms)
        const holdMs =
          Number.isFinite(requestedHold) && requestedHold > 0
            ? Math.min(30000, requestedHold)
            : 5000
        this.state.rgbState = { r, g, b, blink, enabled: true }
        this.state.rgbTestUntilMs = Date.now() + holdMs
        break
      }
      case 'integrate.rgb_led.clear':
        this.state.rgbState = { r: 0, g: 0, b: 0, blink: false, enabled: false }
        this.state.rgbTestUntilMs = 0
        break
      case 'integrate.tof.set_calibration': {
        // Mirror firmware guards at `laser_controller_comms.c`.
        if (!this.state.serviceMode) {
          ackOk = false
          ackNote = 'Enter service mode before updating ToF calibration.'
          break
        }
        if (this.state.deployment.active) {
          ackOk = false
          ackNote = 'Exit deployment mode before updating ToF calibration.'
          break
        }
        if (this.state.faultLatched) {
          ackOk = false
          ackNote = 'Clear the latched fault before updating ToF calibration.'
          break
        }
        const cur = this.state.bringup.tuning.tofCalibration
        const distanceRaw = command.args?.distance_mode
        const nextDistance =
          distanceRaw === 'short' || distanceRaw === 'medium' || distanceRaw === 'long'
            ? distanceRaw
            : cur.distanceMode
        const clampInt = (v: unknown, min: number, max: number, fallback: number): number => {
          const n = Number(v)
          if (!Number.isFinite(n)) return fallback
          return Math.max(min, Math.min(max, Math.round(n)))
        }
        const allowedTiming = [20, 33, 50, 100, 200] as const
        const requestedTiming = Number(command.args?.timing_budget_ms)
        const nextTiming = allowedTiming.includes(
          requestedTiming as (typeof allowedTiming)[number],
        )
          ? (requestedTiming as (typeof allowedTiming)[number])
          : cur.timingBudgetMs
        const nextRoiW = clampInt(command.args?.roi_width_spads, 4, 16, cur.roiWidthSpads)
        const nextRoiH = clampInt(command.args?.roi_height_spads, 4, 16, cur.roiHeightSpads)
        const nextRoiC = clampInt(command.args?.roi_center_spad, 0, 255, cur.roiCenterSpad)
        const nextOffset = clampInt(command.args?.offset_mm, -2000, 2000, cur.offsetMm)
        const nextXtalkCps = clampInt(command.args?.xtalk_cps, 0, 0xffff, cur.xtalkCps)
        const nextXtalkEnabled =
          typeof command.args?.xtalk_enabled === 'boolean'
            ? command.args.xtalk_enabled
            : cur.xtalkEnabled
        this.state.bringup.tuning.tofCalibration = {
          distanceMode: nextDistance,
          timingBudgetMs: nextTiming,
          roiWidthSpads: nextRoiW,
          roiHeightSpads: nextRoiH,
          roiCenterSpad: nextRoiC,
          offsetMm: nextOffset,
          xtalkCps: nextXtalkCps,
          xtalkEnabled: nextXtalkEnabled,
        }
        this.bumpBringupRevision('ToF calibration updated from the host console.')
        break
      }
      case 'apply_bringup_preset':
        if (typeof command.args?.preset === 'string') {
          this.applyBringupPreset(command.args.preset)
        }
        break
      case 'set_profile_name':
        if (typeof command.args?.name === 'string' && command.args.name.length > 0) {
          this.state.bringup.profileName = command.args.name.slice(0, 24)
          this.bumpBringupRevision('Bring-up profile renamed from the host console.')
        }
        break
      case 'set_module_state':
        if (typeof command.args?.module === 'string') {
          const module =
            command.args.module === 'laser_driver'
              ? 'laserDriver'
              : (command.args.module as ModuleKey)
          const moduleStatus = this.state.bringup.modules[module]

          if (moduleStatus !== undefined) {
            moduleStatus.expectedPresent = Boolean(command.args.expected_present)
            moduleStatus.debugEnabled = Boolean(command.args.debug_enabled)
            if (!moduleStatus.expectedPresent) {
              this.resetModuleProbeState(module)
            }
            if (
              module === 'tof' &&
              !moduleStatus.expectedPresent &&
              !moduleStatus.debugEnabled
            ) {
              this.state.bringup.illumination.tof.enabled = false
            }
            this.bumpBringupRevision(`Module ${module} expectations updated.`)
          }
        }
        break
      case 'set_supply_enable':
        if (typeof command.args?.rail === 'string' && typeof command.args?.enabled === 'boolean') {
          if (command.args.rail === 'ld') {
            this.state.bringup.power.ldRequested = command.args.enabled
          }
          if (command.args.rail === 'tec') {
            this.state.bringup.power.tecRequested = command.args.enabled
          }
          this.bumpBringupRevision(
            `${String(command.args.rail).toUpperCase()} rail service request ${command.args.enabled ? 'enabled' : 'disabled'}.`,
          )
        }
        break
      case 'set_interlocks_disabled':
        if (typeof command.args?.enabled === 'boolean') {
          this.state.bringup.interlocksDisabled = command.args.enabled
        }
        break
      case 'set_haptic_enable':
        if (typeof command.args?.enabled === 'boolean') {
          this.state.hapticDriverEnabled = command.args.enabled
          this.bumpBringupRevision(
            `ERM driver enable ${command.args.enabled ? 'asserted on GPIO48' : 'cleared on GPIO48'}.`,
          )
        }
        break
      case 'set_gpio_override':
        if (
          typeof command.args?.gpio === 'number' &&
          typeof command.args?.mode === 'string'
        ) {
          const target = this.state.gpioInspector.pins.find(
            (pin) => pin.gpioNum === command.args?.gpio,
          )

          if (target !== undefined) {
            if (command.args.mode === 'firmware') {
              target.overrideActive = false
              target.overrideMode = 'firmware'
              target.overrideLevelHigh = false
              target.overridePullupEnabled = false
              target.overridePulldownEnabled = false
            } else {
              target.overrideActive = true
              target.overrideMode = command.args.mode as 'input' | 'output'
              target.overrideLevelHigh = Boolean(command.args.level_high)
              target.overridePullupEnabled = Boolean(command.args.pullup_enabled)
              target.overridePulldownEnabled = Boolean(command.args.pulldown_enabled)
            }
          }

          this.state.gpioInspector.anyOverrideActive = this.state.gpioInspector.pins.some(
            (pin) => pin.overrideActive,
          )
          this.state.gpioInspector.activeOverrideCount = this.state.gpioInspector.pins.filter(
            (pin) => pin.overrideActive,
          ).length
          this.state.bringup.tools.lastAction = `GPIO${String(command.args.gpio)} override ${String(command.args.mode)} staged in mock service mode.`
        }
        break
      case 'clear_gpio_overrides':
        for (const pin of this.state.gpioInspector.pins) {
          pin.overrideActive = false
          pin.overrideMode = 'firmware'
          pin.overrideLevelHigh = false
          pin.overridePullupEnabled = false
          pin.overridePulldownEnabled = false
        }
        this.state.gpioInspector.anyOverrideActive = false
        this.state.gpioInspector.activeOverrideCount = 0
        this.state.bringup.tools.lastAction =
          'All GPIO overrides cleared in the mock controller.'
        break
      case 'save_bringup_profile':
      case 'integrate.save_profile':
        this.state.bringup.lastSaveOk = false
        this.state.bringup.persistenceDirty = true
        this.state.bringup.tools.lastAction =
          'Device-side save unavailable in mock parity mode. Use the host-local library instead.'
        break
      case 'dac_debug_set':
        if (typeof command.args?.channel === 'string' && typeof command.args?.voltage_v === 'number') {
          if (command.args.channel === 'tec') {
            this.state.bringup.tuning.dacTecChannelV = command.args.voltage_v
          } else {
            this.state.bringup.tuning.dacLdChannelV = command.args.voltage_v
          }
          this.bumpBringupRevision(`DAC ${command.args.channel} channel shadow updated.`)
        }
        break
      case 'dac_debug_config':
        if (
          typeof command.args?.reference_mode === 'string' &&
          typeof command.args?.gain_2x === 'boolean' &&
          typeof command.args?.ref_div === 'boolean' &&
          typeof command.args?.sync_mode === 'string'
        ) {
          this.state.bringup.tuning.dacReferenceMode =
            command.args.reference_mode as DacReferenceMode
          this.state.bringup.tuning.dacGain2x = command.args.gain_2x
          this.state.bringup.tuning.dacRefDiv = command.args.ref_div
          this.state.bringup.tuning.dacSyncMode =
            command.args.sync_mode as DacSyncMode
          this.bumpBringupRevision('DAC reference and update policy updated.')
        }
        break
      case 'imu_debug_config':
        if (
          typeof command.args?.odr_hz === 'number' &&
          typeof command.args?.accel_range_g === 'number' &&
          typeof command.args?.gyro_range_dps === 'number' &&
          typeof command.args?.gyro_enabled === 'boolean' &&
          typeof command.args?.lpf2_enabled === 'boolean' &&
          typeof command.args?.timestamp_enabled === 'boolean' &&
          typeof command.args?.bdu_enabled === 'boolean' &&
          typeof command.args?.if_inc_enabled === 'boolean' &&
          typeof command.args?.i2c_disabled === 'boolean'
        ) {
          this.state.bringup.tuning.imuOdrHz = command.args.odr_hz
          this.state.bringup.tuning.imuAccelRangeG = command.args.accel_range_g
          this.state.bringup.tuning.imuGyroRangeDps = command.args.gyro_range_dps
          this.state.bringup.tuning.imuGyroEnabled = command.args.gyro_enabled
          this.state.bringup.tuning.imuLpf2Enabled = command.args.lpf2_enabled
          this.state.bringup.tuning.imuTimestampEnabled = command.args.timestamp_enabled
          this.state.bringup.tuning.imuBduEnabled = command.args.bdu_enabled
          this.state.bringup.tuning.imuIfIncEnabled = command.args.if_inc_enabled
          this.state.bringup.tuning.imuI2cDisabled = command.args.i2c_disabled
          this.bumpBringupRevision('IMU bring-up tuning updated.')
        }
        break
      case 'tof_debug_config':
        if (
          typeof command.args?.min_range_m === 'number' &&
          typeof command.args?.max_range_m === 'number' &&
          typeof command.args?.stale_timeout_ms === 'number'
        ) {
          this.state.bringup.tuning.tofMinRangeM = command.args.min_range_m
          this.state.bringup.tuning.tofMaxRangeM = command.args.max_range_m
          this.state.bringup.tuning.tofStaleTimeoutMs = command.args.stale_timeout_ms
          this.bumpBringupRevision('ToF debug thresholds updated.')
        }
        break
      case 'tof_illumination_set':
        if (typeof command.args?.enabled === 'boolean') {
          this.state.bringup.illumination.tof.enabled = command.args.enabled
          if (typeof command.args?.duty_cycle_pct === 'number') {
            this.state.bringup.illumination.tof.dutyCyclePct = clamp(
              command.args.duty_cycle_pct,
              0,
              100,
            )
          }
          if (typeof command.args?.frequency_hz === 'number') {
            this.state.bringup.illumination.tof.frequencyHz = clamp(
              command.args.frequency_hz,
              5000,
              100000,
            )
          }
          this.state.bringup.tools.lastAction = command.args.enabled
            ? `Front illumination staged on GPIO6 at ${this.state.bringup.illumination.tof.dutyCyclePct}% duty.`
            : 'Front illumination forced off on GPIO6.'
        }
        break
      case 'haptic_debug_config':
        if (
          typeof command.args?.effect_id === 'number' &&
          typeof command.args?.mode === 'string' &&
          typeof command.args?.library === 'number' &&
          typeof command.args?.actuator === 'string' &&
          typeof command.args?.rtp_level === 'number'
        ) {
          this.state.bringup.tuning.hapticEffectId = command.args.effect_id
          this.state.bringup.tuning.hapticMode = command.args.mode as HapticMode
          this.state.bringup.tuning.hapticLibrary = command.args.library
          this.state.bringup.tuning.hapticActuator =
            command.args.actuator as HapticActuator
          this.state.bringup.tuning.hapticRtpLevel = command.args.rtp_level
          this.bumpBringupRevision('Haptic debug effect changed.')
        }
        break
      case 'haptic_debug_fire':
        this.state.bringup.tools.lastI2cOp = 'DRV2605 GO pulse simulated in the mock rig.'
        this.state.bringup.tools.lastAction = 'Haptic test pattern fired.'
        break
      case 'haptic_external_trigger_pattern':
        if (
          typeof command.args?.pulse_count === 'number' &&
          typeof command.args?.high_ms === 'number' &&
          typeof command.args?.low_ms === 'number'
        ) {
          this.state.bringup.tools.lastAction =
            `External ERM trigger burst simulated on shared IO37: ${command.args.pulse_count} pulse(s), ${command.args.high_ms} ms high, ${command.args.low_ms} ms low.`
          this.emit({
            kind: 'event',
            event: this.makeEvent(
              'warn',
              'service',
              'Mock external ERM trigger burst',
              'Shared IO37 / GN_LD_EN pulse train simulated for GUI bring-up testing.',
            ),
          })
        }
        break
      case 'i2c_scan':
        this.state.bringup.tools.lastI2cScan = this.deriveI2cScan()
        this.state.bringup.tools.lastAction = 'I2C scan completed.'
        break
      case 'i2c_read':
        this.state.bringup.tools.lastI2cOp = this.describeI2cTransfer(command, false)
        this.state.bringup.tools.lastAction = 'I2C read captured in the service log.'
        break
      case 'i2c_write':
        this.state.bringup.tools.lastI2cOp = this.describeI2cTransfer(command, true)
        this.bumpBringupRevision('I2C register write simulated.')
        break
      case 'spi_read':
        this.state.bringup.tools.lastSpiOp = this.describeSpiTransfer(command, false)
        this.state.bringup.tools.lastAction = 'SPI read captured in the service log.'
        break
      case 'spi_write':
        this.state.bringup.tools.lastSpiOp = this.describeSpiTransfer(command, true)
        this.bumpBringupRevision('SPI register write simulated.')
        break
      case 'simulate_horizon_trip':
        this.raiseFault('horizon_crossed')
        this.state.beamPitchDeg = 7.8
        break
      case 'simulate_distance_trip':
        this.raiseFault('tof_out_of_range')
        this.state.distanceM = 1.3
        break
      case 'simulate_pd_drop':
        this.raiseFault('pd_lost')
        this.state.powerTier = 'programming_only'
        this.state.pdPowerW = 5
        this.state.systemState = 'PROGRAMMING_ONLY'
        this.state.alignmentRequested = false
        this.state.laserRequested = false
        break
      case 'simulate_ld_overtemp_trip':
        /*
         * Test-only path that lets operators (and render-check agents)
         * exercise the LD_OVERTEMP fault banner + triggerDiag sub-card in
         * InspectorRail without running real thermal stress on the bench.
         * The triggerDiag synthesis lives in the snapshot emitter keyed on
         * `activeFaultCode === 'ld_overtemp' && faultLatched`.
         */
        this.raiseFault('ld_overtemp')
        break
      default:
        break
    }

    this.emitRealtimeFrame(true)
    this.emit({
      kind: 'commandAck',
      commandId: command.id,
      ok: ackOk,
      note: ackNote,
    })
  }

  async beginFirmwareTransfer(
    pkg: FirmwarePackageDescriptor,
    onProgress: (progress: FirmwareTransferProgress) => void,
  ): Promise<void> {
    if (!this.state.connected) {
      throw new Error('Mock transport is not connected.')
    }

    const phases: FirmwareTransferProgress[] = [
      {
        phase: 'validate',
        percent: 10,
        detail: 'Package hash and board target accepted.',
      },
      {
        phase: 'prepare',
        percent: 24,
        detail: 'Controller moved into update-safe service flow.',
      },
      {
        phase: 'erase',
        percent: 43,
        detail: 'Mock flash erase in progress.',
      },
      {
        phase: 'write',
        percent: 76,
        detail: 'Streaming firmware payload segments.',
      },
      {
        phase: 'verify',
        percent: 92,
        detail: 'Post-write verification passed.',
      },
      {
        phase: 'reboot',
        percent: 100,
        detail: 'Controller rebooted into the new image.',
      },
    ]

    for (const phase of phases) {
      onProgress(phase)
      await new Promise((resolve) => window.setTimeout(resolve, 640))
    }

    this.state.firmwareVersion = pkg.version
    this.state.systemState = 'PROGRAMMING_ONLY'
    this.state.serviceMode = false
    this.state.bringup.serviceModeActive = false
    this.state.bringup.serviceModeRequested = false
    this.state.bringup.illumination.tof.enabled = false
    this.state.alignmentRequested = false
    this.state.laserRequested = false

    this.emit({
      kind: 'event',
      event: this.makeEvent(
        'ok',
        'firmware',
        'Firmware transfer complete',
        `Mock rig rebooted into ${pkg.packageName} ${pkg.version}.`,
      ),
    })
    this.emitRealtimeFrame(true)
  }

  private tick(deltaSeconds: number): void {
    const previousUptimeSeconds = this.state.uptimeSeconds
    this.state.uptimeSeconds += deltaSeconds

    if (this.state.deployment.active && this.state.deployment.running) {
      this.state.deploymentStepElapsedS += deltaSeconds

      if (this.state.deploymentStepElapsedS >= 0.8) {
        const steps = this.state.deployment.steps.map((step) => ({ ...step }))
        const currentIndex = this.state.deploymentStepIndex
        const currentStep = steps[currentIndex]

        this.state.deploymentStepElapsedS = 0

        if (currentStep !== undefined) {
          if (
            this.state.deploymentPlannedFailure === 'pd_insufficient' &&
            currentIndex === 1
          ) {
            currentStep.status = 'failed'
            this.state.deployment = {
              ...this.state.deployment,
              running: false,
              ready: false,
              failed: true,
              currentStep: 'none',
              lastCompletedStep: 'ownership_reclaim',
              failureCode: 'pd_insufficient',
              failureReason: 'Valid PD source with at least 9 V was not present.',
              maxLaserCurrentA: 0,
              maxOpticalPowerW: 0,
              steps,
            }
            this.state.deploymentStepIndex = -1
            this.state.systemState = 'PROGRAMMING_ONLY'
          } else {
            currentStep.status = 'passed'
            const nextIndex = currentIndex + 1

            if (nextIndex >= steps.length) {
              const reserveBudgetW = this.state.pdPowerW - 20
              const cappedCurrentA = Math.max(
                0,
                Math.min(this.state.safety.maxLaserCurrentA, (reserveBudgetW * 0.9) / 3),
              )

              this.state.deployment = {
                ...this.state.deployment,
                running: false,
                ready: true,
                failed: false,
                currentStep: 'none',
                lastCompletedStep: 'ready_posture',
                failureCode: 'none',
                failureReason: '',
                maxLaserCurrentA:
                  this.state.pdPowerW < 40 ? cappedCurrentA : this.state.safety.maxLaserCurrentA,
                maxOpticalPowerW:
                  this.state.pdPowerW < 40 ? cappedCurrentA : this.state.safety.maxLaserCurrentA,
                steps,
              }
              this.state.deploymentStepIndex = -1
              this.state.tecSettlingTicks = 0
              this.state.systemState = 'READY_NIR'
            } else {
              steps[nextIndex] = {
                ...steps[nextIndex],
                status: 'in_progress',
              }
              this.state.deployment = {
                ...this.state.deployment,
                currentStep: steps[nextIndex].key,
                lastCompletedStep: currentStep.key,
                steps,
              }
              this.state.deploymentStepIndex = nextIndex
            }
          }
        }
      }
    }

    if (this.state.systemState === 'BOOT_INIT') {
      if (this.state.tecSettlingTicks > 0) {
        this.state.tecSettlingTicks = Math.max(0, this.state.tecSettlingTicks - deltaSeconds)
      } else {
        this.state.systemState = this.state.serviceMode ? 'SERVICE_MODE' : 'READY_ALIGNMENT'
      }
    }

    if (this.state.tecSettlingTicks > 0 && this.state.systemState !== 'BOOT_INIT') {
      this.state.tecSettlingTicks = Math.max(0, this.state.tecSettlingTicks - deltaSeconds)
    }

    const tecReady = this.state.tecSettlingTicks <= 0.01
    const nirAllowed =
      !this.state.faultLatched &&
      this.state.powerTier === 'full' &&
      tecReady

    if (this.state.serviceMode) {
      this.state.systemState = 'SERVICE_MODE'
    } else if (this.state.faultLatched) {
      this.state.systemState = 'FAULT_LATCHED'
    } else if (this.state.powerTier === 'programming_only') {
      this.state.systemState = 'PROGRAMMING_ONLY'
    } else if (!tecReady) {
      this.state.systemState = 'TEC_SETTLING'
    } else if (this.state.laserRequested && nirAllowed) {
      this.state.systemState = 'NIR_ACTIVE'
    } else if (this.state.alignmentRequested) {
      this.state.systemState = 'ALIGNMENT_ACTIVE'
    } else if (nirAllowed) {
      this.state.systemState = 'READY_NIR'
    } else {
      this.state.systemState = 'READY_ALIGNMENT'
    }

    const uptime = this.state.uptimeSeconds
    this.state.beamPitchDeg =
      this.state.activeFault === 'horizon_crossed'
        ? 7.8
        : -14 + Math.sin(uptime / 8) * 1.7
    this.state.beamRollDeg = Math.sin(uptime / 6.5) * 11
    this.state.beamYawDeg = ((Math.sin(uptime / 9) * 42) + (uptime * 3.5)) % 360
    if (this.state.beamYawDeg > 180) {
      this.state.beamYawDeg -= 360
    }
    this.state.distanceM =
      this.state.activeFault === 'tof_out_of_range'
        ? 1.3
        : 0.41 + Math.cos(uptime / 9) * 0.06

    if (Math.floor(previousUptimeSeconds / 20) !== Math.floor(uptime / 20)) {
      this.emit({
        kind: 'event',
        event: this.makeEvent(
          'info',
          'telemetry',
          'Power contract revalidated',
          `${this.state.pdPowerW.toFixed(1)} W sink budget remains available.`,
        ),
      })
    }

    this.emitRealtimeFrame(Math.floor(previousUptimeSeconds) !== Math.floor(uptime))
  }

  private raiseFault(code: string): void {
    this.state.activeFault = code
    this.state.faultLatched = !['horizon_crossed', 'tof_out_of_range', 'imu_stale', 'imu_invalid', 'tof_stale', 'tof_invalid', 'lambda_drift', 'tec_temp_adc_high'].includes(code)
    this.state.faultCount += 1
    this.state.tripCounter += 1
    this.state.lastFaultAtIso = nowIso()
    this.state.alignmentRequested = false
    this.state.laserRequested = false
    this.emit({
      kind: 'event',
      event: this.makeEvent(
        'critical',
        'fault',
        `Fault latched: ${code}`,
        'The simulated controller forced the beam path off and recorded a latch.',
      ),
    })
  }

  private bumpBringupRevision(action: string): void {
    this.state.bringup.profileRevision += 1
    this.state.bringup.persistenceDirty = true
    this.state.bringup.lastSaveOk = false
    this.state.bringup.tools.lastAction = action
  }

  private resetModuleProbeState(module: ModuleKey): void {
    this.state.bringup.modules[module].detected = false
    this.state.bringup.modules[module].healthy = false
  }

  private markModuleProbeSuccess(module: ModuleKey): void {
    this.state.bringup.modules[module].detected = true
    this.state.bringup.modules[module].healthy = true
  }

  private applyBringupPreset(preset: string): void {
    const bringup = makeDefaultBringupStatus()

    if (preset === 'add_haptic') {
      bringup.profileName = 'imu-dac-haptic'
      bringup.modules.haptic = {
        expectedPresent: true,
        debugEnabled: true,
        detected: false,
        healthy: false,
      }
    } else if (preset === 'add_tof') {
      bringup.profileName = 'imu-dac-tof'
      bringup.modules.tof = {
        expectedPresent: true,
        debugEnabled: true,
        detected: false,
        healthy: false,
      }
    } else if (preset === 'full_stack') {
      bringup.profileName = 'full-stack'
      for (const key of Object.keys(bringup.modules) as ModuleKey[]) {
        bringup.modules[key] = {
          expectedPresent: true,
          debugEnabled: true,
          detected: false,
          healthy: false,
        }
      }
    }

    this.state.bringup = bringup
    this.bumpBringupRevision(`Bring-up preset applied: ${preset}.`)
  }

  private deriveI2cScan(): string {
    const addresses: string[] = []

    if (this.state.bringup.modules.pd.expectedPresent) {
      this.state.bringup.modules.pd.detected = true
      addresses.push('0x28')
    }
    if (this.state.bringup.modules.dac.expectedPresent) {
      this.state.bringup.modules.dac.detected = true
      addresses.push('0x48')
    }
    if (this.state.bringup.modules.haptic.expectedPresent) {
      this.state.bringup.modules.haptic.detected = true
      addresses.push('0x5A')
    }

    return addresses.length > 0
      ? addresses.join(' ')
      : 'No I2C targets declared in the bring-up profile.'
  }

  private describeI2cTransfer(command: CommandEnvelope, write: boolean): string {
    const address = typeof command.args?.address === 'number' ? command.args.address : 0
    const reg = typeof command.args?.reg === 'number' ? command.args.reg : 0
    const value = typeof command.args?.value === 'number' ? command.args.value : 0
    const supported = [0x28, 0x48, 0x5a].includes(address)

    if (!supported) {
      return `${write ? 'write' : 'read'} 0x${address.toString(16)} reg 0x${reg.toString(16)} -> no-ack`
    }

    if (address === 0x28) {
      this.markModuleProbeSuccess('pd')
    } else if (address === 0x48) {
      this.markModuleProbeSuccess('dac')
    } else if (address === 0x5a) {
      this.markModuleProbeSuccess('haptic')
    }

    if (write) {
      return `write 0x${address.toString(16)} reg 0x${reg.toString(16)} <- 0x${value.toString(16)}`
    }

    return `read 0x${address.toString(16)} reg 0x${reg.toString(16)} -> 0x5a`
  }

  private describeSpiTransfer(command: CommandEnvelope, write: boolean): string {
    const device = typeof command.args?.device === 'string' ? command.args.device : 'unknown'
    const reg = typeof command.args?.reg === 'number' ? command.args.reg : 0
    const value = typeof command.args?.value === 'number' ? command.args.value : 0

    if (device !== 'imu' || !this.state.bringup.modules.imu.expectedPresent) {
      return `${device} reg 0x${reg.toString(16)} -> unavailable`
    }

    this.markModuleProbeSuccess('imu')

    if (write) {
      return `${device} reg 0x${reg.toString(16)} <- 0x${value.toString(16)}`
    }

    return `${device} reg 0x${reg.toString(16)} -> 0x6c`
  }

  private makeSnapshot(): DeviceSnapshot {
    const gpioInspector = {
      ...this.state.gpioInspector,
      pins: this.state.gpioInspector.pins.map((pin) => ({
        ...pin,
      })),
    }
    const gpio4 = gpioInspector.pins.find((pin) => pin.gpioNum === 4)
    const gpio5 = gpioInspector.pins.find((pin) => pin.gpioNum === 5)
    const gpio15 = gpioInspector.pins.find((pin) => pin.gpioNum === 15)
    const gpio16 = gpioInspector.pins.find((pin) => pin.gpioNum === 16)
    const gpio17 = gpioInspector.pins.find((pin) => pin.gpioNum === 17)
    const gpio18 = gpioInspector.pins.find((pin) => pin.gpioNum === 18)
    const gpio37 = gpioInspector.pins.find((pin) => pin.gpioNum === 37)
    const gpio48 = gpioInspector.pins.find((pin) => pin.gpioNum === 48)

    const tecReady = this.state.tecSettlingTicks <= 0.01
    const deploymentActive = this.state.deployment.active
    const deploymentReady = deploymentActive && this.state.deployment.ready
    const runtimeModeLock = runtimeModeLockReason(this.state)
    const tecActualTemp =
      this.state.targetTempC -
      this.state.tecSettlingTicks * 0.28 +
      Math.sin(this.state.uptimeSeconds / 13) * 0.04
    const alignmentEnabled =
      this.state.alignmentRequested &&
      deploymentReady &&
      !this.state.faultLatched &&
      !this.state.laserRequested &&
      this.state.powerTier !== 'programming_only'
    const nirEnabled =
      this.state.laserRequested &&
      this.state.runtimeMode === 'modulated_host' &&
      deploymentReady &&
      !this.state.faultLatched &&
      this.state.powerTier === 'full' &&
      tecReady
    const dutyFraction =
      nirEnabled && this.state.modulationEnabled
        ? this.state.modulationDutyCyclePct / 100
        : nirEnabled
          ? 1
          : 0
    const measuredCurrentA = nirEnabled
      ? dutyFraction * this.state.laserHighCurrentA +
        (1 - dutyFraction) * this.state.lowStateCurrentA +
        Math.sin(this.state.uptimeSeconds / 6) * 0.02
      : 0
    const hostOnly = this.state.powerTier === 'programming_only' && this.state.pdPowerW <= 5.1
    const pdContract = selectMockPdContract(
      this.state.pdPowerW,
      hostOnly,
      this.state.bringup.tuning,
    )
    const tecCurrentA =
      this.state.tecSettlingTicks > 0
        ? 1.8 + this.state.laserHighCurrentA * 0.15
        : 0.55 + (nirEnabled ? 0.35 : 0)
    const tecVoltageV =
      this.state.tecSettlingTicks > 0
        ? 3.2
        : 1.35 + (nirEnabled ? 0.24 : 0)
    const actualLambdaNm = estimateWavelengthFromTempC(tecActualTemp)
    const serviceLdEnabled =
      this.state.serviceMode && this.state.bringup.power.ldRequested
    const serviceTecEnabled =
      this.state.serviceMode && this.state.bringup.power.tecRequested
    const ldRailEnabled =
      this.state.serviceMode
        ? serviceLdEnabled
        : deploymentActive
          ? deploymentReady
          : this.state.powerTier === 'full' && tecReady && !this.state.faultLatched
    const tecRailEnabled =
      this.state.serviceMode
        ? serviceTecEnabled
        : deploymentActive
          ? deploymentReady
          : this.state.powerTier === 'full'
    const driverStandby = !deploymentReady || this.state.faultLatched
    const ldTelemetryValid = ldRailEnabled && !driverStandby
    const tecTelemetryValid = tecRailEnabled
    const reportedActualLambdaNm = tecTelemetryValid
      ? actualLambdaNm
      : this.state.targetLambdaNm
    const reportedLambdaDriftNm = tecTelemetryValid
      ? Math.abs(actualLambdaNm - this.state.targetLambdaNm)
      : 0
    const tempAdcVoltageV = estimateTecVoltageFromTempC(tecActualTemp)
    const horizonBlocked = this.state.beamPitchDeg > this.state.safety.horizonThresholdDeg
    const distanceBlocked =
      this.state.distanceM < this.state.safety.tofMinRangeM ||
      this.state.distanceM > this.state.safety.tofMaxRangeM
    const lambdaDriftBlocked =
      deploymentReady && reportedLambdaDriftNm > this.state.safety.lambdaDriftLimitNm
    const tecTempAdcBlocked =
      tecTelemetryValid && tempAdcVoltageV > this.state.safety.tecTempAdcTripV
    const activeFaultCode =
      this.state.faultLatched
        ? this.state.activeFault
        : this.state.activeFault !== 'none'
          ? this.state.activeFault
          : horizonBlocked
            ? 'horizon_crossed'
            : distanceBlocked
              ? 'tof_out_of_range'
              : lambdaDriftBlocked
                ? 'lambda_drift'
                : tecTempAdcBlocked
                  ? 'tec_temp_adc_high'
                  : 'none'
    const benchDefaults = makeDefaultBenchControlStatus()

    if (gpio4 !== undefined) {
      gpio4.inputEnabled = true
      gpio4.outputEnabled = false
      gpio4.openDrainEnabled = false
      gpio4.pullupEnabled = true
      gpio4.pulldownEnabled = false
      if (gpio4.overrideActive) {
        gpio4.levelHigh =
          gpio4.overrideMode === 'output' ? gpio4.overrideLevelHigh : true
      } else {
        gpio4.levelHigh = true
      }
    }

    if (gpio5 !== undefined) {
      gpio5.inputEnabled = true
      gpio5.outputEnabled = false
      gpio5.openDrainEnabled = false
      gpio5.pullupEnabled = true
      gpio5.pulldownEnabled = false
      if (gpio5.overrideActive) {
        gpio5.levelHigh =
          gpio5.overrideMode === 'output' ? gpio5.overrideLevelHigh : true
      } else {
        gpio5.levelHigh = true
      }
    }

    if (gpio15 !== undefined) {
      gpio15.inputEnabled = false
      gpio15.outputEnabled = true
      gpio15.levelHigh = gpio15.overrideActive && gpio15.overrideMode === 'output'
        ? gpio15.overrideLevelHigh
        : tecRailEnabled
    }

    if (gpio16 !== undefined) {
      gpio16.inputEnabled = true
      gpio16.outputEnabled = false
      gpio16.levelHigh = gpio16.overrideActive && gpio16.overrideMode === 'output'
        ? gpio16.overrideLevelHigh
        : tecRailEnabled
    }

    if (gpio17 !== undefined) {
      gpio17.inputEnabled = false
      gpio17.outputEnabled = true
      gpio17.levelHigh = gpio17.overrideActive && gpio17.overrideMode === 'output'
        ? gpio17.overrideLevelHigh
        : ldRailEnabled
    }

    if (gpio18 !== undefined) {
      gpio18.inputEnabled = true
      gpio18.outputEnabled = false
      gpio18.levelHigh = gpio18.overrideActive && gpio18.overrideMode === 'output'
        ? gpio18.overrideLevelHigh
        : ldRailEnabled
    }

    if (gpio37 !== undefined) {
      gpio37.inputEnabled = false
      gpio37.outputEnabled = true
      gpio37.levelHigh = gpio37.overrideActive && gpio37.overrideMode === 'output'
        ? gpio37.overrideLevelHigh
        : alignmentEnabled
    }

    if (gpio48 !== undefined) {
      gpio48.inputEnabled = false
      gpio48.outputEnabled = true
      gpio48.levelHigh = gpio48.overrideActive && gpio48.overrideMode === 'output'
        ? gpio48.overrideLevelHigh
        : this.state.serviceMode && this.state.hapticDriverEnabled
    }

    const tofArmed =
      this.state.bringup.modules.tof.expectedPresent ||
      this.state.bringup.modules.tof.debugEnabled
    const interlocksDisabled = this.state.bringup.interlocksDisabled

    return {
      identity: {
        label: 'BSL-HTLS Gen2',
        firmwareVersion: this.state.firmwareVersion,
        hardwareRevision: 'rev-A',
        serialNumber: 'BSL-HTLS2-00017',
        protocolVersion: 'host-v1',
      },
      session: {
        uptimeSeconds: this.state.uptimeSeconds,
        state: this.state.systemState,
        powerTier: this.state.powerTier,
        bootReason: 'power_on_reset',
        connectedAtIso: new Date(Date.now() - this.state.uptimeSeconds * 1000).toISOString(),
      },
      wireless: {
        ...this.state.wireless,
      },
      pd: {
        contractValid: this.state.powerTier !== 'unknown',
        negotiatedPowerW: this.state.pdPowerW,
        sourceVoltageV: pdContract.sourceVoltageV,
        sourceCurrentA: pdContract.sourceCurrentA,
        operatingCurrentA: pdContract.operatingCurrentA,
        contractObjectPosition: pdContract.contractObjectPosition,
        sinkProfileCount: pdContract.sinkProfileCount,
        sinkProfiles: pdContract.sinkProfiles,
        sourceIsHostOnly: hostOnly,
        lastUpdatedMs: Math.round(this.state.uptimeSeconds * 1000),
        snapshotFresh: !this.state.deployment.active,
        source: this.state.deployment.active ? 'cached' : 'integrate_refresh',
      },
      rails: {
        ld: {
          enabled: ldRailEnabled,
          pgood: ldRailEnabled,
        },
        tec: {
          enabled: tecRailEnabled,
          pgood: tecRailEnabled,
        },
      },
      imu: {
        valid: true,
        fresh: true,
        beamPitchDeg: this.state.beamPitchDeg,
        beamRollDeg: this.state.beamRollDeg,
        beamYawDeg: this.state.beamYawDeg,
        beamYawRelative: true,
        beamPitchLimitDeg: 0,
      },
      tof: {
        valid: true,
        fresh: true,
        distanceM: this.state.distanceM,
        minRangeM: 0.2,
        maxRangeM: 1,
      },
      laser: {
        alignmentEnabled,
        nirEnabled,
        driverStandby,
        telemetryValid: ldTelemetryValid,
        commandVoltageV:
          ldTelemetryValid ? this.state.laserHighCurrentA * (2.5 / 6) : 0,
        measuredCurrentA: ldTelemetryValid ? measuredCurrentA : 0,
        commandedCurrentA: nirEnabled ? this.state.laserHighCurrentA : 0,
        currentMonitorVoltageV: ldTelemetryValid ? measuredCurrentA / 2.4 : 0,
        loopGood: ldTelemetryValid && !this.state.faultLatched,
        driverTempVoltageV: ldTelemetryValid
          ?
          (192.5576 -
            (29.4 + measuredCurrentA * 2.6 + Math.sin(this.state.uptimeSeconds / 11) * 0.8)) /
          90.104
          : 0,
        driverTempC: ldTelemetryValid
          ? 29.4 + measuredCurrentA * 2.6 + Math.sin(this.state.uptimeSeconds / 11) * 0.8
          : 0,
      },
      tec: {
        targetTempC: this.state.targetTempC,
        targetLambdaNm: this.state.targetLambdaNm,
        actualLambdaNm: reportedActualLambdaNm,
        telemetryValid: tecTelemetryValid,
        commandVoltageV: this.state.serviceMode
          ? this.state.bringup.tuning.dacTecChannelV
          : Math.max(0, Math.min(2.5, this.state.targetTempC * (2.5 / 65))),
        tempGood: tecTelemetryValid && tecReady,
        tempC: tecTelemetryValid ? tecActualTemp : 0,
        tempAdcVoltageV: tecTelemetryValid ? tempAdcVoltageV : 0,
        currentA: tecTelemetryValid ? tecCurrentA : 0,
        voltageV: tecTelemetryValid ? tecVoltageV : 0,
        settlingSecondsRemaining: Math.max(0, Math.ceil(this.state.tecSettlingTicks)),
      },
      buttons: {
        stage1Pressed: this.state.alignmentRequested || this.state.laserRequested,
        stage2Pressed: this.state.laserRequested,
        stage1Edge: false,
        stage2Edge: false,
        side1Pressed: false,
        side2Pressed: false,
        side1Edge: false,
        side2Edge: false,
        boardReachable: this.state.mcpReachable,
        isrFireCount: this.state.buttonIsrFireCount,
      },
      buttonBoard: {
        mcpAddr: '0x20',
        tlcAddr: '0x60',
        mcpReachable: this.state.mcpReachable,
        mcpConfigured: this.state.mcpReachable,
        mcpLastError: 0,
        mcpConsecFailures: 0,
        tlcReachable: this.state.tlcReachable,
        tlcConfigured: this.state.tlcReachable,
        tlcLastError: 0,
        isrFireCount: this.state.buttonIsrFireCount,
        rgb: {
          ...this.state.rgbState,
          testActive:
            this.state.rgbTestUntilMs > 0 &&
            Date.now() <= this.state.rgbTestUntilMs,
        },
        ledBrightnessPct: this.state.buttonLedBrightnessPct,
        ledOwned: this.state.buttonLedOwned,
        triggerLockout: this.state.buttonNirLockout,
        triggerPhase: mockTriggerPhase(this.state),
        nirButtonBlockReason: !this.state.deployment.active
          ? 'deployment-not-active'
          : !this.state.deployment.ready
            ? 'deployment-not-ready'
            : !this.state.deployment.readyIdle
              ? 'deployment-not-ready-idle'
              : this.state.faultLatched
                ? `fault: ${this.state.activeFault}`
                : this.state.buttonNirLockout
                  ? 'lockout-latched'
                  : 'none',
      },
      peripherals: {
        dac: {
          reachable: this.state.bringup.modules.dac.expectedPresent,
          configured: this.state.bringup.modules.dac.expectedPresent,
          refAlarm: false,
          syncReg: this.state.bringup.tuning.dacSyncMode === 'sync' ? 0x0303 : 0x0300,
          configReg: this.state.bringup.tuning.dacReferenceMode === 'external' ? 0x0001 : 0x0000,
          gainReg:
            (this.state.bringup.tuning.dacRefDiv ? 0x0100 : 0) |
            (this.state.bringup.tuning.dacGain2x ? 0x0003 : 0),
          statusReg: 0x0000,
          dataAReg: Math.round((Math.max(0, Math.min(2.5, this.state.bringup.tuning.dacLdChannelV)) / 2.5) * 65535),
          dataBReg: Math.round((Math.max(0, Math.min(2.5, this.state.bringup.tuning.dacTecChannelV)) / 2.5) * 65535),
          lastErrorCode: 0,
          lastError: 'ESP_OK',
        },
        pd: {
          reachable: this.state.bringup.modules.pd.expectedPresent,
          attached: !hostOnly,
          ccStatusReg: hostOnly ? 0x01 : 0x13,
          pdoCountReg: pdContract.sinkProfileCount,
          rdoStatusRaw:
            ((pdContract.contractObjectPosition & 0x07) << 28) |
            ((Math.round(pdContract.operatingCurrentA * 100) & 0x3ff) << 10) |
            (Math.round(pdContract.sourceCurrentA * 100) & 0x3ff),
        },
        imu: {
          reachable: this.state.bringup.modules.imu.expectedPresent,
          configured: this.state.bringup.modules.imu.expectedPresent,
          whoAmI: 0x6c,
          statusReg: 0x03,
          ctrl1XlReg: 0x58,
          ctrl2GReg: 0x54,
          ctrl3CReg: 0x44,
          ctrl4CReg: 0x04,
          ctrl10CReg: this.state.bringup.tuning.imuTimestampEnabled ? 0x20 : 0x00,
          lastErrorCode: 0,
          lastError: 'ESP_OK',
        },
        haptic: {
          reachable: this.state.bringup.modules.haptic.expectedPresent,
          enablePinHigh: this.state.serviceMode && this.state.hapticDriverEnabled,
          triggerPinHigh: alignmentEnabled,
          modeReg: 0x00,
          libraryReg: 0x01,
          goReg: 0x00,
          feedbackReg: this.state.bringup.tuning.hapticActuator === 'lra' ? 0x80 : 0x00,
          lastErrorCode: 0,
          lastError: 'ESP_OK',
        },
        tof: {
          reachable: tofArmed,
          configured: tofArmed,
          interruptLineHigh: tofArmed,
          ledCtrlAsserted:
            tofArmed &&
            this.state.bringup.illumination.tof.enabled,
          dataReady: tofArmed,
          bootState: tofArmed ? 1 : 0,
          rangeStatus: tofArmed ? 0 : 255,
          sensorId: tofArmed ? 0xeacc : 0,
          distanceMm: Math.round(this.state.distanceM * 1000),
          lastErrorCode: 0,
          lastError: 'ESP_OK',
        },
      },
      gpioInspector,
      bench: {
        ...benchDefaults,
        targetMode: this.state.targetMode,
        runtimeMode: this.state.runtimeMode,
        runtimeModeSwitchAllowed: runtimeModeLock === null,
        runtimeModeLockReason: runtimeModeLock ?? '',
        requestedAlignmentEnabled: this.state.alignmentRequested,
        requestedNirEnabled: this.state.laserRequested,
        requestedCurrentA: this.state.laserHighCurrentA,
        illuminationEnabled: this.state.bringup.illumination.tof.enabled,
        illuminationDutyCyclePct: this.state.bringup.illumination.tof.dutyCyclePct,
        illuminationFrequencyHz: this.state.bringup.illumination.tof.frequencyHz,
        /*
         * Mirror laser_controller_comms_led_owner_name priority order:
         * integrate_service > operate_runtime > button_trigger > deployment > none.
         * The button_trigger branch was added 2026-04-15 alongside the firmware
         * led_owner_name update. Keep this in sync with comms.c.
         */
        appliedLedOwner: this.state.serviceMode &&
                         this.state.bringup.illumination.tof.enabled
          ? 'integrate_service'
          : this.state.bringup.illumination.tof.enabled
            ? 'operate_runtime'
            : this.state.buttonLedOwned
              ? 'button_trigger'
              : this.state.deployment.active
                ? 'deployment'
                : 'none',
        appliedLedPinHigh:
          (this.state.bringup.illumination.tof.enabled &&
            this.state.bringup.illumination.tof.dutyCyclePct > 0) ||
          (this.state.buttonLedOwned && this.state.buttonLedBrightnessPct > 0),
        modulationEnabled: this.state.modulationEnabled,
        modulationFrequencyHz: this.state.modulationFrequencyHz,
        modulationDutyCyclePct: this.state.modulationDutyCyclePct,
        lowStateCurrentA: this.state.lowStateCurrentA,
        hostControlReadiness: {
          nirBlockedReason: mockNirBlockedReason(this.state),
          alignmentBlockedReason: 'none',
          ledBlockedReason: mockLedBlockedReason(this.state),
          sbdnState: mockSbdnState(this.state),
        },
        usbDebugMock: {
          active: this.state.usbDebugMockActive,
          pdConflictLatched: this.state.usbDebugMockPdConflictLatched,
          enablePending: false,
          activatedAtMs: this.state.usbDebugMockActivatedAtMs,
          deactivatedAtMs: this.state.usbDebugMockDeactivatedAtMs,
          lastDisableReason: this.state.usbDebugMockLastDisableReason,
        },
      },
      safety: {
        ...this.state.safety,
        allowAlignment: interlocksDisabled
          ? !this.state.faultLatched
          : !this.state.faultLatched && !horizonBlocked && !distanceBlocked,
        allowNir:
          interlocksDisabled
            ? !this.state.faultLatched && this.state.powerTier === 'full'
            : !this.state.faultLatched &&
              !horizonBlocked &&
              !distanceBlocked &&
              !lambdaDriftBlocked &&
              !tecTempAdcBlocked &&
              this.state.powerTier === 'full' &&
              tecReady,
        horizonBlocked: interlocksDisabled ? false : horizonBlocked,
        distanceBlocked: interlocksDisabled ? false : distanceBlocked,
        lambdaDriftBlocked: interlocksDisabled ? false : lambdaDriftBlocked,
        tecTempAdcBlocked: interlocksDisabled ? false : tecTempAdcBlocked,
        actualLambdaNm: reportedActualLambdaNm,
        targetLambdaNm: this.state.targetLambdaNm,
        lambdaDriftNm: reportedLambdaDriftNm,
        tempAdcVoltageV,
      },
      bringup: {
        ...this.state.bringup,
        serviceModeRequested: this.state.serviceMode,
        serviceModeActive: this.state.serviceMode,
      },
      deployment: {
        ...this.state.deployment,
        steps: this.state.deployment.steps.map((step) => ({ ...step })),
      },
      fault: {
        latched: this.state.faultLatched,
        activeCode: activeFaultCode,
        activeClass: this.state.faultLatched ? 'system_major' : 'none',
        latchedCode: this.state.faultLatched ? activeFaultCode : 'none',
        latchedClass: this.state.faultLatched ? 'system_major' : 'none',
        activeReason: '',
        latchedReason: '',
        activeCount: this.state.faultCount,
        tripCounter: this.state.tripCounter,
        lastFaultAtIso: this.state.lastFaultAtIso,
        // Mirror firmware: triggerDiag is populated ONLY for LD_OVERTEMP,
        // frozen at trip time. Representative numbers chosen to look
        // plausible for a real trip (68.3 °C with a 55 °C limit, gates
        // already past the 2 s settle so a real overtemp, not a false
        // trip). Real firmware captures exact ADC voltages; the mock
        // approximates from the same 192.5576 - 90.1040*V curve.
        triggerDiag:
          activeFaultCode === 'ld_overtemp' && this.state.faultLatched
            ? {
                code: 'ld_overtemp',
                measuredC: 68.3,
                measuredVoltageV: 1.3776, // (192.5576 - 68.3) / 90.1040
                limitC: 55.0,
                ldPgoodForMs: 4200,
                sbdnNotOffForMs: 3800,
                expr: 'ld_temp_c > 55.0 C @ 68.3 C, 1.378 V',
              }
            : null,
      },
      counters: {
        commsTimeouts: 0,
        watchdogTrips: 0,
        brownouts: 0,
      },
    }
  }

  private makeEvent(
    severity: SessionEvent['severity'],
    category: string,
    title: string,
    detail: string,
  ): SessionEvent {
    return {
      id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
      atIso: nowIso(),
      severity,
      category,
      title,
      detail,
    }
  }

  private emitRealtimeFrame(includeSnapshot: boolean): void {
    const snapshot = this.makeSnapshot()

    this.emit({
      kind: 'telemetry',
      telemetry: makeRealtimeTelemetryFromSnapshot(snapshot),
    })

    if (includeSnapshot) {
      this.emit({ kind: 'snapshot', snapshot })
    }
  }

  private emit(message: TransportMessage): void {
    for (const listener of this.listeners) {
      listener(message)
    }
  }
}
