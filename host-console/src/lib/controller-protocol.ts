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
      telemetry: parsed.payload as unknown as RealtimeTelemetryPatch,
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
