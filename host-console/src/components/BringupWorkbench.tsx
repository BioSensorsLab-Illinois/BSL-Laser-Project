import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import {
  Activity,
  Cable,
  Compass,
  Cpu,
  Gamepad2,
  Layers3,
  PlugZap,
  Radio,
  Save,
  ShieldAlert,
  SlidersHorizontal,
  Thermometer,
  Waves,
  Wrench,
} from 'lucide-react'

import { HelpHint } from './HelpHint'
import { ConfirmActionDialog } from './ConfirmActionDialog'
import { ControllerBusyOverlay } from './ControllerBusyOverlay'
import { ImuPostureCard } from './ImuPostureCard'
import { ProgressMeter } from './ProgressMeter'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import {
  dacReferenceOptions,
  dacSyncModeOptions,
  hapticActuatorOptions,
  hapticLibraryOptions,
  hapticModeOptions,
  imuAccelRangeOptions,
  imuGyroRangeOptions,
  imuOdrOptions,
  makeDefaultPdProfiles,
  moduleKeys,
  moduleMeta,
  observeBringupModuleStatus,
  pdCurrentOptions,
  pdVoltageOptions,
} from '../lib/bringup'
import { formatNumber } from '../lib/format'
import {
  clampLdCommandCurrentA,
  clampLdCommandVoltageV,
  estimateLdCurrentFromVoltageV,
  estimateLdDiodeElectricalPowerW,
  estimateLdSupplyDrawW,
} from '../lib/ld-calibration'
import type { UiTone } from '../lib/presentation'
import {
  clampTecTempC,
  clampTecVoltageV,
  clampTecWavelengthNm,
  estimateTecVoltageFromTempC,
  estimateTecVoltageFromWavelengthNm,
  estimateTempFromTecVoltageV,
  estimateWavelengthFromTecVoltageV,
  tecCalibrationPoints,
} from '../lib/tec-calibration'
import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type {
  BringupModuleMap,
  BringupModuleStatus,
  DacReferenceMode,
  DacSyncMode,
  DeviceSnapshot,
  HapticActuator,
  HapticMode,
  ModuleKey,
  PdSinkProfile,
  TransportStatus,
} from '../types'

type BringupWorkbenchProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
  transportStatus: TransportStatus
  transportRecovering: boolean
  deploymentLocked: boolean
  onIssueCommandAwaitAck: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
    options?: {
      logHistory?: boolean
      timeoutMs?: number
    },
  ) => Promise<{
    ok: boolean
    note: string
  }>
}

type BringupPageKey = 'workflow' | 'power' | ModuleKey

type BringupFormState = {
  profileName: string
  modules: BringupModuleMap
  dacLdVoltage: string
  dacTecVoltage: string
  dacReferenceMode: DacReferenceMode
  dacGain2x: boolean
  dacRefDiv: boolean
  dacSyncMode: DacSyncMode
  imuOdrHz: string
  imuAccelRangeG: string
  imuGyroRangeDps: string
  imuGyroEnabled: boolean
  imuLpf2Enabled: boolean
  imuTimestampEnabled: boolean
  imuBduEnabled: boolean
  imuIfIncEnabled: boolean
  imuI2cDisabled: boolean
  tofMinRangeM: string
  tofMaxRangeM: string
  tofStaleTimeoutMs: string
  pdProfiles: Array<{
    enabled: boolean
    voltageV: string
    currentA: string
  }>
  pdProgrammingOnlyMaxW: string
  pdReducedModeMinW: string
  pdReducedModeMaxW: string
  pdFullModeMinW: string
  pdFirmwarePlanEnabled: boolean
  hapticEffectId: string
  hapticMode: HapticMode
  hapticLibrary: string
  hapticActuator: HapticActuator
  hapticRtpLevel: string
  safetyHorizonThresholdDeg: string
  safetyHorizonHysteresisDeg: string
  safetyTofMinRangeM: string
  safetyTofMaxRangeM: string
  safetyTofHysteresisM: string
  safetyImuStaleMs: string
  safetyTofStaleMs: string
  safetyRailGoodTimeoutMs: string
  safetyLambdaDriftLimitNm: string
  safetyLambdaDriftHysteresisNm: string
  safetyLambdaDriftHoldMs: string
  safetyLdOvertempLimitC: string
  safetyTecTempAdcTripV: string
  safetyTecTempAdcHysteresisV: string
  safetyTecTempAdcHoldMs: string
  safetyTecMinCommandC: string
  safetyTecMaxCommandC: string
  safetyTecReadyToleranceC: string
  safetyMaxLaserCurrentA: string
}

type BringupDraft = BringupFormState

type PageDefinition = {
  id: BringupPageKey
  label: string
  detail: string
  icon: typeof Wrench
}

type ModuleScore = {
  progress: number
  tone: UiTone
  label: string
}

type BringupStoredState = {
  version: number
  activePage: BringupPageKey
  form: BringupDraft
  i2cAddress: string
  i2cRegister: string
  i2cValue: string
  spiDevice: string
  spiRegister: string
  spiValue: string
}

type BringupOperation = {
  label: string
  detail: string
  percent: number
  tone: UiTone
  requiresConfirm?: boolean
}

type HapticPatternDraft = {
  pulseCount: string
  highMs: string
  lowMs: string
  releaseAfter: boolean
  hazardAccepted: boolean
}

type LdSbdnMode = 'firmware' | 'off-pd' | 'on-pu' | 'standby-hiz'
type LdPcnMode = 'firmware' | 'lisl' | 'lish'

type ProbeRequest = {
  id: string
  cmd: string
  note: string
  args?: Record<string, number | string | boolean>
}

type ProbeCycleState = {
  signature: string
  nextIndex: number
  initialSweepComplete: boolean
  lastProbeAtMs: number
}

const HOST_DRAFT_STORAGE_KEY = 'bsl-bringup-draft-v6'
const LEGACY_HOST_DRAFT_STORAGE_KEY = 'bsl-bringup-draft-v3'
const BRINGUP_ACK_TIMEOUT_MS = 3200
const BRINGUP_PROBE_INTERVAL_MS = 9000
const BRINGUP_PROBE_SWEEP_INTERVAL_MS = 900
const BRINGUP_PLAN_SYNC_INTERVAL_MS = 700
const BRINGUP_AUTO_PROBE_TIMEOUT_MS = 1500
const BRINGUP_PD_REFRESH_TIMEOUT_MS = 2600
const BRINGUP_PD_APPLY_TIMEOUT_MS = 7000
const BRINGUP_PD_NVM_TIMEOUT_MS = 12000
const BRINGUP_SERVICE_MODE_WAIT_MS = 2200
const BRINGUP_SUCCESS_DISMISS_MS = 260
const BRINGUP_HAPTIC_ENABLE_WAIT_MS = 1200
const BRINGUP_HAPTIC_PATTERN_MAX_PULSES = 12
const BRINGUP_HAPTIC_PATTERN_MIN_MS = 10
const BRINGUP_HAPTIC_PATTERN_MAX_MS = 600
const TOF_ILLUMINATION_PWM_HZ = 20000
const DAC80502_BOARD_SPAN_V = 2.5
const DAC80502_CODE_MAX = 0xffff
const LD_CURRENT_FULL_SCALE_A = 6
const LD_CURRENT_UI_MAX_A = 5
const LD_TEMP_MONITOR_MAX_V = 2.5

const bringupPages: PageDefinition[] = [
  {
    id: 'workflow',
    label: 'Service',
    detail: 'Start here for write session control, saved plan, and runtime safety policy.',
    icon: Wrench,
  },
  {
    id: 'power',
    label: 'Power supplies',
    detail: 'MPM3530 rail enables, requested service override, and live PGOOD readback.',
    icon: Cable,
  },
  {
    id: 'imu',
    label: 'IMU',
    detail: 'LSM6DSO identity, sampling, and runtime flags.',
    icon: Compass,
  },
  {
    id: 'dac',
    label: 'DAC',
    detail: 'DAC80502 shadow voltages, reference, and update policy.',
    icon: SlidersHorizontal,
  },
  {
    id: 'haptic',
    label: 'Haptic',
    detail: 'DRV2605 mode, library, actuator, and quick effect tests.',
    icon: Gamepad2,
  },
  {
    id: 'tof',
    label: 'ToF',
    detail: 'Distance window and stale-data supervision placeholder.',
    icon: Waves,
  },
  {
    id: 'buttons',
    label: 'Buttons',
    detail: 'Two-stage trigger population and future debounce path.',
    icon: Layers3,
  },
  {
    id: 'pd',
    label: 'USB-PD',
    detail: 'STUSB4500 sink priorities, allowed power plan, and runtime threshold checks.',
    icon: PlugZap,
  },
  {
    id: 'laserDriver',
    label: 'Laser Driver',
    detail: 'ATLS6A214 supervision notes and readiness checks.',
    icon: Radio,
  },
  {
    id: 'tec',
    label: 'TEC',
    detail: 'TEC supervision readiness and temperature loop status.',
    icon: Thermometer,
  },
]

function toFirmwareModuleName(module: ModuleKey): string {
  return module === 'laserDriver' ? 'laser_driver' : module
}

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function formatRegisterHex(value: number, width = 4): string {
  return `0x${Math.max(0, Math.trunc(value)).toString(16).toUpperCase().padStart(width, '0')}`
}

function dacRegisterCodeToVoltage(value: number): number {
  const code = Math.max(0, Math.min(DAC80502_CODE_MAX, Math.trunc(value)))
  return (code / DAC80502_CODE_MAX) * DAC80502_BOARD_SPAN_V
}

function formatDacRegisterEquivalent(value: number): string {
  return `${formatNumber(dacRegisterCodeToVoltage(value), 3)} V`
}

function clampInteger(value: number, minimum: number, maximum: number): number {
  if (!Number.isFinite(value)) {
    return minimum
  }

  return Math.min(maximum, Math.max(minimum, Math.round(value)))
}

function makeDefaultHapticPatternDraft(): HapticPatternDraft {
  return {
    pulseCount: '3',
    highMs: '45',
    lowMs: '90',
    releaseAfter: true,
    hazardAccepted: false,
  }
}

function readbackErrorLabel(errorCode: number, errorName: string): string {
  return errorCode === 0 ? 'ESP_OK' : errorName
}

function pdProfilePowerW(profile: Pick<PdSinkProfile, 'voltageV' | 'currentA'>): number {
  return profile.voltageV * profile.currentA
}

function classifyPdTierFromPlan(
  negotiatedPowerW: number,
  sourceIsHostOnly: boolean,
  programmingOnlyMaxW: number,
  reducedModeMinW: number,
  reducedModeMaxW: number,
  fullModeMinW: number,
): DeviceSnapshot['session']['powerTier'] {
  const operationalMinW = Math.max(programmingOnlyMaxW, reducedModeMinW)

  if (sourceIsHostOnly) {
    return 'programming_only'
  }

  if (negotiatedPowerW < operationalMinW) {
    return 'insufficient'
  }

  if (negotiatedPowerW <= reducedModeMaxW) {
    return 'reduced'
  }

  if (negotiatedPowerW >= fullModeMinW) {
    return 'full'
  }

  return 'reduced'
}

function pdObjectLabel(index: number): string {
  switch (index) {
    case 2:
      return 'PDO3'
    case 1:
      return 'PDO2'
    default:
      return 'PDO1'
  }
}

function pdPriorityDetail(index: number): string {
  switch (index) {
    case 2:
      return 'First-choice high-power request'
    case 1:
      return 'Second-choice fallback'
    default:
      return 'Mandatory 5 V fallback'
  }
}

function readbackPdProfiles(snapshot: DeviceSnapshot): PdSinkProfile[] {
  const liveProfiles =
    snapshot.pd.sinkProfiles.length > 0
      ? snapshot.pd.sinkProfiles
      : makeDefaultPdProfiles()

  return Array.from({ length: 3 }, (_, index) => {
    const profile = liveProfiles[index]

    return profile ?? {
      enabled: false,
      voltageV: 0,
      currentA: 0,
    }
  })
}

function modulePlanSignature(modules: BringupModuleMap): string {
  return moduleKeys
    .map((module) => {
      const status = modules[module]
      return `${module}:${status.expectedPresent ? 1 : 0}:${status.debugEnabled ? 1 : 0}`
    })
    .join('|')
}

function makeFormState(snapshot: DeviceSnapshot): BringupFormState {
  const pdProfiles = snapshot.bringup.tuning.pdProfiles ?? makeDefaultPdProfiles()

  return {
    profileName: snapshot.bringup.profileName,
    modules: snapshot.bringup.modules,
    dacLdVoltage: String(snapshot.bringup.tuning.dacLdChannelV),
    dacTecVoltage: String(snapshot.bringup.tuning.dacTecChannelV),
    dacReferenceMode: snapshot.bringup.tuning.dacReferenceMode,
    dacGain2x: snapshot.bringup.tuning.dacGain2x,
    dacRefDiv: snapshot.bringup.tuning.dacRefDiv,
    dacSyncMode: snapshot.bringup.tuning.dacSyncMode,
    imuOdrHz: String(snapshot.bringup.tuning.imuOdrHz),
    imuAccelRangeG: String(snapshot.bringup.tuning.imuAccelRangeG),
    imuGyroRangeDps: String(snapshot.bringup.tuning.imuGyroRangeDps),
    imuGyroEnabled: snapshot.bringup.tuning.imuGyroEnabled,
    imuLpf2Enabled: snapshot.bringup.tuning.imuLpf2Enabled,
    imuTimestampEnabled: snapshot.bringup.tuning.imuTimestampEnabled,
    imuBduEnabled: snapshot.bringup.tuning.imuBduEnabled,
    imuIfIncEnabled: snapshot.bringup.tuning.imuIfIncEnabled,
    imuI2cDisabled: snapshot.bringup.tuning.imuI2cDisabled,
    tofMinRangeM: String(snapshot.bringup.tuning.tofMinRangeM),
    tofMaxRangeM: String(snapshot.bringup.tuning.tofMaxRangeM),
    tofStaleTimeoutMs: String(snapshot.bringup.tuning.tofStaleTimeoutMs),
    pdProfiles: pdProfiles.map((profile) => ({
      enabled: profile.enabled,
      voltageV: String(profile.voltageV),
      currentA: String(profile.currentA),
    })),
    pdProgrammingOnlyMaxW: String(snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30),
    pdReducedModeMinW: String(snapshot.bringup.tuning.pdReducedModeMinW ?? 30),
    pdReducedModeMaxW: String(snapshot.bringup.tuning.pdReducedModeMaxW ?? 35),
    pdFullModeMinW: String(snapshot.bringup.tuning.pdFullModeMinW ?? 35.1),
    pdFirmwarePlanEnabled: snapshot.bringup.tuning.pdFirmwarePlanEnabled ?? false,
    hapticEffectId: String(snapshot.bringup.tuning.hapticEffectId),
    hapticMode: snapshot.bringup.tuning.hapticMode,
    hapticLibrary: String(snapshot.bringup.tuning.hapticLibrary),
    hapticActuator: snapshot.bringup.tuning.hapticActuator,
    hapticRtpLevel: String(snapshot.bringup.tuning.hapticRtpLevel),
    safetyHorizonThresholdDeg: formatNumber(snapshot.safety.horizonThresholdDeg, 1),
    safetyHorizonHysteresisDeg: formatNumber(snapshot.safety.horizonHysteresisDeg, 1),
    safetyTofMinRangeM: formatNumber(snapshot.safety.tofMinRangeM, 3),
    safetyTofMaxRangeM: formatNumber(snapshot.safety.tofMaxRangeM, 3),
    safetyTofHysteresisM: formatNumber(snapshot.safety.tofHysteresisM, 3),
    safetyImuStaleMs: String(snapshot.safety.imuStaleMs),
    safetyTofStaleMs: String(snapshot.safety.tofStaleMs),
    safetyRailGoodTimeoutMs: String(snapshot.safety.railGoodTimeoutMs),
    safetyLambdaDriftLimitNm: formatNumber(snapshot.safety.lambdaDriftLimitNm, 2),
    safetyLambdaDriftHysteresisNm: formatNumber(snapshot.safety.lambdaDriftHysteresisNm, 2),
    safetyLambdaDriftHoldMs: String(snapshot.safety.lambdaDriftHoldMs),
    safetyLdOvertempLimitC: formatNumber(snapshot.safety.ldOvertempLimitC, 1),
    safetyTecTempAdcTripV: formatNumber(snapshot.safety.tecTempAdcTripV, 3),
    safetyTecTempAdcHysteresisV: formatNumber(snapshot.safety.tecTempAdcHysteresisV, 3),
    safetyTecTempAdcHoldMs: String(snapshot.safety.tecTempAdcHoldMs),
    safetyTecMinCommandC: formatNumber(snapshot.safety.tecMinCommandC, 1),
    safetyTecMaxCommandC: formatNumber(snapshot.safety.tecMaxCommandC, 1),
    safetyTecReadyToleranceC: formatNumber(snapshot.safety.tecReadyToleranceC, 2),
    safetyMaxLaserCurrentA: formatNumber(snapshot.safety.maxLaserCurrentA, 2),
  }
}

function sanitizeDacDraft(form: BringupFormState): BringupFormState {
  const dacRefDiv = form.dacReferenceMode === 'internal' ? true : form.dacRefDiv
  const dacLdVoltage = formatNumber(
    clampLdCommandVoltageV(parseNumber(form.dacLdVoltage, 0)),
    3,
  )

  return {
    ...form,
    dacLdVoltage,
    dacRefDiv,
  }
}

function ensureModuleDebugWrite(
  form: BringupFormState,
  module: ModuleKey,
): BringupFormState {
  if (form.modules[module].expectedPresent || form.modules[module].debugEnabled) {
    return form
  }

  return {
    ...form,
    modules: {
      ...form.modules,
      [module]: {
        ...form.modules[module],
        debugEnabled: true,
      },
    },
  }
}

function makeStoredState(snapshot: DeviceSnapshot): BringupStoredState {
  return {
    version: 9,
    activePage: 'workflow',
    form: sanitizeDacDraft(makeFormState(snapshot)),
    i2cAddress: '0x48',
    i2cRegister: '0x00',
    i2cValue: '0x00',
    spiDevice: 'imu',
    spiRegister: '0x0F',
    spiValue: '0x00',
  }
}

function makeTecDisplaySample(snapshot: DeviceSnapshot) {
  return {
    tempC: snapshot.tec.tempC,
    actualLambdaNm: snapshot.tec.actualLambdaNm,
    commandVoltageV: snapshot.tec.commandVoltageV,
    tempGood: snapshot.tec.tempGood,
    tempAdcVoltageV: snapshot.tec.tempAdcVoltageV,
    currentA: snapshot.tec.currentA,
    voltageV: snapshot.tec.voltageV,
    railPgood: snapshot.rails.tec.pgood,
    railEnabled: snapshot.rails.tec.enabled,
  }
}

const DEFAULT_TEC_BRINGUP_TEMP_C = 25
const DEFAULT_TEC_BRINGUP_VOLTAGE_V = estimateTecVoltageFromTempC(DEFAULT_TEC_BRINGUP_TEMP_C)
const LD_TRANSIENT_DIP_HOLD_MS = 750

function findGpioPin(snapshot: DeviceSnapshot, gpioNum: number) {
  return snapshot.gpioInspector.pins.find((pin) => pin.gpioNum === gpioNum)
}

function describeLdSbdnMode(snapshot: DeviceSnapshot) {
  const pin = findGpioPin(snapshot, 13)

  if (pin === undefined) {
    return {
      mode: 'firmware' as const,
      label: 'GPIO13 unavailable',
      tone: 'off' as const,
      detail: 'GPIO inspector has not published LD_SBDN yet.',
    }
  }

  if (pin.overrideActive) {
    if (pin.overrideMode === 'input') {
      return {
        mode: 'standby-hiz' as const,
        label: 'Standby',
        tone: 'warn' as const,
        detail: 'GPIO13 is released to high impedance so SBDN sits in the standby threshold window.',
      }
    }

    if (pin.overrideMode === 'output') {
      return pin.overrideLevelHigh
        ? {
            mode: 'on-pu' as const,
            label: 'On',
            tone: 'ok' as const,
            detail: 'GPIO13 is being driven high.',
          }
        : {
            mode: 'off-pd' as const,
            label: 'Off',
            tone: 'off' as const,
            detail: 'GPIO13 is being pulled low for fast shutdown.',
          }
    }
  }

  if (!pin.outputEnabled && pin.inputEnabled) {
    return {
      mode: 'standby-hiz' as const,
      label: 'Standby',
      tone: 'warn' as const,
      detail: 'Firmware currently leaves GPIO13 high impedance for standby.',
    }
  }

  if (pin.outputEnabled && pin.levelHigh) {
    return {
      mode: 'on-pu' as const,
      label: 'On',
      tone: 'ok' as const,
      detail: 'Firmware currently drives GPIO13 high.',
    }
  }

  if (pin.outputEnabled && !pin.levelHigh) {
    return {
      mode: 'off-pd' as const,
      label: 'Off',
      tone: 'off' as const,
      detail: 'Firmware currently drives GPIO13 low.',
    }
  }

  return {
    mode: 'firmware' as const,
    label: 'Auto',
    tone: 'off' as const,
    detail: 'GPIO13 is still under the normal firmware state machine.',
  }
}

function describeLdPcnMode(snapshot: DeviceSnapshot) {
  const pin = findGpioPin(snapshot, 21)

  if (pin === undefined) {
    return {
      mode: 'firmware' as const,
      label: 'GPIO21 unavailable',
      tone: 'off' as const,
      detail: 'GPIO inspector has not published LD_PCN yet.',
    }
  }

  if (pin.overrideActive && pin.overrideMode === 'output') {
    if (pin.levelHigh !== pin.overrideLevelHigh) {
      return pin.overrideLevelHigh
        ? {
            mode: 'lish' as const,
            label: 'LISH requested / live low',
            tone: 'critical' as const,
            detail: 'GPIO21 override requested high, but the live pin is still low.',
          }
        : {
            mode: 'lisl' as const,
            label: 'LISL requested / live high',
            tone: 'critical' as const,
            detail: 'GPIO21 override requested low, but the live pin is still high.',
          }
    }

    return pin.overrideLevelHigh
      ? {
          mode: 'lish' as const,
          label: 'LISH path',
          tone: 'ok' as const,
          detail: 'GPIO21 override is actively driving the high-current LISH leg.',
        }
      : {
          mode: 'lisl' as const,
          label: 'LISL path',
          tone: 'warn' as const,
          detail: 'GPIO21 override is actively driving the low-current LISL leg.',
        }
  }

  if (pin.outputEnabled) {
    return pin.levelHigh
      ? {
          mode: 'lish' as const,
          label: 'LISH path',
          tone: 'ok' as const,
          detail: 'Firmware currently drives GPIO21 high.',
        }
      : {
          mode: 'lisl' as const,
          label: 'LISL path',
          tone: 'warn' as const,
          detail: 'Firmware currently drives GPIO21 low.',
        }
  }

  return {
    mode: 'firmware' as const,
    label: 'Firmware-owned',
    tone: 'off' as const,
    detail: 'GPIO21 is still under the normal firmware state machine.',
  }
}

function describeGreenIo37State(snapshot: DeviceSnapshot) {
  const pin = findGpioPin(snapshot, 37)

  if (pin === undefined) {
    return {
      enabled: false,
      label: 'GPIO37 unavailable',
      tone: 'off' as const,
      owner: 'No telemetry',
      detail: 'GPIO inspector has not published the shared ERM / green-laser net yet.',
    }
  }

  if (pin.overrideActive && pin.overrideMode === 'output') {
    return pin.overrideLevelHigh
      ? {
          enabled: true,
          label: 'High',
          tone: 'ok' as const,
          owner: 'Service override',
          detail: 'GPIO37 is being driven high directly on the shared GN_LD_EN / ERM_TRIG net.',
        }
      : {
          enabled: false,
          label: 'Low',
          tone: 'off' as const,
          owner: 'Service override',
          detail: 'GPIO37 is being driven low directly on the shared GN_LD_EN / ERM_TRIG net.',
        }
  }

  if (pin.outputEnabled) {
    return pin.levelHigh
      ? {
          enabled: true,
          label: 'High',
          tone: 'ok' as const,
          owner: 'Firmware',
          detail: 'Firmware currently drives GPIO37 high.',
        }
      : {
          enabled: false,
          label: 'Low',
          tone: 'off' as const,
          owner: 'Firmware',
          detail: 'Firmware currently drives GPIO37 low.',
        }
  }

  return {
    enabled: pin.levelHigh,
    label: pin.levelHigh ? 'Floating high' : 'Hi-Z / low',
    tone: pin.levelHigh ? ('warning' as const) : ('off' as const),
    owner: 'High-Z',
    detail: 'GPIO37 is not actively driven. This shared net can be misleading when left floating.',
  }
}

