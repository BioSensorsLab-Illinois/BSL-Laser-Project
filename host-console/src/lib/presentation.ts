import { deriveBenchEstimate } from './bench-model'
import { clampPercent, formatNumber } from './format'
import type {
  DeviceSnapshot,
  SystemState,
  TransportStatus,
} from '../types'

export type UiTone = 'steady' | 'warning' | 'critical'

export type SafetyCheck = {
  label: string
  pass: boolean
  detail: string
  progress: number
  tone: UiTone
}

export function getTofDisplayDistanceM(snapshot: DeviceSnapshot): number | null {
  if (snapshot.tof.valid && snapshot.tof.fresh) {
    return snapshot.tof.distanceM
  }

  if (snapshot.peripherals.tof.distanceMm > 0) {
    return snapshot.peripherals.tof.distanceMm / 1000
  }

  if (snapshot.tof.distanceM > 0) {
    return snapshot.tof.distanceM
  }

  return null
}

export function formatTofValidityLabel(snapshot: DeviceSnapshot): string {
  if (snapshot.tof.valid && snapshot.tof.fresh) {
    return 'Fresh and valid'
  }

  if (snapshot.peripherals.tof.reachable && snapshot.peripherals.tof.distanceMm > 0) {
    return 'Raw peripheral reading only'
  }

  if (snapshot.peripherals.tof.reachable && snapshot.peripherals.tof.configured) {
    return 'Configured, awaiting range sample'
  }

  if (snapshot.peripherals.tof.reachable) {
    return 'Reachable, not configured'
  }

  return 'Stale or invalid'
}

export function formatTofWindowSummary(snapshot: DeviceSnapshot): string {
  return `${formatNumber(snapshot.safety.tofMinRangeM, 2)}–${formatNumber(snapshot.safety.tofMaxRangeM, 2)} m safe • ${snapshot.safety.tofStaleMs} ms timeout`
}

export function formatTofDistanceDetail(snapshot: DeviceSnapshot): string {
  const distance = getTofDisplayDistanceM(snapshot)

  if (distance !== null && snapshot.tof.valid && snapshot.tof.fresh) {
    return `${formatNumber(distance, 2)} m live • controller-valid safety sample`
  }

  if (distance !== null && snapshot.peripherals.tof.distanceMm > 0) {
    return `${formatNumber(distance, 2)} m raw VL53L1X readback • not yet safety-valid`
  }

  if (snapshot.peripherals.tof.reachable && snapshot.peripherals.tof.configured) {
    return 'VL53L1X is configured, but no data-ready range sample is available yet'
  }

  if (snapshot.peripherals.tof.reachable) {
    return 'VL53L1X responds on I2C, but setup has not completed yet'
  }

  return 'No live VL53L1X reading is available yet'
}

export function formatEnumLabel(value: string): string {
  const uppercaseTokens = new Set([
    'NIR',
    'TEC',
    'IMU',
    'TOF',
    'USB',
    'PD',
    'LD',
    'RST',
    'BOOT',
  ])

  return value
    .split('_')
    .filter(Boolean)
    .map((segment) =>
      uppercaseTokens.has(segment.toUpperCase())
        ? segment.toUpperCase()
        : segment.charAt(0).toUpperCase() + segment.slice(1).toLowerCase(),
    )
    .join(' ')
}

export function toneFromSystemState(state: SystemState): UiTone {
  if (state === 'FAULT_LATCHED') {
    return 'critical'
  }

  if (
    state === 'PROGRAMMING_ONLY' ||
    state === 'TEC_SETTLING' ||
    state === 'TEC_WARMUP' ||
    state === 'SERVICE_MODE'
  ) {
    return 'warning'
  }

  return 'steady'
}

export function toneFromTransportStatus(status: TransportStatus): UiTone {
  if (status === 'error' || status === 'disconnected') {
    return 'critical'
  }

  if (status === 'connecting') {
    return 'warning'
  }

  return 'steady'
}

export function computePowerHeadroomPercent(snapshot: DeviceSnapshot): number {
  if (!snapshot.pd.contractValid || snapshot.pd.negotiatedPowerW <= 0) {
    return 0
  }

  const estimate = deriveBenchEstimate(snapshot)
  return clampPercent((estimate.pdHeadroomW / snapshot.pd.negotiatedPowerW) * 100)
}

export function computeDistanceWindowPercent(snapshot: DeviceSnapshot): number {
  const distance = getTofDisplayDistanceM(snapshot)

  if (distance === null) {
    return 0
  }

  const min = snapshot.safety.tofMinRangeM
  const max = snapshot.safety.tofMaxRangeM

  if (distance < min || distance > max || max <= min) {
    return 0
  }

  const center = (min + max) / 2
  const halfWindow = (max - min) / 2
  const margin = halfWindow - Math.abs(distance - center)

  return clampPercent((margin / halfWindow) * 100)
}

export function computePitchMarginPercent(snapshot: DeviceSnapshot): number {
  if (!snapshot.imu.valid || !snapshot.imu.fresh || snapshot.imu.beamPitchLimitDeg <= 0) {
    return 0
  }

  const margin = snapshot.imu.beamPitchLimitDeg - snapshot.imu.beamPitchDeg
  return clampPercent((margin / snapshot.imu.beamPitchLimitDeg) * 100)
}

export function computeTecSettlePercent(snapshot: DeviceSnapshot): number {
  if (snapshot.tec.tempGood) {
    return 100
  }

  const error = Math.abs(snapshot.tec.targetTempC - snapshot.tec.tempC)
  return clampPercent(100 - error * 12)
}

