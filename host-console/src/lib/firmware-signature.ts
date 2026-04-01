import type { FirmwareEmbeddedSignature } from '../types'

const SIGNATURE_MAGIC = 'BSLFWS1'
const SIGNATURE_END_MAGIC = 'BSLEND1'
const EXPECTED_PRODUCT = 'BSL-HTLS Gen2'
const EXPECTED_PROJECT = 'bsl_laser_controller'
const EXPECTED_BOARD = 'esp32s3'
const EXPECTED_PROTOCOL = 'host-v1'
const EXPECTED_SCOPE = 'main-controller'
const STRUCT_SIZE_V1 = 256
const textDecoder = new TextDecoder()

function readFixedString(bytes: Uint8Array, offset: number, length: number): string {
  const slice = bytes.slice(offset, offset + length)
  const zeroIndex = slice.indexOf(0)
  const end = zeroIndex >= 0 ? zeroIndex : slice.length
  return textDecoder.decode(slice.slice(0, end)).trim()
}

async function digestText(text: string): Promise<string> {
  const buffer = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))
  return Array.from(new Uint8Array(buffer))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('')
}

function buildPayload(signature: Omit<FirmwareEmbeddedSignature, 'verified' | 'note'>): string {
  return [
    `schema=${signature.schemaVersion}`,
    `product=${signature.productName}`,
    `project=${signature.projectName}`,
    `board=${signature.boardName}`,
    `protocol=${signature.protocolVersion}`,
    `scope=${signature.hardwareScope}`,
    `fw=${signature.firmwareVersion}`,
    `build=${signature.buildUtc}`,
  ].join('|')
}

export async function parseEmbeddedFirmwareSignature(
  buffer: ArrayBuffer,
): Promise<FirmwareEmbeddedSignature | null> {
  const bytes = new Uint8Array(buffer)
  const magicBytes = new TextEncoder().encode(SIGNATURE_MAGIC)

  for (let offset = 0; offset <= bytes.length - STRUCT_SIZE_V1; offset += 1) {
    let matches = true

    for (let index = 0; index < magicBytes.length; index += 1) {
      if (bytes[offset + index] !== magicBytes[index]) {
        matches = false
        break
      }
    }

    if (!matches) {
      continue
    }

    const view = new DataView(buffer, offset, STRUCT_SIZE_V1)
    const schemaVersion = view.getUint16(8, true)
    const structSize = view.getUint16(10, true)

    if (schemaVersion !== 1 || structSize !== STRUCT_SIZE_V1) {
      continue
    }

    const parsed: Omit<FirmwareEmbeddedSignature, 'verified' | 'note'> = {
      schemaVersion,
      productName: readFixedString(bytes, offset + 12, 24),
      projectName: readFixedString(bytes, offset + 36, 32),
      boardName: readFixedString(bytes, offset + 68, 16),
      protocolVersion: readFixedString(bytes, offset + 84, 16),
      hardwareScope: readFixedString(bytes, offset + 100, 24),
      firmwareVersion: readFixedString(bytes, offset + 124, 32),
      buildUtc: readFixedString(bytes, offset + 156, 24),
      payloadSha256: readFixedString(bytes, offset + 180, 65),
    }

    const endMagic = readFixedString(bytes, offset + 245, 8)
    const computedPayloadSha = await digestText(buildPayload(parsed))

    const contractProblems: string[] = []
    if (parsed.productName !== EXPECTED_PRODUCT) {
      contractProblems.push(`product ${parsed.productName}`)
    }
    if (parsed.projectName !== EXPECTED_PROJECT) {
      contractProblems.push(`project ${parsed.projectName}`)
    }
    if (parsed.boardName !== EXPECTED_BOARD) {
      contractProblems.push(`board ${parsed.boardName}`)
    }
    if (parsed.protocolVersion !== EXPECTED_PROTOCOL) {
      contractProblems.push(`protocol ${parsed.protocolVersion}`)
    }
    if (parsed.hardwareScope !== EXPECTED_SCOPE) {
      contractProblems.push(`scope ${parsed.hardwareScope}`)
    }

    const hashMatches = parsed.payloadSha256.toLowerCase() === computedPayloadSha
    const endMatches = endMagic === SIGNATURE_END_MAGIC
    const verified = hashMatches && endMatches && contractProblems.length === 0

    let note = 'Embedded BSL firmware signature verified for browser flashing.'
    if (!endMatches) {
      note = 'Embedded BSL firmware signature footer is malformed.'
    } else if (!hashMatches) {
      note = 'Embedded BSL firmware signature hash does not match its declared metadata.'
    } else if (contractProblems.length > 0) {
      note = `Embedded BSL firmware signature is for a different compatibility contract: ${contractProblems.join(', ')}.`
    }

    return {
      ...parsed,
      verified,
      note,
    }
  }

  return null
}
