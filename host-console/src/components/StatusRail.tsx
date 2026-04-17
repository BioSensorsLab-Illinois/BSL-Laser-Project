import { motion } from 'framer-motion'
import { Crosshair, Gauge, PlugZap, ScanLine, ShieldAlert, ThermometerSnowflake } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { mergeObservedBringupModules, moduleMeta } from '../lib/bringup'
import { deriveBenchEstimate } from '../lib/bench-model'
import { formatNumber } from '../lib/format'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import {
  computeDistanceWindowPercent,
  computePitchMarginDeg,
  computePitchMarginPercent,
  computePowerUsagePercent,
  computeTecSettlePercent,
  formatEnumLabel,
  formatHorizonConditionDetail,
  formatTofValidityLabel,
  formatTofWindowSummary,
  getHorizonPitchClearDeg,
  getHorizonPitchLimitDeg,
  getTofDisplayDistanceM,
} from '../lib/presentation'
import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type { BringupModuleStatus, DeviceSnapshot, ModuleKey } from '../types'

type StatusRailProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
}

type StripAvailability = {
  available: boolean
  value: string
  detail: string
}

function describeStripAvailability(
  key: ModuleKey,
  status: BringupModuleStatus,
  label: string,
): StripAvailability {
  if (!status.expectedPresent) {
    return {
      available: false,
      value: 'Not installed',
      detail: `${label} is not declared on this bench build.`,
    }
  }

  if (moduleMeta[key].validationMode === 'monitored') {
    return {
      available: true,
      value: '',
      detail: '',
    }
  }

  if (!status.detected) {
    return {
      available: false,
      value: 'Awaiting probe',
      detail: `${label} has not responded to a successful probe yet.`,
    }
  }

  if (!status.healthy) {
    return {
      available: false,
      value: 'Needs check',
      detail: `${label} responded, but it is not marked healthy.`,
    }
  }

  return {
    available: true,
    value: '',
    detail: '',
  }
}

function toneFromWindowDistance(
  distanceM: number | null,
  minRangeM: number,
  maxRangeM: number,
  validSample: boolean,
): 'steady' | 'warning' | 'critical' {
  if (distanceM === null || maxRangeM <= minRangeM) {
    return 'critical'
  }

  if (distanceM < minRangeM || distanceM > maxRangeM) {
    return 'critical'
  }

  const warningBandM = (maxRangeM - minRangeM) * 0.1
  const nearLowerLimit = distanceM - minRangeM <= warningBandM
  const nearUpperLimit = maxRangeM - distanceM <= warningBandM

  if (!validSample || nearLowerLimit || nearUpperLimit) {
    return 'warning'
  }

  return 'steady'
}

function toneFromPitchMargin(
  pitchDeg: number,
  limitDeg: number,
  blocked: boolean,
  validSample: boolean,
): 'steady' | 'warning' | 'critical' {
  if (!validSample || limitDeg <= 0) {
    return 'critical'
  }

  if (blocked || pitchDeg >= limitDeg) {
    return 'critical'
  }

  const warningBandDeg = Math.max(0.5, limitDeg * 0.1)
  if (limitDeg - pitchDeg <= warningBandDeg) {
    return 'warning'
  }

  return 'steady'
}

