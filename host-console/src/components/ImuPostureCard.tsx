import { useEffect, useRef, useState } from 'react'

import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import { formatNumber } from '../lib/format'
import {
  computePitchMarginPercent,
  getHorizonPitchClearDeg,
  getHorizonPitchLimitDeg,
} from '../lib/presentation'
import { ProgressMeter } from './ProgressMeter'
import type { DeviceSnapshot } from '../types'

type ImuPostureCardProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
}

type DisplayPose = {
  pitchDeg: number
  rollDeg: number
}

const PITCH_MARKS_DEG = [-40, -30, -20, -10, 10, 20, 30, 40]
const ROLL_TICKS_DEG = [-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60]
const PITCH_TO_PIXELS = 2.3

function clamp(value: number, min: number, max: number): number {
  if (value < min) {
    return min
  }

  if (value > max) {
    return max
  }

  return value
}

function rollTickPath(angleDeg: number): string {
  const angleRad = ((angleDeg - 90) * Math.PI) / 180
  const outerRadius = angleDeg % 30 === 0 ? 102 : 96
  const innerRadius = angleDeg % 30 === 0 ? 86 : 90
  const x1 = 160 + Math.cos(angleRad) * innerRadius
  const y1 = 120 + Math.sin(angleRad) * innerRadius
  const x2 = 160 + Math.cos(angleRad) * outerRadius
  const y2 = 120 + Math.sin(angleRad) * outerRadius
  return `M ${x1} ${y1} L ${x2} ${y2}`
}

function pitchMarkWidth(markDeg: number): number {
  return Math.abs(markDeg) % 20 === 0 ? 116 : 84
}