type LdDisplaySample = {
  commandVoltageV: number
  commandCurrentA: number
  measuredCurrentA: number
  currentMonitorVoltageV: number
  driverTempVoltageV: number
  driverTempC: number
  diodeElectricalPowerW: number
  supplyDrawW: number
  loopGood: boolean
  railEnabled: boolean
  railPgood: boolean
  pcnMode: ReturnType<typeof describeLdPcnMode>['mode']
  dipHoldUntilMs: number
}

function makeLdDisplaySample(snapshot: DeviceSnapshot): LdDisplaySample {
  const commandVoltageV = clampLdCommandVoltageV(
    snapshot.laser.commandVoltageV || snapshot.bringup.tuning.dacLdChannelV,
  )
  const measuredCurrentA = Math.max(0, snapshot.laser.measuredCurrentA)
  const diodeElectricalPowerW = estimateLdDiodeElectricalPowerW(measuredCurrentA)

  return {
    commandVoltageV,
    commandCurrentA: estimateLdCurrentFromVoltageV(commandVoltageV),
    measuredCurrentA,
    currentMonitorVoltageV: Math.max(0, snapshot.laser.currentMonitorVoltageV),
    driverTempVoltageV: Math.max(0, snapshot.laser.driverTempVoltageV),
    driverTempC: snapshot.laser.driverTempC,
    diodeElectricalPowerW,
    supplyDrawW: estimateLdSupplyDrawW(measuredCurrentA),
    loopGood: snapshot.laser.loopGood,
    railEnabled: snapshot.rails.ld.enabled,
    railPgood: snapshot.rails.ld.pgood,
    pcnMode: describeLdPcnMode(snapshot).mode,
    dipHoldUntilMs: 0,
  }
}

function reconcileLdDisplaySample(
  previous: LdDisplaySample,
  next: LdDisplaySample,
) {
  const now = window.performance.now()
  const pcnModeChanged = previous.pcnMode !== next.pcnMode
  const railStateChanged =
    previous.railEnabled !== next.railEnabled ||
    previous.railPgood !== next.railPgood
  const commandShifted =
    Math.abs(previous.commandVoltageV - next.commandVoltageV) >= 0.05

  if (pcnModeChanged || railStateChanged || commandShifted) {
    return {
      ...next,
      dipHoldUntilMs: 0,
    }
  }

  const likelyTransientCurrentDip =
    previous.measuredCurrentA >= 0.2 &&
    next.measuredCurrentA <= Math.max(0.03, previous.measuredCurrentA * 0.2) &&
    next.currentMonitorVoltageV <= Math.max(0.02, previous.currentMonitorVoltageV * 0.35) &&
    next.commandVoltageV >= 0.05 &&
    next.railEnabled &&
    next.railPgood

  if (!likelyTransientCurrentDip) {
    return {
      ...next,
      dipHoldUntilMs: 0,
    }
  }

  if (previous.dipHoldUntilMs > 0 && previous.dipHoldUntilMs <= now) {
    return {
      ...next,
      dipHoldUntilMs: 0,
    }
  }

  const holdUntilMs =
    previous.dipHoldUntilMs > now
      ? previous.dipHoldUntilMs
      : now + LD_TRANSIENT_DIP_HOLD_MS

  return {
    ...next,
    measuredCurrentA: previous.measuredCurrentA,
    currentMonitorVoltageV: previous.currentMonitorVoltageV,
    diodeElectricalPowerW: previous.diodeElectricalPowerW,
    supplyDrawW: previous.supplyDrawW,
    dipHoldUntilMs: holdUntilMs,
  }
}

function persistStoredState(state: BringupStoredState): void {
  window.localStorage.setItem(HOST_DRAFT_STORAGE_KEY, JSON.stringify(state))
}

function loadHostDraftState(snapshot: DeviceSnapshot): BringupStoredState {
  try {
    const raw = window.localStorage.getItem(HOST_DRAFT_STORAGE_KEY)

    if (raw !== null) {
      const parsed = JSON.parse(raw) as Partial<BringupStoredState>

      if (
        parsed.form !== undefined &&
        typeof parsed.activePage === 'string'
      ) {
        const defaultState = makeStoredState(snapshot)
        return {
          ...defaultState,
          ...parsed,
          form: sanitizeDacDraft({
            ...defaultState.form,
            ...(parsed.form as Partial<BringupDraft>),
          } as BringupDraft),
          activePage:
            parsed.activePage === 'workflow' ||
            parsed.activePage === 'power' ||
            parsed.activePage in moduleMeta
              ? (parsed.activePage as BringupPageKey)
              : 'workflow',
        }
      }
    }

    const legacyRaw = window.localStorage.getItem(LEGACY_HOST_DRAFT_STORAGE_KEY)
    if (legacyRaw !== null) {
      const legacyDraft = JSON.parse(legacyRaw) as BringupDraft
      return {
        ...makeStoredState(snapshot),
        form: sanitizeDacDraft(legacyDraft),
      }
    }
  } catch {
    return makeStoredState(snapshot)
  }

  return makeStoredState(snapshot)
}

function mergePlannedAndLiveModules(
  snapshot: DeviceSnapshot,
  planned: BringupModuleMap,
  live: BringupModuleMap,
  connected: boolean,
): BringupModuleMap {
  function mergeModuleStatus(
    module: ModuleKey,
    plannedStatus: BringupModuleStatus,
    liveStatus: BringupModuleStatus,
  ): BringupModuleStatus {
    const merged: BringupModuleStatus = {
      ...liveStatus,
      expectedPresent: plannedStatus.expectedPresent,
      debugEnabled: plannedStatus.debugEnabled,
    }

    if (!connected) {
      return merged
    }

    const observed = observeBringupModuleStatus(module, snapshot)
    return {
      ...merged,
      detected: merged.detected || observed.detected,
      healthy: merged.healthy || observed.healthy,
    }
  }

  return {
    imu: mergeModuleStatus('imu', planned.imu, live.imu),
    dac: mergeModuleStatus('dac', planned.dac, live.dac),
    haptic: mergeModuleStatus('haptic', planned.haptic, live.haptic),
    tof: mergeModuleStatus('tof', planned.tof, live.tof),
    buttons: mergeModuleStatus('buttons', planned.buttons, live.buttons),
    pd: mergeModuleStatus('pd', planned.pd, live.pd),
    laserDriver: mergeModuleStatus('laserDriver', planned.laserDriver, live.laserDriver),
    tec: mergeModuleStatus('tec', planned.tec, live.tec),
  }
}

function pause(ms: number): Promise<void> {
  return new Promise((resolve) => {
    window.setTimeout(resolve, ms)
  })
}

function buildProbeQueue(modules: BringupModuleMap): ProbeRequest[] {
  const probes: ProbeRequest[] = []

  if (
    moduleMeta.imu.validationMode === 'probe' &&
    (modules.imu.expectedPresent || modules.imu.debugEnabled)
  ) {
    probes.push({
      id: 'imu-whoami',
      cmd: 'spi_read',
      note: 'Background IMU identity probe.',
      args: {
        device: 'imu',
        reg: 0x0f,
      },
    })
  }

  if (
    moduleMeta.pd.validationMode === 'probe' &&
    (modules.pd.expectedPresent || modules.pd.debugEnabled)
  ) {
    probes.push({
      id: 'pd-status',
      cmd: 'i2c_read',
      note: 'Background STUSB4500 status probe.',
      args: {
        address: 0x28,
        reg: 0x06,
      },
    })
  }

  if (
    moduleMeta.dac.validationMode === 'probe' &&
    (modules.dac.expectedPresent || modules.dac.debugEnabled)
  ) {
    probes.push({
      id: 'dac-config',
      cmd: 'i2c_read',
      note: 'Background DAC CONFIG probe.',
      args: {
        address: 0x48,
        reg: 0x03,
      },
    })
  }

  if (
    moduleMeta.haptic.validationMode === 'probe' &&
    (modules.haptic.expectedPresent || modules.haptic.debugEnabled)
  ) {
    probes.push({
      id: 'haptic-mode',
      cmd: 'i2c_read',
      note: 'Background haptic MODE probe.',
      args: {
        address: 0x5a,
        reg: 0x01,
      },
    })
  }

  if (
    moduleMeta.tof.validationMode === 'probe' &&
    (modules.tof.expectedPresent || modules.tof.debugEnabled)
  ) {
    probes.push({
      id: 'tof-live-status',
      cmd: 'get_status',
      note: 'Background ToF live-status refresh.',
    })
  }

  return probes
}

function moduleScore(
  module: ModuleKey,
  status: BringupModuleStatus,
  connected: boolean,
): ModuleScore {
  const meta = moduleMeta[module]

  if (!status.expectedPresent) {
    return {
      progress: 0,
      tone: 'critical',
      label: status.detected ? 'Unexpected response' : 'Not installed',
    }
  }

  if (!connected) {
    return {
      progress: status.debugEnabled ? 38 : 22,
      tone: 'warning',
      label: 'Offline plan',
    }
  }

  if (meta.validationMode === 'monitored') {
    if (!status.debugEnabled) {
      return {
        progress: 82,
        tone: 'warning',
        label: 'Declared, tools off',
      }
    }

    return {
      progress: 100,
      tone: 'steady',
      label: 'Live monitored',
    }
  }

  if (!status.detected) {
    return {
      progress: status.debugEnabled ? 42 : 28,
      tone: 'warning',
      label: 'Awaiting probe',
    }
  }

  if (!status.healthy) {
    return {
      progress: 68,
      tone: 'warning',
      label: 'Needs validation',
    }
  }

  if (!status.debugEnabled) {
    return {
      progress: 82,
      tone: 'warning',
      label: 'Live, tools off',
    }
  }

  return {
    progress: 100,
    tone: 'steady',
    label: 'Detected',
  }
}

function moduleSummary(
  module: ModuleKey,
  status: BringupModuleStatus,
  connected: boolean,
): { headline: string; detail: string } {
  const meta = moduleMeta[module]

  if (!status.expectedPresent) {
    return {
      headline: status.detected ? 'Unexpected response on undeclared hardware' : 'Not part of this bench build',
      detail: status.detected
        ? 'This module is not declared installed, but the firmware saw bus or register activity from it.'
        : 'Leave this module off until the hardware is physically populated.',
    }
  }

  if (!connected) {
    return {
      headline:
        meta.validationMode === 'probe' ? 'Planned, not probed yet' : 'Planned, not monitored yet',
      detail:
        meta.validationMode === 'probe'
          ? 'This is only a bench plan until the board is connected and you run a probe.'
          : 'This path does not have a digital identity probe. The controller only validates it once live GPIO and analog readback are available.',
    }
  }

  if (meta.validationMode === 'monitored') {
    return {
      headline: status.debugEnabled ? 'Live electrical monitoring active' : 'Declared hardware, tools disabled',
      detail: meta.validationDetail,
    }
  }

  if (!status.detected) {
    return {
      headline: 'Expected, waiting for first probe',
      detail: 'The board says this module is planned, but there is no successful identity or register probe yet.',
    }
  }

  if (!status.healthy) {
    return {
      headline: 'Detected, still needs validation',
      detail: 'A response was seen, but the module is not yet marked healthy.',
    }
  }

  return {
    headline: 'Detected and responding',
    detail: 'The current snapshot shows a successful probe and a healthy module status.',
  }
}

function overallBringupPercent(
  modules: BringupModuleMap,
  connected: boolean,
): number {
  const plannedModules = moduleKeys.filter((module) => modules[module].expectedPresent)

  if (plannedModules.length === 0) {
    return 0
  }

  const scores = plannedModules.map((module) =>
    moduleScore(module, modules[module], connected).progress,
  )
  return Math.round(
    scores.reduce((total, progress) => total + progress, 0) / scores.length,
  )
}

function powerSupplyScore(
  snapshot: DeviceSnapshot,
  connected: boolean,
): ModuleScore {
  if (!connected) {
    return {
      progress: 0,
      tone: 'warning',
      label: 'Offline',
    }
  }

  const ldRequested = snapshot.bringup.power.ldRequested
  const tecRequested = snapshot.bringup.power.tecRequested

  if (!ldRequested && !tecRequested) {
    return {
      progress: 0,
      tone: 'warning',
      label: 'Rails safe off',
    }
  }

  const ldGood = !ldRequested || (snapshot.rails.ld.enabled && snapshot.rails.ld.pgood)
  const tecGood = !tecRequested || (snapshot.rails.tec.enabled && snapshot.rails.tec.pgood)

  if (ldGood && tecGood) {
    return {
      progress: 100,
      tone: 'steady',
      label: 'Requested rails good',
    }
  }

  return {
    progress: 56,
    tone: 'warning',
    label: 'Waiting for PGOOD',
  }
}

function FieldLabel({
  label,
  help,
}: {
  label: string
  help: string
}) {
  return (
    <span className="field-label">
      <span>{label}</span>
      <HelpHint text={help} />
    </span>
  )
}

