import { motion } from 'framer-motion'
import { Crosshair, Gauge, PlugZap, ScanLine, ShieldAlert, ThermometerSnowflake } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { deriveBenchEstimate } from '../lib/bench-model'
import { formatNumber } from '../lib/format'
import {
  computeDistanceWindowPercent,
  computePitchMarginPercent,
  computePowerHeadroomPercent,
  computeTecSettlePercent,
  formatEnumLabel,
} from '../lib/presentation'
import type { BringupModuleStatus, DeviceSnapshot } from '../types'

type StatusRailProps = {
  snapshot: DeviceSnapshot
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

export function StatusRail({ snapshot }: StatusRailProps) {
  const estimate = deriveBenchEstimate(snapshot)
  const powerPercent = computePowerHeadroomPercent(snapshot)
  const distancePercent = computeDistanceWindowPercent(snapshot)
  const pitchPercent = computePitchMarginPercent(snapshot)
  const tecPercent = computeTecSettlePercent(snapshot)
  const pdAvailability = describeStripAvailability(snapshot.bringup.modules.pd, 'USB-PD')
  const tofAvailability = describeStripAvailability(snapshot.bringup.modules.tof, 'ToF range sensing')
  const imuAvailability = describeStripAvailability(snapshot.bringup.modules.imu, 'IMU posture sensing')
  const tecAvailability = describeStripAvailability(snapshot.bringup.modules.tec, 'TEC control')
  const laserAvailability = describeStripAvailability(snapshot.bringup.modules.laserDriver, 'Laser driver')

  const stats = [
    {
      key: 'power',
      label: 'Power headroom',
      icon: PlugZap,
      value: pdAvailability.available ? `${formatNumber(estimate.pdHeadroomW, 1)} W` : pdAvailability.value,
      detail: pdAvailability.available
        ? `${formatEnumLabel(snapshot.session.powerTier)} from ${formatNumber(snapshot.pd.negotiatedPowerW, 1)} W`
        : pdAvailability.detail,
      progress: pdAvailability.available ? powerPercent : 0,
      tone:
        !pdAvailability.available
          ? 'steady'
          : !snapshot.pd.contractValid || snapshot.pd.negotiatedPowerW <= 0
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
      value: tofAvailability.available ? `${formatNumber(snapshot.tof.distanceM, 2)} m` : tofAvailability.value,
      detail: tofAvailability.available
        ? `${formatNumber(snapshot.tof.minRangeM, 2)}–${formatNumber(snapshot.tof.maxRangeM, 2)} m safe`
        : tofAvailability.detail,
      progress: tofAvailability.available ? distancePercent : 0,
      tone:
        !tofAvailability.available
          ? 'steady'
          : snapshot.tof.valid && snapshot.tof.fresh && distancePercent >= 55
          ? 'steady'
          : snapshot.tof.valid && snapshot.tof.fresh && distancePercent > 0
            ? 'warning'
            : 'critical',
      disabled: !tofAvailability.available,
    },
    {
      key: 'pitch',
      label: 'Horizon margin',
      icon: ShieldAlert,
      value: imuAvailability.available
        ? `${formatNumber(snapshot.imu.beamPitchLimitDeg - snapshot.imu.beamPitchDeg, 1)}°`
        : imuAvailability.value,
      detail: imuAvailability.available
        ? `${formatNumber(snapshot.imu.beamPitchDeg, 1)}° vs ${formatNumber(snapshot.imu.beamPitchLimitDeg, 1)}° limit`
        : imuAvailability.detail,
      progress: imuAvailability.available ? pitchPercent : 0,
      tone:
        !imuAvailability.available
          ? 'steady'
          : snapshot.imu.valid && snapshot.imu.fresh && pitchPercent >= 55
          ? 'steady'
          : snapshot.imu.valid && snapshot.imu.fresh && pitchPercent > 0
            ? 'warning'
            : 'critical',
      disabled: !imuAvailability.available,
    },
    {
      key: 'tec',
      label: 'TEC settle',
      icon: ThermometerSnowflake,
      value: tecAvailability.available
        ? snapshot.tec.tempGood
          ? 'Locked'
          : `${formatNumber(Math.abs(snapshot.tec.targetTempC - snapshot.tec.tempC), 1)}°C error`
        : tecAvailability.value,
      detail: tecAvailability.available
        ? `${formatNumber(snapshot.tec.targetLambdaNm, 1)} nm target`
        : tecAvailability.detail,
      progress: tecAvailability.available ? tecPercent : 0,
      tone: !tecAvailability.available ? 'steady' : snapshot.tec.tempGood ? 'steady' : tecPercent > 55 ? 'warning' : 'critical',
      disabled: !tecAvailability.available,
    },
    {
      key: 'output',
      label: 'Output posture',
      icon: snapshot.laser.nirEnabled ? ScanLine : snapshot.laser.alignmentEnabled ? Crosshair : ScanLine,
      value: laserAvailability.available
        ? snapshot.laser.nirEnabled
          ? 'NIR laser on'
          : snapshot.laser.alignmentEnabled
            ? 'Green laser on'
            : 'Beam safe'
        : laserAvailability.value,
      detail: laserAvailability.available
        ? `${formatNumber(estimate.averageOpticalPowerW, 2)} W optical estimate`
        : laserAvailability.detail,
      progress: laserAvailability.available
        ? snapshot.laser.nirEnabled
          ? 18
          : snapshot.laser.alignmentEnabled
            ? 55
            : 100
        : 0,
      tone: !laserAvailability.available ? 'steady' : snapshot.laser.nirEnabled ? 'critical' : snapshot.laser.alignmentEnabled ? 'warning' : 'steady',
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