export function ImuPostureCard({
  snapshot,
  telemetryStore,
}: ImuPostureCardProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const pitchDeg = liveSnapshot.imu.beamPitchDeg
  const rollDeg = liveSnapshot.imu.beamRollDeg
  const pitchLimitDeg = getHorizonPitchLimitDeg(liveSnapshot)
  const clearPitchDeg = getHorizonPitchClearDeg(liveSnapshot)
  const pitchMarginPercent = computePitchMarginPercent(liveSnapshot)
  const streamReady = liveSnapshot.imu.valid && liveSnapshot.imu.fresh
  const controllerSafe = streamReady && !liveSnapshot.safety.horizonBlocked
  const tone = !streamReady ? 'warning' : controllerSafe ? 'steady' : 'critical'

  const [displayPose, setDisplayPose] = useState<DisplayPose>({
    pitchDeg,
    rollDeg,
  })
  const targetPoseRef = useRef<DisplayPose>({
    pitchDeg,
    rollDeg,
  })
  const currentPoseRef = useRef<DisplayPose>({
    pitchDeg,
    rollDeg,
  })
  const averageSampleIntervalMsRef = useRef(110)
  const lastSampleAtRef = useRef<number | null>(null)
  const streamReadyRef = useRef(streamReady)

  useEffect(() => {
    const now = window.performance.now()
    if (lastSampleAtRef.current !== null) {
      averageSampleIntervalMsRef.current = clamp(
        averageSampleIntervalMsRef.current * 0.72 + (now - lastSampleAtRef.current) * 0.28,
        40,
        220,
      )
    }
    lastSampleAtRef.current = now
    targetPoseRef.current = {
      pitchDeg,
      rollDeg,
    }
    streamReadyRef.current = streamReady
  }, [pitchDeg, rollDeg, streamReady])

  useEffect(() => {
    let frameId = 0
    let lastFrameAt = 0

    const animate = (frameAt: number) => {
      const deltaMs = lastFrameAt === 0 ? 16.7 : clamp(frameAt - lastFrameAt, 8, 40)
      const current = currentPoseRef.current
      const target = targetPoseRef.current
      const responseMs = streamReadyRef.current
        ? Math.max(85, averageSampleIntervalMsRef.current * 0.92)
        : 220
      const blend = 1 - Math.exp(-deltaMs / responseMs)

      lastFrameAt = frameAt
      current.pitchDeg += (target.pitchDeg - current.pitchDeg) * blend
      current.rollDeg += (target.rollDeg - current.rollDeg) * blend

      if (
        Math.abs(target.pitchDeg - current.pitchDeg) < 0.02 &&
        Math.abs(target.rollDeg - current.rollDeg) < 0.02
      ) {
        current.pitchDeg = target.pitchDeg
        current.rollDeg = target.rollDeg
      }

      setDisplayPose({
        pitchDeg: current.pitchDeg,
        rollDeg: current.rollDeg,
      })

      frameId = window.requestAnimationFrame(animate)
    }

    frameId = window.requestAnimationFrame(animate)
    return () => {
      window.cancelAnimationFrame(frameId)
    }
  }, [])

  const horizonTranslateY = clamp(displayPose.pitchDeg * PITCH_TO_PIXELS, -110, 110)

  return (
    <article className="panel-cutout imu-posture-card" data-tone={tone}>
      <div className="cutout-head">
        <div>
          <p className="eyebrow">Live posture</p>
          <strong>2D beam attitude</strong>
        </div>
        <div className="status-badges">
          <span className={streamReady ? 'status-badge is-steady' : 'status-badge is-warning'}>
            {streamReady ? 'Fresh stream' : 'Awaiting live stream'}
          </span>
          <span className={controllerSafe ? 'status-badge is-steady' : 'status-badge is-warn'}>
            {!streamReady
              ? 'Awaiting interlock truth'
              : controllerSafe
                ? 'Controller clear'
                : 'Controller blocked'}
          </span>
          <span className="status-badge is-muted">
            Trip {formatNumber(pitchLimitDeg, 1)}° / clear {formatNumber(clearPitchDeg, 1)}°
          </span>
        </div>
      </div>

      <div className="imu-posture-card__viewport">
        <svg
          className="imu-posture-card__svg"
          viewBox="0 0 320 240"
          role="img"
          aria-label="2D pitch and roll attitude indicator"
        >
          <defs>
            <linearGradient id="imuSky" x1="0" x2="0" y1="0" y2="1">
              <stop offset="0%" stopColor="rgba(122, 202, 235, 0.95)" />
              <stop offset="100%" stopColor="rgba(202, 240, 255, 0.92)" />
            </linearGradient>
            <linearGradient id="imuGround" x1="0" x2="0" y1="0" y2="1">
              <stop offset="0%" stopColor="rgba(72, 99, 76, 0.95)" />
              <stop offset="100%" stopColor="rgba(35, 48, 39, 0.98)" />
            </linearGradient>
            <clipPath id="imuAttitudeMask">
              <rect x="20" y="18" width="280" height="204" rx="28" ry="28" />
            </clipPath>
          </defs>

          <g clipPath="url(#imuAttitudeMask)">
            <rect x="20" y="18" width="280" height="204" fill="rgba(240, 245, 246, 0.78)" />
            <g
              transform={`translate(160 120) rotate(${displayPose.rollDeg}) translate(0 ${horizonTranslateY})`}
            >
              <rect x="-260" y="-260" width="520" height="260" fill="url(#imuSky)" />
              <rect x="-260" y="0" width="520" height="260" fill="url(#imuGround)" />
              <line
                x1="-260"
                y1="0"
                x2="260"
                y2="0"
                stroke="rgba(245, 248, 248, 0.92)"
                strokeWidth="3"
              />

              {PITCH_MARKS_DEG.map((markDeg) => {
                const y = -markDeg * PITCH_TO_PIXELS
                const width = pitchMarkWidth(markDeg)
                const label = Math.abs(markDeg)
                return (
                  <g key={markDeg} transform={`translate(0 ${y})`}>
                    <line
                      x1={-width / 2}
                      y1="0"
                      x2={width / 2}
                      y2="0"
                      stroke="rgba(242, 248, 248, 0.82)"
                      strokeWidth={Math.abs(markDeg) % 20 === 0 ? 2.4 : 1.4}
                    />
                    <text
                      x={-width / 2 - 14}
                      y="5"
                      fill="rgba(242, 248, 248, 0.78)"
                      fontSize="11"
                      textAnchor="end"
                    >
                      {label}
                    </text>
                    <text
                      x={width / 2 + 14}
                      y="5"
                      fill="rgba(242, 248, 248, 0.78)"
                      fontSize="11"
                    >
                      {label}
                    </text>
                  </g>
                )
              })}
            </g>
          </g>

          <path
            d="M 88 52 A 92 92 0 0 1 232 52"
            fill="none"
            stroke="rgba(29, 43, 40, 0.14)"
            strokeWidth="12"
            strokeLinecap="round"
          />
          <path
            d="M 88 52 A 92 92 0 0 1 232 52"
            fill="none"
            stroke="rgba(255, 255, 255, 0.72)"
            strokeWidth="4"
            strokeLinecap="round"
          />
          {ROLL_TICKS_DEG.map((tickDeg) => (
            <path
              key={tickDeg}
              d={rollTickPath(tickDeg)}
              fill="none"
              stroke="rgba(31, 46, 44, 0.72)"
              strokeWidth={tickDeg % 30 === 0 ? 2.4 : 1.5}
              strokeLinecap="round"
            />
          ))}
          <path
            d="M 160 26 L 152 40 L 168 40 Z"
            fill={tone === 'critical' ? 'rgba(209, 95, 80, 0.96)' : 'rgba(27, 52, 50, 0.9)'}
          />

          <g className="imu-posture-card__reticle">
            <circle
              cx="160"
              cy="120"
              r="18"
              fill="rgba(255, 255, 255, 0.18)"
              stroke="rgba(255, 255, 255, 0.28)"
              strokeWidth="1.2"
            />
            <path
              d="M 118 120 L 145 120 M 175 120 L 202 120"
              fill="none"
              stroke="rgba(22, 34, 38, 0.94)"
              strokeWidth="7"
              strokeLinecap="round"
            />
            <path
              d="M 120 120 L 146 120 M 174 120 L 200 120"
              fill="none"
              stroke="rgba(240, 249, 248, 0.96)"
              strokeWidth="3"
              strokeLinecap="round"
            />
            <path
              d="M 148 120 L 160 129 L 172 120"
              fill="none"
              stroke="rgba(22, 34, 38, 0.94)"
              strokeWidth="6"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
            <path
              d="M 148 120 L 160 129 L 172 120"
              fill="none"
              stroke="rgba(240, 249, 248, 0.96)"
              strokeWidth="2.5"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
            <path
              d="M 160 120 L 160 144"
              fill="none"
              stroke="rgba(240, 249, 248, 0.72)"
              strokeWidth="1.5"
              strokeLinecap="round"
            />
          </g>
        </svg>
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
          <span>Trip threshold</span>
          <strong>{formatNumber(pitchLimitDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Clear threshold</span>
          <strong>{formatNumber(clearPitchDeg, 1)} deg</strong>
        </div>
        <div>
          <span>Stream</span>
          <strong>{streamReady ? 'Fresh' : 'Waiting'}</strong>
        </div>
        <div>
          <span>State</span>
          <strong>
            {!streamReady
              ? 'Waiting'
              : controllerSafe
                ? 'Controller clear'
                : 'Blocked until clear'}
          </strong>
        </div>
      </div>

      <ProgressMeter
        value={controllerSafe ? pitchMarginPercent : 0}
        tone={tone}
        compact
      />

      <p className="inline-help">
        The attitude view is now pitch and roll only. Pitch shifts the horizon vertically,
        roll rotates it around the reticle, and the center reticle stays fixed so the motion
        reads like an instrument instead of a decorative 3D object.
      </p>
      <p className="inline-help">
        Horizon interlock state follows controller truth, including hysteresis. A slightly
        negative pitch can still remain blocked until it clears the lower reset threshold.
      </p>
    </article>
  )
}