function toneFromProgress(progress: number, pass: boolean): UiTone {
  if (!pass || progress <= 15) {
    return 'critical'
  }

  if (progress < 65) {
    return 'warning'
  }

  return 'steady'
}

export function buildSafetyChecks(snapshot: DeviceSnapshot): SafetyCheck[] {
  const pitchProgress = computePitchMarginPercent(snapshot)
  const distanceProgress = computeDistanceWindowPercent(snapshot)
  const tecProgress = computeTecSettlePercent(snapshot)
  const tofDisplayDistance = getTofDisplayDistanceM(snapshot)
  const tofWindowDetail = formatTofWindowSummary(snapshot)
  const tofPass =
    snapshot.tof.valid &&
    snapshot.tof.fresh &&
    tofDisplayDistance !== null &&
    tofDisplayDistance >= snapshot.safety.tofMinRangeM &&
    tofDisplayDistance <= snapshot.safety.tofMaxRangeM

  let tofDetail = 'No valid range sample'
  if (tofDisplayDistance !== null && snapshot.tof.valid && snapshot.tof.fresh) {
    tofDetail = `${formatNumber(tofDisplayDistance, 2)} m live • ${tofWindowDetail}`
  } else if (tofDisplayDistance !== null) {
    tofDetail = `${formatNumber(tofDisplayDistance, 2)} m raw • controller still holds unsafe`
  } else if (snapshot.peripherals.tof.reachable && snapshot.peripherals.tof.configured) {
    tofDetail = 'VL53L1X configured • no data-ready sample yet'
  } else if (snapshot.peripherals.tof.reachable) {
    tofDetail = 'VL53L1X reachable • configuration incomplete'
  }

  const checks: SafetyCheck[] = [
    {
      label: 'PD power tier',
      pass: snapshot.session.powerTier === 'full',
      detail: formatEnumLabel(snapshot.session.powerTier),
      progress:
        snapshot.session.powerTier === 'full'
          ? 100
          : snapshot.session.powerTier === 'reduced'
            ? 58
            : snapshot.session.powerTier === 'programming_only'
              ? 18
              : 0,
      tone:
        snapshot.session.powerTier === 'full'
          ? 'steady'
          : snapshot.session.powerTier === 'reduced'
            ? 'warning'
            : 'critical',
    },
    {
      label: 'LD rail verified',
      pass: snapshot.rails.ld.enabled && snapshot.rails.ld.pgood,
      detail: snapshot.rails.ld.pgood ? 'PGOOD asserted' : 'LD rail not good',
      progress: snapshot.rails.ld.enabled && snapshot.rails.ld.pgood ? 100 : 0,
      tone: snapshot.rails.ld.enabled && snapshot.rails.ld.pgood ? 'steady' : 'critical',
    },
    {
      label: 'TEC settled',
      pass: snapshot.tec.tempGood,
      detail: snapshot.tec.tempGood
        ? `${formatNumber(snapshot.tec.tempC, 2)} °C locked`
        : `${snapshot.tec.settlingSecondsRemaining}s remaining`,
      progress: tecProgress,
      tone: toneFromProgress(tecProgress, snapshot.tec.tempGood),
    },
    {
      label: 'Orientation safe',
      pass:
        snapshot.imu.valid &&
        snapshot.imu.fresh &&
        snapshot.imu.beamPitchDeg < snapshot.imu.beamPitchLimitDeg,
      detail: `${formatNumber(snapshot.imu.beamPitchDeg, 1)}° measured`,
      progress: pitchProgress,
      tone: toneFromProgress(
        pitchProgress,
        snapshot.imu.valid &&
          snapshot.imu.fresh &&
          snapshot.imu.beamPitchDeg < snapshot.imu.beamPitchLimitDeg,
      ),
    },
    {
      label: 'Distance window safe',
      pass: tofPass,
      detail: tofDetail,
      progress: distanceProgress,
      tone: toneFromProgress(distanceProgress, tofPass),
    },
    {
      label: 'Driver loop good',
      pass: snapshot.laser.loopGood,
      detail: snapshot.laser.loopGood ? 'Loop stable' : 'Loop mismatch',
      progress: snapshot.laser.loopGood ? 100 : 0,
      tone: snapshot.laser.loopGood ? 'steady' : 'critical',
    },
    {
      label: 'No latched fault',
      pass: !snapshot.fault.latched,
      detail: snapshot.fault.latched ? snapshot.fault.activeCode : 'Clean',
      progress: snapshot.fault.latched ? 0 : 100,
      tone: snapshot.fault.latched ? 'critical' : 'steady',
    },
    {
      label: 'Output path off by default',
      pass: !snapshot.laser.nirEnabled,
      detail: snapshot.laser.driverStandby ? 'Driver in standby' : 'Standby released',
      progress:
        !snapshot.laser.nirEnabled && snapshot.laser.driverStandby
          ? 100
          : snapshot.laser.alignmentEnabled
            ? 40
            : 0,
      tone:
        !snapshot.laser.nirEnabled && snapshot.laser.driverStandby
          ? 'steady'
          : snapshot.laser.alignmentEnabled
            ? 'warning'
            : 'critical',
    },
  ]

  return checks
}

export function summarizeSafetyChecks(checks: SafetyCheck[]) {
  const passCount = checks.filter((check) => check.pass).length
  const total = checks.length
  const percent = total === 0 ? 0 : Math.round((passCount / total) * 100)

  let tone: UiTone = 'critical'
  let label = 'Hold: bench is not ready'

  if (percent >= 88) {
    tone = 'steady'
    label = 'Ready for guarded bench work'
  } else if (percent >= 50) {
    tone = 'warning'
    label = 'Partial readiness: review failing gates'
  }

  return {
    passCount,
    total,
    percent,
    tone,
    label,
  }
}