export function BringupWorkbench({
  snapshot,
  telemetryStore,
  transportStatus,
  transportRecovering,
  deploymentLocked,
  onIssueCommandAwaitAck,
}: BringupWorkbenchProps) {
  const initialStoredStateRef = useRef<BringupStoredState | null>(null)
  if (initialStoredStateRef.current === null) {
    initialStoredStateRef.current = loadHostDraftState(snapshot)
  }

  const initialStoredState = initialStoredStateRef.current
  const [activePage, setActivePage] = useState<BringupPageKey>(initialStoredState.activePage)
  const [draftNote, setDraftNote] = useState('Bench plan auto-saves locally in this browser.')
  const [form, setForm] = useState<BringupFormState>(initialStoredState.form)
  const [i2cAddress, setI2cAddress] = useState(initialStoredState.i2cAddress)
  const [i2cRegister, setI2cRegister] = useState(initialStoredState.i2cRegister)
  const [i2cValue, setI2cValue] = useState(initialStoredState.i2cValue)
  const [spiDevice, setSpiDevice] = useState(initialStoredState.spiDevice)
  const [spiRegister, setSpiRegister] = useState(initialStoredState.spiRegister)
  const [spiValue, setSpiValue] = useState(initialStoredState.spiValue)
  const [operation, setOperation] = useState<BringupOperation | null>(null)
  const [pdNvmConfirmOpen, setPdNvmConfirmOpen] = useState(false)
  const [hapticPattern, setHapticPattern] = useState<HapticPatternDraft>(
    makeDefaultHapticPatternDraft,
  )
  const [tecSliderMode, setTecSliderMode] = useState<'temp' | 'lambda'>('temp')
  const [tofIlluminationDutyPct, setTofIlluminationDutyPct] = useState(() =>
    String(
      snapshot.bringup.illumination.tof.dutyCyclePct > 0
        ? snapshot.bringup.illumination.tof.dutyCyclePct
        : 35,
    ),
  )

  const commandReady = transportStatus === 'connected'
  const hasLiveControllerSnapshot = snapshot.identity.firmwareVersion !== 'unavailable'
  const connected =
    commandReady ||
    (hasLiveControllerSnapshot &&
      (transportRecovering || transportStatus === 'connecting'))
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const serviceModeRequested = commandReady && snapshot.bringup.serviceModeRequested
  const serviceModeActive = commandReady && snapshot.bringup.serviceModeActive
  const readsDisabled = !commandReady || operation !== null
  const writesDisabled = !commandReady || operation !== null
  const liveModules = connected ? snapshot.bringup.modules : form.modules
  const displayModules = useMemo(
    () => mergePlannedAndLiveModules(snapshot, form.modules, liveModules, connected),
    [connected, form.modules, liveModules, snapshot],
  )
  const livePdProfiles = snapshot.bringup.tuning.pdProfiles ?? makeDefaultPdProfiles()
  const overallProgress = overallBringupPercent(displayModules, connected)
  const powerPageScore = powerSupplyScore(liveSnapshot, connected)
  const livePageModule =
    activePage !== 'workflow' && activePage !== 'power'
      ? displayModules[activePage]
      : null
  const livePageScore =
    activePage === 'power'
      ? powerPageScore
      : livePageModule === null
        ? null
        : moduleScore(activePage as ModuleKey, livePageModule, connected)
  const operationBusyRef = useRef(false)
  const operationConfirmResolveRef = useRef<(() => void) | null>(null)
  const probeBusyRef = useRef(false)
  const moduleSyncBusyRef = useRef(false)
  const latestSnapshotRef = useRef(snapshot)
  const latestLiveSnapshotRef = useRef(liveSnapshot)
  const probeCycleRef = useRef<ProbeCycleState>({
    signature: '',
    nextIndex: 0,
    initialSweepComplete: false,
    lastProbeAtMs: 0,
  })
  const lastModuleSyncFailureRef = useRef('')
  const [ldDisplaySample, setLdDisplaySample] = useState(() =>
    makeLdDisplaySample(liveSnapshot),
  )
  const [tecDisplaySample, setTecDisplaySample] = useState(() =>
    makeTecDisplaySample(liveSnapshot),
  )
  const desiredModulePlanSignature = useMemo(
    () => modulePlanSignature(form.modules),
    [form.modules],
  )

  useEffect(() => {
    latestSnapshotRef.current = snapshot
  }, [snapshot])

  useEffect(() => {
    latestLiveSnapshotRef.current = liveSnapshot
  }, [liveSnapshot])

  useEffect(() => {
    if (!commandReady || serviceModeRequested || serviceModeActive) {
      return
    }

    const nextLdVoltage = formatNumber(
      clampLdCommandVoltageV(liveSnapshot.laser.commandVoltageV),
      3,
    )
    const nextTecVoltage = formatNumber(
      clampTecVoltageV(liveSnapshot.tec.commandVoltageV),
      3,
    )

    setForm((current) => {
      if (
        current.dacLdVoltage === nextLdVoltage &&
        current.dacTecVoltage === nextTecVoltage
      ) {
        return current
      }

      return sanitizeDacDraft({
        ...current,
        dacLdVoltage: nextLdVoltage,
        dacTecVoltage: nextTecVoltage,
      })
    })
  }, [
    commandReady,
    liveSnapshot.laser.commandVoltageV,
    liveSnapshot.tec.commandVoltageV,
    serviceModeActive,
    serviceModeRequested,
  ])

  useEffect(() => {
    if (activePage !== 'laserDriver') {
      return
    }

    const syncLdDisplay = () => {
      setLdDisplaySample((current) =>
        reconcileLdDisplaySample(
          current,
          makeLdDisplaySample(latestLiveSnapshotRef.current),
        ),
      )
    }

    syncLdDisplay()
    const timerId = window.setInterval(syncLdDisplay, 500)

    return () => {
      window.clearInterval(timerId)
    }
  }, [activePage])

  useEffect(() => {
    if (activePage !== 'tec') {
      return
    }

    const syncTecDisplay = () => {
      setTecDisplaySample(makeTecDisplaySample(latestLiveSnapshotRef.current))
    }

    syncTecDisplay()
    const timerId = window.setInterval(syncTecDisplay, 1000)

    return () => {
      window.clearInterval(timerId)
    }
  }, [activePage])

  useEffect(() => {
    if (!commandReady) {
      moduleSyncBusyRef.current = false
      probeBusyRef.current = false
      probeCycleRef.current = {
        signature: '',
        nextIndex: 0,
        initialSweepComplete: false,
        lastProbeAtMs: 0,
      }
      lastModuleSyncFailureRef.current = ''
    }
  }, [commandReady])

  useEffect(() => {
    const liveDuty = snapshot.bringup.illumination.tof.dutyCyclePct
    if (snapshot.bringup.illumination.tof.enabled || liveDuty > 0) {
      setTofIlluminationDutyPct(String(liveDuty))
    }
  }, [
    snapshot.bringup.illumination.tof.dutyCyclePct,
    snapshot.bringup.illumination.tof.enabled,
  ])

  useEffect(() => {
    if (!commandReady) {
      return
    }

    const timerId = window.setInterval(() => {
      if (moduleSyncBusyRef.current || operationBusyRef.current || probeBusyRef.current) {
        return
      }

      const livePlan = latestSnapshotRef.current.bringup.modules
      const nextModule = moduleKeys.find((module) => {
        const desired = form.modules[module]
        const live = livePlan[module]
        return (
          desired.expectedPresent !== live.expectedPresent ||
          desired.debugEnabled !== live.debugEnabled
        )
      })

      if (nextModule === undefined) {
        lastModuleSyncFailureRef.current = ''
        return
      }

      const desired = form.modules[nextModule]
      moduleSyncBusyRef.current = true

      void onIssueCommandAwaitAck(
        'set_module_state',
        'write',
        `Auto-sync the ${moduleMeta[nextModule].label} module plan before background validation.`,
        {
          module: toFirmwareModuleName(nextModule),
          expected_present: desired.expectedPresent,
          debug_enabled: desired.debugEnabled,
        },
        {
          logHistory: false,
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      )
        .then((result) => {
          if (result.ok) {
            lastModuleSyncFailureRef.current = ''
            return
          }

          const failureNote = `${moduleMeta[nextModule].label} plan auto-sync is retrying. ${result.note}`
          if (lastModuleSyncFailureRef.current !== failureNote) {
            setDraftNote(failureNote)
            lastModuleSyncFailureRef.current = failureNote
          }
        })
        .finally(() => {
          moduleSyncBusyRef.current = false
        })
    }, BRINGUP_PLAN_SYNC_INTERVAL_MS)

    return () => {
      window.clearInterval(timerId)
    }
  }, [commandReady, form.modules, onIssueCommandAwaitAck])

  function dismissOperation() {
    const resolve = operationConfirmResolveRef.current
    operationConfirmResolveRef.current = null
    setOperation(null)
    resolve?.()
  }

  function waitForOperationConfirm() {
    return new Promise<void>((resolve) => {
      operationConfirmResolveRef.current = resolve
    })
  }

  async function holdOperationError(label: string, detail: string) {
    setDraftNote(detail)
    setOperation({
      label,
      detail,
      percent: 100,
      tone: 'critical',
      requiresConfirm: true,
    })
    await waitForOperationConfirm()
  }

  function describeDacCommandFailure(note: string): string {
    if (!note.toLowerCase().includes('dac')) {
      return note
    }

    const live = latestSnapshotRef.current
    const module = live.bringup.modules.dac
    const dac = live.peripherals.dac

    return [
      note,
      `Live DAC expected=${module.expectedPresent ? 1 : 0} debug=${module.debugEnabled ? 1 : 0} detected=${module.detected ? 1 : 0} healthy=${module.healthy ? 1 : 0}.`,
      `STATUS ${formatRegisterHex(dac.statusReg)}; REF_ALARM ${dac.refAlarm ? 'asserted' : 'clear'}; GAIN ${formatRegisterHex(dac.gainReg)}; CONFIG ${formatRegisterHex(dac.configReg)}.`,
    ].join(' ')
  }

  function buildPdCommandArgs(includeFirmwarePlan = false) {
    return {
      ...(includeFirmwarePlan
        ? { firmware_plan_enabled: form.pdFirmwarePlanEnabled }
        : {}),
      programming_only_max_w: parseNumber(
        form.pdProgrammingOnlyMaxW,
        snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
      ),
      reduced_mode_min_w: parseNumber(
        form.pdReducedModeMinW,
        snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
      ),
      reduced_mode_max_w: parseNumber(
        form.pdReducedModeMaxW,
        snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
      ),
      full_mode_min_w: parseNumber(
        form.pdFullModeMinW,
        snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
      ),
      pdo1_enabled: form.pdProfiles[0]?.enabled ?? false,
      pdo1_voltage_v: parseNumber(
        form.pdProfiles[0]?.voltageV ?? '0',
        livePdProfiles[0]?.voltageV ?? 0,
      ),
      pdo1_current_a: parseNumber(
        form.pdProfiles[0]?.currentA ?? '0',
        livePdProfiles[0]?.currentA ?? 0,
      ),
      pdo2_enabled: form.pdProfiles[1]?.enabled ?? false,
      pdo2_voltage_v: parseNumber(
        form.pdProfiles[1]?.voltageV ?? '0',
        livePdProfiles[1]?.voltageV ?? 0,
      ),
      pdo2_current_a: parseNumber(
        form.pdProfiles[1]?.currentA ?? '0',
        livePdProfiles[1]?.currentA ?? 0,
      ),
      pdo3_enabled: form.pdProfiles[2]?.enabled ?? false,
      pdo3_voltage_v: parseNumber(
        form.pdProfiles[2]?.voltageV ?? '0',
        livePdProfiles[2]?.voltageV ?? 0,
      ),
      pdo3_current_a: parseNumber(
        form.pdProfiles[2]?.currentA ?? '0',
        livePdProfiles[2]?.currentA ?? 0,
      ),
    }
  }

  function patchForm<K extends keyof BringupFormState>(
    key: K,
    value: BringupFormState[K],
  ) {
    setForm((current) => ({
      ...current,
      [key]: value,
    }))
  }

  function patchModule(module: ModuleKey, patch: Partial<BringupModuleStatus>) {
    setForm((current) => {
      const nextModule = {
        ...current.modules[module],
        ...patch,
      }
      const shouldSeedTecDefault =
        module === 'tec' &&
        (nextModule.expectedPresent || nextModule.debugEnabled) &&
        parseNumber(current.dacTecVoltage, 0) <= 0.001

      return {
        ...current,
        dacTecVoltage: shouldSeedTecDefault
          ? formatNumber(DEFAULT_TEC_BRINGUP_VOLTAGE_V, 3)
          : current.dacTecVoltage,
        modules: {
          ...current.modules,
          [module]: nextModule,
        },
      }
    })
  }

  function patchPdProfile(
    index: number,
    patch: Partial<BringupFormState['pdProfiles'][number]>,
  ) {
    setForm((current) => ({
      ...current,
      pdProfiles: current.pdProfiles.map((profile, profileIndex) =>
        profileIndex === index ? { ...profile, ...patch } : profile,
      ),
    }))
  }

  const pollLiveStatus = useCallback(
    async (logHistory = false) =>
      onIssueCommandAwaitAck(
        'get_status',
        'read',
        'Refresh the live bring-up snapshot.',
        undefined,
        {
          logHistory,
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ),
    [onIssueCommandAwaitAck],
  )

  useEffect(() => {
    persistStoredState({
      version: 9,
      activePage,
      form,
      i2cAddress,
      i2cRegister,
      i2cValue,
      spiDevice,
      spiRegister,
      spiValue,
    })
  }, [
    activePage,
    form,
    i2cAddress,
    i2cRegister,
    i2cValue,
    spiDevice,
    spiRegister,
    spiValue,
  ])

  function syncFormFromSnapshot() {
    setForm(makeFormState(snapshot))
    setDraftNote('Live device bring-up values loaded into the local bench plan.')
  }

  function syncSafetyPolicyFromSnapshot() {
    const liveForm = makeFormState(snapshot)
    setForm((current) => ({
      ...current,
      safetyHorizonThresholdDeg: liveForm.safetyHorizonThresholdDeg,
      safetyHorizonHysteresisDeg: liveForm.safetyHorizonHysteresisDeg,
      safetyTofMinRangeM: liveForm.safetyTofMinRangeM,
      safetyTofMaxRangeM: liveForm.safetyTofMaxRangeM,
      safetyTofHysteresisM: liveForm.safetyTofHysteresisM,
      safetyImuStaleMs: liveForm.safetyImuStaleMs,
      safetyTofStaleMs: liveForm.safetyTofStaleMs,
      safetyRailGoodTimeoutMs: liveForm.safetyRailGoodTimeoutMs,
      safetyLambdaDriftLimitNm: liveForm.safetyLambdaDriftLimitNm,
      safetyLambdaDriftHysteresisNm: liveForm.safetyLambdaDriftHysteresisNm,
      safetyLambdaDriftHoldMs: liveForm.safetyLambdaDriftHoldMs,
      safetyLdOvertempLimitC: liveForm.safetyLdOvertempLimitC,
      safetyTecTempAdcTripV: liveForm.safetyTecTempAdcTripV,
      safetyTecTempAdcHysteresisV: liveForm.safetyTecTempAdcHysteresisV,
      safetyTecTempAdcHoldMs: liveForm.safetyTecTempAdcHoldMs,
      safetyTecMinCommandC: liveForm.safetyTecMinCommandC,
      safetyTecMaxCommandC: liveForm.safetyTecMaxCommandC,
      safetyTecReadyToleranceC: liveForm.safetyTecReadyToleranceC,
      safetyMaxLaserCurrentA: liveForm.safetyMaxLaserCurrentA,
    }))
    setDraftNote('Live runtime safety policy loaded into the local bench plan.')
  }

  function saveHostDraft() {
    persistStoredState({
      version: 9,
      activePage,
      form,
      i2cAddress,
      i2cRegister,
      i2cValue,
      spiDevice,
      spiRegister,
      spiValue,
    })
    setDraftNote(`Bench plan saved locally at ${new Date().toLocaleTimeString()}.`)
  }

  function restoreHostDraft() {
    const stored = loadHostDraftState(snapshot)
    setActivePage(stored.activePage)
    setForm(stored.form)
    setI2cAddress(stored.i2cAddress)
    setI2cRegister(stored.i2cRegister)
    setI2cValue(stored.i2cValue)
    setSpiDevice(stored.spiDevice)
    setSpiRegister(stored.spiRegister)
    setSpiValue(stored.spiValue)
    setDraftNote('Auto-saved bench plan restored into the bring-up workspace.')
  }

  async function refreshLiveStatus(logHistory = false, showBusy = false) {
    if (!showBusy) {
      return pollLiveStatus(logHistory)
    }

    const ok = await runCommandSequence(
      'Refresh live plan',
      'Live bring-up status refreshed from the controller.',
      [
        {
          detail: 'Refreshing the live bring-up profile and module snapshot...',
          cmd: 'get_status',
          risk: 'read',
          note: 'Refresh the live bring-up snapshot.',
          logHistory,
        },
      ],
      {
        refreshAfter: false,
      },
    )

    return {
      ok,
      note: ok
        ? 'Live bring-up status refreshed from the controller.'
        : 'Live bring-up refresh did not complete.',
    }
  }

  async function pollPdStatus(logHistory = true) {
    return onIssueCommandAwaitAck(
      'refresh_pd_status',
      'read',
      'Force the controller to refresh the live STUSB4500 contract snapshot now.',
      undefined,
      {
        logHistory,
        timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
      },
    )
  }

  async function refreshPdStatus(logHistory = true, showBusy = false) {
    if (!showBusy) {
      return pollPdStatus(logHistory)
    }

    const ok = await runCommandSequence(
      'Refresh PD status',
      'USB-PD contract and runtime PDO readback refreshed from the controller.',
      [
        {
          detail: 'Refreshing live STUSB4500 contract and PDO readback...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Force the controller to refresh the live STUSB4500 contract snapshot now.',
          logHistory,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )

    return {
      ok,
      note: ok
        ? 'USB-PD contract and runtime PDO readback refreshed from the controller.'
        : 'USB-PD refresh did not complete.',
    }
  }

  async function ensureServiceMode(label: string): Promise<boolean> {
    if (!commandReady) {
      setDraftNote('Connect the board before starting a write session.')
      return false
    }

    if (serviceModeActive) {
      return true
    }

    setOperation({
      label,
      detail: serviceModeRequested
        ? 'Write session already requested. Waiting for controller acknowledgement...'
        : 'Requesting service mode for controller writes...',
      percent: 22,
      tone: 'warning',
    })

    if (!serviceModeRequested) {
      const enterResult = await onIssueCommandAwaitAck(
        'enter_service_mode',
        'service',
        'Request bring-up service mode with outputs held safe.',
        undefined,
        {
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      )

      if (!enterResult.ok) {
        await holdOperationError(label, enterResult.note)
        return false
      }

      if (latestSnapshotRef.current.bringup.serviceModeActive) {
        return true
      }
    }

    const waitStartedAt = window.performance.now()

    while (window.performance.now() - waitStartedAt < BRINGUP_SERVICE_MODE_WAIT_MS) {
      if (latestSnapshotRef.current.bringup.serviceModeActive) {
        return true
      }
      await pause(80)
    }

    await holdOperationError(
      label,
      'Service mode did not become active before the write timeout expired.',
    )
    return false
  }

  async function runCommandSequence(
    label: string,
    successNote: string,
    steps: Array<{
      detail: string
      cmd: string
      risk: 'read' | 'write' | 'service' | 'firmware'
      note: string
      args?: Record<string, number | string | boolean>
      requireService?: boolean
      logHistory?: boolean
      timeoutMs?: number
      retryOnModuleBlocked?: {
        module: ModuleKey
        expectedPresent: boolean
        debugEnabled: boolean
        label: string
      }
    }>,
    options?: {
      refreshAfter?: boolean
    },
  ): Promise<boolean> {
    if (!commandReady) {
      setDraftNote('Board is offline. The local bench plan is still saved in this browser.')
      return false
    }

    if (operationBusyRef.current) {
      setDraftNote('A bring-up action is already running. Wait for it to finish.')
      return false
    }

    operationBusyRef.current = true
    const refreshAfter = options?.refreshAfter ?? false
    const needsService = steps.some((step) => step.requireService)
    const totalSteps = steps.length + (needsService ? 1 : 0) + (refreshAfter ? 1 : 0)
    let completedSteps = 0
    let clearOnExit = true

    try {
      for (let attempts = 0; probeBusyRef.current && attempts < 12; attempts += 1) {
        await pause(60)
      }

      if (needsService) {
        const ready = await ensureServiceMode(label)
        if (!ready) {
          clearOnExit = false
          return false
        }
        completedSteps += 1
      }

      for (const step of steps) {
        setOperation({
          label,
          detail: step.detail,
          percent: Math.max(
            18,
            Math.round((completedSteps / totalSteps) * 100),
          ),
          tone: 'warning',
        })

        let result = await onIssueCommandAwaitAck(
          step.cmd,
          step.risk,
          step.note,
          step.args,
          {
            logHistory: step.logHistory ?? true,
            timeoutMs: step.timeoutMs ?? BRINGUP_ACK_TIMEOUT_MS,
          },
        )

        if (
          !result.ok &&
          step.requireService &&
          result.note.toLowerCase().includes('service mode')
        ) {
          const recovered = await ensureServiceMode(label)
          if (recovered) {
            result = await onIssueCommandAwaitAck(
              step.cmd,
              step.risk,
              step.note,
              step.args,
              {
                logHistory: step.logHistory ?? true,
                timeoutMs: step.timeoutMs ?? BRINGUP_ACK_TIMEOUT_MS,
              },
            )
          }
        }

        if (
          !result.ok &&
          step.retryOnModuleBlocked !== undefined &&
          result.note.toLowerCase().includes('arm debug')
        ) {
          setOperation({
            label,
            detail: `Controller still reports ${step.retryOnModuleBlocked.label} write-gate closed. Re-syncing the module plan and retrying once...`,
            percent: Math.max(
              28,
              Math.round((completedSteps / totalSteps) * 100),
            ),
            tone: 'warning',
          })

          const resyncResult = await onIssueCommandAwaitAck(
            'set_module_state',
            'write',
            `Re-sync the ${step.retryOnModuleBlocked.label} module plan before retrying the write.`,
            {
              module: toFirmwareModuleName(step.retryOnModuleBlocked.module),
              expected_present: step.retryOnModuleBlocked.expectedPresent,
              debug_enabled: step.retryOnModuleBlocked.debugEnabled,
            },
            {
              logHistory: false,
              timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
            },
          )

          if (resyncResult.ok) {
            await pause(80)
            const refreshResult = await pollLiveStatus(false)

            if (refreshResult.ok) {
              result = await onIssueCommandAwaitAck(
                step.cmd,
                step.risk,
                step.note,
                step.args,
                {
                  logHistory: step.logHistory ?? true,
                  timeoutMs: step.timeoutMs ?? BRINGUP_ACK_TIMEOUT_MS,
                },
              )
            }
          }
        }

        if (!result.ok) {
          await holdOperationError(
            label,
            step.cmd === 'dac_debug_config'
              ? describeDacCommandFailure(result.note)
              : result.note,
          )
          clearOnExit = false
          return false
        }

        completedSteps += 1
      }

      if (refreshAfter) {
        setOperation({
          label,
          detail: 'Refreshing live status and readback...',
          percent: Math.max(
            72,
            Math.round((completedSteps / totalSteps) * 100),
          ),
          tone: 'warning',
        })
        const refreshResult = await pollLiveStatus(false)
        if (!refreshResult.ok) {
          await holdOperationError(label, refreshResult.note)
          clearOnExit = false
          return false
        }
        completedSteps += 1
      }

      setDraftNote(successNote)
      setOperation({
        label,
        detail: 'Complete.',
        percent: 100,
        tone: 'steady',
      })
      await pause(BRINGUP_SUCCESS_DISMISS_MS)
      return true
    } finally {
      operationBusyRef.current = false
      if (clearOnExit) {
        setOperation(null)
      }
    }
  }

  async function saveModulePlan(module: ModuleKey) {
    const moduleStatus = form.modules[module]

    saveHostDraft()

    if (!commandReady) {
      setDraftNote(
        `${moduleMeta[module].label} plan saved locally. Connect the board when you want to mirror it.`,
      )
      return
    }

    await runCommandSequence(
      `Save ${moduleMeta[module].label} plan`,
      `${moduleMeta[module].label} plan saved to the controller profile.`,
      [
        {
          detail: `Saving ${moduleMeta[module].label} plan...`,
          cmd: 'set_module_state',
          risk: 'write',
          note: `Save bring-up plan for ${moduleMeta[module].label}.`,
          args: {
            module: toFirmwareModuleName(module),
            expected_present: moduleStatus.expectedPresent,
            debug_enabled: moduleStatus.debugEnabled,
          },
        },
      ],
    )
  }

  async function saveProfileName() {
    saveHostDraft()

    if (!commandReady) {
      setDraftNote('Profile name saved locally. Connect the board when you want to mirror it.')
      return
    }

    await runCommandSequence(
      'Save profile name',
      'Bring-up profile name saved to the controller.',
      [
        {
          detail: 'Saving profile name...',
          cmd: 'set_profile_name',
          risk: 'write',
          note: 'Rename the active bring-up profile.',
          args: {
            name: form.profileName,
          },
        },
      ],
    )
  }

  async function requestServiceMode() {
    if (!commandReady) {
      setDraftNote('Connect the board before starting a write session.')
      return
    }

    if (operationBusyRef.current) {
      setDraftNote('A bring-up action is already running. Wait for it to finish.')
      return
    }

    operationBusyRef.current = true
    let clearOnExit = true

    try {
      const ready = await ensureServiceMode('Start write session')
      if (!ready) {
        clearOnExit = false
        return
      }

      setDraftNote('Service mode requested. Hardware-write tools are now armed.')
      setOperation({
        label: 'Start write session',
        detail: 'Write session ready.',
        percent: 100,
        tone: 'steady',
      })
      await pause(BRINGUP_SUCCESS_DISMISS_MS)
    } finally {
      operationBusyRef.current = false
      if (clearOnExit) {
        setOperation(null)
      }
    }
  }

  async function applySafetyPolicy() {
    const safetyArgs = buildSafetyPolicyArgs()

    await runCommandSequence(
      'Apply runtime safety policy',
      'Runtime safety thresholds applied to the controller.',
      [
        {
          detail: 'Applying runtime safety thresholds and hold windows...',
          cmd: 'set_runtime_safety',
          risk: 'service',
          note: 'Update runtime safety thresholds, hysteresis, and timeout policy from the bring-up service page.',
          requireService: true,
          args: safetyArgs,
        },
      ],
    )
  }

  function buildSafetyPolicyArgs() {
    return {
      horizon_threshold_deg: parseNumber(
        form.safetyHorizonThresholdDeg,
        snapshot.safety.horizonThresholdDeg,
      ),
      horizon_hysteresis_deg: parseNumber(
        form.safetyHorizonHysteresisDeg,
        snapshot.safety.horizonHysteresisDeg,
      ),
      tof_min_range_m: parseNumber(
        form.safetyTofMinRangeM,
        snapshot.safety.tofMinRangeM,
      ),
      tof_max_range_m: parseNumber(
        form.safetyTofMaxRangeM,
        snapshot.safety.tofMaxRangeM,
      ),
      tof_hysteresis_m: parseNumber(
        form.safetyTofHysteresisM,
        snapshot.safety.tofHysteresisM,
      ),
      imu_stale_ms: Math.round(
        parseNumber(form.safetyImuStaleMs, snapshot.safety.imuStaleMs),
      ),
      tof_stale_ms: Math.round(
        parseNumber(form.safetyTofStaleMs, snapshot.safety.tofStaleMs),
      ),
      rail_good_timeout_ms: Math.round(
        parseNumber(
          form.safetyRailGoodTimeoutMs,
          snapshot.safety.railGoodTimeoutMs,
        ),
      ),
      lambda_drift_limit_nm: parseNumber(
        form.safetyLambdaDriftLimitNm,
        snapshot.safety.lambdaDriftLimitNm,
      ),
      lambda_drift_hysteresis_nm: parseNumber(
        form.safetyLambdaDriftHysteresisNm,
        snapshot.safety.lambdaDriftHysteresisNm,
      ),
      lambda_drift_hold_ms: Math.round(
        parseNumber(
          form.safetyLambdaDriftHoldMs,
          snapshot.safety.lambdaDriftHoldMs,
        ),
      ),
      ld_overtemp_limit_c: parseNumber(
        form.safetyLdOvertempLimitC,
        snapshot.safety.ldOvertempLimitC,
      ),
      tec_temp_adc_trip_v: parseNumber(
        form.safetyTecTempAdcTripV,
        snapshot.safety.tecTempAdcTripV,
      ),
      tec_temp_adc_hysteresis_v: parseNumber(
        form.safetyTecTempAdcHysteresisV,
        snapshot.safety.tecTempAdcHysteresisV,
      ),
      tec_temp_adc_hold_ms: Math.round(
        parseNumber(
          form.safetyTecTempAdcHoldMs,
          snapshot.safety.tecTempAdcHoldMs,
        ),
      ),
      tec_min_command_c: parseNumber(
        form.safetyTecMinCommandC,
        snapshot.safety.tecMinCommandC,
      ),
      tec_max_command_c: parseNumber(
        form.safetyTecMaxCommandC,
        snapshot.safety.tecMaxCommandC,
      ),
      tec_ready_tolerance_c: parseNumber(
        form.safetyTecReadyToleranceC,
        snapshot.safety.tecReadyToleranceC,
      ),
      max_laser_current_a: parseNumber(
        form.safetyMaxLaserCurrentA,
        snapshot.safety.maxLaserCurrentA,
      ),
    }
  }

  function moduleStateStep(module: ModuleKey, detail: string) {
    return {
      detail,
      cmd: 'set_module_state',
      risk: 'write' as const,
      note: `Sync the ${moduleMeta[module].label} module plan before applying module-specific settings.`,
      args: {
        module: toFirmwareModuleName(module),
        expected_present: form.modules[module].expectedPresent,
        debug_enabled: form.modules[module].debugEnabled,
      },
    }
  }

  async function applyDacProfile() {
    const preparedForm = ensureModuleDebugWrite(sanitizeDacDraft(form), 'dac')

    if (preparedForm !== form) {
      setForm(preparedForm)
      if (!form.modules.dac.expectedPresent && !form.modules.dac.debugEnabled) {
        setDraftNote('DAC debug tooling was auto-enabled for this write so the controller can accept DAC commands.')
      }
    }

    await runCommandSequence(
      'Apply DAC profile',
      'DAC profile applied to the controller bring-up state.',
      [
        {
          detail: 'Syncing DAC module plan...',
          cmd: 'set_module_state',
          risk: 'write',
          note: 'Sync the DAC module plan before applying DAC settings.',
          args: {
            module: toFirmwareModuleName('dac'),
            expected_present: preparedForm.modules.dac.expectedPresent,
            debug_enabled: preparedForm.modules.dac.debugEnabled,
          },
        },
        {
          detail: 'Applying DAC reference and update policy...',
          cmd: 'dac_debug_config',
          risk: 'service',
          note: 'Stage DAC reference, gain, divider, and update behavior for bring-up.',
          requireService: true,
          args: {
            reference_mode: preparedForm.dacReferenceMode,
            gain_2x: preparedForm.dacGain2x,
            ref_div: preparedForm.dacRefDiv,
            sync_mode: preparedForm.dacSyncMode,
          },
          retryOnModuleBlocked: {
            module: 'dac',
            expectedPresent: preparedForm.modules.dac.expectedPresent,
            debugEnabled: preparedForm.modules.dac.debugEnabled,
            label: moduleMeta.dac.label,
          },
        },
        {
          detail: 'Staging DAC LD channel shadow voltage...',
          cmd: 'dac_debug_set',
          risk: 'service',
          note: 'Stage the DAC LD channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'ld',
            voltage_v: parseNumber(
              preparedForm.dacLdVoltage,
              snapshot.bringup.tuning.dacLdChannelV,
            ),
          },
        },
        {
          detail: 'Staging DAC TEC channel shadow voltage...',
          cmd: 'dac_debug_set',
          risk: 'service',
          note: 'Stage the DAC TEC channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'tec',
            voltage_v: parseNumber(
              preparedForm.dacTecVoltage,
              snapshot.bringup.tuning.dacTecChannelV,
            ),
          },
        },
        {
          detail: 'Checking DAC status register for reference alarm...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 STATUS to confirm REF-ALARM is clear and the analog outputs are allowed to drive.',
          args: {
            address: 0x48,
            reg: 0x07,
          },
        },
        {
          detail: 'Reading back DAC LD output register...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 channel A data register to verify the staged LD output code.',
          args: {
            address: 0x48,
            reg: 0x08,
          },
        },
        {
          detail: 'Reading back DAC TEC output register...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 channel B data register to verify the staged TEC output code.',
          args: {
            address: 0x48,
            reg: 0x09,
          },
        },
      ],
    )
  }

  async function applySingleDacShadowChannel(
    channel: 'ld' | 'tec',
    stagedVoltage: string,
  ) {
    const preparedForm = ensureModuleDebugWrite(sanitizeDacDraft(form), 'dac')

    if (preparedForm !== form) {
      setForm(preparedForm)
      if (!form.modules.dac.expectedPresent && !form.modules.dac.debugEnabled) {
        setDraftNote('DAC debug tooling was auto-enabled for this write so the controller can accept DAC commands.')
      }
    }

    const channelLabel = channel === 'ld' ? 'LD' : 'TEC'
    const readbackRegister = channel === 'ld' ? 0x08 : 0x09
    const snapshotFallback =
      channel === 'ld'
        ? snapshot.bringup.tuning.dacLdChannelV
        : snapshot.bringup.tuning.dacTecChannelV

    await runCommandSequence(
      `Apply ${channelLabel} setpoint`,
      `${channelLabel} DAC shadow setpoint applied and read back.`,
      [
        {
          detail: 'Syncing DAC module plan...',
          cmd: 'set_module_state',
          risk: 'write',
          note: 'Sync the DAC module plan before applying DAC channel settings.',
          args: {
            module: toFirmwareModuleName('dac'),
            expected_present: preparedForm.modules.dac.expectedPresent,
            debug_enabled: preparedForm.modules.dac.debugEnabled,
          },
        },
        {
          detail: 'Applying DAC reference and update policy...',
          cmd: 'dac_debug_config',
          risk: 'service',
          note: 'Keep the DAC reference, gain, divider, and update mode aligned before writing a single channel setpoint.',
          requireService: true,
          args: {
            reference_mode: preparedForm.dacReferenceMode,
            gain_2x: preparedForm.dacGain2x,
            ref_div: preparedForm.dacRefDiv,
            sync_mode: preparedForm.dacSyncMode,
          },
          retryOnModuleBlocked: {
            module: 'dac',
            expectedPresent: preparedForm.modules.dac.expectedPresent,
            debugEnabled: preparedForm.modules.dac.debugEnabled,
            label: moduleMeta.dac.label,
          },
        },
        {
          detail: `Staging ${channelLabel} DAC shadow voltage...`,
          cmd: 'dac_debug_set',
          risk: 'service',
          note: `Stage the DAC ${channelLabel} channel shadow voltage.`,
          requireService: true,
          args: {
            channel,
            voltage_v: parseNumber(stagedVoltage, snapshotFallback),
          },
        },
        {
          detail: 'Checking DAC status register for reference alarm...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 STATUS to confirm REF-ALARM is clear after the channel write.',
          args: {
            address: 0x48,
            reg: 0x07,
          },
        },
        {
          detail: `Reading back DAC ${channelLabel} data register...`,
          cmd: 'i2c_read',
          risk: 'read',
          note: `Read back DAC80502 ${channelLabel} data register to confirm the staged output code.`,
          args: {
            address: 0x48,
            reg: readbackRegister,
          },
        },
      ],
    )
  }

  async function applyImuProfile() {
    await runCommandSequence(
      'Apply IMU tuning',
      'IMU tuning applied to the controller bring-up state.',
      [
        moduleStateStep('imu', 'Syncing IMU module plan...'),
        {
          detail: 'Applying IMU runtime settings...',
          cmd: 'imu_debug_config',
          risk: 'service',
          note: 'Stage the IMU bring-up configuration.',
          requireService: true,
          args: {
            odr_hz: parseNumber(form.imuOdrHz, snapshot.bringup.tuning.imuOdrHz),
            accel_range_g: parseNumber(
              form.imuAccelRangeG,
              snapshot.bringup.tuning.imuAccelRangeG,
            ),
            gyro_range_dps: parseNumber(
              form.imuGyroRangeDps,
              snapshot.bringup.tuning.imuGyroRangeDps,
            ),
            gyro_enabled: form.imuGyroEnabled,
            lpf2_enabled: form.imuLpf2Enabled,
            timestamp_enabled: form.imuTimestampEnabled,
            bdu_enabled: form.imuBduEnabled,
            if_inc_enabled: form.imuIfIncEnabled,
            i2c_disabled: form.imuI2cDisabled,
          },
        },
      ],
    )
  }

  async function applyTofIllumination(enabled: boolean) {
    const parsedDuty = Math.round(
      parseNumber(
        tofIlluminationDutyPct,
        snapshot.bringup.illumination.tof.dutyCyclePct > 0
          ? snapshot.bringup.illumination.tof.dutyCyclePct
          : 35,
      ),
    )
    const clampedDuty = Math.max(0, Math.min(100, parsedDuty))
    const effectiveDuty = enabled ? Math.max(1, clampedDuty) : 0

    await runCommandSequence(
      enabled ? 'Enable front illumination' : 'Disable front illumination',
      enabled
        ? `Front illumination staged on GPIO6 at ${effectiveDuty}% duty.`
        : 'Front illumination returned low on GPIO6.',
      [
        moduleStateStep('tof', 'Syncing ToF module plan...'),
        {
          detail: enabled
            ? 'Driving TPS61169 CTRL from GPIO6 with service PWM...'
            : 'Forcing GPIO6 low so the front illumination path shuts down...',
          cmd: 'tof_illumination_set',
          risk: 'service',
          note: enabled
            ? 'Stage service-only front illumination on GPIO6 for bring-up visibility tests.'
            : 'Disable the service-only front illumination on GPIO6.',
          requireService: true,
          args: {
            enabled,
            duty_cycle_pct: effectiveDuty,
            frequency_hz: TOF_ILLUMINATION_PWM_HZ,
          },
          retryOnModuleBlocked: {
            module: 'tof',
            expectedPresent: form.modules.tof.expectedPresent,
            debugEnabled: form.modules.tof.debugEnabled,
            label: moduleMeta.tof.label,
          },
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function applyPdProfile() {
    await runCommandSequence(
      'Apply PD runtime PDOs',
      'USB-PD runtime PDOs applied, renegotiation requested, and live readback refreshed.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: 'Uploading STUSB4500 runtime PDO plan...',
          cmd: 'pd_debug_config',
          risk: 'service',
          note: 'Apply STUSB4500 runtime PDO settings and update firmware power thresholds.',
          requireService: true,
          logHistory: true,
          timeoutMs: BRINGUP_PD_APPLY_TIMEOUT_MS,
          args: buildPdCommandArgs(),
        },
        {
          detail: 'Refreshing live STUSB contract and PDO readback...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Force the controller to refresh STUSB4500 live status and PDO readback.',
          logHistory: false,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function burnPdProfileToNvm() {
    setPdNvmConfirmOpen(false)
    await runCommandSequence(
      'Burn PD PDOs to NVM',
      'STUSB4500 NVM burn verified against raw NVM readback.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: 'Validating runtime PDOs, then writing the current PDO plan into STUSB4500 nonvolatile memory...',
          cmd: 'pd_burn_nvm',
          risk: 'service',
          note: 'Validate the current runtime PDO plan, burn it into STUSB4500 NVM, and verify the raw NVM readback before reporting success.',
          requireService: true,
          timeoutMs: BRINGUP_PD_NVM_TIMEOUT_MS,
          args: buildPdCommandArgs(),
        },
        {
          detail: 'Refreshing live STUSB contract and PDO readback after NVM action...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Refresh STUSB4500 contract and PDO readback after the NVM action.',
          logHistory: false,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function savePdProfileToFirmware() {
    await runCommandSequence(
      form.pdFirmwarePlanEnabled
        ? 'Save firmware PDO plan'
        : 'Save firmware PDO plan disabled',
      form.pdFirmwarePlanEnabled
        ? 'Firmware-owned PDO plan saved and will be reconciled into STUSB runtime state whenever the controller sees a mismatch online.'
        : 'Firmware-owned PDO plan saved with auto-reconcile disabled.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: form.pdFirmwarePlanEnabled
            ? 'Validating runtime PDO plan before saving it to controller firmware...'
            : 'Saving PDO thresholds and disabling firmware auto-reconcile...',
          cmd: 'pd_save_firmware_plan',
          risk: 'service',
          note: 'Save the current PDO plan into controller firmware so the ESP32 can compare it against STUSB runtime PDOs and re-apply it only when needed.',
          requireService: true,
          logHistory: true,
          timeoutMs: BRINGUP_PD_APPLY_TIMEOUT_MS,
          args: buildPdCommandArgs(true),
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function applyHapticProfile() {
    await runCommandSequence(
      'Apply haptic profile',
      'Haptic profile applied to the controller bring-up state.',
      [
        moduleStateStep('haptic', 'Syncing haptic module plan...'),
        {
          detail: 'Applying DRV2605 profile...',
          cmd: 'haptic_debug_config',
          risk: 'service',
          note: 'Stage DRV2605 mode, library, actuator, RTP, and effect settings.',
          requireService: true,
          args: {
            effect_id: parseNumber(
              form.hapticEffectId,
              snapshot.bringup.tuning.hapticEffectId,
            ),
            mode: form.hapticMode,
            library: parseNumber(
              form.hapticLibrary,
              snapshot.bringup.tuning.hapticLibrary,
            ),
            actuator: form.hapticActuator,
            rtp_level: parseNumber(
              form.hapticRtpLevel,
              snapshot.bringup.tuning.hapticRtpLevel,
            ),
          },
        },
      ],
    )
  }

  async function pushDraftToDevice() {
    await runCommandSequence(
      'Push full bring-up plan',
      'Full bring-up plan pushed to the controller profile.',
      [
        {
          detail: 'Saving bring-up profile name...',
          cmd: 'set_profile_name',
          risk: 'write',
          note: 'Update the active bring-up profile name.',
          args: {
            name: form.profileName,
          },
        },
        ...moduleKeys.map((module) => ({
          detail: `Saving ${moduleMeta[module].label} plan...`,
          cmd: 'set_module_state',
          risk: 'write' as const,
          note: `Update bring-up plan for ${moduleMeta[module].label}.`,
          args: {
            module: toFirmwareModuleName(module),
            expected_present: form.modules[module].expectedPresent,
            debug_enabled: form.modules[module].debugEnabled,
          },
        })),
        {
          detail: 'Applying runtime safety policy...',
          cmd: 'set_runtime_safety',
          risk: 'service' as const,
          note: 'Push the shared Service-page safety policy as part of the full bring-up plan.',
          requireService: true,
          args: buildSafetyPolicyArgs(),
        },
        {
          detail: 'Applying DAC profile...',
          cmd: 'dac_debug_config',
          risk: 'service' as const,
          note: 'Stage DAC reference, gain, divider, and update behavior for bring-up.',
          requireService: true,
          args: {
            reference_mode: form.dacReferenceMode,
            gain_2x: form.dacGain2x,
            ref_div: form.dacRefDiv,
            sync_mode: form.dacSyncMode,
          },
        },
        {
          detail: 'Applying DAC LD shadow...',
          cmd: 'dac_debug_set',
          risk: 'service' as const,
          note: 'Stage the DAC LD channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'ld',
            voltage_v: parseNumber(
              form.dacLdVoltage,
              snapshot.bringup.tuning.dacLdChannelV,
            ),
          },
        },
        {
          detail: 'Applying DAC TEC shadow...',
          cmd: 'dac_debug_set',
          risk: 'service' as const,
          note: 'Stage the DAC TEC channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'tec',
            voltage_v: parseNumber(
              form.dacTecVoltage,
              snapshot.bringup.tuning.dacTecChannelV,
            ),
          },
        },
        {
          detail: 'Applying IMU tuning...',
          cmd: 'imu_debug_config',
          risk: 'service' as const,
          note: 'Stage the IMU bring-up configuration.',
          requireService: true,
          args: {
            odr_hz: parseNumber(form.imuOdrHz, snapshot.bringup.tuning.imuOdrHz),
            accel_range_g: parseNumber(
              form.imuAccelRangeG,
              snapshot.bringup.tuning.imuAccelRangeG,
            ),
            gyro_range_dps: parseNumber(
              form.imuGyroRangeDps,
              snapshot.bringup.tuning.imuGyroRangeDps,
            ),
            gyro_enabled: form.imuGyroEnabled,
            lpf2_enabled: form.imuLpf2Enabled,
            timestamp_enabled: form.imuTimestampEnabled,
            bdu_enabled: form.imuBduEnabled,
            if_inc_enabled: form.imuIfIncEnabled,
            i2c_disabled: form.imuI2cDisabled,
          },
        },
        {
          detail: 'Applying PD runtime PDOs...',
          cmd: 'pd_debug_config',
          risk: 'service' as const,
          note: 'Apply STUSB4500 runtime PDO settings and update firmware power thresholds.',
          requireService: true,
          args: buildPdCommandArgs(),
        },
        {
          detail: 'Applying haptic profile...',
          cmd: 'haptic_debug_config',
          risk: 'service' as const,
          note: 'Stage DRV2605 mode, library, actuator, RTP, and effect settings.',
          requireService: true,
          args: {
            effect_id: parseNumber(
              form.hapticEffectId,
              snapshot.bringup.tuning.hapticEffectId,
            ),
            mode: form.hapticMode,
            library: parseNumber(
              form.hapticLibrary,
              snapshot.bringup.tuning.hapticLibrary,
            ),
            actuator: form.hapticActuator,
            rtp_level: parseNumber(
              form.hapticRtpLevel,
              snapshot.bringup.tuning.hapticRtpLevel,
            ),
          },
        },
      ],
    )
  }

  function moduleQuickReadI2c(
    label: string,
    address: number,
    reg: number,
    note: string,
  ) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'i2c_read',
          risk: 'read',
          note,
          args: { address, reg },
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function moduleQuickReadSpi(
    label: string,
    device: string,
    reg: number,
    note: string,
  ) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'spi_read',
          risk: 'read',
          note,
          args: { device, reg },
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function moduleQuickScanI2c(label: string, note: string) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'i2c_scan',
          risk: 'read',
          note,
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  useEffect(() => {
    if (!commandReady) {
      return
    }

    const timerId = window.setInterval(() => {
      if (operationBusyRef.current || probeBusyRef.current || moduleSyncBusyRef.current) {
        return
      }

      const livePlanSignature = modulePlanSignature(latestSnapshotRef.current.bringup.modules)
      if (desiredModulePlanSignature !== livePlanSignature) {
        probeCycleRef.current = {
          signature: '',
          nextIndex: 0,
          initialSweepComplete: false,
          lastProbeAtMs: 0,
        }
        return
      }

      const probeQueue = buildProbeQueue(form.modules)
      if (probeQueue.length === 0) {
        probeCycleRef.current = {
          signature: '',
          nextIndex: 0,
          initialSweepComplete: false,
          lastProbeAtMs: 0,
        }
        return
      }

      const cycleSignature = `${desiredModulePlanSignature}|${probeQueue
        .map((probe) => probe.id)
        .join(',')}`

      if (probeCycleRef.current.signature !== cycleSignature) {
        probeCycleRef.current = {
          signature: cycleSignature,
          nextIndex: 0,
          initialSweepComplete: false,
          lastProbeAtMs: 0,
        }
      }

      const nowMs = window.performance.now()
      const minimumDelayMs = probeCycleRef.current.initialSweepComplete
        ? BRINGUP_PROBE_INTERVAL_MS
        : BRINGUP_PROBE_SWEEP_INTERVAL_MS

      if (
        probeCycleRef.current.lastProbeAtMs > 0 &&
        nowMs - probeCycleRef.current.lastProbeAtMs < minimumDelayMs
      ) {
        return
      }

      const probe =
        probeQueue[probeCycleRef.current.nextIndex % probeQueue.length]
      probeBusyRef.current = true
      probeCycleRef.current.lastProbeAtMs = nowMs

      void onIssueCommandAwaitAck(
        probe.cmd,
        'read',
        probe.note,
        probe.args,
        {
          logHistory: false,
          timeoutMs: BRINGUP_AUTO_PROBE_TIMEOUT_MS,
        },
      ).finally(() => {
        if (probeCycleRef.current.signature === cycleSignature) {
          if (!probeCycleRef.current.initialSweepComplete) {
            const nextIndex = probeCycleRef.current.nextIndex + 1
            probeCycleRef.current.nextIndex = nextIndex
            probeCycleRef.current.initialSweepComplete =
              nextIndex >= probeQueue.length
          } else {
            probeCycleRef.current.nextIndex =
              (probeCycleRef.current.nextIndex + 1) % probeQueue.length
          }
          probeCycleRef.current.lastProbeAtMs = window.performance.now()
        }
        probeBusyRef.current = false
      })
    }, 220)

    return () => {
      window.clearInterval(timerId)
    }
  }, [
    commandReady,
    desiredModulePlanSignature,
    form.modules,
    onIssueCommandAwaitAck,
  ])

  function renderModuleSettings(module: ModuleKey) {
    const liveStatus = displayModules[module]
    const plannedStatus = form.modules[module]
    const score = moduleScore(module, liveStatus, connected)
    const summary = moduleSummary(module, liveStatus, connected)
    const meta = moduleMeta[module]

    return (
      <article className="bringup-module-frame">
        <div className="bringup-module-frame__head">
          <div>
            <p className="eyebrow">{meta.transport}</p>
            <h2>{meta.label}</h2>
          </div>
          <div className="bringup-module-frame__status">
            {connected ? (
              <>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  planned {plannedStatus.expectedPresent ? 'yes' : 'no'}
                </span>
                {plannedStatus.expectedPresent ? (
                  meta.validationMode === 'probe' ? (
                    <span className={liveStatus.detected ? 'status-badge is-on' : 'status-badge is-warn'}>
                      probe {liveStatus.detected ? 'seen' : 'not yet'}
                    </span>
                  ) : (
                    <span className="status-badge is-on">
                      readback monitored
                    </span>
                  )
                ) : null}
                {meta.validationMode === 'probe' && liveStatus.detected ? (
                  <span className={liveStatus.healthy ? 'status-badge is-on' : 'status-badge is-warn'}>
                    health {liveStatus.healthy ? 'ok' : 'check'}
                  </span>
                ) : null}
              </>
            ) : (
              <>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  offline plan
                </span>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  installed {plannedStatus.expectedPresent ? 'planned' : 'no'}
                </span>
              </>
            )}
            <span
              className={
                plannedStatus.expectedPresent && plannedStatus.debugEnabled
                  ? 'status-badge is-on'
                  : 'status-badge'
              }
            >
              tools {plannedStatus.expectedPresent && plannedStatus.debugEnabled ? 'enabled' : 'off'}
            </span>
          </div>
        </div>

        <p className="panel-note">{meta.detail}</p>

        <div className="bringup-module-frame__summary">
          <div>
            <strong>{summary.headline}</strong>
            <p className="inline-help">{summary.detail}</p>
          </div>
          <div className="bringup-module-frame__meter">
            <span>{score.progress}%</span>
            <ProgressMeter value={score.progress} tone={score.tone} compact />
          </div>
        </div>

        <div className="bringup-toggle-row">
          <label className="arming-toggle is-compact">
            <input
              type="checkbox"
              checked={plannedStatus.expectedPresent}
              title="Declare whether this module is physically populated on the current bench build."
              onChange={(event) =>
                patchModule(module, {
                  expectedPresent: event.target.checked,
                })
              }
            />
            <span>Expected populated on this bench build.</span>
          </label>

          <label className="arming-toggle is-compact">
            <input
              type="checkbox"
              checked={plannedStatus.debugEnabled}
              title="Allow service-only tools and staged config for this module."
              onChange={(event) =>
                patchModule(module, {
                  debugEnabled: event.target.checked,
                })
              }
            />
            <span>Enable service-mode debug tooling for this module.</span>
          </label>

          <button
            type="button"
            className="action-button is-inline"
            disabled={operation !== null}
            title={`Save the planned install state and debug-tool flag for ${meta.label}. This does not require service mode.`}
            onClick={() => {
              void saveModulePlan(module)
            }}
          >
            Save module plan
          </button>
        </div>
      </article>
    )
  }

  async function setSupplyEnable(rail: 'ld' | 'tec', enabled: boolean) {
    const railLabel = rail === 'ld' ? 'LD supply' : 'TEC supply'

    await runCommandSequence(
      `${enabled ? 'Enable' : 'Disable'} ${railLabel}`,
      `${railLabel} service request updated and live rail status refreshed.`,
      [
        {
          detail: `${enabled ? 'Enabling' : 'Disabling'} ${railLabel.toLowerCase()} service override...`,
          cmd: 'set_supply_enable',
          risk: 'service',
          note: `${enabled ? 'Enable' : 'Disable'} the ${railLabel.toLowerCase()} service-only MPM3530 rail request while keeping beam outputs forced safe.`,
          requireService: true,
          args: {
            rail,
            enabled,
          },
        },
      ],
    )
  }

  async function setHapticEnable(enabled: boolean) {
    const ok = await runCommandSequence(
      `${enabled ? 'Enable' : 'Disable'} ERM driver`,
      `GPIO48 ${enabled ? 'asserted' : 'cleared'} for the DRV2605/ERM driver enable path.`,
      [
        {
          detail: `${enabled ? 'Asserting' : 'Clearing'} ERM EN on GPIO48...`,
          cmd: 'set_haptic_enable',
          risk: 'service',
          note: 'Service-only direct control of the dedicated ERM driver enable pin on GPIO48.',
          requireService: true,
          args: {
            enabled,
          },
        },
      ],
      {
        refreshAfter: false,
      },
    )

    if (!ok) {
      return
    }

    if (latestSnapshotRef.current.peripherals.haptic.enablePinHigh !== enabled) {
      await pause(Math.min(BRINGUP_HAPTIC_ENABLE_WAIT_MS, 140))
      if (latestSnapshotRef.current.peripherals.haptic.enablePinHigh !== enabled) {
        setDraftNote(
          `ERM EN request applied, but live GPIO48 readback is still ${latestSnapshotRef.current.peripherals.haptic.enablePinHigh ? 'high' : 'low'}. Verify service mode plus board wiring.`,
        )
      }
    }
  }

async function setLdSbdnMode(mode: LdSbdnMode) {
    const labelByMode: Record<LdSbdnMode, string> = {
      firmware: 'Auto',
      'off-pd': 'Off',
      'on-pu': 'On',
      'standby-hiz': 'Standby',
    }
    const detailByMode: Record<LdSbdnMode, string> = {
      firmware: 'Releasing GPIO13 back to normal firmware ownership...',
      'off-pd': 'Driving GPIO13 low for Off...',
      'on-pu': 'Driving GPIO13 high for On...',
      'standby-hiz': 'Releasing GPIO13 to input mode for Standby...',
    }
    const noteByMode: Record<LdSbdnMode, string> = {
      firmware: 'Release GPIO13 so the original firmware logic regains SBDN ownership.',
      'off-pd': 'Service-only override: drive GPIO13 low to hold the laser driver off.',
      'on-pu': 'Service-only override: drive GPIO13 high to force the laser driver on.',
      'standby-hiz': 'Service-only override: leave GPIO13 high impedance so SBDN sits in the standby threshold window.',
    }
    const argsByMode: Record<LdSbdnMode, Record<string, number | string | boolean>> = {
      firmware: {
        gpio: 13,
        mode: 'firmware',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
      'off-pd': {
        gpio: 13,
        mode: 'output',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
      'on-pu': {
        gpio: 13,
        mode: 'output',
        level_high: true,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
      'standby-hiz': {
        gpio: 13,
        mode: 'input',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
    }

    await runCommandSequence(
      `Set LD SBDN ${labelByMode[mode]}`,
      `LD SBDN moved to ${labelByMode[mode]}.`,
      [
        {
          detail: detailByMode[mode],
          cmd: 'set_gpio_override',
          risk: 'service',
          note: noteByMode[mode],
          requireService: true,
          args: argsByMode[mode],
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function setLdPcnMode(mode: LdPcnMode) {
    const detailByMode: Record<LdPcnMode, string> = {
      firmware: 'Releasing GPIO21 back to normal firmware ownership...',
      lisl: 'Driving GPIO21 low for LISL selection...',
      lish: 'Driving GPIO21 high for LISH selection...',
    }
    const noteByMode: Record<LdPcnMode, string> = {
      firmware: 'Release GPIO21 so the original firmware logic regains PCN ownership.',
      lisl: 'Service-only override: drive GPIO21 low to select the LISL leg.',
      lish: 'Service-only override: drive GPIO21 high to select the LISH leg.',
    }
    const argsByMode: Record<LdPcnMode, Record<string, number | string | boolean>> = {
      firmware: {
        gpio: 21,
        mode: 'firmware',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
      lisl: {
        gpio: 21,
        mode: 'output',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
      lish: {
        gpio: 21,
        mode: 'output',
        level_high: true,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
    }

    await runCommandSequence(
      `Set LD PCN ${mode}`,
      `LD PCN moved to ${mode}.`,
      [
        {
          detail: detailByMode[mode],
          cmd: 'set_gpio_override',
          risk: 'service',
          note: noteByMode[mode],
          requireService: true,
          args: argsByMode[mode],
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function setBringupGreenLaserEnabled(enabled: boolean) {
    await runCommandSequence(
      enabled ? 'Enable green laser' : 'Disable green laser',
      enabled
        ? 'GPIO37 is now driving the shared green-laser enable net high.'
        : 'GPIO37 is now driving the shared green-laser enable net low.',
      [
        {
          detail: enabled
            ? 'Driving GPIO37 high on the shared GN_LD_EN / ERM_TRIG net...'
            : 'Driving GPIO37 low on the shared GN_LD_EN / ERM_TRIG net...',
          cmd: 'set_gpio_override',
          risk: 'service',
          note: enabled
            ? 'Service-only direct green-laser enable: drive GPIO37 high on the shared GN_LD_EN / ERM_TRIG net.'
            : 'Service-only direct green-laser disable: drive GPIO37 low on the shared GN_LD_EN / ERM_TRIG net.',
          requireService: true,
          args: {
            gpio: 37,
            mode: 'output',
            level_high: enabled,
            pullup_enabled: false,
            pulldown_enabled: false,
          },
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function patchHapticPattern<Key extends keyof HapticPatternDraft>(
    key: Key,
    value: HapticPatternDraft[Key],
  ) {
    setHapticPattern((current) => ({
      ...current,
      [key]: value,
    }))
  }

  function applyHapticPatternPreset(
    pulseCount: number,
    highMs: number,
    lowMs: number,
  ) {
    setHapticPattern((current) => ({
      ...current,
      pulseCount: String(pulseCount),
      highMs: String(highMs),
      lowMs: String(lowMs),
    }))
  }

  async function runHapticExternalPattern() {
    const pulseCount = clampInteger(
      parseNumber(hapticPattern.pulseCount, 3),
      1,
      BRINGUP_HAPTIC_PATTERN_MAX_PULSES,
    )
    const highMs = clampInteger(
      parseNumber(hapticPattern.highMs, 45),
      BRINGUP_HAPTIC_PATTERN_MIN_MS,
      BRINGUP_HAPTIC_PATTERN_MAX_MS,
    )
    const lowMs = clampInteger(
      parseNumber(hapticPattern.lowMs, 90),
      BRINGUP_HAPTIC_PATTERN_MIN_MS,
      BRINGUP_HAPTIC_PATTERN_MAX_MS,
    )
    const totalPatternMs = pulseCount * (highMs + lowMs)

    if (!hapticPattern.hazardAccepted) {
      setDraftNote(
        'Acknowledge the shared IO37 / GN_LD_EN hazard before running an external trigger burst.',
      )
      return
    }

    await runCommandSequence(
      'Run external ERM trigger pattern',
      `External trigger burst completed: ${pulseCount} pulse${pulseCount === 1 ? '' : 's'} on IO37.`,
      [
        {
          detail: 'Applying the staged DRV2605 profile for external trigger mode...',
          cmd: 'haptic_debug_config',
          risk: 'service',
          note: 'Stage the DRV2605 profile before driving the external ERM trigger burst.',
          requireService: true,
          retryOnModuleBlocked: {
            module: 'haptic',
            expectedPresent: form.modules.haptic.expectedPresent,
            debugEnabled: form.modules.haptic.debugEnabled,
            label: 'haptic',
          },
          args: {
            effect_id: parseNumber(
              form.hapticEffectId,
              snapshot.bringup.tuning.hapticEffectId,
            ),
            mode: form.hapticMode,
            library: parseNumber(
              form.hapticLibrary,
              snapshot.bringup.tuning.hapticLibrary,
            ),
            actuator: form.hapticActuator,
            rtp_level: parseNumber(
              form.hapticRtpLevel,
              snapshot.bringup.tuning.hapticRtpLevel,
            ),
          },
        },
        {
          detail: `Driving a ${pulseCount}-pulse burst on shared IO37 / GN_LD_EN...`,
          cmd: 'haptic_external_trigger_pattern',
          risk: 'service',
          note: 'Bench-only hazardous shared-net ERM trigger burst on IO37 / GN_LD_EN. Use only with the visible alignment path controlled and terminated.',
          requireService: true,
          timeoutMs: Math.max(BRINGUP_ACK_TIMEOUT_MS, totalPatternMs + 1800),
          args: {
            pulse_count: pulseCount,
            high_ms: highMs,
            low_ms: lowMs,
            release_after: hapticPattern.releaseAfter,
          },
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function renderPowerPage() {
    const rails = [
      {
        key: 'ld' as const,
        label: 'LD MPM3530',
        detail:
          'Service-only VIN enable for the laser-driver power rail. Driver standby stays asserted; this page never enables emission.',
        requested: connected ? snapshot.bringup.power.ldRequested : false,
        enabled: liveSnapshot.rails.ld.enabled,
        pgood: liveSnapshot.rails.ld.pgood,
      },
      {
        key: 'tec' as const,
        label: 'TEC MPM3530',
        detail:
          'Service-only VIN enable for the TEC controller rail. Use this to prove rail startup and PGOOD behavior before closing the loop.',
        requested: connected ? snapshot.bringup.power.tecRequested : false,
        enabled: liveSnapshot.rails.tec.enabled,
        pgood: liveSnapshot.rails.tec.pgood,
      },
    ]

    return (
      <div className="bringup-page-grid">
        <article className="panel-cutout bringup-hero">
          <div className="cutout-head">
            <Cable size={16} />
            <strong>Power supply bring-up</strong>
          </div>
          <p className="panel-note">
            This page controls the two MPM3530 rail enables in service mode only. It
            is for proving rail sequencing and PGOOD wiring with the optical path held
            safe: alignment stays off, driver standby stays asserted, and no NIR
            request is generated here.
          </p>
          <div className="bringup-fact-grid">
            <div>
              <span>Service rail requests</span>
              <strong>
                {snapshot.bringup.power.ldRequested || snapshot.bringup.power.tecRequested
                  ? `${Number(snapshot.bringup.power.ldRequested) + Number(snapshot.bringup.power.tecRequested)} active`
                  : 'none'}
              </strong>
            </div>
            <div>
              <span>Live LD rail</span>
              <strong>
                {liveSnapshot.rails.ld.enabled
                  ? liveSnapshot.rails.ld.pgood
                    ? 'enabled / PGOOD'
                    : 'enabled / waiting'
                  : 'off'}
              </strong>
            </div>
            <div>
              <span>Live TEC rail</span>
              <strong>
                {liveSnapshot.rails.tec.enabled
                  ? liveSnapshot.rails.tec.pgood
                    ? 'enabled / PGOOD'
                    : 'enabled / waiting'
                  : 'off'}
              </strong>
            </div>
            <div>
              <span>Write session</span>
              <strong>{serviceModeActive ? 'service active' : 'required for rail control'}</strong>
            </div>
          </div>
        </article>

        {rails.map((rail) => (
          <article key={rail.key} className="panel-cutout">
            <div className="cutout-head">
              <Cable size={16} />
              <strong>{rail.label}</strong>
            </div>
            <p className="panel-note">{rail.detail}</p>
            <div className="bringup-module-frame__status">
              <span className={rail.requested ? 'status-badge is-on' : 'status-badge'}>
                request {rail.requested ? 'on' : 'off'}
              </span>
              <span
                className={
                  rail.enabled
                    ? rail.pgood
                      ? 'status-badge is-on'
                      : 'status-badge is-warn'
                    : 'status-badge'
                }
              >
                rail {rail.enabled ? 'enabled' : 'off'}
              </span>
              <span className={rail.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
                pgood {rail.pgood ? 'high' : 'low'}
              </span>
            </div>
            <div className="bringup-fact-grid">
              <div>
                <span>Requested service override</span>
                <strong>{rail.requested ? 'Enabled' : 'Disabled'}</strong>
              </div>
              <div>
                <span>Actual output enable</span>
                <strong>{rail.enabled ? 'Asserted' : 'Off'}</strong>
              </div>
              <div>
                <span>PGOOD</span>
                <strong>{rail.pgood ? 'High' : 'Low'}</strong>
              </div>
              <div>
                <span>Status</span>
                <strong>
                  {rail.requested
                    ? rail.enabled
                      ? rail.pgood
                        ? 'Ready'
                        : 'Waiting for PGOOD'
                      : 'Requested, output still off'
                    : 'Safe off'}
                </strong>
              </div>
            </div>
            <div className="button-row">
              <button
                type="button"
                className="action-button is-inline is-accent"
                disabled={writesDisabled || rail.requested}
                title={`Request ${rail.label} on in service mode. Beam outputs remain forced safe.`}
                onClick={() => {
                  void setSupplyEnable(rail.key, true)
                }}
              >
                Enable {rail.key === 'ld' ? 'LD' : 'TEC'} rail
              </button>
              <button
                type="button"
                className="action-button is-inline"
                disabled={writesDisabled || !rail.requested}
                title={`Clear the ${rail.label} service override and force the rail back off.`}
                onClick={() => {
                  void setSupplyEnable(rail.key, false)
                }}
              >
                Disable {rail.key === 'ld' ? 'LD' : 'TEC'} rail
              </button>
            </div>
          </article>
        ))}
      </div>
    )
  }

  function renderWorkflowPage() {
    return (
      <div className="bringup-page-grid">
        <article className="panel-cutout bringup-hero">
          <div className="cutout-head">
            <Wrench size={16} />
            <strong>Bring-up start guide</strong>
          </div>
          <p className="panel-note">
            Keep this simple: pick only the hardware that is physically installed,
            read identity registers first, and only use service mode when you want
            to write settings back to the controller. Read-only probes do not need
            service mode.
          </p>

          <div className="bringup-hero__meter">
            <div>
              <span className="eyebrow">Selected modules</span>
              <strong>
                {overallProgress}% readiness for the modules you have marked as installed
              </strong>
            </div>
            <ProgressMeter
              value={overallProgress}
              tone={overallProgress >= 80 ? 'steady' : overallProgress >= 50 ? 'warning' : 'critical'}
            />
          </div>

          <div className="bringup-step-list">
            <div className="bringup-step-list__item">
              <strong>1. Connect the board</strong>
              <p>Use Web Serial or Wireless for the real unit, or Mock rig if you just want to learn the flow.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>2. Choose one module page</strong>
              <p>IMU, DAC, PD, haptic, ToF, trigger, TEC, and laser driver can all be tackled independently.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>3. Mark only installed hardware</strong>
              <p>If a board or IC is not populated yet, leave it unplanned so the UI does not pretend it should respond.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>4. Probe first, tune second</strong>
              <p>Run WHO_AM_I or simple register reads first. Only after a successful probe should you apply tuning.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>5. Use service mode only for writes</strong>
              <p>Read-only probes and bench planning work without service mode. Use a write session only for hardware writes and staged tuning.</p>
            </div>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Radio size={16} />
            <strong>Connection and service mode</strong>
          </div>

          <div className="status-badges">
            <span className={connected ? 'status-badge is-on' : 'status-badge'}>
              Link {connected ? 'live' : 'offline'}
            </span>
            <span
              className={
                serviceModeActive
                  ? 'status-badge is-on'
                  : serviceModeRequested
                    ? 'status-badge is-warn'
                    : 'status-badge'
              }
            >
              Service {serviceModeActive ? 'active' : serviceModeRequested ? 'requesting' : 'off'}
            </span>
            <span className={serviceModeActive ? 'status-badge is-on' : 'status-badge is-warn'}>
              Writes {serviceModeActive ? 'armed' : 'read only'}
            </span>
            <span className="status-badge is-on">Local plan auto-saved</span>
          </div>

          <p className="panel-note">
            If you only want live status or register reads, stay in read-only mode. Service mode is just the guarded write session for staged hardware config and register writes.
          </p>

          {deploymentLocked ? (
            <div className="note-strip">
              <span>
                Deployment mode is active. Bring-up telemetry stays visible, but every bring-up write path is locked until deployment mode is exited.
              </span>
            </div>
          ) : null}

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={!commandReady || serviceModeActive || operation !== null}
              title="Request service mode with beam permission held off."
              onClick={() => {
                void requestServiceMode()
              }}
            >
              Enter service mode
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={!commandReady || !serviceModeActive || operation !== null}
              title="Return the controller to normal safe supervision."
              onClick={() =>
                void runCommandSequence(
                  'Exit write session',
                  'Service mode exited.',
                  [
                    {
                      detail: 'Leaving service mode...',
                      cmd: 'exit_service_mode',
                      risk: 'service',
                      note: 'Return the controller to normal safe supervision.',
                    },
                  ],
                )
              }
            >
              Exit service mode
            </button>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <ShieldAlert size={16} />
            <strong>Runtime safety policy</strong>
          </div>

          <p className="panel-note">
            Keep operational control in the Control page. Use this service page for all threshold, hysteresis, and timeout policy that changes how the firmware supervises the bench. Module pages may show those live values, but they should not own safety edits.
          </p>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={operation !== null}
              title="Overwrite the staged safety policy with the latest live controller snapshot."
              onClick={syncSafetyPolicyFromSnapshot}
            >
              Sync live policy
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the staged runtime safety policy. A write session will be requested automatically if needed."
              onClick={() => {
                void applySafetyPolicy()
              }}
            >
              Apply safety policy
            </button>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Horizon threshold (deg)"
                help="Beam pitch angle above which the horizon interlock trips."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyHorizonThresholdDeg}
                title="Beam pitch angle above which the horizon interlock trips."
                onChange={(event) => patchForm('safetyHorizonThresholdDeg', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Horizon hysteresis (deg)"
                help="Clear margin below the trip threshold for the horizon interlock."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyHorizonHysteresisDeg}
                title="Clear margin below the trip threshold for the horizon interlock."
                onChange={(event) => patchForm('safetyHorizonHysteresisDeg', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF min range (m)"
                help="Minimum permitted distance before the range interlock trips."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTofMinRangeM}
                title="Minimum permitted distance before the range interlock trips."
                onChange={(event) => patchForm('safetyTofMinRangeM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF max range (m)"
                help="Maximum permitted distance before the range interlock trips."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTofMaxRangeM}
                title="Maximum permitted distance before the range interlock trips."
                onChange={(event) => patchForm('safetyTofMaxRangeM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF hysteresis (m)"
                help="Clear margin used after a ToF trip."
              />
              <input
                type="number"
                step="0.005"
                value={form.safetyTofHysteresisM}
                title="Clear margin used after a ToF trip."
                onChange={(event) => patchForm('safetyTofHysteresisM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="IMU stale timeout (ms)"
                help="Maximum age of IMU data before it is treated unsafe."
              />
              <input
                type="number"
                step="1"
                value={form.safetyImuStaleMs}
                title="Maximum age of IMU data before it is treated unsafe."
                onChange={(event) => patchForm('safetyImuStaleMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF stale timeout (ms)"
                help="Maximum age of ToF data before it is treated unsafe."
              />
              <input
                type="number"
                step="1"
                value={form.safetyTofStaleMs}
                title="Maximum age of ToF data before it is treated unsafe."
                onChange={(event) => patchForm('safetyTofStaleMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Rail-good timeout (ms)"
                help="How long a commanded rail can miss PGOOD before the controller faults."
              />
              <input
                type="number"
                step="1"
                value={form.safetyRailGoodTimeoutMs}
                title="How long a commanded rail can miss PGOOD before the controller faults."
                onChange={(event) => patchForm('safetyRailGoodTimeoutMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda drift limit (nm)"
                help="Maximum absolute wavelength drift before NIR is blocked."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyLambdaDriftLimitNm}
                title="Maximum absolute wavelength drift before NIR is blocked."
                onChange={(event) => patchForm('safetyLambdaDriftLimitNm', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda hysteresis (nm)"
                help="Recovery margin required before a lambda drift trip clears."
              />
              <input
                type="number"
                step="0.05"
                value={form.safetyLambdaDriftHysteresisNm}
                title="Recovery margin required before a lambda drift trip clears."
                onChange={(event) => patchForm('safetyLambdaDriftHysteresisNm', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda hold (ms)"
                help="Persistence window before lambda drift changes the interlock state."
              />
              <input
                type="number"
                step="1"
                value={form.safetyLambdaDriftHoldMs}
                title="Persistence window before lambda drift changes the interlock state."
                onChange={(event) => patchForm('safetyLambdaDriftHoldMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="LD overtemp (°C)"
                help="Laser driver temperature limit before a major fault is raised."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyLdOvertempLimitC}
                title="Laser driver temperature limit before a major fault is raised."
                onChange={(event) => patchForm('safetyLdOvertempLimitC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC trip (V)"
                help="Trip voltage on the TEC temperature ADC monitor."
              />
              <input
                type="number"
                step="0.001"
                value={form.safetyTecTempAdcTripV}
                title="Trip voltage on the TEC temperature ADC monitor."
                onChange={(event) => patchForm('safetyTecTempAdcTripV', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC hysteresis (V)"
                help="Recovery margin below the ADC trip threshold."
              />
              <input
                type="number"
                step="0.001"
                value={form.safetyTecTempAdcHysteresisV}
                title="Recovery margin below the ADC trip threshold."
                onChange={(event) => patchForm('safetyTecTempAdcHysteresisV', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC hold (ms)"
                help="Persistence window before the TEC ADC thermal trip changes state."
              />
              <input
                type="number"
                step="1"
                value={form.safetyTecTempAdcHoldMs}
                title="Persistence window before the TEC ADC thermal trip changes state."
                onChange={(event) => patchForm('safetyTecTempAdcHoldMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC min command (°C)"
                help="Lower software clamp for TEC target commands."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyTecMinCommandC}
                title="Lower software clamp for TEC target commands."
                onChange={(event) => patchForm('safetyTecMinCommandC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC max command (°C)"
                help="Upper software clamp for TEC target commands."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyTecMaxCommandC}
                title="Upper software clamp for TEC target commands."
                onChange={(event) => patchForm('safetyTecMaxCommandC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC ready tolerance (°C)"
                help="Tolerance around the TEC target used for ready and settled status."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTecReadyToleranceC}
                title="Tolerance around the TEC target used for ready and settled status."
                onChange={(event) => patchForm('safetyTecReadyToleranceC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Max laser current (A)"
                help="Maximum host-stageable laser current."
              />
              <input
                type="number"
                step="0.05"
                value={form.safetyMaxLaserCurrentA}
                title="Maximum host-stageable laser current."
                onChange={(event) => patchForm('safetyMaxLaserCurrentA', event.target.value)}
              />
            </label>
          </div>

          <div className="status-badges">
            <span className={snapshot.safety.horizonBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Compass size={14} />
              Horizon {snapshot.safety.horizonBlocked ? 'blocked' : 'clear'}
            </span>
            <span className={snapshot.safety.distanceBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Activity size={14} />
              Range {snapshot.safety.distanceBlocked ? 'blocked' : 'clear'}
            </span>
            <span className={snapshot.safety.lambdaDriftBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Radio size={14} />
              Lambda {snapshot.safety.lambdaDriftBlocked ? 'drifting' : 'stable'}
            </span>
            <span className={snapshot.safety.tecTempAdcBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Thermometer size={14} />
              Temp ADC {snapshot.safety.tecTempAdcBlocked ? 'high' : 'clear'}
            </span>
          </div>
        </article>

        <article className="panel-cutout bringup-module-overview">
          <div className="cutout-head">
            <Cpu size={16} />
            <strong>Choose a module</strong>
          </div>
          <p className="panel-note">
            Start with the subsystem you actually have wired. Bus peripherals validate through direct probes, while analog and GPIO paths stay live through readback monitoring instead of fake probe stages.
          </p>
          <div className="bringup-roster">
            {moduleKeys.map((module) => {
              const meta = moduleMeta[module]
              const score = moduleScore(module, displayModules[module], connected)
              const summary = moduleSummary(module, displayModules[module], connected)

              return (
                <button
                  key={module}
                  type="button"
                  className="bringup-roster__item"
                  title={`Open the ${meta.label} bring-up page.`}
                  onClick={() => setActivePage(module)}
                >
                  <div>
                    <strong>{meta.label}</strong>
                    <p>{summary.detail}</p>
                  </div>
                  <div className="bringup-roster__meter">
                    <span>{score.label}</span>
                    <ProgressMeter value={score.progress} tone={score.tone} compact />
                  </div>
                </button>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Save size={16} />
            <strong>Optional plan tools</strong>
          </div>

          <p className="panel-note">
            The browser keeps this plan automatically. Use the controls below if you want to restore a previous local draft or mirror the full plan into the controller.
          </p>

          <div className="field">
            <div className="field__head">
              <FieldLabel
                label="Profile name"
                help="Short bench profile name mirrored into the service snapshot and host draft."
              />
              <strong>rev {snapshot.bringup.profileRevision}</strong>
            </div>
            <div className="field__pair">
              <input
                type="text"
                maxLength={24}
                value={form.profileName}
                title="Rename the active bring-up profile."
                onChange={(event) => patchForm('profileName', event.target.value)}
              />
              <button
                type="button"
                className="action-button is-inline"
                disabled={operation !== null}
                title="Save the profile name to the controller. This does not require service mode."
                onClick={() =>
                  void saveProfileName()
                }
              >
                Save name
              </button>
            </div>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              title="Save the full bring-up form into browser-local storage."
              onClick={saveHostDraft}
            >
              Save local draft
            </button>
            <button
              type="button"
              className="action-button is-inline"
              title="Restore the last browser-local bring-up draft."
              onClick={restoreHostDraft}
            >
              Load local draft
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={!commandReady || operation !== null}
              title="Push the entire staged bring-up draft to the controller. A write session will be requested automatically only when needed."
              onClick={() => {
                void pushDraftToDevice()
              }}
            >
              Push full plan
            </button>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={!commandReady || operation !== null}
              title="Ask the current bench firmware to persist the active bring-up profile, including runtime safety thresholds and hold-time policy. Service mode is not required."
              onClick={() =>
                void runCommandSequence(
                  'Save bring-up profile',
                  'Bring-up profile save requested from the controller.',
                  [
                    {
                      detail: 'Requesting device-side profile save...',
                      cmd: 'save_bringup_profile',
                      risk: 'write',
                      note: 'Request device-side persistence for the active bring-up profile, including runtime safety policy.',
                    },
                  ],
                )
              }
            >
              Attempt device save
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled || operation !== null}
              title="Refresh the live bring-up profile from the controller."
              onClick={() =>
                void refreshLiveStatus(true, true)
              }
            >
              Refresh live plan
            </button>
            <button
              type="button"
              className="action-button is-inline"
              title="Overwrite the form with the latest live snapshot."
              onClick={syncFormFromSnapshot}
            >
              Load live into form
            </button>
          </div>

          <div className="note-strip">
            <span>{draftNote}</span>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Cable size={16} />
            <strong>Bus tools moved</strong>
          </div>
          <p className="panel-note">
            Bus Lab, decoded I2C and SPI traffic, and device identity workspace now
            live in the top-level Tools page so module bring-up stays focused on
            installation, probing, and tuning.
          </p>
          <div className="status-badges">
            <span className="status-badge is-on">Use Tools for bus scan and register reads</span>
            <span className="status-badge is-warn">Writes still need service mode</span>
          </div>
        </article>
      </div>
    )
  }

  function renderImuPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('imu')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Compass size={16} />
            <strong>Runtime sampling</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Output data rate"
                help="Datasheet CTRL1_XL and CTRL2_G operating data-rate selection. Start around 104 to 208 Hz for bench bring-up."
              />
              <select
                value={form.imuOdrHz}
                title="Select the IMU output data rate to stage into service mode."
                onChange={(event) => patchForm('imuOdrHz', event.target.value)}
              >
                {imuOdrOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} Hz
                  </option>
                ))}
              </select>
            </label>

            <label className="field">
              <FieldLabel
                label="Accel full scale"
                help="CTRL1_XL full-scale range for the accelerometer path."
              />
              <select
                value={form.imuAccelRangeG}
                title="Select the accelerometer full-scale range."
                onChange={(event) => patchForm('imuAccelRangeG', event.target.value)}
              >
                {imuAccelRangeOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} g
                  </option>
                ))}
              </select>
            </label>

            <label className="field">
              <FieldLabel
                label="Gyro full scale"
                help="CTRL2_G full-scale range for the gyroscope path."
              />
              <select
                value={form.imuGyroRangeDps}
                title="Select the gyroscope full-scale range."
                onChange={(event) => patchForm('imuGyroRangeDps', event.target.value)}
              >
                {imuGyroRangeOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} dps
                  </option>
                ))}
              </select>
            </label>
          </div>

          <div className="bringup-toggle-grid">
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuGyroEnabled}
                title="Keep the gyroscope enabled during bring-up."
                onChange={(event) => patchForm('imuGyroEnabled', event.target.checked)}
              />
              <span>Gyroscope enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuLpf2Enabled}
                title="Route accelerometer output through LPF2 when staged."
                onChange={(event) => patchForm('imuLpf2Enabled', event.target.checked)}
              />
              <span>LPF2 enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuTimestampEnabled}
                title="Enable the timestamp counter for freshness work."
                onChange={(event) => patchForm('imuTimestampEnabled', event.target.checked)}
              />
              <span>Timestamp counter enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuBduEnabled}
                title="Block-data-update keeps multi-byte samples coherent across reads."
                onChange={(event) => patchForm('imuBduEnabled', event.target.checked)}
              />
              <span>BDU enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuIfIncEnabled}
                title="IF_INC enables multi-byte auto-increment during SPI register access."
                onChange={(event) => patchForm('imuIfIncEnabled', event.target.checked)}
              />
              <span>Address auto-increment enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuI2cDisabled}
                title="Disable the I2C block in CTRL4_C after SPI verification."
                onChange={(event) => patchForm('imuI2cDisabled', event.target.checked)}
              />
              <span>I2C block disabled</span>
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the full IMU runtime configuration. A write session will be requested automatically if needed."
              onClick={() => {
                void applyImuProfile()
              }}
            >
              Apply IMU tuning
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read WHO_AM_I over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU WHO_AM_I',
                  'imu',
                  0x0f,
                  'Read the LSM6DSO WHO_AM_I register over SPI.',
                )
              }}
            >
              Read WHO_AM_I
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read CTRL3_C over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU CTRL3_C',
                  'imu',
                  0x12,
                  'Read the LSM6DSO CTRL3_C register over SPI.',
                )
              }}
            >
              Read CTRL3_C
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read CTRL4_C over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU CTRL4_C',
                  'imu',
                  0x13,
                  'Read the LSM6DSO CTRL4_C register over SPI.',
                )
              }}
            >
              Read CTRL4_C
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.imu.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Configured in silicon</span>
              <strong>{snapshot.peripherals.imu.configured ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>WHO_AM_I</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.whoAmI, 2)}</strong>
            </div>
            <div>
              <span>STATUS_REG</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.statusReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL1_XL</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl1XlReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL2_G</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl2GReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL3_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl3CReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL4_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl4CReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL10_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl10CReg, 2)}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.imu.lastErrorCode,
                  snapshot.peripherals.imu.lastError,
                )}
              </strong>
            </div>
          </div>

          <p className="inline-help">
            This block is actual IMU peripheral readback from the LSM6DSO over SPI.
            It is not the staged host tuning form.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastSpiOp}</p>
        </article>

        <ImuPostureCard snapshot={snapshot} telemetryStore={telemetryStore} />
      </div>
    )
  }

  function renderDacPage() {
    const dacInternalReferenceSelected = form.dacReferenceMode === 'internal'

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('dac')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Reference and output policy</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Reference source"
                help="DAC80502 supports internal or external reference selection. Default to internal during first bring-up."
              />
              <select
                value={form.dacReferenceMode}
                title="Choose the DAC reference source."
                onChange={(event) =>
                  patchForm('dacReferenceMode', event.target.value as DacReferenceMode)
                }
              >
                {dacReferenceOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{dacReferenceOptions.find((option) => option.value === form.dacReferenceMode)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Update mode"
                help="Asynchronous updates act immediately on I2C write. Synchronous mode waits for a later trigger or LDAC-style commit."
              />
              <select
                value={form.dacSyncMode}
                title="Choose whether DAC outputs update asynchronously or synchronously."
                onChange={(event) =>
                  patchForm('dacSyncMode', event.target.value as DacSyncMode)
                }
              >
                {dacSyncModeOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{dacSyncModeOptions.find((option) => option.value === form.dacSyncMode)?.detail}</small>
            </label>
          </div>

          <div className="bringup-toggle-grid">
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.dacGain2x}
                title="Use the DAC output buffer gain x2 option."
                onChange={(event) => patchForm('dacGain2x', event.target.checked)}
              />
              <span>Buffer gain x2</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={dacInternalReferenceSelected ? true : form.dacRefDiv}
                disabled={dacInternalReferenceSelected}
                title={
                  dacInternalReferenceSelected
                    ? 'REF-DIV is forced on with the internal 2.5 V reference on this 3.3 V board.'
                    : 'Enable the reference divide-by-two path for span scaling.'
                }
                onChange={(event) => patchForm('dacRefDiv', event.target.checked)}
              />
              <span>Reference divider enabled</span>
            </label>
          </div>

          <p className="inline-help">
            Board-valid DAC output span on this PCB is `0.0-2.5 V` for both channels.
            `DAC_OUTA` drives laser `LISH`; `DAC_OUTB` drives TEC `TMS`.
          </p>
          <p className="inline-help">
            The Laser driver and TEC bring-up pages now edit these same channel A/B
            draft values. There is only one DAC shadow plan in the host.
          </p>
          {dacInternalReferenceSelected ? (
            <p className="inline-help">
              On this `3.3 V` board, the DAC80502 internal `2.5 V` reference requires
              `REF-DIV` enabled. The controller auto-enforces that rule; if it is violated,
              `REF-ALARM` forces both outputs to `0 V`.
            </p>
          ) : null}

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="LD shadow voltage"
                help="Service-only shadow voltage for the laser-current DAC channel. Board range is 0.0 V to 2.5 V."
              />
              <input
                value={form.dacLdVoltage}
                title="Stage the laser-current DAC shadow voltage in volts."
                onChange={(event) => patchForm('dacLdVoltage', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="TEC shadow voltage"
                help="Service-only shadow voltage for the TEC target DAC channel. Board range is 0.0 V to 2.5 V."
              />
              <input
                value={form.dacTecVoltage}
                title="Stage the TEC-target DAC shadow voltage in volts."
                onChange={(event) => patchForm('dacTecVoltage', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Push the full DAC bring-up profile and both channel shadow voltages."
              onClick={() => {
                void applyDacProfile()
              }}
            >
              Apply DAC profile
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC SYNC register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC SYNC',
                  0x48,
                  0x02,
                  'Read the DAC80502 SYNC register.',
                )
              }}
            >
              Read SYNC
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC CONFIG register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC CONFIG',
                  0x48,
                  0x03,
                  'Read the DAC80502 CONFIG register.',
                )
              }}
            >
              Read CONFIG
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC STATUS register to check REF-ALARM."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC STATUS',
                  0x48,
                  0x07,
                  'Read the DAC80502 STATUS register. REF-ALARM=1 means the reference headroom is invalid and all outputs are forced to 0 V.',
                )
              }}
            >
              Read STATUS
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC GAIN register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC GAIN',
                  0x48,
                  0x04,
                  'Read the DAC80502 GAIN register.',
                )
              }}
            >
              Read GAIN
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC channel A data register that drives DAC_OUTA / LISH."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC LD DATA',
                  0x48,
                  0x08,
                  'Read the DAC80502 channel A data register that drives DAC_OUTA toward the laser current command input.',
                )
              }}
            >
              Read LD DATA
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC channel B data register that drives DAC_OUTB / TMS."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC TEC DATA',
                  0x48,
                  0x09,
                  'Read the DAC80502 channel B data register that drives DAC_OUTB toward the TEC target input.',
                )
              }}
            >
              Read TEC DATA
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.dac.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Configured in silicon</span>
              <strong>{snapshot.peripherals.dac.configured ? 'Yes' : 'No'}</strong>
            </div>
            <div className={snapshot.peripherals.dac.refAlarm ? 'is-critical-glow' : undefined}>
              <span>REF_ALARM</span>
              <strong>{snapshot.peripherals.dac.refAlarm ? 'Asserted' : 'Clear'}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.dac.lastErrorCode,
                  snapshot.peripherals.dac.lastError,
                )}
              </strong>
            </div>
            <div>
              <span>SYNC</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.syncReg)}</strong>
            </div>
            <div>
              <span>CONFIG</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.configReg)}</strong>
            </div>
            <div>
              <span>GAIN</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.gainReg)}</strong>
            </div>
            <div>
              <span>STATUS</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.statusReg)}</strong>
            </div>
            <div>
              <span>DATA A / LD</span>
              <strong>
                {formatDacRegisterEquivalent(snapshot.peripherals.dac.dataAReg)} ·{' '}
                {formatRegisterHex(snapshot.peripherals.dac.dataAReg)}
              </strong>
            </div>
            <div>
              <span>DATA B / TEC</span>
              <strong>
                {formatDacRegisterEquivalent(snapshot.peripherals.dac.dataBReg)} ·{' '}
                {formatRegisterHex(snapshot.peripherals.dac.dataBReg)}
              </strong>
            </div>
          </div>

          <p className="inline-help">
            These DAC fields are actual readback from DAC80502 registers. The voltage
            figures decode the register code against this board&apos;s `0.0-2.5 V` DAC
            span; they show the commanded DAC code, not a downstream analog measurement.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Cpu size={16} />
            <strong>Safe-start note</strong>
          </div>
          <p className="inline-help">
            This page writes the real DAC80502 output registers. `DAC_OUTA` feeds the
            laser driver `LISH` command input and `DAC_OUTB` feeds the TEC controller
            `TMS` input. Keep both actuator rails disabled while proving those analog
            command voltages so the downstream laser and TEC power stages stay off.
          </p>
          <div className="bringup-fact-grid">
            <div>
              <span>Staged LD setpoint</span>
              <strong>{formatNumber(snapshot.bringup.tuning.dacLdChannelV, 3)} V</strong>
            </div>
            <div>
              <span>Staged TEC setpoint</span>
              <strong>{formatNumber(snapshot.bringup.tuning.dacTecChannelV, 3)} V</strong>
            </div>
          </div>
        </article>
      </div>
    )
  }

  function renderHapticPage() {
    const externalPatternModeSelected =
      form.hapticMode === 'external_edge' || form.hapticMode === 'external_level'
    const previewPulseCount = clampInteger(
      parseNumber(hapticPattern.pulseCount, 3),
      1,
      BRINGUP_HAPTIC_PATTERN_MAX_PULSES,
    )
    const previewHighMs = clampInteger(
      parseNumber(hapticPattern.highMs, 45),
      BRINGUP_HAPTIC_PATTERN_MIN_MS,
      BRINGUP_HAPTIC_PATTERN_MAX_MS,
    )
    const previewLowMs = clampInteger(
      parseNumber(hapticPattern.lowMs, 90),
      BRINGUP_HAPTIC_PATTERN_MIN_MS,
      BRINGUP_HAPTIC_PATTERN_MAX_MS,
    )
    const previewTotalMs = previewPulseCount * (previewHighMs + previewLowMs)
    const previewSegments = Array.from({ length: previewPulseCount * 2 }, (_, index) => {
      const active = index % 2 === 0
      const durationMs = active ? previewHighMs : previewLowMs
      return {
        key: `${active ? 'high' : 'low'}-${index}`,
        active,
        durationMs,
        widthPercent: (durationMs / (previewTotalMs || 1)) * 100,
      }
    })

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('haptic')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Gamepad2 size={16} />
            <strong>Mode and actuator</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Operating mode"
                help="DRV2605 MODE[2:0] selection. Start with internal trigger or RTP during bring-up."
              />
              <select
                value={form.hapticMode}
                title="Choose the DRV2605 operating mode to stage."
                onChange={(event) =>
                  patchForm('hapticMode', event.target.value as HapticMode)
                }
              >
                {hapticModeOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{hapticModeOptions.find((option) => option.value === form.hapticMode)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Actuator type"
                help="Select ERM or LRA so the feedback control and library assumptions stay aligned."
              />
              <select
                value={form.hapticActuator}
                title="Select the haptic actuator type."
                onChange={(event) =>
                  patchForm(
                    'hapticActuator',
                    event.target.value as HapticActuator,
                  )
                }
              >
                {hapticActuatorOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{hapticActuatorOptions.find((option) => option.value === form.hapticActuator)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Library"
                help="ROM library choice. Libraries 1 to 5 are ERM families, 6 is the LRA library."
              />
              <select
                value={form.hapticLibrary}
                title="Choose the DRV2605 ROM library."
                onChange={(event) => patchForm('hapticLibrary', event.target.value)}
              >
                {hapticLibraryOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
            </label>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Effect ID"
                help="Waveform sequence register value. Use 1 to 127 for library effects."
              />
              <input
                value={form.hapticEffectId}
                title="Stage the primary DRV2605 effect ID."
                onChange={(event) => patchForm('hapticEffectId', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="RTP level"
                help="Real-time playback amplitude register value from 0 to 255."
              />
              <input
                value={form.hapticRtpLevel}
                title="Stage the DRV2605 RTP amplitude byte."
                onChange={(event) => patchForm('hapticRtpLevel', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled || snapshot.peripherals.haptic.enablePinHigh}
              title="Assert the dedicated ERM driver enable pin on GPIO48 in service mode."
              onClick={() => {
                void setHapticEnable(true)
              }}
            >
              Enable ERM EN
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={writesDisabled || !snapshot.peripherals.haptic.enablePinHigh}
              title="Force the dedicated ERM driver enable pin on GPIO48 low."
              onClick={() => {
                void setHapticEnable(false)
              }}
            >
              Disable ERM EN
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the full DRV2605 configuration. A write session will be requested automatically if needed."
              onClick={() => {
                void applyHapticProfile()
              }}
            >
              Apply haptic profile
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={writesDisabled || !snapshot.peripherals.haptic.enablePinHigh}
              title="Fire the staged haptic test effect. GPIO48 must already be high for the ERM driver path."
              onClick={() =>
                void runCommandSequence(
                  'Fire haptic test',
                  'Haptic test command issued.',
                  [
                    {
                      detail: 'Firing haptic test...',
                      cmd: 'haptic_debug_fire',
                      risk: 'service',
                      note: 'Fire the selected haptic test effect.',
                      requireService: true,
                    },
                  ],
                )
              }
            >
              Fire haptic test
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DRV2605 mode register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DRV2605 MODE',
                  0x5a,
                  0x01,
                  'Read the DRV2605 MODE register.',
                )
              }}
            >
              Read MODE
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DRV2605 GO register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DRV2605 GO',
                  0x5a,
                  0x0c,
                  'Read the DRV2605 GO register.',
                )
              }}
            >
              Read GO
            </button>
          </div>

          <div
            className={`haptic-pattern-lab ${externalPatternModeSelected ? 'is-armed' : ''}`}
          >
            <div className="haptic-pattern-lab__head">
              <div>
                <span className="eyebrow">External trigger lab</span>
                <strong>Pattern designer for ERM IN/TRIG bursts</strong>
              </div>
              <div className="status-badges">
                <span
                  className={`status-badge ${
                    externalPatternModeSelected ? 'is-warn' : 'is-off'
                  }`}
                >
                  {externalPatternModeSelected
                    ? 'External trigger mode staged'
                    : 'Select external edge or level mode'}
                </span>
                <span className="status-badge is-muted">
                  {previewPulseCount} pulse{previewPulseCount === 1 ? '' : 's'} · {previewTotalMs} ms
                </span>
              </div>
            </div>

            <div className="segmented haptic-pattern-lab__presets">
              <button
                type="button"
                className="segmented__button"
                onClick={() => applyHapticPatternPreset(1, 30, 120)}
              >
                Tick
              </button>
              <button
                type="button"
                className="segmented__button"
                onClick={() => applyHapticPatternPreset(3, 40, 90)}
              >
                Triple
              </button>
              <button
                type="button"
                className="segmented__button"
                onClick={() => applyHapticPatternPreset(6, 28, 55)}
              >
                Buzz
              </button>
            </div>

            <div className="field-grid">
              <label className="field">
                <FieldLabel
                  label="Pulse count"
                  help="Number of high-going trigger bursts to drive on the shared IO37 net."
                />
                <input
                  value={hapticPattern.pulseCount}
                  title="Set the number of trigger pulses."
                  onChange={(event) => patchHapticPattern('pulseCount', event.target.value)}
                />
              </label>

              <label className="field">
                <FieldLabel
                  label="High time (ms)"
                  help="Duration that IO37 stays high for each pulse."
                />
                <input
                  value={hapticPattern.highMs}
                  title="Set the trigger high time in milliseconds."
                  onChange={(event) => patchHapticPattern('highMs', event.target.value)}
                />
              </label>

              <label className="field">
                <FieldLabel
                  label="Low gap (ms)"
                  help="Low-time gap between trigger pulses."
                />
                <input
                  value={hapticPattern.lowMs}
                  title="Set the trigger low gap in milliseconds."
                  onChange={(event) => patchHapticPattern('lowMs', event.target.value)}
                />
              </label>
            </div>

            <div className="haptic-pattern-lab__preview" aria-hidden="true">
              {previewSegments.map((segment) => (
                <span
                  key={segment.key}
                  className={segment.active ? 'is-active' : 'is-gap'}
                  style={{ width: `${segment.widthPercent}%` }}
                />
              ))}
            </div>

            <div className="haptic-pattern-lab__facts">
              <div>
                <span>High</span>
                <strong>{previewHighMs} ms</strong>
              </div>
              <div>
                <span>Gap</span>
                <strong>{previewLowMs} ms</strong>
              </div>
              <div>
                <span>Release</span>
                <strong>{hapticPattern.releaseAfter ? 'Firmware after burst' : 'Hold low override'}</strong>
              </div>
            </div>

            <label className="toggle-line haptic-pattern-lab__toggle">
              <input
                type="checkbox"
                checked={hapticPattern.releaseAfter}
                onChange={(event) =>
                  patchHapticPattern('releaseAfter', event.target.checked)
                }
              />
              <span>Release IO37 back to firmware after the burst completes.</span>
            </label>

            <label className="toggle-line haptic-pattern-lab__toggle is-danger">
              <input
                type="checkbox"
                checked={hapticPattern.hazardAccepted}
                onChange={(event) =>
                  patchHapticPattern('hazardAccepted', event.target.checked)
                }
              />
              <span>
                I understand IO37 is shared with `GN_LD_EN`, so this burst is bench-only and may
                assert the visible-laser enable net.
              </span>
            </label>

            <div className="button-row is-compact">
              <button
                type="button"
                className="action-button is-inline is-accent"
                disabled={
                  writesDisabled ||
                  !snapshot.peripherals.haptic.enablePinHigh ||
                  !externalPatternModeSelected ||
                  !hapticPattern.hazardAccepted
                }
                title="Apply the staged external-trigger profile and run the designed IO37 burst."
                onClick={() => {
                  void runHapticExternalPattern()
                }}
              >
                Run external trigger pattern
              </button>
            </div>

            <p className="inline-help">
              This uses the staged DRV2605 external mode and then drives a bounded service-only
              pulse train on the shared `IO37 / GN_LD_EN` net. Keep the visible alignment path
              terminated and only use it on an intentional bench setup.
            </p>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.haptic.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>ERM EN GPIO48</span>
              <strong>{snapshot.peripherals.haptic.enablePinHigh ? 'High' : 'Low'}</strong>
            </div>
            <div>
              <span>Shared TRIG / green net</span>
              <strong>{snapshot.peripherals.haptic.triggerPinHigh ? 'High' : 'Low'}</strong>
            </div>
            <div>
              <span>MODE</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.modeReg, 2)}</strong>
            </div>
            <div>
              <span>LIBRARY</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.libraryReg, 2)}</strong>
            </div>
            <div>
              <span>GO</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.goReg, 2)}</strong>
            </div>
            <div>
              <span>FEEDBACK</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.feedbackReg, 2)}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.haptic.lastErrorCode,
                  snapshot.peripherals.haptic.lastError,
                )}
              </strong>
            </div>
          </div>

          <p className="inline-help">
            These fields are actual DRV2605 register readback from the peripheral, not
            just staged host settings.
          </p>
          <p className="inline-help">
            `GPIO48` is the dedicated ERM driver enable pin. For bench tests, assert
            `ERM EN`, apply the DRV2605 profile, then fire `GO`.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Waves size={16} />
            <strong>Board note</strong>
          </div>
          <p className="inline-help">
            `IO37` still appears shared between DRV2605 trigger and the green-laser
            net. This page now shows that shared level for visibility, but the primary
            ERM bring-up path is the dedicated `GPIO48` enable plus I2C `GO`.
          </p>
        </article>
      </div>
    )
  }

  function renderTofPage() {
    const liveIllumination = snapshot.bringup.illumination.tof
    const stagedIlluminationDuty = Math.max(
      0,
      Math.min(
        100,
        Math.round(
          parseNumber(
            tofIlluminationDutyPct,
            liveIllumination.dutyCyclePct > 0 ? liveIllumination.dutyCyclePct : 35,
          ),
        ),
      ),
    )

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('tof')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Waves size={16} />
            <strong>VL53L1X board status</strong>
          </div>
          <p className="panel-note">
            This board is now modeled as a shared-I2C `VL53L1X` daughtercard.
            Firmware probes `0x29` on `GPIO4/GPIO5`, treats `GPIO7` as the optional
            `GPIO1` interrupt input, and holds `GPIO6` low so the onboard LED-driver
            control path stays inactive unless it is intentionally exercised.
          </p>

          <div className="bringup-fact-grid">
            <div>
              <span>Safe window</span>
              <strong>
                {formatNumber(snapshot.safety.tofMinRangeM, 2)}-{formatNumber(snapshot.safety.tofMaxRangeM, 2)} m
              </strong>
            </div>
            <div>
              <span>Stale timeout</span>
              <strong>{snapshot.safety.tofStaleMs} ms</strong>
            </div>
            <div>
              <span>Hysteresis</span>
              <strong>{formatNumber(snapshot.safety.tofHysteresisM, 3)} m</strong>
            </div>
            <div>
              <span>Policy owner</span>
              <strong>Service tab</strong>
            </div>
          </div>

          <p className="inline-help">
            ToF distance limits and stale-data timeout are runtime safety policy, not
            module-local tuning. Edit them only in <code>Bring-up -&gt; Service</code>, where the
            full interlock policy is staged together.
          </p>

          <div className="panel-cutout bringup-illumination-card">
            <div className="cutout-head">
              <Activity size={16} />
              <strong>Front illumination</strong>
            </div>
            <p className="inline-help">
              The front visible LED path uses the TPS61169 `CTRL` pin on `GPIO6`.
              Firmware keeps that line low by default, and only drives a service-only
              `20 kHz` PWM dimming signal here while service mode is active.
            </p>

            <label className="field">
              <FieldLabel
                label="Brightness duty cycle"
                help="Service-only PWM duty on GPIO6 into TPS61169 CTRL. Zero keeps the illumination path low."
              />
              <input
                type="range"
                min="0"
                max="100"
                step="1"
                value={stagedIlluminationDuty}
                title="Stage the service-only front illumination brightness."
                onChange={(event) => setTofIlluminationDutyPct(event.target.value)}
              />
            </label>

            <div className="bringup-illumination-meter">
              <div>
                <span>Staged duty</span>
                <strong>{stagedIlluminationDuty}%</strong>
              </div>
              <span className={liveIllumination.enabled ? 'status-badge is-on' : 'status-badge is-off'}>
                {liveIllumination.enabled ? 'service PWM active' : 'forced low'}
              </span>
            </div>

            <ProgressMeter
              value={stagedIlluminationDuty}
              tone={liveIllumination.enabled ? 'warning' : 'steady'}
              compact
            />

            <div className="button-row">
              <button
                type="button"
                className="action-button is-inline is-accent"
                disabled={writesDisabled || stagedIlluminationDuty <= 0}
                onClick={() => {
                  void applyTofIllumination(true)
                }}
              >
                Apply front light
              </button>
              <button
                type="button"
                className="action-button is-inline"
                disabled={writesDisabled || !liveIllumination.enabled}
                onClick={() => {
                  void applyTofIllumination(false)
                }}
              >
                Lights off
              </button>
            </div>

            <p className="inline-help">
              TPS61169 brightness dimming is PWM-based on `CTRL`; this bring-up path
              keeps a fixed `20 kHz` carrier and only changes duty cycle so the test
              stays easy to reproduce. Leaving service mode forces the line low again.
            </p>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Service illumination</span>
              <strong>{liveIllumination.enabled ? 'Active' : 'Off'}</strong>
            </div>
            <div>
              <span>Service duty</span>
              <strong>{liveIllumination.dutyCyclePct}%</strong>
            </div>
            <div>
              <span>PWM carrier</span>
              <strong>{formatNumber(liveIllumination.frequencyHz / 1000, 1)} kHz</strong>
            </div>
            <div>
              <span>Controller filtered distance</span>
              <strong>{formatNumber(snapshot.tof.distanceM, 2)} m</strong>
            </div>
            <div>
              <span>Controller validity</span>
              <strong>{snapshot.tof.valid && snapshot.tof.fresh ? 'Fresh + valid' : 'Unsafe / held low'}</strong>
            </div>
            <div>
              <span>Peripheral probe</span>
              <strong>{snapshot.peripherals.tof.reachable ? 'Reachable' : 'No response'}</strong>
            </div>
            <div>
              <span>Sensor ID</span>
              <strong>{`0x${snapshot.peripherals.tof.sensorId.toString(16).toUpperCase().padStart(4, '0')}`}</strong>
            </div>
            <div>
              <span>Boot state register</span>
              <strong>{snapshot.peripherals.tof.bootState}</strong>
            </div>
            <div>
              <span>Configured</span>
              <strong>{snapshot.peripherals.tof.configured ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Data-ready flag</span>
              <strong>{snapshot.peripherals.tof.dataReady ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Range status code</span>
              <strong>{snapshot.peripherals.tof.rangeStatus}</strong>
            </div>
            <div>
              <span>Raw distance register</span>
              <strong>{snapshot.peripherals.tof.distanceMm} mm</strong>
            </div>
            <div>
              <span>GPIO1 interrupt line</span>
              <strong>{snapshot.peripherals.tof.interruptLineHigh ? 'High' : 'Low'}</strong>
            </div>
            <div>
              <span>LED control line</span>
              <strong>{snapshot.peripherals.tof.ledCtrlAsserted ? 'Asserted' : 'Held low'}</strong>
            </div>
            <div>
              <span>Last low-level result</span>
              <strong>{snapshot.peripherals.tof.lastError}</strong>
            </div>
          </div>

          <p className="inline-help">
            These fields should now reflect actual `VL53L1X` peripheral readback:
            probe reachability, boot state, sensor ID, data-ready state, range
            status, and the raw distance register in millimeters.
          </p>
          <p className="inline-help">
            `Controller filtered distance` and `Controller validity` are the safety-path
            values the rest of the GUI uses for interlock cards. `Raw distance register`
            is direct peripheral readback and can jump before the controller accepts it
            as a stable, fresh safety sample.
          </p>
        </article>
      </div>
    )
  }

  function renderPdPage() {
    const displayOrder = [2, 1, 0] as const
    const plannedProfiles = form.pdProfiles.map((profile) => ({
      enabled: profile.enabled,
      voltageV: parseNumber(profile.voltageV, 0),
      currentA: parseNumber(profile.currentA, 0),
    }))
    const liveProfiles = readbackPdProfiles(snapshot)
    const plannedTier = classifyPdTierFromPlan(
      snapshot.pd.negotiatedPowerW,
      snapshot.pd.sourceIsHostOnly,
      parseNumber(
        form.pdProgrammingOnlyMaxW,
        snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMinW,
        snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMaxW,
        snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
      ),
      parseNumber(
        form.pdFullModeMinW,
        snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
      ),
    )
    const matchedProfileIndex =
      snapshot.pd.contractObjectPosition > 0
        ? snapshot.pd.contractObjectPosition - 1
        : -1
    const highestEnabledProfilePowerW = plannedProfiles.reduce((highest, profile) => {
      if (!profile.enabled) {
        return highest
      }

      return Math.max(highest, pdProfilePowerW(profile))
    }, 0)
    const minimumOperationalW = Math.max(
      parseNumber(
        form.pdProgrammingOnlyMaxW,
        snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMinW,
        snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
      ),
    )

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('pd')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <PlugZap size={16} />
            <strong>Live contract</strong>
          </div>
          <p className="panel-note">
            This block is the live contract the firmware is reading from the STUSB4500
            and the current RDO. It is the closest thing to ground truth on the board.
          </p>
          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Force the controller to re-read the STUSB4500 live contract and PDO runtime registers now."
              onClick={() => {
                void refreshPdStatus(true, true)
              }}
            >
              Refresh PD now
            </button>
          </div>
          <div className="bringup-fact-grid">
            <div>
              <span>Negotiated power</span>
              <strong>{formatNumber(snapshot.pd.negotiatedPowerW, 1)} W</strong>
            </div>
            <div>
              <span>Source voltage</span>
              <strong>{formatNumber(snapshot.pd.sourceVoltageV, 1)} V</strong>
            </div>
            <div>
              <span>Source current</span>
              <strong>{formatNumber(snapshot.pd.sourceCurrentA, 2)} A</strong>
            </div>
            <div>
              <span>Operating current</span>
              <strong>{formatNumber(snapshot.pd.operatingCurrentA, 2)} A</strong>
            </div>
            <div>
              <span>Live runtime tier</span>
              <strong>{snapshot.session.powerTier}</strong>
            </div>
            <div>
              <span>Selected PDO</span>
              <strong>
                {matchedProfileIndex >= 0
                  ? `${pdObjectLabel(matchedProfileIndex)} active`
                  : snapshot.pd.sourceIsHostOnly
                    ? 'Host-only 5 V'
                    : 'No explicit PDO'}
              </strong>
            </div>
            <div>
              <span>Chip PDO count</span>
              <strong>{snapshot.pd.sinkProfileCount}</strong>
            </div>
            <div>
              <span>Source class</span>
              <strong>{snapshot.pd.sourceIsHostOnly ? 'Computer host' : 'PD supply'}</strong>
            </div>
          </div>
          <div className="bringup-fact-grid bringup-fact-grid--compact">
            <div>
              <span>Chip reachable</span>
              <strong>{snapshot.peripherals.pd.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>CC_STATUS</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.ccStatusReg, 2)}</strong>
            </div>
            <div>
              <span>DPM_PDO_NUMB</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.pdoCountReg, 2)}</strong>
            </div>
            <div>
              <span>RDO_STATUS_0..3</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.rdoStatusRaw, 8)}</strong>
            </div>
          </div>
          <p className="inline-help">
            These raw fields are actual STUSB4500 register readback. They are not the
            staged firmware PDO plan.
          </p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Wrench size={16} />
            <strong>Actual STUSB runtime PDOs</strong>
          </div>
          <p className="panel-note">
            These are the live sink PDO values read back from the STUSB4500 runtime
            registers. Priority is highest to lowest: PDO3, then PDO2, then PDO1. PDO1
            is the mandatory 5 V fallback.
          </p>

          <div className="bringup-profile-grid">
            {displayOrder.map((index) => {
              const profile = liveProfiles[index]
              const liveMatch = matchedProfileIndex === index

              return (
                <article key={`pd-live-profile-${index}`} className="bringup-profile-card">
                  <div className="bringup-profile-card__head">
                    <div>
                      <p className="eyebrow">{pdPriorityDetail(index)}</p>
                      <h3>{pdObjectLabel(index)}</h3>
                    </div>
                    <span
                      className={
                        liveMatch
                          ? 'status-badge is-on'
                          : profile.enabled
                            ? 'status-badge is-warn'
                            : 'status-badge'
                      }
                    >
                      {liveMatch ? 'active contract' : profile.enabled ? 'available' : 'disabled'}
                    </span>
                  </div>

                  <div className="field-grid">
                    <div className="field">
                      <FieldLabel
                        label="Voltage"
                        help="Read back from the STUSB4500 runtime PDO registers."
                      />
                      <div className="field-readback">{formatNumber(profile.voltageV, 2)} V</div>
                    </div>

                    <div className="field">
                      <FieldLabel
                        label="Current"
                        help="Read back from the STUSB4500 runtime PDO registers."
                      />
                      <div className="field-readback">{formatNumber(profile.currentA, 2)} A</div>
                    </div>
                  </div>

                  <div className="bringup-fact-grid bringup-fact-grid--compact">
                    <div>
                      <span>Configured power</span>
                      <strong>
                        {formatNumber(pdProfilePowerW(profile), 1)} W
                      </strong>
                    </div>
                    <div>
                      <span>Role</span>
                      <strong>{pdPriorityDetail(index)}</strong>
                    </div>
                  </div>
                </article>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Requested runtime PDOs</strong>
          </div>
          <p className="panel-note">
            Edit the runtime PDO plan you want the firmware to write into the STUSB4500.
            PDO1 stays at 5 V as the fallback object. PDO3 is the highest-priority
            request, PDO2 is the middle request, and PDO1 is the last fallback.
          </p>
          <p className="inline-help">
            Applying PDOs can trigger immediate USB-PD renegotiation. If this eval board
            is powering the controller on the same bench, the USB link may drop briefly
            while the source and sink settle on the new contract.
          </p>
          <p className="inline-help">
            Runtime PDO apply is for iteration. NVM burn is a separate manufacturing-style
            action and should only be used after the runtime plan has been validated.
          </p>
          <div className="bringup-toggle-row">
            <label className="arming-toggle">
              <input
                type="checkbox"
                checked={form.pdFirmwarePlanEnabled}
                title="When enabled, the controller stores this PDO plan in its own NVS and compares it against the live STUSB runtime PDO table whenever the ESP32 is online."
                onChange={(event) =>
                  patchForm('pdFirmwarePlanEnabled', event.target.checked)
                }
              />
              <span>Firmware owns this PDO plan and auto-loads it on mismatch</span>
            </label>
            <span className={form.pdFirmwarePlanEnabled ? 'status-badge is-on' : 'status-badge'}>
              {form.pdFirmwarePlanEnabled ? 'firmware auto-load on' : 'firmware auto-load off'}
            </span>
          </div>
          <p className="inline-help">
            Saving to firmware is different from STUSB NVM burn. The ESP32 stores the
            validated plan in its own NVS, compares it against live STUSB readback on
            startup, and only pushes runtime PDOs when the chip does not already match.
          </p>

          <div className="bringup-profile-grid">
            {displayOrder.map((index) => {
              const profile = form.pdProfiles[index]
              const liveMatch = matchedProfileIndex === index
              const requestedPowerW = pdProfilePowerW({
                voltageV: index === 0 ? 5 : parseNumber(profile.voltageV, 0),
                currentA: parseNumber(profile.currentA, 0),
              })

              return (
                <article key={`pd-plan-profile-${index}`} className="bringup-profile-card">
                  <div className="bringup-profile-card__head">
                    <div>
                      <p className="eyebrow">{pdPriorityDetail(index)}</p>
                      <h3>{pdObjectLabel(index)}</h3>
                    </div>
                    <span
                      className={
                        liveMatch
                          ? 'status-badge is-on'
                          : (index === 0 ? true : profile.enabled)
                            ? 'status-badge is-warn'
                            : 'status-badge'
                      }
                    >
                      {liveMatch
                        ? 'matches contract'
                        : index === 0
                          ? 'fixed fallback'
                          : profile.enabled
                            ? 'ready to apply'
                            : 'disabled'}
                    </span>
                  </div>

                  <div className="bringup-toggle-row">
                    <label className="arming-toggle is-compact">
                      <input
                        type="checkbox"
                        checked={index === 0 ? true : profile.enabled}
                        disabled={index === 0}
                        title={
                          index === 0
                            ? 'PDO1 is kept enabled as the mandatory 5 V fallback.'
                            : `Enable or disable ${pdObjectLabel(index)}.`
                        }
                        onChange={(event) =>
                          patchPdProfile(index, { enabled: event.target.checked })
                        }
                      />
                      <span>{index === 0 ? '5 V fallback always enabled' : 'Allow this request'}</span>
                    </label>
                  </div>

                  <div className="field-grid">
                    <label className="field">
                      <FieldLabel
                        label="Voltage"
                        help={
                          index === 0
                            ? 'PDO1 is fixed to 5 V by STUSB4500 hardware.'
                            : 'Choose a standard fixed-voltage PDO target. This editor intentionally limits you to the common fixed-voltage set.'
                        }
                      />
                      <select
                        value={index === 0 ? '5' : profile.voltageV}
                        disabled={index === 0}
                        title={
                          index === 0
                            ? 'PDO1 voltage is fixed at 5 V.'
                            : `Choose the requested voltage for ${pdObjectLabel(index)}.`
                        }
                        onChange={(event) =>
                          patchPdProfile(index, { voltageV: event.target.value })
                        }
                      >
                        {pdVoltageOptions.map((option) => (
                          <option key={`pd-voltage-${option.value}`} value={String(option.value)}>
                            {option.label}
                          </option>
                        ))}
                      </select>
                    </label>

                    <label className="field">
                      <FieldLabel
                        label="Current"
                        help="Choose a curated fixed-current request instead of free-form entry so the saved firmware plan stays sane and auditable."
                      />
                      <select
                        value={profile.currentA}
                        title={`Choose the requested current for ${pdObjectLabel(index)}.`}
                        onChange={(event) =>
                          patchPdProfile(index, { currentA: event.target.value })
                        }
                      >
                        {pdCurrentOptions.map((option) => (
                          <option key={`pd-current-${option.value}`} value={String(option.value)}>
                            {option.label}
                          </option>
                        ))}
                      </select>
                    </label>
                  </div>

                  <div className="bringup-fact-grid bringup-fact-grid--compact">
                    <div>
                      <span>Requested power</span>
                      <strong>{formatNumber(requestedPowerW, 1)} W</strong>
                    </div>
                    <div>
                      <span>Actual readback</span>
                      <strong>
                        {formatNumber(liveProfiles[index].voltageV, 1)} V / {formatNumber(liveProfiles[index].currentA, 2)} A
                      </strong>
                    </div>
                  </div>
                </article>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Firmware power thresholds</strong>
          </div>
          <p className="panel-note">
            These thresholds classify the current source as programming-only,
            insufficient, reduced, or full. They do not force a source to offer a
            higher-power contract if the source cannot satisfy it.
          </p>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Programming-only ceiling"
                help="Below this power, the bench stays non-operational unless it is just a plain 5 V host attachment."
              />
              <input
                value={form.pdProgrammingOnlyMaxW}
                title="Set the maximum power still considered too weak for normal operation."
                onChange={(event) =>
                  patchForm('pdProgrammingOnlyMaxW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Reduced-mode minimum"
                help="Minimum negotiated power required before reduced mode is allowed."
              />
              <input
                value={form.pdReducedModeMinW}
                title="Set the minimum power required for reduced mode."
                onChange={(event) =>
                  patchForm('pdReducedModeMinW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Reduced-mode ceiling"
                help="Upper edge of reduced mode. Anything above this can qualify for full mode if the full threshold is met."
              />
              <input
                value={form.pdReducedModeMaxW}
                title="Set the upper bound for reduced mode."
                onChange={(event) =>
                  patchForm('pdReducedModeMaxW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Full-mode minimum"
                help="Minimum negotiated power required before the controller may classify the source as full power."
              />
              <input
                value={form.pdFullModeMinW}
                title="Set the minimum power required for full mode."
                onChange={(event) =>
                  patchForm('pdFullModeMinW', event.target.value)
                }
              />
            </label>
          </div>

          <div className="bringup-verification-grid">
            <div>
              <span>Highest enabled request</span>
              <strong>{formatNumber(highestEnabledProfilePowerW, 1)} W</strong>
            </div>
            <div>
              <span>Minimum operational power</span>
              <strong>{formatNumber(minimumOperationalW, 1)} W</strong>
            </div>
            <div>
              <span>Draft tier with current source</span>
              <strong>{plannedTier}</strong>
            </div>
            <div>
              <span>Current live selection</span>
              <strong>
                {matchedProfileIndex >= 0 ? pdObjectLabel(matchedProfileIndex) : '5 V / none'}
              </strong>
            </div>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Write the requested runtime PDOs into the STUSB4500 and update firmware power thresholds."
              onClick={() => {
                void applyPdProfile()
              }}
            >
              Apply runtime PDOs
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={writesDisabled}
              title="Verify the requested PDOs by applying them, refreshing live STUSB readback, then saving the validated plan into controller firmware."
              onClick={() => {
                void savePdProfileToFirmware()
              }}
            >
              Save PDO plan to firmware
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Force the controller to re-read the STUSB4500 live contract and PDO runtime registers now."
              onClick={() =>
                void refreshPdStatus(true, true)
              }
            >
              Refresh PD now
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Run an I2C discovery scan against declared bring-up targets."
              onClick={() =>
                void moduleQuickScanI2c(
                  'Scan I2C bus',
                  'Run an I2C discovery scan against declared bring-up targets.',
                )
              }
            >
              Scan I2C
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read STUSB4500 status register 0x06."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read STUSB4500 0x06',
                  0x28,
                  0x06,
                  'Read the STUSB4500 status byte at 0x06.',
                )
              }}
            >
              Read 0x06
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read STUSB4500 status register 0x07."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read STUSB4500 0x07',
                  0x28,
                  0x07,
                  'Read the STUSB4500 status byte at 0x07.',
                )
              }}
            >
              Read 0x07
            </button>
            <button
              type="button"
              className="action-button is-inline is-danger"
              disabled={writesDisabled}
              title="Burn the currently validated PDO plan into STUSB4500 NVM. Use only for final provisioning because NVM endurance is finite."
              onClick={() => setPdNvmConfirmOpen(true)}
            >
              Burn PDOs to NVM
            </button>
          </div>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cScan}</p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>
      </div>
    )
  }

  function renderButtonsPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('buttons')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Layers3 size={16} />
            <strong>Two-stage trigger note</strong>
          </div>
          <p className="panel-note">
            The current bench plan keeps the trigger absent. This page exists so the
            absence is explicit and auditable instead of surfacing as a fault.
          </p>
          <div className="bringup-step-list">
            <div className="bringup-step-list__item">
              <strong>Stage 1 future job</strong>
              <p>Green alignment laser request gated by the same hard interlocks.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>Stage 2 future job</strong>
              <p>NIR request with immediate beam-off on release or illegal transition.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>Current status</strong>
              <p>No GPIO mapping is treated as authoritative until the missing sensor board files are reviewed.</p>
            </div>
          </div>
        </article>
      </div>
    )
  }

  function renderLaserPage() {
    const draftLdVoltageV = clampLdCommandVoltageV(
      parseNumber(form.dacLdVoltage, snapshot.bringup.tuning.dacLdChannelV),
    )
    const draftLdCurrentA = clampLdCommandCurrentA(
      estimateLdCurrentFromVoltageV(draftLdVoltageV),
    )
    const liveLaser = ldDisplaySample
    const loopTone = liveLaser.loopGood ? 'ok' : liveLaser.railEnabled ? 'warn' : 'off'
    const railTone = liveLaser.railPgood ? 'ok' : liveLaser.railEnabled ? 'warn' : 'off'
    const dacTone = snapshot.peripherals.dac.reachable
      ? snapshot.peripherals.dac.configured
        ? 'ok'
        : 'warn'
      : 'off'
    const refAlarmTone = snapshot.peripherals.dac.refAlarm
      ? 'critical'
      : snapshot.peripherals.dac.reachable
        ? 'ok'
        : 'off'
    const sbdnStatus = describeLdSbdnMode(liveSnapshot)
    const pcnStatus = describeLdPcnMode(liveSnapshot)
    const greenIo37Status = describeGreenIo37State(liveSnapshot)
    const nirRequested = liveSnapshot.bench.requestedNirEnabled
    const nirActive = liveSnapshot.laser.nirEnabled
    const nirTone: UiTone =
      nirActive
        ? 'steady'
        : nirRequested
          ? liveSnapshot.safety.allowNir
            ? 'warning'
            : 'critical'
          : 'steady'
    const tmoTempValid = liveLaser.railPgood && sbdnStatus.mode !== 'off-pd'
    const liveCurrentPercent = Math.max(
      0,
      Math.min(100, (liveLaser.measuredCurrentA / LD_CURRENT_FULL_SCALE_A) * 100),
    )
    const stagedCurrentPercent = Math.max(
      0,
      Math.min(100, (draftLdCurrentA / LD_CURRENT_UI_MAX_A) * 100),
    )
    const commandVoltagePercent = Math.max(
      0,
      Math.min(100, (liveLaser.commandVoltageV / DAC80502_BOARD_SPAN_V) * 100),
    )
    const currentMonitorPercent = Math.max(
      0,
      Math.min(100, (liveLaser.currentMonitorVoltageV / DAC80502_BOARD_SPAN_V) * 100),
    )
    const tempMonitorPercent = Math.max(
      0,
      Math.min(100, (liveLaser.driverTempVoltageV / LD_TEMP_MONITOR_MAX_V) * 100),
    )
    const liveCurrentTone: UiTone =
      liveLaser.measuredCurrentA >= 4.5
        ? 'critical'
        : liveLaser.measuredCurrentA > 0.08
          ? 'steady'
          : 'warning'

    function patchLdVoltageDraft(rawValue: string) {
      const nextVoltageV = clampLdCommandVoltageV(parseNumber(rawValue, draftLdVoltageV))
      patchForm('dacLdVoltage', formatNumber(nextVoltageV, 3))
    }

    function patchLdCurrentDraft(rawValue: string) {
      const nextCurrentA = clampLdCommandCurrentA(parseNumber(rawValue, draftLdCurrentA))
      patchForm('dacLdVoltage', formatNumber((nextCurrentA / LD_CURRENT_FULL_SCALE_A) * DAC80502_BOARD_SPAN_V, 3))
    }

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('laserDriver')}

        <article className="panel-cutout bringup-ld">
          <div className="cutout-head">
            <Radio size={16} />
            <strong>Laser-driver bring-up</strong>
          </div>
          <p className="bringup-ld__lead">
            DAC channel A drives `LISH`. Stage the request here in driver terms,
            then confirm the real `LIO`, `TMO`, `PCN`, `SBDN`, loop-good, and rail
            readback on the live side.
          </p>
          <div className="bringup-ld__layout">
            <section className="bringup-ld__panel">
              <div className="bringup-ld__section-head">
                <div>
                  <strong>Command path</strong>
                  <small>One shared DAC A shadow. Host setpoints stay capped at 5.00 A.</small>
                </div>
              </div>
              <div className="bringup-ld__input-grid">
                <label className="field">
                  <FieldLabel
                    label="Set current (A)"
                    help="Stage the LD current request in driver terms. The real ATLS6A214 span is 0–6 A across 0–2.5 V on LISH, but this page clamps commands at 5 A."
                  />
                  <input
                    value={formatNumber(draftLdCurrentA, 3)}
                    title="Stage the LD current request in amps. This stays capped at 5.00 A."
                    onChange={(event) => patchLdCurrentDraft(event.target.value)}
                  />
                </label>
                <label className="field">
                  <FieldLabel
                    label="DAC A / LISH (V)"
                    help="Same shared DAC channel A draft shown on the DAC page. The host caps this bring-up path at 2.083 V, which corresponds to 5 A."
                  />
                  <input
                    value={formatNumber(draftLdVoltageV, 3)}
                    title="Stage the LD / LISH DAC voltage in volts."
                    onChange={(event) => patchLdVoltageDraft(event.target.value)}
                  />
                </label>
              </div>

              <div className="bringup-ld__summary-strip">
                <div className="bringup-ld__live-summary">
                  <span>Estimated diode load</span>
                  <strong>{formatNumber(liveLaser.diodeElectricalPowerW, 2)} W</strong>
                  <small>Based on live current with a 3.0 V diode model.</small>
                </div>
                <div className="bringup-ld__live-summary">
                  <span>Estimated VIN draw</span>
                  <strong>{formatNumber(liveLaser.supplyDrawW, 2)} W</strong>
                  <small>Derived from live load using 90% driver efficiency.</small>
                </div>
              </div>

              <div className="field">
                <FieldLabel
                  label="Current request slider"
                  help="0–5 A host safety range. Moving this slider updates the same DAC channel A draft used by the DAC page."
                />
                <input
                  className="bringup-ld__target-slider"
                  type="range"
                  min={0}
                  max={LD_CURRENT_UI_MAX_A}
                  step={0.01}
                  value={draftLdCurrentA}
                  title="Slide the staged LD current request from 0 to 5 A."
                  onChange={(event) => patchLdCurrentDraft(event.target.value)}
                />
              </div>

              <div className="bringup-ld__progress-card">
                <div className="bringup-ld__meter-head">
                  <span>Staged current request</span>
                  <strong>{formatNumber(draftLdCurrentA, 2)} A</strong>
                </div>
                <ProgressMeter value={stagedCurrentPercent} tone="steady" />
                <div className="bringup-ld__meter-scale">
                  <span>0 A</span>
                  <span>5 A cap</span>
                </div>
              </div>

              <div className="button-row">
                <button
                  type="button"
                  className="action-button is-inline is-accent"
                  disabled={writesDisabled}
                  title="Apply the LD / LISH DAC setpoint and read back the DAC status plus channel A register."
                  onClick={() => {
                    void applySingleDacShadowChannel('ld', form.dacLdVoltage)
                  }}
                >
                  Apply LD setpoint
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  disabled={readsDisabled}
                  title="Read the DAC STATUS register to confirm REF-ALARM is clear."
                  onClick={() => {
                    void moduleQuickReadI2c(
                      'Read DAC STATUS',
                      0x48,
                      0x07,
                      'Read the DAC80502 STATUS register. REF-ALARM=1 forces both outputs to 0 V.',
                    )
                  }}
                >
                  Read STATUS
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  disabled={readsDisabled}
                  title="Read the DAC channel A data register that drives LISH."
                  onClick={() => {
                    void moduleQuickReadI2c(
                      'Read DAC LD DATA',
                      0x48,
                      0x08,
                      'Read the DAC80502 channel A data register that drives DAC_OUTA / LISH.',
                    )
                  }}
                >
                  Read LD DATA
                </button>
              </div>

              <div className="button-row">
                <button
                  type="button"
                  className="action-button is-inline"
                  disabled={writesDisabled}
                  title={
                    greenIo37Status.enabled
                      ? 'Drive GPIO37 low to disable the shared green-laser enable net.'
                      : 'Drive GPIO37 high to enable the shared green-laser enable net.'
                  }
                  onClick={() => {
                    void setBringupGreenLaserEnabled(!greenIo37Status.enabled)
                  }}
                >
                  {greenIo37Status.enabled ? 'Disable Green Laser' : 'Enable Green Laser'}
                </button>
              </div>

              <div className="bringup-ld__mode-grid">
                <div className="bringup-ld__mode-card">
                  <div className="bringup-ld__meter-head">
                    <span>SBDN mode</span>
                    <strong>{sbdnStatus.label}</strong>
                  </div>
                  <small>{sbdnStatus.detail}</small>
                  <div className="bringup-ld__mode-switch bringup-ld__mode-switch--sbdn">
                    <button
                      type="button"
                      className={sbdnStatus.mode === 'firmware' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Release GPIO13 back to firmware ownership."
                      onClick={() => {
                        void setLdSbdnMode('firmware')
                      }}
                    >
                      Auto
                    </button>
                    <button
                      type="button"
                      className={sbdnStatus.mode === 'off-pd' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Drive GPIO13 low to force the laser driver off."
                      onClick={() => {
                        void setLdSbdnMode('off-pd')
                      }}
                    >
                      Off
                    </button>
                    <button
                      type="button"
                      className={sbdnStatus.mode === 'standby-hiz' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Release GPIO13 to high impedance for standby."
                      onClick={() => {
                        void setLdSbdnMode('standby-hiz')
                      }}
                    >
                      Standby
                    </button>
                    <button
                      type="button"
                      className={sbdnStatus.mode === 'on-pu' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Drive GPIO13 high to force the laser driver on."
                      onClick={() => {
                        void setLdSbdnMode('on-pu')
                      }}
                    >
                      On
                    </button>
                  </div>
                </div>

                <div className="bringup-ld__mode-card">
                  <div className="bringup-ld__meter-head">
                    <span>PCN current leg</span>
                    <strong>{pcnStatus.label}</strong>
                  </div>
                  <small>{pcnStatus.detail}</small>
                  <div className="bringup-ld__mode-switch bringup-ld__mode-switch--pcn">
                    <button
                      type="button"
                      className={pcnStatus.mode === 'firmware' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Release GPIO21 back to firmware ownership."
                      onClick={() => {
                        void setLdPcnMode('firmware')
                      }}
                    >
                      Firmware
                    </button>
                    <button
                      type="button"
                      className={pcnStatus.mode === 'lisl' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Drive GPIO21 low to force LISL."
                      onClick={() => {
                        void setLdPcnMode('lisl')
                      }}
                    >
                      LISL
                    </button>
                    <button
                      type="button"
                      className={pcnStatus.mode === 'lish' ? 'segmented__button is-active' : 'segmented__button'}
                      disabled={writesDisabled}
                      title="Drive GPIO21 high to force LISH."
                      onClick={() => {
                        void setLdPcnMode('lish')
                      }}
                    >
                      LISH
                    </button>
                  </div>
                </div>
              </div>
            </section>

            <section className="bringup-ld__panel bringup-ld__panel--live">
              <div className="bringup-ld__section-head">
                <div>
                  <strong>Live driver readback</strong>
                  <small>These cards reflect controller telemetry and GPIO truth, not just the staged host request. Sampled at 2 Hz for readability.</small>
                </div>
              </div>

              <div className="bringup-ld__live-summary-grid">
                <div>
                  <span>Live current</span>
                  <strong>{formatNumber(liveLaser.measuredCurrentA, 2)} A</strong>
                  <small>{formatNumber(liveLaser.currentMonitorVoltageV, 3)} V on LIO</small>
                </div>
                <div>
                  <span>TMO temp</span>
                  <strong>{tmoTempValid ? `${formatNumber(liveLaser.driverTempC, 1)} °C` : 'Not valid'}</strong>
                  <small>
                    {tmoTempValid
                      ? `${formatNumber(liveLaser.driverTempVoltageV, 3)} V on TMO`
                      : 'Only valid when LD rail PGOOD is high and SBDN is not OFF-PD.'}
                  </small>
                </div>
                <div>
                  <span>Laser load</span>
                  <strong>{formatNumber(liveLaser.diodeElectricalPowerW, 2)} W</strong>
                  <small>Live current multiplied by the 3.0 V diode model.</small>
                </div>
                <div>
                  <span>VIN draw</span>
                  <strong>{formatNumber(liveLaser.supplyDrawW, 2)} W</strong>
                  <small>Estimated upstream electrical draw.</small>
                </div>
              </div>

              <div className="bringup-ld__status-tags">
                <div className="bringup-ld__status-tag" data-tone={loopTone}>
                  <span>Loop Good</span>
                  <strong>{liveLaser.loopGood ? 'Good' : 'Low'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={railTone}>
                  <span>LD rail PGOOD</span>
                  <strong>{liveLaser.railPgood ? 'Good' : 'Low'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={dacTone}>
                  <span>DAC path</span>
                  <strong>{snapshot.peripherals.dac.reachable ? 'Reachable' : 'No response'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={refAlarmTone}>
                  <span>REF_ALARM</span>
                  <strong>{snapshot.peripherals.dac.refAlarm ? 'Asserted' : 'Clear'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={sbdnStatus.tone}>
                  <span>SBDN state</span>
                  <strong>{sbdnStatus.label}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={pcnStatus.tone}>
                  <span>PCN state</span>
                  <strong>{pcnStatus.label}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={nirTone}>
                  <span>NIR request</span>
                  <strong>{nirRequested ? 'Staged' : 'Clear'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={nirActive ? 'ok' : liveSnapshot.safety.allowNir ? 'off' : 'warning'}>
                  <span>NIR live</span>
                  <strong>{nirActive ? 'On' : 'Off'}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={greenIo37Status.tone}>
                  <span>Green net</span>
                  <strong>{greenIo37Status.label}</strong>
                </div>
                <div className="bringup-ld__status-tag" data-tone={greenIo37Status.tone}>
                  <span>Green owner</span>
                  <strong>{greenIo37Status.owner}</strong>
                </div>
              </div>

              <div className="bringup-ld__progress-card">
                <div className="bringup-ld__meter-head">
                  <span>Live driver current</span>
                  <strong>{formatNumber(liveLaser.measuredCurrentA, 2)} A</strong>
                </div>
                <ProgressMeter value={liveCurrentPercent} tone={liveCurrentTone} />
                <div className="bringup-ld__meter-scale">
                  <span>0 A</span>
                  <span>6 A full span</span>
                </div>
              </div>

              <div className="bringup-ld__meter-grid">
                <div className="bringup-ld__meter-card">
                  <div className="bringup-ld__meter-head">
                    <span>LISH command</span>
                    <strong>{formatNumber(liveLaser.commandVoltageV, 3)} V</strong>
                  </div>
                  <ProgressMeter value={commandVoltagePercent} tone="steady" />
                  <div className="bringup-ld__meter-scale">
                    <span>0 V</span>
                    <span>2.5 V span</span>
                  </div>
                </div>
                <div className="bringup-ld__meter-card">
                  <div className="bringup-ld__meter-head">
                    <span>LIO current monitor</span>
                    <strong>{formatNumber(liveLaser.currentMonitorVoltageV, 3)} V</strong>
                  </div>
                  <ProgressMeter value={currentMonitorPercent} tone={liveCurrentTone} />
                  <div className="bringup-ld__meter-scale">
                    <span>0 V</span>
                    <span>2.5 V</span>
                  </div>
                </div>
                <div className="bringup-ld__meter-card">
                  <div className="bringup-ld__meter-head">
                    <span>TMO voltage</span>
                    <strong>{tmoTempValid ? `${formatNumber(liveLaser.driverTempVoltageV, 3)} V` : 'Invalid now'}</strong>
                  </div>
                  <ProgressMeter value={tmoTempValid ? tempMonitorPercent : 0} tone="warning" />
                  <div className="bringup-ld__meter-scale">
                    <span>0 V</span>
                    <span>2.5 V</span>
                  </div>
                </div>
                <div className="bringup-ld__meter-card bringup-ld__meter-card--register">
                  <div className="bringup-ld__meter-head">
                    <span>DAC channel A</span>
                    <strong>{formatRegisterHex(snapshot.peripherals.dac.dataAReg)}</strong>
                  </div>
                  <small>{formatDacRegisterEquivalent(snapshot.peripherals.dac.dataAReg)}</small>
                  <div className="bringup-ld__meter-scale">
                    <span>STATUS {formatRegisterHex(snapshot.peripherals.dac.statusReg)}</span>
                    <span>GAIN {formatRegisterHex(snapshot.peripherals.dac.gainReg)}</span>
                  </div>
                </div>
              </div>
            </section>
          </div>
          <p className="inline-help">
            `TMO` temperature is only treated as valid when the LD rail is good and
            `SBDN` is not forcing OFF-PD. `PCN` and `SBDN` controls on this page are
            service-only GPIO overrides layered on top of the normal firmware logic.
            On this page, green-laser control is also a direct service override on
            shared `GPIO37 / GN_LD_EN`, so it drives the pin high or low directly
            instead of staging a normal runtime alignment request.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>
      </div>
    )
  }

  function renderTecPage() {
    const tecVoltageRange = {
      min: tecCalibrationPoints[0].tecVoltageV,
      max: tecCalibrationPoints[tecCalibrationPoints.length - 1].tecVoltageV,
    }
    const tecTempRange = {
      min: tecCalibrationPoints[0].tempC,
      max: tecCalibrationPoints[tecCalibrationPoints.length - 1].tempC,
    }
    const tecWavelengthRange = {
      min: tecCalibrationPoints[0].wavelengthNm,
      max: tecCalibrationPoints[tecCalibrationPoints.length - 1].wavelengthNm,
    }
    const draftTecVoltageV = clampTecVoltageV(
      parseNumber(form.dacTecVoltage, snapshot.bringup.tuning.dacTecChannelV),
    )
    const draftTecTempC = estimateTempFromTecVoltageV(draftTecVoltageV)
    const draftTecLambdaNm = estimateWavelengthFromTecVoltageV(draftTecVoltageV)
    const sampledTec = tecDisplaySample
    const tecElectricalPowerW = Math.abs(sampledTec.currentA * sampledTec.voltageV)
    const tecSupplyDrawW = tecElectricalPowerW / 0.9
    const liveLambdaDeltaNm = sampledTec.actualLambdaNm - draftTecLambdaNm
    const commandVoltagePercent = Math.max(0, Math.min(100, (sampledTec.commandVoltageV / 2.5) * 100))
    const tempAdcPercent = Math.max(0, Math.min(100, (sampledTec.tempAdcVoltageV / 2.5) * 100))
    const tecVoltagePercent = Math.max(0, Math.min(100, (sampledTec.voltageV / 5.0) * 100))
    const currentMagnitudePercent = Math.min(
      50,
      (Math.abs(sampledTec.currentA) / 3.5) * 50,
    )
    const currentDirectionClass =
      sampledTec.currentA < -0.03
        ? 'is-negative'
        : sampledTec.currentA > 0.03
          ? 'is-positive'
          : 'is-neutral'
    const pgoodTone = sampledTec.railPgood
      ? 'ok'
      : sampledTec.railEnabled
        ? 'warn'
        : 'off'
    const railEnableTone = sampledTec.railEnabled ? 'ok' : 'off'
    const powerTone = tecElectricalPowerW > 0.2 ? 'ok' : 'off'

    function patchTecVoltageDraft(rawValue: string) {
      const nextVoltageV = clampTecVoltageV(parseNumber(rawValue, draftTecVoltageV))
      patchForm('dacTecVoltage', formatNumber(nextVoltageV, 3))
    }

    function patchTecTempDraft(rawValue: string) {
      const nextTempC = clampTecTempC(parseNumber(rawValue, draftTecTempC))
      patchForm('dacTecVoltage', formatNumber(estimateTecVoltageFromTempC(nextTempC), 3))
    }

    function patchTecWavelengthDraft(rawValue: string) {
      const nextWavelengthNm = clampTecWavelengthNm(parseNumber(rawValue, draftTecLambdaNm))
      patchForm(
        'dacTecVoltage',
        formatNumber(estimateTecVoltageFromWavelengthNm(nextWavelengthNm), 3),
      )
    }

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('tec')}

        <article className="panel-cutout bringup-tec">
          <div className="cutout-head">
            <Thermometer size={16} />
            <strong>TEC loop bring-up</strong>
          </div>
          <p className="panel-note">
            Stage the TEC command once, then watch the loop respond. This page keeps
            the editable target on the left and the sampled live loop truth on the
            right so write intent and readback never blur together.
          </p>
          <div className="bringup-tec__layout">
            <section className="bringup-tec__panel">
              <div className="segmented is-three">
                <button
                  type="button"
                  className={tecSliderMode === 'temp' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={writesDisabled}
                  title="Use the main target slider in temperature mode."
                  onClick={() => setTecSliderMode('temp')}
                >
                  Temp slider
                </button>
                <button
                  type="button"
                  className={tecSliderMode === 'lambda' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={writesDisabled}
                  title="Use the main target slider in wavelength mode."
                  onClick={() => setTecSliderMode('lambda')}
                >
                  Wavelength slider
                </button>
                <button
                  type="button"
                  className="segmented__button"
                  disabled={writesDisabled}
                  title="Overwrite the TEC draft with the controller's current staged DAC channel B voltage."
                  onClick={() => {
                    patchForm(
                      'dacTecVoltage',
                      formatNumber(snapshot.bringup.tuning.dacTecChannelV, 3),
                    )
                  }}
                >
                  Sync live
                </button>
              </div>

              <div className="bringup-tec__input-grid">
                <label className="field">
                  <FieldLabel
                    label="Target TEC temp (°C)"
                    help="Editable TEC temperature target derived from the same channel B DAC draft."
                  />
                  <input
                    type="number"
                    min={tecTempRange.min}
                    max={70}
                    step="0.1"
                    value={formatNumber(draftTecTempC, 1)}
                    title="Stage the TEC target temperature in degrees Celsius."
                    onChange={(event) => patchTecTempDraft(event.target.value)}
                  />
                </label>
                <label className="field">
                  <FieldLabel
                    label="Target wavelength (nm)"
                    help="Editable wavelength target derived from the same TEC calibration table."
                  />
                  <input
                    type="number"
                    min={tecWavelengthRange.min}
                    max={tecWavelengthRange.max}
                    step="0.1"
                    value={formatNumber(draftTecLambdaNm, 1)}
                    title="Stage the TEC wavelength target in nanometers."
                    onChange={(event) => patchTecWavelengthDraft(event.target.value)}
                  />
                </label>
                <label className="field">
                  <FieldLabel
                    label="TMS setpoint (V)"
                    help="Editable DAC channel B command voltage toward the TEC controller TMS input."
                  />
                  <input
                    type="number"
                    min={tecVoltageRange.min}
                    max={tecVoltageRange.max}
                    step="0.001"
                    value={formatNumber(draftTecVoltageV, 3)}
                    title="Stage the TEC / TMS DAC shadow voltage in volts."
                    onChange={(event) => patchTecVoltageDraft(event.target.value)}
                  />
                </label>
              </div>

              <label className="field field--full">
                <div className="field__head">
                  <span>{tecSliderMode === 'temp' ? 'Target TEC temp slider' : 'Target wavelength slider'}</span>
                  <strong>
                    {tecSliderMode === 'temp'
                      ? `${formatNumber(draftTecTempC, 1)} °C`
                      : `${formatNumber(draftTecLambdaNm, 1)} nm`}
                  </strong>
                </div>
                <input
                  className="bringup-tec__target-slider"
                  type="range"
                  min={tecSliderMode === 'temp' ? tecTempRange.min : tecWavelengthRange.min}
                  max={tecSliderMode === 'temp' ? 70 : tecWavelengthRange.max}
                  step="0.1"
                  value={tecSliderMode === 'temp' ? draftTecTempC : draftTecLambdaNm}
                  title={
                    tecSliderMode === 'temp'
                      ? 'Drag to stage TEC temperature and auto-update wavelength and TMS voltage.'
                      : 'Drag to stage wavelength and auto-update TEC temperature and TMS voltage.'
                  }
                  onChange={(event) => {
                    if (tecSliderMode === 'temp') {
                      patchTecTempDraft(event.target.value)
                      return
                    }

                    patchTecWavelengthDraft(event.target.value)
                  }}
                />
                <span className="inline-help">
                  One target, three linked views: {formatNumber(draftTecTempC, 1)} °C, {formatNumber(draftTecLambdaNm, 1)} nm, and {formatNumber(draftTecVoltageV, 3)} V on TMS.
                </span>
              </label>

              <div className="button-row">
                <button
                  type="button"
                  className="action-button is-inline is-accent"
                  disabled={writesDisabled}
                  title="Apply the TEC / TMS DAC setpoint and read back the channel B register."
                  onClick={() => {
                    void applySingleDacShadowChannel('tec', form.dacTecVoltage)
                  }}
                >
                  Apply TEC setpoint
                </button>
              </div>
            </section>

            <section className="bringup-tec__panel bringup-tec__panel--live">
              <div className="bringup-tec__live-summary">
                <div>
                  <span>Actual lambda</span>
                  <strong>{formatNumber(sampledTec.actualLambdaNm, 2)} nm</strong>
                  <small>{formatNumber(liveLambdaDeltaNm, 2)} nm from staged target</small>
                </div>
                <div>
                  <span>TEC electrical load</span>
                  <strong>{formatNumber(tecElectricalPowerW, 2)} W</strong>
                  <small>{formatNumber(tecSupplyDrawW, 2)} W estimated supply draw</small>
                </div>
              </div>

              <label className="field field--full">
                <div className="field__head">
                  <span>Current TEC temp readback</span>
                  <strong>{formatNumber(sampledTec.tempC, 2)} °C</strong>
                </div>
                <input
                  className="bringup-tec__readback-slider"
                  type="range"
                  min={5}
                  max={70}
                  step="0.01"
                  value={clampTecTempC(sampledTec.tempC)}
                  disabled
                  title="Sampled live TEC temperature readback."
                  readOnly
                />
                <span className="inline-help">
                  Sampled once per second for readability. This is live readback, not the staged target slider.
                </span>
              </label>

              <div className="bringup-tec__status-tags">
                <div className="bringup-tec__status-tag" data-tone={sampledTec.tempGood ? 'ok' : 'warn'}>
                  <span>TEMPGD</span>
                  <strong>{sampledTec.tempGood ? 'High' : 'Low'}</strong>
                </div>
                <div className="bringup-tec__status-tag" data-tone={pgoodTone}>
                  <span>TEC rail PGOOD</span>
                  <strong>{sampledTec.railPgood ? 'High' : 'Low'}</strong>
                </div>
                <div className="bringup-tec__status-tag" data-tone={railEnableTone}>
                  <span>TEC rail enable</span>
                  <strong>{sampledTec.railEnabled ? 'On' : 'Off'}</strong>
                </div>
                <div className="bringup-tec__status-tag" data-tone={powerTone}>
                  <span>Power draw</span>
                  <strong>{formatNumber(tecElectricalPowerW, 2)} W</strong>
                </div>
              </div>
            </section>
          </div>
          <div className="bringup-tec__meter-grid">
            <div className="bringup-tec__meter-card">
              <div className="bringup-tec__meter-head">
                <span>TMS set voltage</span>
                <strong>{formatNumber(sampledTec.commandVoltageV, 3)} V</strong>
              </div>
              <ProgressMeter value={commandVoltagePercent} tone="steady" />
              <div className="bringup-tec__meter-scale">
                <span>0 V</span>
                <span>2.5 V</span>
              </div>
            </div>
            <div className="bringup-tec__meter-card">
              <div className="bringup-tec__meter-head">
                <span>TMO thermistor voltage</span>
                <strong>{formatNumber(sampledTec.tempAdcVoltageV, 3)} V</strong>
              </div>
              <ProgressMeter
                value={tempAdcPercent}
                tone={sampledTec.tempAdcVoltageV >= 2.2 ? 'warning' : 'steady'}
              />
              <div className="bringup-tec__meter-scale">
                <span>0 V</span>
                <span>2.5 V</span>
              </div>
            </div>
            <div className="bringup-tec__meter-card">
              <div className="bringup-tec__meter-head">
                <span>VTEC</span>
                <strong>{formatNumber(sampledTec.voltageV, 2)} V</strong>
              </div>
              <ProgressMeter value={tecVoltagePercent} tone="steady" />
              <div className="bringup-tec__meter-scale">
                <span>0 V</span>
                <span>5.0 V</span>
              </div>
            </div>
            <div className="bringup-tec__meter-card bringup-tec__meter-card--power">
              <div className="bringup-tec__meter-head">
                <span>Estimated supply draw</span>
                <strong>{formatNumber(tecSupplyDrawW, 2)} W</strong>
              </div>
              <ProgressMeter
                value={Math.min(100, (tecSupplyDrawW / 20) * 100)}
                tone={tecSupplyDrawW > 12 ? 'warning' : 'steady'}
              />
              <div className="bringup-tec__meter-scale">
                <span>0 W</span>
                <span>20 W</span>
              </div>
            </div>
            <div className="bringup-tec__current-card">
              <div className="bringup-tec__meter-head">
                <span>ITEC</span>
                <strong>{formatNumber(sampledTec.currentA, 2)} A</strong>
              </div>
              <div className="bringup-tec__current-bar">
                <div className="bringup-tec__current-track" />
                <div className="bringup-tec__current-center" />
                <div
                  className={`bringup-tec__current-fill ${currentDirectionClass}`}
                  style={
                    sampledTec.currentA < -0.03
                      ? {
                          left: `calc(50% - ${currentMagnitudePercent}%)`,
                          width: `${currentMagnitudePercent}%`,
                        }
                      : {
                          left: '50%',
                          width: `${currentMagnitudePercent}%`,
                        }
                  }
                />
              </div>
              <div className="bringup-tec__meter-scale bringup-tec__meter-scale--triple">
                <span>-3.5 A</span>
                <span>0 A</span>
                <span>+3.5 A</span>
              </div>
            </div>
          </div>
          <p className="inline-help">
            What matters here is whether the staged target turns into believable loop
            behavior: `TMS` should move, `TMO` should stay plausible, `VTEC` and
            `ITEC` should respond, and `TEMPGD` / rail `PGOOD` should make sense.
            The live readback panel is intentionally sampled at 1 Hz so the page is
            readable while still showing the real trend.
          </p>
        </article>
      </div>
    )
  }

  function renderActivePage() {
    switch (activePage) {
      case 'workflow':
        return renderWorkflowPage()
      case 'power':
        return renderPowerPage()
      case 'imu':
        return renderImuPage()
      case 'dac':
        return renderDacPage()
      case 'haptic':
        return renderHapticPage()
      case 'tof':
        return renderTofPage()
      case 'pd':
        return renderPdPage()
      case 'buttons':
        return renderButtonsPage()
      case 'laserDriver':
        return renderLaserPage()
      case 'tec':
        return renderTecPage()
      default:
        return renderWorkflowPage()
    }
  }

  return (
    <section
      className={operation !== null ? 'panel-section bringup-workspace is-busy' : 'panel-section bringup-workspace'}
      aria-busy={operation !== null}
    >
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Bring-up mode</p>
          <h2>Module bring-up</h2>
        </div>
        <p className="panel-note">
          Pick one subsystem, verify that it is really present, then stage settings only after a successful probe.
        </p>
      </div>

      <div className="bringup-shell">
        <aside className="bringup-sidebar">
          <div className="bringup-sidebar__summary">
            <div>
              <p className="eyebrow">Current bench plan</p>
              <strong>{form.profileName}</strong>
            </div>
            <div className="status-badges">
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                {connected ? 'Board linked' : 'Offline'}
              </span>
              <span
                className={
                  serviceModeActive
                    ? 'status-badge is-on'
                    : serviceModeRequested
                      ? 'status-badge is-warn'
                      : 'status-badge'
                }
              >
                {serviceModeActive
                  ? 'Write session active'
                  : serviceModeRequested
                    ? 'Write session requested'
                    : 'Read-only link'}
              </span>
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                {connected ? 'Probe loop running' : 'Probe loop paused'}
              </span>
            </div>
            <ProgressMeter
              value={overallProgress}
              tone={overallProgress >= 80 ? 'steady' : overallProgress >= 50 ? 'warning' : 'critical'}
            />
            {operation !== null ? (
              <div className="bringup-operation">
                <div className="bringup-operation__head">
                  <strong>{operation.label}</strong>
                  <span>{operation.percent}%</span>
                </div>
                <ProgressMeter value={operation.percent} tone={operation.tone} compact />
                <small>{operation.detail}</small>
              </div>
            ) : (
              <small>{snapshot.bringup.tools.lastAction}</small>
            )}
          </div>

          <nav className="bringup-nav" aria-label="Bring-up pages">
            {bringupPages.map((page) => {
              const Icon = page.icon
              const isActive = page.id === activePage
              const score =
                page.id === 'workflow'
                  ? {
                      progress: overallProgress,
                      tone: (
                        overallProgress >= 80
                          ? 'steady'
                          : overallProgress >= 50
                            ? 'warning'
                            : 'critical'
                      ) as UiTone,
                      label: 'Service and policy',
                    }
                  : page.id === 'power'
                    ? powerPageScore
                    : moduleScore(page.id, displayModules[page.id], connected)

              return (
                <button
                  key={page.id}
                  type="button"
                  className={isActive ? 'bringup-nav__item is-active' : 'bringup-nav__item'}
                  title={page.detail}
                  onClick={() => setActivePage(page.id)}
                >
                  <div className="bringup-nav__icon">
                    <Icon size={16} />
                  </div>
                  <div className="bringup-nav__copy">
                    <strong>{page.label}</strong>
                    <small>{score.label}</small>
                  </div>
                  <div className="bringup-nav__meter">
                    <ProgressMeter value={score.progress} tone={score.tone} compact />
                  </div>
                </button>
              )
            })}
          </nav>
        </aside>

        <div className="bringup-content">
          <div className="bringup-content__header">
            <div>
              <p className="eyebrow">Current page</p>
              <h3>
                {bringupPages.find((page) => page.id === activePage)?.label}
              </h3>
            </div>
            <div className="bringup-content__status">
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                Link {connected ? 'live' : 'offline'}
              </span>
              <span
                className={
                  serviceModeActive
                    ? 'status-badge is-on'
                    : serviceModeRequested
                      ? 'status-badge is-warn'
                      : 'status-badge'
                }
              >
                {serviceModeActive ? 'Service active' : serviceModeRequested ? 'Service requested' : 'Service off'}
              </span>
              {livePageScore !== null ? (
                <span className={livePageScore.tone === 'critical' ? 'status-badge is-warn' : 'status-badge is-on'}>
                  {livePageScore.label}
                </span>
              ) : null}
            </div>
          </div>

          {renderActivePage()}
        </div>
      </div>

      <div className="command-footer">
        <div className="inline-token">
          <Cpu size={14} />
          <span>Live action: {snapshot.bringup.tools.lastAction}</span>
        </div>
        <div className="inline-token">
          <span>Profile: {form.profileName}</span>
        </div>
      </div>

      {operation !== null ? (
        <ControllerBusyOverlay
          label={operation.label}
          detail={operation.detail}
          percent={operation.percent}
          tone={operation.tone}
          footer={
            operation.requiresConfirm
              ? 'Controller action failed. Review the message and confirm before controls unlock.'
              : 'Controller busy. Bring-up controls are locked until this action finishes.'
          }
          confirmLabel={operation.requiresConfirm ? 'Confirm and close' : undefined}
          onConfirm={operation.requiresConfirm ? dismissOperation : undefined}
        />
      ) : null}
      {pdNvmConfirmOpen ? (
        <ConfirmActionDialog
          title="Burn STUSB4500 PDOs to NVM"
          detail="This action is for final provisioning only. The STUSB4500 uses these NVM values as startup defaults after reset or power-up."
          confirmLabel="Burn to NVM"
          tone="critical"
          bullets={[
            'NVM endurance is finite. Do not use NVM burn for iterative tuning.',
            'This flow validates runtime PDO readback first, then burns NVM, then compares raw NVM readback before reporting success.',
            'A successful burn changes the controller startup defaults after STUSB reset or power cycle.',
          ]}
          onCancel={() => setPdNvmConfirmOpen(false)}
          onConfirm={() => {
            void burnPdProfileToNvm()
          }}
        />
      ) : null}
    </section>
  )
}
