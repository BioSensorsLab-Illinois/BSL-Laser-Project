import { formatBytes } from './format'
import { parseEmbeddedFirmwareSignature } from './firmware-signature'
import type {
  DeviceSnapshot,
  FirmwarePackageDescriptor,
  TransportKind,
} from '../types'

export type FirmwareChecklistItem = {
  label: string
  pass: boolean
  note: string
  blocking: boolean
}

async function digestBuffer(buffer: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', buffer)
  const bytes = Array.from(new Uint8Array(digest))
  return bytes.map((byte) => byte.toString(16).padStart(2, '0')).join('')
}

function inferPackageFromManifest(
  fileName: string,
  manifest: Record<string, unknown>,
  sha256: string,
): FirmwarePackageDescriptor {
  const files = Array.isArray(manifest.files) ? manifest.files : []
  const segments = files.map((entry, index) => {
    const segment = entry as Record<string, unknown>
    return {
      name: String(segment.path ?? segment.name ?? `segment-${index + 1}`),
      address:
        typeof segment.address === 'string' ? segment.address : undefined,
      bytes:
        typeof segment.bytes === 'number'
          ? segment.bytes
          : typeof segment.sizeBytes === 'number'
            ? segment.sizeBytes
            : 0,
      sha256: typeof segment.sha256 === 'string' ? segment.sha256 : undefined,
    }
  })

  const notes = Array.isArray(manifest.notes)
    ? manifest.notes.map((entry) => String(entry))
    : Array.isArray(manifest.releaseNotes)
      ? manifest.releaseNotes.map((entry) => String(entry))
      : ['Manifest imported. Verify target images before transfer.']

  const inferredBytes = segments.reduce((total, segment) => total + segment.bytes, 0)

  return {
    fileName,
    packageName: String(manifest.name ?? manifest.packageName ?? fileName),
    version: String(manifest.version ?? 'unversioned'),
    board: String(manifest.board ?? manifest.target ?? 'esp32s3'),
    format: 'manifest',
    bytes:
      typeof manifest.bytes === 'number'
        ? manifest.bytes
        : typeof manifest.sizeBytes === 'number'
          ? manifest.sizeBytes
          : inferredBytes,
    sha256,
    segments,
    notes,
    loadedAtIso: new Date().toISOString(),
    signature: null,
    webFlash: {
      supported: false,
      note:
        'Manifest review is supported, but browser flashing currently requires raw local image data for each segment. Use a raw app binary or the ESP-IDF CLI for full multi-image programming.',
      eraseAll: false,
      images: [],
    },
  }
}

async function inferPackageFromBinary(
  fileName: string,
  buffer: ArrayBuffer,
  sha256: string,
): Promise<FirmwarePackageDescriptor> {
  const image = new Uint8Array(buffer.slice(0))
  const signature = await parseEmbeddedFirmwareSignature(buffer)
  const signatureVerified = signature?.verified === true

  return {
    fileName,
    packageName: fileName.replace(/\.[^.]+$/, ''),
    version: signature?.firmwareVersion ?? 'raw-binary',
    board: signature?.boardName ?? 'esp32s3',
    format: 'binary',
    bytes: buffer.byteLength,
    sha256,
    segments: [
      {
        name: fileName,
        bytes: buffer.byteLength,
      },
    ],
    notes: [
      `Raw binary loaded (${formatBytes(buffer.byteLength)}).`,
      signature?.note ??
        'No embedded BSL firmware signature block was found in this image.',
      'Browser flashing treats a raw binary as an application image at 0x10000.',
      'Use the ESP-IDF CLI for first-program, bootloader, partition-table, or full-flash recovery work.',
    ],
    loadedAtIso: new Date().toISOString(),
    signature,
    webFlash: {
      supported: signatureVerified,
      note: signatureVerified
        ? 'Embedded BSL firmware signature verified. Raw browser flash will write this image to 0x10000 without erasing the full chip.'
        : signature?.note ??
          'Browser flashing is blocked until the image contains a valid embedded BSL firmware signature block.',
      eraseAll: false,
      images: [
        {
          name: fileName,
          address: 0x10000,
          bytes: buffer.byteLength,
          data: image,
          sha256,
        },
      ],
    },
  }
}

