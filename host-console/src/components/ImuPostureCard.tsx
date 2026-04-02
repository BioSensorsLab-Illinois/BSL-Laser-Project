import { useEffect, useRef } from 'react'

import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import { formatNumber } from '../lib/format'
import { computePitchMarginPercent } from '../lib/presentation'
import { ProgressMeter } from './ProgressMeter'
import type { DeviceSnapshot } from '../types'

type ImuPostureCardProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
}

type DisplayPose = {
  pitchDeg: number
  rollDeg: number
  yawDeg: number
}

type Vec3 = {
  x: number
  y: number
  z: number
}

type FaceSpec = {
  vertices: [Vec3, Vec3, Vec3, Vec3]
  normal: Vec3
  fill: string
  stroke: string
}

type RenderState = {
  tone: 'warning' | 'steady' | 'critical'
  streamReady: boolean
  yawRelative: boolean
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

function wrapDegrees(value: number): number {
  let wrapped = value

  while (wrapped > 180) {
    wrapped -= 360
  }

  while (wrapped < -180) {
    wrapped += 360
  }

  return wrapped
}

function degToRad(value: number): number {
  return value * (Math.PI / 180)
}

function rotateX(point: Vec3, angleRad: number): Vec3 {
  const cos = Math.cos(angleRad)
  const sin = Math.sin(angleRad)
  return {
    x: point.x,
    y: point.y * cos - point.z * sin,
    z: point.y * sin + point.z * cos,
  }
}

function rotateY(point: Vec3, angleRad: number): Vec3 {
  const cos = Math.cos(angleRad)
  const sin = Math.sin(angleRad)
  return {
    x: point.x * cos + point.z * sin,
    y: point.y,
    z: -point.x * sin + point.z * cos,
  }
}

function rotateZ(point: Vec3, angleRad: number): Vec3 {
  const cos = Math.cos(angleRad)
  const sin = Math.sin(angleRad)
  return {
    x: point.x * cos - point.y * sin,
    y: point.x * sin + point.y * cos,
    z: point.z,
  }
}

function transformPoint(point: Vec3, pose: DisplayPose): Vec3 {
  let transformed = point
  transformed = rotateY(transformed, degToRad(clamp(pose.yawDeg * 0.62, -115, 115)))
  transformed = rotateX(transformed, degToRad(clamp(-pose.pitchDeg, -42, 42)))
  transformed = rotateZ(transformed, degToRad(clamp(pose.rollDeg, -58, 58)))
  transformed = rotateY(transformed, degToRad(-28))
  transformed = rotateX(transformed, degToRad(13))
  return transformed
}

function projectPoint(point: Vec3, width: number, height: number): { x: number; y: number; scale: number } {
  const cameraDistance = 9.4
  const perspective = Math.min(width, height) * 0.28
  const depth = point.z + cameraDistance
  const scale = perspective / depth

  return {
    x: width * 0.52 + point.x * scale,
    y: height * 0.6 - point.y * scale,
    scale,
  }
}

function cross(a: Vec3, b: Vec3): Vec3 {
  return {
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  }
}

function subtract(a: Vec3, b: Vec3): Vec3 {
  return {
    x: a.x - b.x,
    y: a.y - b.y,
    z: a.z - b.z,
  }
}

function averageDepth(vertices: Vec3[]): number {
  return vertices.reduce((sum, vertex) => sum + vertex.z, 0) / vertices.length
}

function buildBox(center: Vec3, size: Vec3, palette: {
  top: string
  bottom: string
  left: string
  right: string
  front: string
  back: string
  stroke: string
}): FaceSpec[] {
  const hx = size.x / 2
  const hy = size.y / 2
  const hz = size.z / 2

  const p = {
    lbf: { x: center.x - hx, y: center.y - hy, z: center.z + hz },
    lbb: { x: center.x - hx, y: center.y - hy, z: center.z - hz },
    ltf: { x: center.x - hx, y: center.y + hy, z: center.z + hz },
    ltb: { x: center.x - hx, y: center.y + hy, z: center.z - hz },
    rbf: { x: center.x + hx, y: center.y - hy, z: center.z + hz },
    rbb: { x: center.x + hx, y: center.y - hy, z: center.z - hz },
    rtf: { x: center.x + hx, y: center.y + hy, z: center.z + hz },
    rtb: { x: center.x + hx, y: center.y + hy, z: center.z - hz },
  }

  return [
    {
      vertices: [p.ltf, p.rtf, p.rtb, p.ltb],
      normal: { x: 0, y: 1, z: 0 },
      fill: palette.top,
      stroke: palette.stroke,
    },
    {
      vertices: [p.lbf, p.lbb, p.rbb, p.rbf],
      normal: { x: 0, y: -1, z: 0 },
      fill: palette.bottom,
      stroke: palette.stroke,
    },
    {
      vertices: [p.lbb, p.ltb, p.rtb, p.rbb],
      normal: { x: 0, y: 0, z: -1 },
      fill: palette.left,
      stroke: palette.stroke,
    },
    {
      vertices: [p.lbf, p.rbf, p.rtf, p.ltf],
      normal: { x: 0, y: 0, z: 1 },
      fill: palette.right,
      stroke: palette.stroke,
    },
    {
      vertices: [p.rbb, p.rtb, p.rtf, p.rbf],
      normal: { x: 1, y: 0, z: 0 },
      fill: palette.front,
      stroke: palette.stroke,
    },
    {
      vertices: [p.lbb, p.lbf, p.ltf, p.ltb],
      normal: { x: -1, y: 0, z: 0 },
      fill: palette.back,
      stroke: palette.stroke,
    },
  ]
}

function drawPolygon(
  ctx: CanvasRenderingContext2D,
  points: Array<{ x: number; y: number }>,
  fill: string,
  stroke: string,
): void {
  if (points.length === 0) {
    return
  }

  ctx.beginPath()
  ctx.moveTo(points[0].x, points[0].y)
  for (let index = 1; index < points.length; index += 1) {
    ctx.lineTo(points[index].x, points[index].y)
  }
  ctx.closePath()
  ctx.fillStyle = fill
  ctx.fill()
  ctx.strokeStyle = stroke
  ctx.lineWidth = 1
  ctx.stroke()
}

function drawGrid(ctx: CanvasRenderingContext2D, width: number, height: number): void {
  const horizonY = height * 0.56
  const vanishingX = width * 0.72

  ctx.save()
  ctx.strokeStyle = 'rgba(61, 93, 100, 0.12)'
  ctx.lineWidth = 1

  for (let index = -5; index <= 9; index += 1) {
    const x = width * 0.16 + index * 34
    ctx.beginPath()
    ctx.moveTo(x, height - 10)
    ctx.lineTo(vanishingX + index * 8, horizonY)
    ctx.stroke()
  }

  for (let index = 0; index < 7; index += 1) {
    const y = horizonY + 18 + index * 20
    ctx.beginPath()
    ctx.moveTo(width * 0.12 + index * 4, y)
    ctx.lineTo(width * 0.93 - index * 10, y - index * 2)
    ctx.stroke()
  }

  ctx.restore()
}

function drawYawDial(
  ctx: CanvasRenderingContext2D,
  pose: DisplayPose,
  state: RenderState,
): void {
  const centerX = 78
  const centerY = 74
  const radius = 40

  ctx.save()
  ctx.beginPath()
  ctx.arc(centerX, centerY, radius, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(255, 255, 255, 0.72)'
  ctx.fill()
  ctx.strokeStyle = 'rgba(27, 52, 50, 0.18)'
  ctx.lineWidth = 1
  ctx.stroke()

  ctx.beginPath()
  ctx.arc(centerX, centerY, radius - 10, 0, Math.PI * 2)
  ctx.strokeStyle = 'rgba(27, 52, 50, 0.12)'
  ctx.stroke()

  const pointerAngle = degToRad(wrapDegrees(pose.yawDeg) - 90)
  ctx.beginPath()
  ctx.moveTo(centerX, centerY)
  ctx.lineTo(
    centerX + Math.cos(pointerAngle) * (radius - 10),
    centerY + Math.sin(pointerAngle) * (radius - 10),
  )
  ctx.strokeStyle =
    state.tone === 'critical'
      ? 'rgba(209, 95, 80, 0.95)'
      : state.tone === 'warning'
        ? 'rgba(210, 154, 58, 0.95)'
        : 'rgba(50, 188, 141, 0.95)'
  ctx.lineWidth = 4
  ctx.lineCap = 'round'
  ctx.stroke()

  ctx.beginPath()
  ctx.arc(centerX, centerY, 6, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(27, 52, 50, 0.9)'
  ctx.fill()

  ctx.fillStyle = 'rgba(58, 79, 79, 0.9)'
  ctx.font = '700 11px "IBM Plex Mono", monospace'
  ctx.textAlign = 'center'
  ctx.fillText(state.yawRelative ? 'REL YAW' : 'YAW', centerX, centerY + radius + 18)
  ctx.restore()
}

function drawBeam(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  pose: DisplayPose,
  state: RenderState,
): void {
  const origin = transformPoint({ x: 2.45, y: 0, z: 0 }, pose)
  const upper = transformPoint({ x: 6.7, y: 0.22, z: -0.12 }, pose)
  const lower = transformPoint({ x: 6.7, y: -0.22, z: 0.12 }, pose)
  const center = transformPoint({ x: 6.9, y: 0, z: 0 }, pose)

  const origin2d = projectPoint(origin, width, height)
  const upper2d = projectPoint(upper, width, height)
  const lower2d = projectPoint(lower, width, height)
  const center2d = projectPoint(center, width, height)

  const beamColor =
    state.tone === 'critical'
      ? 'rgba(209, 95, 80, 0.86)'
      : state.tone === 'warning'
        ? 'rgba(210, 154, 58, 0.84)'
        : 'rgba(50, 188, 141, 0.84)'

  const haloColor =
    state.tone === 'critical'
      ? 'rgba(209, 95, 80, 0.18)'
      : state.tone === 'warning'
        ? 'rgba(210, 154, 58, 0.18)'
        : 'rgba(50, 188, 141, 0.18)'

  ctx.save()
  drawPolygon(
    ctx,
    [
      { x: origin2d.x, y: origin2d.y - 4 },
      { x: upper2d.x, y: upper2d.y },
      { x: lower2d.x, y: lower2d.y },
    ],
    haloColor,
    'rgba(0, 0, 0, 0)',
  )

  const gradient = ctx.createLinearGradient(origin2d.x, origin2d.y, center2d.x, center2d.y)
  gradient.addColorStop(0, 'rgba(255, 255, 255, 0.05)')
  gradient.addColorStop(1, beamColor)
  ctx.beginPath()
  ctx.moveTo(origin2d.x, origin2d.y)
  ctx.lineTo(center2d.x, center2d.y)
  ctx.strokeStyle = gradient
  ctx.lineWidth = 9
  ctx.lineCap = 'round'
  ctx.stroke()
  ctx.restore()
}

function drawLens(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  pose: DisplayPose,
): void {
  const lens = transformPoint({ x: 2.53, y: 0, z: 0 }, pose)
  const projected = projectPoint(lens, width, height)
  const radius = clamp(projected.scale * 0.18, 5, 12)

  ctx.save()
  const gradient = ctx.createRadialGradient(
    projected.x - radius * 0.2,
    projected.y - radius * 0.2,
    radius * 0.12,
    projected.x,
    projected.y,
    radius,
  )
  gradient.addColorStop(0, 'rgba(255, 255, 255, 0.98)')
  gradient.addColorStop(0.35, 'rgba(166, 214, 255, 0.86)')
  gradient.addColorStop(1, 'rgba(24, 36, 45, 0.96)')
  ctx.beginPath()
  ctx.arc(projected.x, projected.y, radius, 0, Math.PI * 2)
  ctx.fillStyle = gradient
  ctx.fill()
  ctx.strokeStyle = 'rgba(176, 210, 232, 0.5)'
  ctx.lineWidth = 1.5
  ctx.stroke()
  ctx.restore()
}

function drawBody(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  pose: DisplayPose,
  state: RenderState,
): void {
  const faces = [
    ...buildBox(
      { x: 0.15, y: 0, z: 0 },
      { x: 3.25, y: 0.98, z: 1.22 },
      {
        top: 'rgba(77, 88, 102, 0.98)',
        bottom: 'rgba(10, 13, 16, 0.98)',
        left: 'rgba(29, 36, 42, 0.98)',
        right: 'rgba(21, 27, 33, 0.98)',
        front: 'rgba(112, 122, 136, 0.96)',
        back: 'rgba(36, 43, 51, 0.98)',
        stroke: 'rgba(15, 24, 30, 0.35)',
      },
    ),
    ...buildBox(
      { x: 2.1, y: 0, z: 0 },
      { x: 0.78, y: 0.64, z: 0.72 },
      {
        top: 'rgba(130, 143, 156, 0.96)',
        bottom: 'rgba(18, 22, 28, 0.98)',
        left: 'rgba(60, 71, 82, 0.96)',
        right: 'rgba(40, 50, 60, 0.96)',
        front: 'rgba(205, 216, 228, 0.94)',
        back: 'rgba(82, 94, 106, 0.96)',
        stroke: 'rgba(15, 24, 30, 0.28)',
      },
    ),
    ...buildBox(
      { x: 0, y: -0.82, z: 0.04 },
      { x: 2.0, y: 0.22, z: 0.78 },
      {
        top: 'rgba(25, 31, 36, 0.95)',
        bottom: 'rgba(7, 10, 12, 0.98)',
        left: 'rgba(12, 15, 18, 0.96)',
        right: 'rgba(12, 15, 18, 0.96)',
        front: 'rgba(20, 24, 30, 0.96)',
        back: 'rgba(20, 24, 30, 0.96)',
        stroke: 'rgba(9, 14, 18, 0.25)',
      },
    ),
  ]

  const light = { x: 0.35, y: 0.55, z: 0.76 }
  const projectedFaces = faces
    .map((face) => {
      const transformedVertices = face.vertices.map((vertex) => transformPoint(vertex, pose))
      const transformedNormal = transformPoint(face.normal, {
        pitchDeg: pose.pitchDeg,
        rollDeg: pose.rollDeg,
        yawDeg: pose.yawDeg,
      })
      const faceVectorA = subtract(transformedVertices[1], transformedVertices[0])
      const faceVectorB = subtract(transformedVertices[2], transformedVertices[0])
      const realNormal = cross(faceVectorA, faceVectorB)
      const visible = realNormal.z > 0
      const depth = averageDepth(transformedVertices)
      const projected = transformedVertices.map((vertex) => projectPoint(vertex, width, height))
      const lightAmount = clamp(
        (transformedNormal.x * light.x + transformedNormal.y * light.y + transformedNormal.z * light.z) /
          1.1,
        -0.18,
        0.26,
      )
      return {
        ...face,
        visible,
        depth,
        projected,
        lightAmount,
      }
    })
    .filter((face) => face.visible)
    .sort((left, right) => left.depth - right.depth)

  for (const face of projectedFaces) {
    const alpha = state.streamReady ? 1 : 0.68
    const fill = face.fill.replace(/0\.\d+\)$/u, `${clamp(alpha + face.lightAmount, 0.3, 1).toFixed(2)})`)
    drawPolygon(
      ctx,
      face.projected.map((vertex) => ({ x: vertex.x, y: vertex.y })),
      fill,
      face.stroke,
    )
  }

  const accentStart = projectPoint(transformPoint({ x: -0.88, y: 0.48, z: 0.62 }, pose), width, height)
  const accentEnd = projectPoint(transformPoint({ x: 1.06, y: 0.48, z: 0.62 }, pose), width, height)
  ctx.save()
  ctx.strokeStyle =
    state.tone === 'critical'
      ? 'rgba(214, 98, 84, 0.84)'
      : state.tone === 'warning'
        ? 'rgba(212, 159, 62, 0.82)'
        : 'rgba(62, 202, 155, 0.86)'
  ctx.lineWidth = 10
  ctx.lineCap = 'round'
  ctx.beginPath()
  ctx.moveTo(accentStart.x, accentStart.y)
  ctx.lineTo(accentEnd.x, accentEnd.y)
  ctx.stroke()

  const plaqueStart = projectPoint(transformPoint({ x: -0.86, y: -0.22, z: 0.62 }, pose), width, height)
  const plaqueEnd = projectPoint(transformPoint({ x: 1.02, y: -0.22, z: 0.62 }, pose), width, height)
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)'
  ctx.lineWidth = 8
  ctx.beginPath()
  ctx.moveTo(plaqueStart.x, plaqueStart.y)
  ctx.lineTo(plaqueEnd.x, plaqueEnd.y)
  ctx.stroke()
  ctx.restore()
}

function drawScene(
  ctx: CanvasRenderingContext2D,
  width: number,
  height: number,
  pose: DisplayPose,
  state: RenderState,
): void {
  const background = ctx.createLinearGradient(0, 0, 0, height)
  background.addColorStop(0, 'rgba(240, 245, 242, 0.98)')
  background.addColorStop(1, 'rgba(248, 249, 248, 0.98)')
  ctx.clearRect(0, 0, width, height)
  ctx.fillStyle = background
  ctx.fillRect(0, 0, width, height)

  const glow = ctx.createRadialGradient(width * 0.56, height * 0.6, 30, width * 0.56, height * 0.6, width * 0.46)
  glow.addColorStop(0, 'rgba(255, 255, 255, 0.34)')
  glow.addColorStop(1, 'rgba(255, 255, 255, 0)')
  ctx.fillStyle = glow
  ctx.fillRect(0, 0, width, height)

  drawGrid(ctx, width, height)

  const horizonY = height * 0.56
  ctx.fillStyle = 'rgba(50, 188, 141, 0.08)'
  ctx.fillRect(width * 0.12, horizonY - 18, width * 0.76, 36)
  ctx.fillStyle = 'rgba(27, 52, 50, 0.82)'
  ctx.fillRect(width * 0.1, horizonY - 1.5, width * 0.8, 3)

  ctx.save()
  ctx.fillStyle = 'rgba(57, 82, 82, 0.78)'
  ctx.font = '700 12px "Space Grotesk", sans-serif'
  ctx.textAlign = 'right'
  ctx.fillText('Bench horizon', width - 18, horizonY - 10)
  ctx.restore()

  drawYawDial(ctx, pose, state)

  ctx.save()
  ctx.beginPath()
  ctx.ellipse(width * 0.54, height * 0.77, 112, 28, 0, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(12, 19, 28, 0.18)'
  ctx.fill()
  ctx.restore()

  drawBeam(ctx, width, height, pose, state)
  drawBody(ctx, width, height, pose, state)
  drawLens(ctx, width, height, pose)
}

export function ImuPostureCard({
  snapshot,
  telemetryStore,
}: ImuPostureCardProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const pitchDeg = liveSnapshot.imu.beamPitchDeg
  const rollDeg = liveSnapshot.imu.beamRollDeg
  const yawDeg = liveSnapshot.imu.beamYawDeg
  const pitchLimitDeg = liveSnapshot.imu.beamPitchLimitDeg
  const marginDeg = pitchLimitDeg - pitchDeg
  const pitchMarginPercent = computePitchMarginPercent(liveSnapshot)
  const streamReady = liveSnapshot.imu.valid && liveSnapshot.imu.fresh
  const belowHorizon = streamReady && pitchDeg < pitchLimitDeg
  const tone = !streamReady ? 'warning' : belowHorizon ? 'steady' : 'critical'

  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const targetPoseRef = useRef<DisplayPose>({
    pitchDeg,
    rollDeg,
    yawDeg,
  })
  const currentPoseRef = useRef<DisplayPose>({
    pitchDeg,
    rollDeg,
    yawDeg,
  })
  const streamReadyRef = useRef(streamReady)
  const averageSampleIntervalMsRef = useRef(110)
  const lastSampleAtRef = useRef<number | null>(null)
  const renderStateRef = useRef<RenderState>({
    tone,
    streamReady,
    yawRelative: liveSnapshot.imu.beamYawRelative,
  })

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
      yawDeg,
    }
    streamReadyRef.current = streamReady
    renderStateRef.current = {
      tone,
      streamReady,
      yawRelative: liveSnapshot.imu.beamYawRelative,
    }
  }, [liveSnapshot.imu.beamYawRelative, pitchDeg, rollDeg, streamReady, tone, yawDeg])

  useEffect(() => {
    let frameId = 0
    let lastFrameAt = 0

    const animate = (frameAt: number) => {
      const canvas = canvasRef.current
      if (canvas !== null) {
        const rect = canvas.getBoundingClientRect()
        const dpr = window.devicePixelRatio || 1
        const pixelWidth = Math.max(1, Math.round(rect.width * dpr))
        const pixelHeight = Math.max(1, Math.round(rect.height * dpr))

        if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
          canvas.width = pixelWidth
          canvas.height = pixelHeight
        }

        const ctx = canvas.getContext('2d')
        if (ctx !== null) {
          const deltaMs = lastFrameAt === 0 ? 16.7 : clamp(frameAt - lastFrameAt, 8, 40)
          lastFrameAt = frameAt
          const target = targetPoseRef.current
          const current = currentPoseRef.current
          const responseMs = streamReadyRef.current
            ? Math.max(80, averageSampleIntervalMsRef.current * 0.95)
            : 250
          const blend = 1 - Math.exp(-deltaMs / responseMs)
          const yawDelta = wrapDegrees(target.yawDeg - current.yawDeg)

          current.pitchDeg += (target.pitchDeg - current.pitchDeg) * blend
          current.rollDeg += (target.rollDeg - current.rollDeg) * blend
          current.yawDeg = wrapDegrees(current.yawDeg + yawDelta * blend)

          if (
            Math.abs(target.pitchDeg - current.pitchDeg) < 0.02 &&
            Math.abs(target.rollDeg - current.rollDeg) < 0.02 &&
            Math.abs(wrapDegrees(target.yawDeg - current.yawDeg)) < 0.02
          ) {
            current.pitchDeg = target.pitchDeg
            current.rollDeg = target.rollDeg
            current.yawDeg = target.yawDeg
          }

          ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
          drawScene(ctx, rect.width, rect.height, current, renderStateRef.current)
        }
      }

      frameId = window.requestAnimationFrame(animate)
    }

    frameId = window.requestAnimationFrame(animate)
    return () => {
      window.cancelAnimationFrame(frameId)
    }
  }, [])

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
            {liveSnapshot.imu.beamYawRelative ? 'Yaw is relative' : 'Yaw absolute'}
          </span>
        </div>
      </div>

      <div className="imu-posture-card__viewport">
        <canvas ref={canvasRef} className="imu-posture-card__canvas" />
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

      <ProgressMeter value={pitchMarginPercent} tone={tone} compact />

      <p className="inline-help">
        Pitch and roll are gravity-referenced from the beam-frame transform. Yaw is shown as a
        relative integrated heading and may drift over long bench runs. The viewport is now drawn
        from a single canvas model so it moves as one solid object instead of a stack of floating
        CSS pieces.
      </p>
    </article>
  )
}
