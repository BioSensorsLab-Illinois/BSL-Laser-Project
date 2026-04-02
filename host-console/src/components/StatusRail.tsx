import { motion } from 'framer-motion'
import { Crosshair, Gauge, PlugZap, ScanLine, ShieldAlert, ThermometerSnowflake } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { deriveBenchEstimate } from '../lib/bench-model'
import { formatNumber } from '../lib/format'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import {
  computePitchMarginDeg,
  computeDistanceWindowPercent,
  computePitchMarginPercent,
  computePowerHeadroomPercent,
  computeTecSettlePercent,
  formatTofDistanceDetail,
  formatEnumLabel,
  formatTofWindowSummary,
  formatTofValidityLabel,
  getTofDisplayDistanceM,
  getHorizonPitchLimitDeg,
} from '../lib/presentation'
import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type { BringupModuleStatus, DeviceSnapshot } from '../types'

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

export function StatusRail({ snapshot, telemetryStore }: StatusRailProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const estimate = deriveBenchEstimate(liveSnapshot)
  const powerPercent = computePowerHeadroomPercent(liveSnapshot)
  const distancePercent = computeDistanceWindowPercent(liveSnapshot)
  const pitchPercent = computePitchMarginPercent(liveSnapshot)
  const pitchMarginDeg = computePitchMarginDeg(liveSnapshot)
  const pitchLimitDeg = getHorizonPitchLimitDeg(liveSnapshot)
  const tecPercent = computeTecSettlePercent(liveSnapshot)
  const pdAvailability = describeStripAvailability(liveSnapshot.bringup.modules.pd, 'USB-PD')
  const tofAvailability = describeStripAvailability(liveSnapshot.bringup.modules.tof, 'ToF range sensing')
  const imuAvailability = describeStripAvailability(liveSnapshot.bringup.modules.imu, 'IMU posture sensing')
  const tecAvailability = describeStripAvailability(liveSnapshot.bringup.modules.tec, 'TEC control')
  const laserAvailability = describeStripAvailability(liveSnapshot.bringup.modules.laserDriver, 'Laser driver')
  const tofDisplayDistance = getTofDisplayDistanceM(liveSnapshot)
  const tofHasRawReadback = liveSnapshot.peripherals.tof.distanceMm > 0
  const tofWithinWindow =
    tofDisplayDistance !== null &&
    tofDisplayDistance >= liveSnapshot.safety.tofMinRangeM &&
    tofDisplayDistance <= liveSnapshot.safety.tofMaxRangeM
  const horizonWarningBandDeg = Math.max(
    1.5,
    liveSnapshot.safety.horizonHysteresisDeg * 2,
  )

  const stats = [
    {
      key: 'power',
      label: 'Power headroom',
      icon: PlugZap,
      value: pdAvailability.available ? `${formatNumber(estimate.pdHeadroomW, 1)} W` : pdAvailability.value,
      detail: pdAvailability.available
        ? `${formatEnumLabel(liveSnapshot.session.powerTier)} from ${formatNumber(liveSnapshot.pd.negotiatedPowerW, 1)} W`
        : pdAvailability.detail,
      progress: pdAvailability.available ? powerPercent : 0,
      tone:
        !pdAvailability.available
          ? 'steady'
          : !liveSnapshot.pd.contractValid || liveSnapshot.pd.negotiatedPowerW <= 0
          ? 'critical'
          : powerPercent < 18
            ? 'critical'
            : powerPercent < 40
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
      detail: tofAvailability.available
        ? `${formatTofWindowSummary(liveSnapshot)} • ${formatTofDistanceDetail(liveSnapshot)}`
        : tofAvailability.detail,
      progress: tofAvailability.available ? distancePercent : 0,
      tone:
        !tofAvailability.available
          ? 'steady'
          : liveSnapshot.tof.valid && liveSnapshot.tof.fresh && distancePercent >= 55
          ? 'steady'
          : tofHasRawReadback && tofWithinWindow
            ? 'warning'
          : liveSnapshot.tof.valid && liveSnapshot.tof.fresh && distancePercent > 0
            ? 'warning'
            : 'critical',
      disabled: !tofAvailability.available,
    },
    {
      key: 'pitch',
      label: 'Horizon margin',
      icon: ShieldAlert,
      value: imuAvailability.available
        ? pitchMarginDeg !== null
          ? `${formatNumber(pitchMarginDeg, 1)}°`
          : 'Waiting'
        : imuAvailability.value,
      detail: imuAvailability.available
        ? pitchLimitDeg > 0
          ? `${formatNumber(liveSnapshot.imu.beamPitchDeg, 1)}° pitch vs ${formatNumber(pitchLimitDeg, 1)}° trip`
          : 'Horizon trip threshold is not available yet.'
        : imuAvailability.detail,
      progress: imuAvailability.available ? pitchPercent : 0,
      tone:
        !imuAvailability.available
          ? 'steady'
          : !liveSnapshot.imu.valid || !liveSnapshot.imu.fresh || pitchMarginDeg === null
          ? 'critical'
          : liveSnapshot.safety.horizonBlocked || pitchMarginDeg <= 0
          ? 'critical'
          : pitchMarginDeg <= horizonWarningBandDeg
            ? 'warning'
            : 'steady',
      disabled: !imuAvailability.available,
    },
    {
      key: 'tec',
      label: 'TEC settle',
      icon: ThermometerSnowflake,
      value: tecAvailability.available
        ? liveSnapshot.tec.tempGood
          ? 'Locked'
          : `${formatNumber(Math.abs(liveSnapshot.tec.targetTempC - liveSnapshot.tec.tempC), 1)}°C error`
        : tecAvailability.value,
      detail: tecAvailability.available
        ? `${formatNumber(liveSnapshot.tec.targetLambdaNm, 1)} nm target`
        : tecAvailability.detail,
      progress: tecAvailability.available ? tecPercent : 0,
      tone: !tecAvailability.available ? 'steady' : liveSnapshot.tec.tempGood ? 'steady' : tecPercent > 55 ? 'warning' : 'critical',
      disabled: !tecAvailability.available,
    },
    {
      key: 'output',
      label: 'Output posture',
      icon: liveSnapshot.laser.nirEnabled ? ScanLine : liveSnapshot.laser.alignmentEnabled ? Crosshair : ScanLine,
      value: laserAvailability.available
        ? liveSnapshot.laser.nirEnabled
          ? 'NIR laser on'
          : liveSnapshot.laser.alignmentEnabled
            ? 'Green laser on'
            : 'Beam safe'
        : laserAvailability.value,
      detail: laserAvailability.available
        ? `${formatNumber(estimate.averageOpticalPowerW, 2)} W optical estimate`
        : laserAvailability.detail,
      progress: laserAvailability.available
        ? liveSnapshot.laser.nirEnabled
          ? 18
          : liveSnapshot.laser.alignmentEnabled
            ? 55
            : 100
        : 0,
      tone: !laserAvailability.available ? 'steady' : liveSnapshot.laser.nirEnabled ? 'critical' : liveSnapshot.laser.alignmentEnabled ? 'warning' : 'steady',
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