export async function parseFirmwareFile(file: File): Promise<FirmwarePackageDescriptor> {
  const buffer = await file.arrayBuffer()
  const sha256 = await digestBuffer(buffer)
  const isJson =
    file.type.includes('json') || file.name.toLowerCase().endsWith('.json')

  if (!isJson) {
    return await inferPackageFromBinary(file.name, buffer, sha256)
  }

  const text = new TextDecoder().decode(buffer)

  try {
    const manifest = JSON.parse(text) as Record<string, unknown>
    return inferPackageFromManifest(file.name, manifest, sha256)
  } catch {
    return await inferPackageFromBinary(file.name, buffer, sha256)
  }
}

export function buildFirmwareChecklist(
  snapshot: DeviceSnapshot,
  connected: boolean,
  transportKind: TransportKind,
  supportsFirmwareTransfer: boolean,
  pkg: FirmwarePackageDescriptor | null,
): FirmwareChecklistItem[] {
  const browserFlashTransportReady =
    supportsFirmwareTransfer &&
    (transportKind === 'serial' || transportKind === 'wifi' || transportKind === 'mock')

  return [
    {
      label: 'Package loaded',
      pass: pkg !== null,
      note: pkg === null ? 'Select a signed or bench-approved package first.' : pkg.fileName,
      blocking: true,
    },
    {
      label: 'BSL firmware signature',
      pass: pkg?.signature?.verified ?? false,
      note:
        pkg === null
          ? 'Load a current BSL firmware image with an embedded signature block.'
          : pkg.signature?.note ??
            'Browser flashing is blocked until the image carries a valid embedded BSL firmware signature.',
      blocking: true,
    },
    {
      label: 'Web flash image',
      pass: pkg?.webFlash.supported ?? false,
      note:
        pkg === null
          ? 'Load a raw app binary to enable browser flashing.'
          : pkg.webFlash.note,
      blocking: true,
    },
    {
      label: 'Host link live',
      pass: connected || browserFlashTransportReady,
      note: connected
        ? 'Device transport is active.'
        : browserFlashTransportReady
          ? transportKind === 'serial'
            ? 'Web Serial flashing can request or reopen the approved port when the flash starts.'
            : transportKind === 'wifi'
              ? 'Browser flashing can open a temporary Web Serial connection even while the live session transport stays on Wi‑Fi.'
            : 'Mock transport can simulate the full browser flashing workflow without a live board link.'
          : 'Switch to Web Serial and connect the controller before staging an update.',
      blocking: !browserFlashTransportReady,
    },
    {
      label: 'Beam outputs off',
      pass: !snapshot.laser.nirEnabled && !snapshot.laser.alignmentEnabled,
      note:
        snapshot.laser.nirEnabled || snapshot.laser.alignmentEnabled
          ? 'Visible and NIR outputs must be off before update.'
          : 'No optical output is currently enabled.',
      blocking: true,
    },
    {
      label: 'Service or programming state',
      pass:
        snapshot.session.state === 'PROGRAMMING_ONLY' ||
        snapshot.session.state === 'SERVICE_MODE',
      note:
        snapshot.session.state === 'PROGRAMMING_ONLY' ||
        snapshot.session.state === 'SERVICE_MODE'
          ? `Controller is in ${snapshot.session.state}.`
          : 'Recommended for controlled prep, but browser flashing can still try the ESP32 reset sequence automatically.',
      blocking: false,
    },
    {
      label: 'No active latched fault',
      pass: !snapshot.fault.latched,
      note: snapshot.fault.latched
        ? `Bench fault present: ${snapshot.fault.activeCode}. Review it after flashing; this does not block browser flashing.`
        : 'No latched safety fault is present.',
      blocking: false,
    },
  ]
}
