import type { CSSProperties } from 'react'
import { Compass } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { formatNumber } from '../lib/format'
import { computePitchMarginPercent } from '../lib/presentation'
import type { DeviceSnapshot } from '../types'

type ImuPostureCardProps = {
  snapshot: DeviceSnapshot
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

export function ImuPostureCard({
  snapshot,
}: ImuPostureCardProps) {
  const pitchDeg = snapshot.imu.beamPitchDeg
  const rollDeg = snapshot.imu.beamRollDeg
  const yawDeg = snapshot.imu.beamYawDeg
  const pitchLimitDeg = snapshot.imu.beamPitchLimitDeg
  const marginDeg = pitchLimitDeg - pitchDeg
  const streamReady = snapshot.imu.valid && snapshot.imu.fresh
  const belowHorizon = streamReady && pitchDeg < pitchLimitDeg
  const tone = !streamReady ? 'warning' : belowHorizon ? 'steady' : 'critical'
  const sceneStyle = {
    '--imu-pitch-deg': `${clamp(-pitchDeg, -35, 35)}deg`,
    '--imu-roll-deg': `${clamp(rollDeg, -45, 45)}deg`,
    '--imu-yaw-deg': `${clamp(yawDeg, -70, 70)}deg`,
    '--imu-yaw-pointer-deg': `${yawDeg}deg`,
  } as CSSProperties

  return (
    <article className="panel-cutout imu-posture-card" data-tone={tone}>
      <div className="cutout-head">
        <div>
          <p className="eyebrow">Live posture</p>
          <strong>3-axis beam-frame posture</strong>
        </div>
        <div className="status-badges">
          <span className={streamReady ? 'status-badge is-steady' : 'status-badge is-warning'}>
            {streamReady ? 'Fresh stream' : 'Awaiting live stream'}
          </span>
          <span className="status-badge is-muted">
            {snapshot.imu.beamYawRelative ? 'Yaw is relative' : 'Yaw absolute'}
          </span>
        </div>
      </div>

      <div className="imu-posture-card__viewport" style={sceneStyle}>
        <div className="imu-posture-card__scene">
          <div className="imu-posture-card__horizon" />
          <div className="imu-posture-card__safe-band" />
          <div className="imu-posture-card__yaw-dial">
            <div className="imu-posture-card__yaw-dial-ring" />
            <div className="imu-posture-card__yaw-dial-pointer" />
            <div className="imu-posture-card__yaw-dial-center" />
            <span className="imu-posture-card__yaw-dial-label">
              {snapshot.imu.beamYawRelative ? 'Relative yaw' : 'Yaw'}
            </span>
          </div>
          <div className="imu-posture-card__device">
            <div className="imu-posture-card__body">
              <div className="imu-posture-card__top" />
              <div className="imu-posture-card__side" />
              <div className="imu-posture-card__window" />
            </div>
            <div className="imu-posture-card__beam" data-tone={tone} />
          </div>
          <div className="imu-posture-card__horizon-label">
            <Compass size={14} />
            <span>Horizon</span>
          </div>
        </div>
      </div>

      <div className="bringup-fact-grid">
        <div>
          <span>Pitch</span>
          <strong>{formatNumber(pitchDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Roll</span>
          <strong>{formatNumber(rollDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Yaw</span>
          <strong>{formatNumber(yawDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Threshold</span>
          <strong>{formatNumber(pitchLimitDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Margin</span>
          <strong>{formatNumber(marginDeg, 1)} deg</strong>
        </div>
        <div>
          <span>State</span>
          <strong>{!streamReady ? 'Waiting' : belowHorizon ? 'Below horizon' : 'Above horizon'}</strong>
        </div>
      </div>

      <ProgressMeter value={computePitchMarginPercent(snapshot)} tone={tone} compact />

      <p className="inline-help">
        Pitch and roll are gravity-referenced from the beam-frame transform. Yaw is shown as a
        relative integrated heading and may drift over long bench runs.
      </p>
    </article>
  )
}
