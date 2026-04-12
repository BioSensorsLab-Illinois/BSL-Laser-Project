import type {
  DeviceSnapshot,
  RealtimeTelemetryPatch,
  SessionEvent,
  TransportMessage,
} from '../types'
import { normalizeGpioInspector } from './gpio-inspector'

type RawProtocolMessage = {
  type?: 'resp' | 'event'
  id?: number
  ok?: boolean
  event?: string
  timestamp_ms?: number
  result?: Record<string, unknown>
  payload?: Record<string, unknown>
  error?: string
  detail?: string
}

function asNumber(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null
}

function hasBit(value: number, bit: number): boolean {
  return ((value >> bit) & 0x1) === 0x1
}

function decodeFastTelemetryPayload(payload: unknown): RealtimeTelemetryPatch {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    !('v' in payload) ||
    !('m' in payload)
  ) {
    return payload as RealtimeTelemetryPatch
  }

  const version = asNumber((payload as { v?: unknown }).v)
  const metrics = (payload as { m?: unknown }).m

  if (version !== 1 || !Array.isArray(metrics) || metrics.length < 17) {
    return payload as RealtimeTelemetryPatch
  }

  const values = metrics.map((entry) => asNumber(entry))

  if (values.some((entry) => entry === null)) {
    return payload as RealtimeTelemetryPatch
  }

  const [
    imuFlags,
    pitchCenti,
    rollCenti,
    yawCenti,
    pitchLimitCenti,
    tofFlags,
    distanceMm,
    laserFlags,
    measuredCurrentMa,
    driverTempCenti,
    tecFlags,
    tecTempCenti,
    tecTempAdcMv,
    tecCurrentCentiA,
    tecVoltageCentiV,
    safetyFlags,
    buttonFlags,
  ] = values as number[]

  return {
    imu: {
      valid: hasBit(imuFlags, 0),
      fresh: hasBit(imuFlags, 1),
      beamYawRelative: hasBit(imuFlags, 2),
      beamPitchDeg: pitchCenti / 100,
      beamRollDeg: rollCenti / 100,
      beamYawDeg: yawCenti / 100,
      beamPitchLimitDeg: pitchLimitCenti / 100,
    },
    tof: {
      valid: hasBit(tofFlags, 0),
      fresh: hasBit(tofFlags, 1),
      distanceM: distanceMm / 1000,
    },
    laser: {
      alignmentEnabled: hasBit(laserFlags, 0),
      nirEnabled: hasBit(laserFlags, 1),
      driverStandby: hasBit(laserFlags, 2),
      loopGood: hasBit(laserFlags, 3),
      telemetryValid: hasBit(laserFlags, 4),
      measuredCurrentA: measuredCurrentMa / 1000,
      driverTempC: driverTempCenti / 100,
    },
    tec: {
      tempGood: hasBit(tecFlags, 0),
      telemetryValid: hasBit(tecFlags, 1),
      tempC: tecTempCenti / 100,
      tempAdcVoltageV: tecTempAdcMv / 1000,
      currentA: tecCurrentCentiA / 100,
      voltageV: tecVoltageCentiV / 100,
    },
    safety: {
      allowAlignment: hasBit(safetyFlags, 0),
      allowNir: hasBit(safetyFlags, 1),
      horizonBlocked: hasBit(safetyFlags, 2),
      distanceBlocked: hasBit(safetyFlags, 3),
      lambdaDriftBlocked: hasBit(safetyFlags, 4),
      tecTempAdcBlocked: hasBit(safetyFlags, 5),
    },
    buttons: {
      stage1Pressed: hasBit(buttonFlags, 0),
      stage2Pressed: hasBit(buttonFlags, 1),
      stage1Edge: false,
      stage2Edge: false,
    },
  }
}

function nowIso(): string {
  return new Date().toISOString()
}

function severityFromConsoleLine(line: string): SessionEvent['severity'] {
  if (line.startsWith('E ') || line.includes('E (')) {
    return 'critical'
  }

  if (line.startsWith('W ') || line.includes('W (')) {
    return 'warn'
  }

  if (line.startsWith('I ') || line.includes('I (')) {
    return 'info'
  }

  return 'info'
}

type ParsedConsoleEvent = Pick<SessionEvent, 'severity' | 'category' | 'title' | 'detail'>

