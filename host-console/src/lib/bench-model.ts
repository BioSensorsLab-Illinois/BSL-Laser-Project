import type { BenchControlStatus, DeviceSnapshot } from '../types'
import {
  estimateTecVoltageFromTempC,
  estimateTempFromWavelengthNm,
  estimateWavelengthFromTempC,
} from './tec-calibration'

export const LASER_FULL_CURRENT_A = 5.2
export const LASER_FORWARD_VOLTAGE_V = 3
export const LASER_FULL_OPTICAL_POWER_W = 5
export const DRIVER_EFFICIENCY = 0.9
export const TEC_EFFICIENCY = 0.9
/* Green alignment laser is a fixed-current driver — ~0.4 W input
 * whenever the green-enable line is asserted (user spec 2026-04-16). */
export const GREEN_ALIGNMENT_POWER_W = 0.4
/* ToF-board front LED: ~3 W at 50 % PWM duty → linear 6 W at 100 %
 * (user spec 2026-04-16). Scales linearly with the active duty
 * because the LED driver is a buck-set-current PWM. */
export const TOF_LED_POWER_AT_FULL_W = 6.0

export interface BenchEstimate {
  commandedOpticalPowerW: number
  averageOpticalPowerW: number
  laserElectricalPowerW: number
  laserInputPowerW: number
  tecElectricalPowerW: number
  tecInputPowerW: number
  tecCoolingPowerW: number
  greenAlignmentInputPowerW: number
  tofLedInputPowerW: number
  totalEstimatedInputPowerW: number
  pdHeadroomW: number
  targetTempC: number
  targetLambdaNm: number
  targetTecVoltageV: number
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

export function makeDefaultBenchControlStatus(): BenchControlStatus {
  return {
    targetMode: 'lambda',
    runtimeMode: 'modulated_host',
    runtimeModeSwitchAllowed: false,
    runtimeModeLockReason: 'Enter deployment mode before changing runtime mode.',
    requestedAlignmentEnabled: false,
    appliedAlignmentEnabled: false,
    requestedNirEnabled: false,
    requestedCurrentA: 0,
    requestedLedEnabled: false,
    requestedLedDutyCyclePct: 0,
    appliedLedOwner: 'none',
    appliedLedPinHigh: false,
    illuminationEnabled: false,
    illuminationDutyCyclePct: 0,
    illuminationFrequencyHz: 20000,
    modulationEnabled: false,
    modulationFrequencyHz: 2000,
    modulationDutyCyclePct: 50,
    lowStateCurrentA: 0,
    hostControlReadiness: {
      nirBlockedReason: 'not-connected',
      alignmentBlockedReason: 'none',
      ledBlockedReason: 'not-connected',
      sbdnState: 'off',
    },
    usbDebugMock: {
      active: false,
      pdConflictLatched: false,
      enablePending: false,
      activatedAtMs: 0,
      deactivatedAtMs: 0,
      lastDisableReason: '',
    },
  }
}

export function resolveBenchControlStatus(snapshot: DeviceSnapshot): BenchControlStatus {
  return {
    ...makeDefaultBenchControlStatus(),
    ...snapshot.bench,
  }
}

export function opticalPowerFromCurrentA(currentA: number): number {
  return clamp(currentA, 0, LASER_FULL_CURRENT_A) * (LASER_FULL_OPTICAL_POWER_W / LASER_FULL_CURRENT_A)
}

export function currentFromOpticalPowerW(opticalPowerW: number): number {
  return clamp(opticalPowerW, 0, LASER_FULL_OPTICAL_POWER_W) * (LASER_FULL_CURRENT_A / LASER_FULL_OPTICAL_POWER_W)
}

export function deriveBenchEstimate(snapshot: DeviceSnapshot): BenchEstimate {
  const bench = resolveBenchControlStatus(snapshot)
  const highCurrentA = clamp(snapshot.laser.commandedCurrentA, 0, LASER_FULL_CURRENT_A)
  const lowCurrentA = bench.modulationEnabled
    ? 0
    : clamp(bench.lowStateCurrentA, 0, highCurrentA)
  const dutyFraction =
    snapshot.laser.nirEnabled && bench.modulationEnabled
      ? clamp(bench.modulationDutyCyclePct, 0, 100) / 100
      : snapshot.laser.nirEnabled
        ? 1
        : 0
  const commandedOpticalPowerW = opticalPowerFromCurrentA(highCurrentA)
  const averageCurrentA =
    snapshot.laser.nirEnabled
      ? dutyFraction * highCurrentA + (1 - dutyFraction) * lowCurrentA
      : 0
  const averageOpticalPowerW = snapshot.laser.nirEnabled
    ? dutyFraction * opticalPowerFromCurrentA(highCurrentA) + (1 - dutyFraction) * opticalPowerFromCurrentA(lowCurrentA)
    : 0
  const laserElectricalPowerW = averageCurrentA * LASER_FORWARD_VOLTAGE_V
  const laserInputPowerW = laserElectricalPowerW > 0 ? laserElectricalPowerW / DRIVER_EFFICIENCY : 0
  const tecElectricalPowerW = Math.abs(snapshot.tec.currentA * snapshot.tec.voltageV)
  const tecInputPowerW = tecElectricalPowerW > 0 ? tecElectricalPowerW / TEC_EFFICIENCY : 0
  const tecCoolingPowerW = tecElectricalPowerW * TEC_EFFICIENCY
  /*
   * Green alignment input power: fixed 0.4 W whenever the alignment
   * line is asserted. Constant per the alignment driver datasheet
   * (user spec 2026-04-16).
   */
  const greenAlignmentInputPowerW = snapshot.laser.alignmentEnabled
    ? GREEN_ALIGNMENT_POWER_W
    : 0
  /*
   * ToF-board front LED draw. Brightness source-of-truth: when the
   * deployment-armed path owns the LED (button_runtime publishes
   * ledOwned = true), use buttonBoard.ledBrightnessPct; otherwise
   * fall back to the bench `illuminationDutyCyclePct`. Linear power
   * scaling: 0 % → 0 W, 50 % → 3 W, 100 % → 6 W (user spec).
   */
  const ledDutyPct = snapshot.buttonBoard.ledOwned
    ? snapshot.buttonBoard.ledBrightnessPct
    : bench.illuminationEnabled
      ? bench.illuminationDutyCyclePct
      : 0
  const tofLedInputPowerW =
    (clamp(ledDutyPct, 0, 100) / 100) * TOF_LED_POWER_AT_FULL_W
  const totalEstimatedInputPowerW =
    laserInputPowerW +
    tecInputPowerW +
    greenAlignmentInputPowerW +
    tofLedInputPowerW
  const pdHeadroomW = snapshot.pd.negotiatedPowerW - totalEstimatedInputPowerW
  const targetTempC =
    bench.targetMode === 'lambda'
      ? estimateTempFromWavelengthNm(snapshot.tec.targetLambdaNm)
      : snapshot.tec.targetTempC
  const targetLambdaNm =
    bench.targetMode === 'lambda'
      ? snapshot.tec.targetLambdaNm
      : estimateWavelengthFromTempC(snapshot.tec.targetTempC)

  return {
    commandedOpticalPowerW,
    averageOpticalPowerW,
    laserElectricalPowerW,
    laserInputPowerW,
    tecElectricalPowerW,
    tecInputPowerW,
    tecCoolingPowerW,
    greenAlignmentInputPowerW,
    tofLedInputPowerW,
    totalEstimatedInputPowerW,
    pdHeadroomW,
    targetTempC,
    targetLambdaNm,
    targetTecVoltageV: estimateTecVoltageFromTempC(targetTempC),
  }
}