export function StatusRail({ snapshot, telemetryStore }: StatusRailProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const liveModules = mergeObservedBringupModules(
    liveSnapshot.bringup.modules,
    liveSnapshot,
    true,
  )
  const estimate = deriveBenchEstimate(liveSnapshot)
  const powerPercent = computePowerUsagePercent(liveSnapshot)
  const distancePercent = computeDistanceWindowPercent(liveSnapshot)
  const pitchPercent = computePitchMarginPercent(liveSnapshot)
  const pitchMarginDeg = computePitchMarginDeg(liveSnapshot)
  const pitchLimitDeg = getHorizonPitchLimitDeg(liveSnapshot)
  const pitchClearDeg = getHorizonPitchClearDeg(liveSnapshot)
  const tecPercent = computeTecSettlePercent(liveSnapshot)
  const pdAvailability = describeStripAvailability('pd', liveModules.pd, 'USB-PD')
  const tofAvailability = describeStripAvailability('tof', liveModules.tof, 'ToF range sensing')
  const imuAvailability = describeStripAvailability('imu', liveModules.imu, 'IMU posture sensing')
  const tecAvailability = describeStripAvailability('tec', liveModules.tec, 'TEC control')
  const laserAvailability = describeStripAvailability('laserDriver', liveModules.laserDriver, 'Laser driver')
  const tofDisplayDistance = getTofDisplayDistanceM(liveSnapshot)
  const tofHasRawReadback = liveSnapshot.peripherals.tof.distanceMm > 0
  const tofWithinWindow =
    tofDisplayDistance !== null &&
    tofDisplayDistance >= liveSnapshot.safety.tofMinRangeM &&
    tofDisplayDistance <= liveSnapshot.safety.tofMaxRangeM
  const tofStripDetail = !tofAvailability.available
    ? tofAvailability.detail
    : liveSnapshot.tof.valid && liveSnapshot.tof.fresh
      ? formatTofWindowSummary(liveSnapshot)
      : tofHasRawReadback
        ? 'Raw sample only • safety hold'
        : liveSnapshot.peripherals.tof.reachable && liveSnapshot.peripherals.tof.configured
          ? 'Configured • awaiting stable sample'
          : liveSnapshot.peripherals.tof.reachable
            ? 'Reachable • setup incomplete'
            : 'Awaiting probe'
  const rangeTone = toneFromWindowDistance(
    tofDisplayDistance,
    liveSnapshot.safety.tofMinRangeM,
    liveSnapshot.safety.tofMaxRangeM,
    liveSnapshot.tof.valid && liveSnapshot.tof.fresh,
  )
  const horizonTone = toneFromPitchMargin(
    liveSnapshot.imu.beamPitchDeg,
    pitchLimitDeg,
    liveSnapshot.safety.horizonBlocked,
    liveSnapshot.imu.valid && liveSnapshot.imu.fresh && pitchMarginDeg !== null,
  )
  const tecErrorDeg = Math.abs(liveSnapshot.tec.targetTempC - liveSnapshot.tec.tempC)

  const stats = [
    {
      key: 'power',
      label: 'Power usage',
      icon: PlugZap,
      /*
       * 2026-04-16 user spec: show "used / total" with the bar filling
       * as draw climbs. `totalEstimatedInputPowerW` now sums NIR + TEC
       * + green alignment + ToF LED (see bench-model.ts).
       */
      value: pdAvailability.available
        ? `${formatNumber(estimate.totalEstimatedInputPowerW, 1)} / ${formatNumber(liveSnapshot.pd.negotiatedPowerW, 1)} W`
        : pdAvailability.value,
      detail: pdAvailability.available
        ? `${formatEnumLabel(liveSnapshot.session.powerTier)} · ${formatNumber(estimate.pdHeadroomW, 1)} W headroom`
        : pdAvailability.detail,
      progress: pdAvailability.available ? powerPercent : 0,
      tone:
        !pdAvailability.available
          ? 'steady'
          : !liveSnapshot.pd.contractValid || liveSnapshot.pd.negotiatedPowerW <= 0
            ? 'critical'
            : powerPercent > 82
              ? 'critical'
              : powerPercent > 60
                ? 'warning'
                : 'steady',
      disabled: !pdAvailability.available,
    },
    {
      key: 'distance',
      label: 'Range margin',
      icon: Gauge,
      value: tofAvailability.available
        ? tofDisplayDistance !== null
          ? `${formatNumber(tofDisplayDistance, 2)} m`
          : formatTofValidityLabel(liveSnapshot)
        : tofAvailability.value,
      detail: tofStripDetail,
      progress: tofAvailability.available ? distancePercent : 0,
      tone:
        !tofAvailability.available
          ? 'steady'
          : tofHasRawReadback && tofWithinWindow && rangeTone === 'steady' && !liveSnapshot.tof.valid
            ? 'warning'
            : rangeTone,
      disabled: !tofAvailability.available,
    },
    {
      key: 'pitch',
      label: 'Horizon guard',
      icon: ShieldAlert,
      value: imuAvailability.available
        ? pitchMarginDeg !== null
          ? `${formatNumber(liveSnapshot.imu.beamPitchDeg, 1)}° pitch`
          : 'Waiting'
        : imuAvailability.value,
      detail: imuAvailability.available
        ? pitchLimitDeg > pitchClearDeg
          ? liveSnapshot.safety.horizonBlocked
            ? formatHorizonConditionDetail(liveSnapshot)
            : `Trip above ${formatNumber(pitchLimitDeg, 1)}° • clear below ${formatNumber(pitchClearDeg, 1)}°`
          : formatHorizonConditionDetail(liveSnapshot)
        : imuAvailability.detail,
      progress:
        imuAvailability.available && !liveSnapshot.safety.horizonBlocked
          ? pitchPercent
          : 0,
      tone: !imuAvailability.available ? 'steady' : horizonTone,
      disabled: !imuAvailability.available,
    },
    {
      key: 'tec',
      label: 'TEC settle',
      icon: ThermometerSnowflake,
      value: tecAvailability.available
        ? liveSnapshot.tec.telemetryValid
          ? `${formatNumber(liveSnapshot.tec.tempC, 1)} / ${formatNumber(liveSnapshot.tec.targetTempC, 1)} °C`
          : 'OFF / INVALID'
        : tecAvailability.value,
      detail: tecAvailability.available
        ? liveSnapshot.tec.telemetryValid
          ? liveSnapshot.tec.tempGood
            ? 'Settled to target'
            : `${formatNumber(tecErrorDeg, 1)} °C from target • ${formatNumber(liveSnapshot.tec.voltageV, 2)} VTEC`
          : 'TEC readback is invalid while the rail is off or not yet good'
        : tecAvailability.detail,
      progress: tecAvailability.available ? tecPercent : 0,
      tone:
        !tecAvailability.available
          ? 'steady'
          : liveSnapshot.tec.tempGood
            ? 'steady'
            : tecPercent > 55
              ? 'warning'
              : 'critical',
      disabled: !tecAvailability.available,
    },
    {
      key: 'output',
      label: 'Output posture',
      icon: liveSnapshot.laser.nirEnabled ? ScanLine : liveSnapshot.laser.alignmentEnabled ? Crosshair : ScanLine,
      value: laserAvailability.available
        ? liveSnapshot.laser.nirEnabled
          ? 'NIR active'
          : liveSnapshot.laser.alignmentEnabled
            ? 'Green active'
            : liveSnapshot.laser.driverStandby
              ? 'Standby'
              : liveSnapshot.laser.telemetryValid
                ? 'Ready'
                : 'OFF / INVALID'
        : laserAvailability.value,
      detail: laserAvailability.available
        ? liveSnapshot.laser.telemetryValid
          ? liveSnapshot.laser.driverStandby
            ? `${formatNumber(estimate.averageOpticalPowerW, 2)} W optical estimate • standby asserted`
            : `${formatNumber(estimate.averageOpticalPowerW, 2)} W optical estimate • output path awake`
          : 'LD telemetry is invalid while the rail is off or SBDN is not high'
        : laserAvailability.detail,
      progress: laserAvailability.available
        ? liveSnapshot.laser.nirEnabled
          ? 18
          : liveSnapshot.laser.alignmentEnabled
            ? 55
            : 100
        : 0,
      tone:
        !laserAvailability.available
          ? 'steady'
          : liveSnapshot.laser.nirEnabled
            ? 'critical'
            : liveSnapshot.laser.alignmentEnabled
              ? 'warning'
              : 'steady',
      disabled: !laserAvailability.available,
    },
  ] as const

  return (
    <section className="status-rail">
      {stats.map((stat, index) => {
        const Icon = stat.icon

        return (
          <motion.article
            key={stat.key}
            className="status-strip"
            data-tone={stat.tone}
            data-disabled={stat.disabled ? 'true' : 'false'}
            initial={{ opacity: 0, y: 12 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ delay: index * 0.05, duration: 0.32 }}
          >
            <div className="status-strip__label">
              <Icon size={16} />
              <span>{stat.label}</span>
            </div>
            <strong>{stat.value}</strong>
            <span>{stat.detail}</span>
            <ProgressMeter value={stat.progress} tone={stat.tone} compact />
          </motion.article>
        )
      })}
    </section>
  )
}