function parseConsoleEvent(line: string): ParsedConsoleEvent {
  const firmwareLogMatch = line.match(
    /laser_log:\s*\[(\d+)\s*ms\]\s*([a-zA-Z0-9_]+):\s*(.+)$/u,
  )

  if (firmwareLogMatch !== null) {
    const [, uptimeMs, category, message] = firmwareLogMatch
    return {
      severity:
        category === 'fault'
          ? 'critical'
          : category === 'service' || category === 'bench'
            ? 'ok'
            : severityFromConsoleLine(line),
      category,
      title: `Firmware ${category}`,
      detail: `[${uptimeMs} ms] ${message}`,
    }
  }

  if (line.startsWith('Backtrace:')) {
    return {
      severity: 'critical',
      category: 'panic',
      title: 'MCU backtrace',
      detail: line,
    }
  }

  if (line.includes('Rebooting...') || line.startsWith('rst:')) {
    return {
      severity: 'warn',
      category: 'reset',
      title: 'MCU reset',
      detail: line,
    }
  }

  return {
    severity: severityFromConsoleLine(line),
    category: 'console',
    title: 'Console log',
    detail: line,
  }
}

type ProtocolLineContext = {
  makeEventId: (kind: string) => string
  emit: (message: TransportMessage) => void
  onProtocolReady?: () => void
}

function normalizeSnapshotCandidate(raw: unknown): DeviceSnapshot | undefined {
  if (typeof raw !== 'object' || raw === null || !('session' in raw)) {
    return undefined
  }

  const normalized = { ...(raw as Record<string, unknown>) } as unknown as DeviceSnapshot
  const gpioInspector = normalizeGpioInspector(
    (raw as { gpioInspector?: unknown }).gpioInspector,
  )

  if (gpioInspector !== undefined) {
    normalized.gpioInspector = gpioInspector
  }

  return normalized
}

export function interpretControllerLine(
  line: string,
  context: ProtocolLineContext,
): void {
  let parsed: RawProtocolMessage

  try {
    parsed = JSON.parse(line) as RawProtocolMessage
  } catch {
    const consoleEvent = parseConsoleEvent(line)
    context.emit({
      kind: 'event',
      event: {
        id: context.makeEventId('console'),
        atIso: nowIso(),
        ...consoleEvent,
      },
    })
    return
  }

  if (parsed.type === 'resp' && typeof parsed.id === 'number') {
    context.onProtocolReady?.()
    const candidateSnapshot = normalizeSnapshotCandidate(
      (parsed.result?.snapshot ?? parsed.result) as Record<string, unknown> | undefined,
    )

    if (candidateSnapshot !== undefined) {
      context.emit({
        kind: 'snapshot',
        snapshot: candidateSnapshot,
      })
    }

    context.emit({
      kind: 'commandAck',
      commandId: parsed.id,
      ok: parsed.ok !== false,
      note:
        parsed.ok === false
          ? parsed.error ?? 'Command rejected.'
          : 'Controller acknowledged command.',
    })

    return
  }

  if (parsed.type !== 'event') {
    return
  }

  if (parsed.event === 'live_telemetry' || parsed.event === 'fast_telemetry') {
    context.onProtocolReady?.()
    context.emit({
      kind: 'telemetry',
      telemetry:
        parsed.event === 'fast_telemetry'
          ? decodeFastTelemetryPayload(parsed.payload)
          : (parsed.payload as unknown as RealtimeTelemetryPatch),
    })
    return
  }

  const maybeSnapshot = normalizeSnapshotCandidate(
    (parsed.payload?.snapshot ??
      parsed.payload ??
      parsed.result) as Record<string, unknown> | undefined,
  )

  if (maybeSnapshot !== undefined) {
    context.onProtocolReady?.()
    context.emit({
      kind: 'snapshot',
      snapshot: maybeSnapshot,
    })
  }

  if (parsed.event === 'status_snapshot') {
    return
  }

  const detailPayload = parsed.payload as
    | { category?: string; message?: string }
    | undefined
  const severity =
    parsed.event?.includes('fault') === true
      ? 'critical'
      : parsed.event === 'log' && detailPayload?.category === 'fault'
        ? 'critical'
        : 'info'
  const event: SessionEvent = {
    id: context.makeEventId(parsed.event ?? 'event'),
    atIso: nowIso(),
    severity,
    category:
      parsed.event === 'log'
        ? detailPayload?.category ?? 'log'
        : parsed.event ?? 'event',
    title:
      parsed.event === 'log'
        ? 'Firmware log'
        : parsed.event ?? 'Device event',
    detail:
      parsed.event === 'log'
        ? detailPayload?.message ?? parsed.detail ?? 'Log event'
        : parsed.detail ?? JSON.stringify(parsed.payload ?? parsed.result ?? {}),
  }

  context.emit({
    kind: 'event',
    event,
  })
}
