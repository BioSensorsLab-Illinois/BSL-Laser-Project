import type {
  CommandHistoryEntry,
  DeviceSnapshot,
  FirmwareTransferProgress,
  SessionArchivePayload,
  SessionEvent,
  TransportKind,
} from '../types'

type BuildSessionArchiveOptions = {
  transportKind: TransportKind
  snapshot: DeviceSnapshot
  events: SessionEvent[]
  commands: CommandHistoryEntry[]
  firmwareProgress: FirmwareTransferProgress | null
  exportedAtIso?: string
}

export function makeSessionArchivePayload(
  options: BuildSessionArchiveOptions,
): SessionArchivePayload {
  return {
    exportedAtIso: options.exportedAtIso ?? new Date().toISOString(),
    transportKind: options.transportKind,
    snapshot: options.snapshot,
    events: options.events,
    commands: options.commands,
    firmwareProgress: options.firmwareProgress,
  }
}

export function makeSessionArchiveFileName(date = new Date()): string {
  return `bsl-session-${date.toISOString().replaceAll(':', '-')}.json`
}

export function downloadSessionArchive(
  payload: SessionArchivePayload,
  fileName = makeSessionArchiveFileName(),
): void {
  const blob = new Blob([JSON.stringify(payload, null, 2)], {
    type: 'application/json',
  })
  const url = URL.createObjectURL(blob)
  const anchor = document.createElement('a')

  anchor.href = url
  anchor.download = fileName
  anchor.click()
  URL.revokeObjectURL(url)
}
