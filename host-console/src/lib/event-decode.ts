import type { SessionEvent } from '../types'

type RegisterDefinition = {
  name: string
  summary: string
  describe?: (value?: number) => string
}

type RegisterMap = Record<string, RegisterDefinition>

export type KnownBusTarget = {
  key: string
  module: string
  device: string
  bus: 'i2c' | 'spi'
  addressHex?: string
  purpose: string
  benchRole: string
  registers: RegisterMap
}

export type BusSelectionInfo = {
  target?: KnownBusTarget
  registerHex?: string
  registerName?: string
  registerSummary?: string
  registerDetail?: string
}

function hex(value: number, width = 2): string {
  return `0x${value.toString(16).padStart(width, '0')}`
}

function parseHexToken(token: string | undefined): number | null {
  if (token === undefined) {
    return null
  }

  const normalized = token.trim().toLowerCase()
  if (normalized.startsWith('0x')) {
    const parsed = Number.parseInt(normalized.slice(2), 16)
    return Number.isFinite(parsed) ? parsed : null
  }

  const parsed = Number.parseInt(normalized, 10)
  return Number.isFinite(parsed) ? parsed : null
}

function normalizeHexByte(token: string | number | undefined): string | undefined {
  const value =
    typeof token === 'number'
      ? token
      : token === undefined
        ? null
        : parseHexToken(token)

  if (value === null || value === undefined || value < 0 || value > 0xff) {
    return undefined
  }

  return hex(value)
}

function listBits(value: number, entries: Array<[boolean, string]>): string {
  const active = entries.filter(([enabled]) => enabled).map(([, label]) => label)
  return active.length > 0 ? active.join(', ') : `raw ${hex(value)}`
}

const dacRegisters: RegisterMap = {
  '0x02': {
    name: 'SYNC',
    summary: 'Output update policy',
    describe: (value) =>
      value === undefined
        ? 'Controls whether DAC outputs update immediately on I2C write or wait for a synchronous commit.'
        : `SYNC ${hex(value, 4)} controls immediate-versus-buffered DAC output updates.`,
  },
  '0x03': {
    name: 'CONFIG',
    summary: 'Reference and power-down control',
    describe: (value) =>
      value === undefined
        ? 'Controls internal reference state and DAC output power-down posture.'
        : `CONFIG ${hex(value, 4)} sets DAC reference and output power-down posture.`,
  },
  '0x04': {
    name: 'GAIN',
    summary: 'Reference divide and buffer gain',
    describe: (value) =>
      value === undefined
        ? 'Controls REF-DIV and per-channel output buffer gain.'
        : listBits(value, [
            [Boolean(value & 0x0100), 'reference divide enabled'],
            [Boolean(value & 0x0001), 'channel A gain x2'],
            [Boolean(value & 0x0002), 'channel B gain x2'],
          ]),
  },
  '0x07': {
    name: 'STATUS',
    summary: 'Reference alarm and fault status',
    describe: (value) =>
      value === undefined
        ? 'Reports DAC alarm flags. REF-ALARM forces both DAC outputs to 0 V until the reference/headroom condition is corrected.'
        : listBits(value, [[Boolean(value & 0x0001), 'REF-ALARM asserted']]),
  },
  '0x08': {
    name: 'DAC_A_DATA',
    summary: 'Channel A code',
    describe: (value) =>
      value === undefined
        ? 'Channel A data code. On this board channel A stages the laser-current command path.'
        : `Channel A DAC code ${hex(value, 4)}. On this board that stages the laser-current command path.`,
  },
  '0x09': {
    name: 'DAC_B_DATA',
    summary: 'Channel B code',
    describe: (value) =>
      value === undefined
        ? 'Channel B data code. On this board channel B stages the TEC target command path.'
        : `Channel B DAC code ${hex(value, 4)}. On this board that stages the TEC target command path.`,
  },
}

const hapticRegisters: RegisterMap = {
  '0x01': {
    name: 'MODE',
    summary: 'Mode, standby, and reset',
    describe: (value) =>
      value === undefined
        ? 'Contains MODE bits plus STANDBY and DEV_RESET control.'
        : listBits(value, [
            [Boolean(value & 0x40), 'standby asserted'],
            [Boolean(value & 0x80), 'device reset requested'],
          ]),
  },
  '0x02': {
    name: 'RTP_INPUT',
    summary: 'Real-time playback amplitude',
    describe: (value) =>
      value === undefined
        ? 'Real-time playback amplitude byte for RTP mode.'
        : `RTP amplitude byte ${hex(value)}.`,
  },
  '0x03': {
    name: 'LIBRARY_SEL',
    summary: 'Effect library select',
    describe: (value) =>
      value === undefined
        ? 'Chooses the internal ERM or LRA effect library.'
        : `Library select ${value & 0x07}.`,
  },
  '0x0c': {
    name: 'GO',
    summary: 'Playback trigger',
    describe: (value) =>
      value === undefined
        ? 'Triggers or stops playback depending on mode.'
        : value & 0x01
          ? 'GO bit asserted; effect playback requested.'
          : 'GO bit clear.',
  },
  '0x1a': {
    name: 'FEEDBACK',
    summary: 'Actuator feedback and type',
    describe: () =>
      'Feedback-control configuration. This path selects ERM versus LRA behavior and tunes the haptic closed loop.',
  },
}

const imuRegisters: RegisterMap = {
  '0x0f': {
    name: 'WHO_AM_I',
    summary: 'IMU identity register',
    describe: (value) =>
      value === undefined
        ? 'LSM6DSO identity register. Expected value is 0x6C.'
        : value === 0x6c
          ? 'WHO_AM_I matched the expected LSM6DSO value 0x6C.'
          : `Unexpected WHO_AM_I ${hex(value)}. Expected 0x6C for LSM6DSO.`,
  },
  '0x10': {
    name: 'CTRL1_XL',
    summary: 'Accelerometer ODR and full scale',
    describe: (value) =>
      value === undefined
        ? 'Sets accelerometer output-data-rate and full-scale range.'
        : `CTRL1_XL ${hex(value)} sets accelerometer ODR and full-scale range.`,
  },
  '0x11': {
    name: 'CTRL2_G',
    summary: 'Gyroscope ODR and full scale',
    describe: (value) =>
      value === undefined
        ? 'Sets gyroscope output-data-rate and full-scale range.'
        : `CTRL2_G ${hex(value)} sets gyroscope ODR and full-scale range.`,
  },
  '0x12': {
    name: 'CTRL3_C',
    summary: 'Core interface control',
    describe: (value) =>
      value === undefined
        ? 'Core control register for software reset, IF_INC, and BDU.'
        : listBits(value, [
            [Boolean(value & 0x01), 'software reset'],
            [Boolean(value & 0x04), 'IF_INC enabled'],
            [Boolean(value & 0x40), 'BDU enabled'],
          ]),
  },
  '0x13': {
    name: 'CTRL4_C',
    summary: 'Interface mode control',
    describe: (value) =>
      value === undefined
        ? 'Contains the I2C_disable bit and related interface controls.'
        : listBits(value, [[Boolean(value & 0x04), 'I2C disabled']]),
  },
  '0x1e': {
    name: 'STATUS_REG',
    summary: 'Fresh sample flags',
    describe: () =>
      'Status flags indicating when accelerometer and gyroscope fresh data are ready.',
  },
  '0x28': {
    name: 'OUTX_L_A',
    summary: 'Accelerometer output data start',
    describe: () =>
      'Start of the accelerometer output register block. Multi-byte reads require IF_INC for convenient burst access.',
  },
}

const pdRegisters: RegisterMap = {
  '0x06': {
    name: 'STATUS_0',
    summary: 'Legacy status byte',
    describe: () =>
      'General STUSB4500 status byte used by some bring-up paths and mock snapshots.',
  },
  '0x07': {
    name: 'STATUS_1',
    summary: 'Legacy status byte',
    describe: () =>
      'General STUSB4500 status byte used by some bring-up paths and mock snapshots.',
  },
  '0x11': {
    name: 'CC_STATUS',
    summary: 'Type-C attach and current advertisement',
    describe: () =>
      'Reports attachment and advertised Type-C current so firmware can tell whether VBUS is only a host-level 5 V source or a real PD-capable source.',
  },
  '0x1a': {
    name: 'PD_COMMAND_CTRL',
    summary: 'PD soft-reset command path',
    describe: () =>
      'Used by firmware to ask the STUSB4500 to send commands such as a PD soft reset after runtime PDO changes.',
  },
  '0x51': {
    name: 'TX_HEADER_LOW',
    summary: 'Transmit header low byte',
    describe: () =>
      'Part of the PD transmit command framing used before requesting a soft reset or other transmit-side action.',
  },
  '0x70': {
    name: 'DPM_PDO_NUMB',
    summary: 'Sink PDO count',
    describe: () =>
      'Reports how many sink PDO slots are currently active in the runtime DPM configuration.',
  },
  '0x85': {
    name: 'DPM_PDO1_0',
    summary: 'Sink PDO runtime table start',
    describe: () =>
      'Start address of the runtime sink PDO table. Firmware reads and writes this block when staging 5 V, reduced-power, and full-power sink requests.',
  },
  '0x91': {
    name: 'RDO_STATUS_0',
    summary: 'Negotiated request-data-object status',
    describe: () =>
      'Start of the RDO status block that describes the currently negotiated PD operating point.',
  },
}

const tofRegisters: RegisterMap = {
  '0x0030': {
    name: 'GPIO_HV_MUX__CTRL',
    summary: 'Interrupt polarity control',
    describe: () =>
      'Controls the VL53L1X GPIO1 interrupt polarity. The firmware reads this before checking whether a new measurement is ready.',
  },
  '0x0031': {
    name: 'GPIO__TIO_HV_STATUS',
    summary: 'Data-ready interrupt status',
    describe: () =>
      'Reports the current GPIO1 interrupt state. Firmware uses this together with the configured polarity to decide whether a fresh ranging result is available.',
  },
  '0x0086': {
    name: 'SYSTEM__INTERRUPT_CLEAR',
    summary: 'Interrupt clear register',
    describe: () =>
      'Must be written after a result is consumed so the next ranging sample can raise data-ready again.',
  },
  '0x0087': {
    name: 'SYSTEM__MODE_START',
    summary: 'Ranging start and stop control',
    describe: () =>
      'Writing 0x40 starts continuous ranging. Writing 0x00 stops the measurement engine.',
  },
  '0x0089': {
    name: 'RESULT__RANGE_STATUS',
    summary: 'Measurement validity code',
    describe: () =>
      'Raw range-status code from the sensor. Zero-equivalent means the distance is usable; nonzero codes indicate sigma, signal, bounds, or wraparound faults.',
  },
  '0x0096': {
    name: 'RESULT__FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0',
    summary: 'Distance result in millimeters',
    describe: () =>
      'Primary distance result register read by the firmware once the VL53L1X reports data ready.',
  },
  '0x00e5': {
    name: 'FIRMWARE__SYSTEM_STATUS',
    summary: 'Boot-state register',
    describe: () =>
      'Returns 1 once the sensor has completed boot. Firmware should not attempt full configuration until this register reports boot complete.',
  },
  '0x010f': {
    name: 'IDENTIFICATION__MODEL_ID',
    summary: 'Sensor identification word',
    describe: () =>
      'Should read back 0xEACC on a real VL53L1X. This is the cleanest identity check before starting ranging.',
  },
}

const i2cDevices: Record<string, KnownBusTarget> = {
  '0x28': {
    key: 'stusb4500',
    module: 'pd',
    device: 'STUSB4500',
    bus: 'i2c',
    addressHex: '0x28',
    purpose: 'Autonomous USB-PD sink controller on the shared bench I2C bus.',
    benchRole:
      'Negotiates source power, exposes active PDO and RDO status, and feeds the firmware power-tier classifier.',
    registers: pdRegisters,
  },
  '0x29': {
    key: 'vl53l1x',
    module: 'tof',
    device: 'VL53L1X',
    bus: 'i2c',
    addressHex: '0x29',
    purpose: 'Distance sensor on the external ToF daughterboard.',
    benchRole:
      'Uses the shared GPIO4/GPIO5 I2C bus. GPIO7 is the optional GPIO1 interrupt line, and GPIO6 is the separate LED-control sideband for the board illumination path.',
    registers: tofRegisters,
  },
  '0x48': {
    key: 'dac80502',
    module: 'dac',
    device: 'DAC80502',
    bus: 'i2c',
    addressHex: '0x48',
    purpose: 'Dual 16-bit actuator DAC on the shared bench I2C bus.',
    benchRole:
      'Channel A stages the laser-current command path and channel B stages the TEC target command path.',
    registers: dacRegisters,
  },
  '0x5a': {
    key: 'drv2605',
    module: 'haptic',
    device: 'DRV2605',
    bus: 'i2c',
    addressHex: '0x5A',
    purpose: 'Haptic driver on the shared bench I2C bus.',
    benchRole:
      'Used for operator feedback only. It is not part of the beam safety authority.',
    registers: hapticRegisters,
  },
}

const spiDevices: Record<string, KnownBusTarget> = {
  imu: {
    key: 'imu',
    module: 'imu',
    device: 'LSM6DSO',
    bus: 'spi',
    purpose: '6-axis IMU on the dedicated SPI bus.',
    benchRole:
      'Provides the orientation data used for the horizon safety path and beam-frame posture estimation.',
    registers: imuRegisters,
  },
}

function decodeRegisterDescription(
  registers: RegisterMap | undefined,
  registerHex: string | undefined,
  valueHex?: string,
): {
  registerName?: string
  registerSummary?: string
  description?: string
} {
  if (registers === undefined || registerHex === undefined) {
    return {}
  }

  const register = registers[registerHex.toLowerCase()]
  if (register === undefined) {
    return {}
  }

  const value = parseHexToken(valueHex)
  return {
    registerName: register.name,
    registerSummary: register.summary,
    description: register.describe?.(value === null ? undefined : value),
  }
}

function describeTarget(target: KnownBusTarget): string {
  const address = target.addressHex !== undefined ? ` at ${target.addressHex}` : ''
  return `${target.device}${address} is ${target.purpose} ${target.benchRole}`
}

export function lookupI2cTarget(address: string | number | undefined): KnownBusTarget | undefined {
  const key = normalizeHexByte(address)
  return key === undefined ? undefined : i2cDevices[key.toLowerCase()]
}

export function lookupSpiTarget(device: string | undefined): KnownBusTarget | undefined {
  if (device === undefined) {
    return undefined
  }

  return spiDevices[device.trim().toLowerCase()]
}

export function describeI2cSelection(
  address: string | number | undefined,
  register: string | number | undefined,
): BusSelectionInfo {
  const target = lookupI2cTarget(address)
  const registerHex = normalizeHexByte(register)
  const decode = decodeRegisterDescription(target?.registers, registerHex)

  return {
    target,
    registerHex: registerHex?.toUpperCase(),
    registerName: decode.registerName,
    registerSummary: decode.registerSummary,
    registerDetail: decode.description,
  }
}

export function describeSpiSelection(
  device: string | undefined,
  register: string | number | undefined,
): BusSelectionInfo {
  const target = lookupSpiTarget(device)
  const registerHex = normalizeHexByte(register)
  const decode = decodeRegisterDescription(target?.registers, registerHex)

  return {
    target,
    registerHex: registerHex?.toUpperCase(),
    registerName: decode.registerName,
    registerSummary: decode.registerSummary,
    registerDetail: decode.description,
  }
}

export function listKnownBusTargets(): KnownBusTarget[] {
  return [
    i2cDevices['0x28'],
    i2cDevices['0x29'],
    i2cDevices['0x48'],
    i2cDevices['0x5a'],
    spiDevices.imu,
  ]
}

export function listKnownI2cTargets(): KnownBusTarget[] {
  return [
    i2cDevices['0x28'],
    i2cDevices['0x29'],
    i2cDevices['0x48'],
    i2cDevices['0x5a'],
  ]
}

export function listKnownSpiTargets(): KnownBusTarget[] {
  return [spiDevices.imu]
}

function parseI2cScan(detail: string): SessionEvent | null {
  const normalized = detail.trim()

  if (normalized.startsWith('Shared I2C unavailable')) {
    return {
      id: '',
      atIso: '',
      severity: 'warn',
      category: 'bus',
      title: 'I2C scan unavailable',
      detail: normalized,
      decodedDetail:
        'GPIO4/GPIO5 are the shared bench I2C bus on this board. Expected populated targets are STUSB4500 at 0x28, VL53L1X at 0x29, DAC80502 at 0x48, and DRV2605 at 0x5A. If SDA is held low, no address probe can complete reliably.',
      module: 'bus',
      source: 'derived',
      bus: 'i2c',
      operation: 'scan',
      summary: 'I2C scan unavailable',
    }
  }

  if (normalized.startsWith('No known shared-I2C devices acknowledged')) {
    return {
      id: '',
      atIso: '',
      severity: 'warn',
      category: 'bus',
      title: 'No known I2C target acknowledged',
      detail: normalized,
      decodedDetail:
        'The bench scan only probes the known shared-I2C targets for this board: STUSB4500 at 0x28, VL53L1X at 0x29, DAC80502 at 0x48, and DRV2605 at 0x5A.',
      module: 'bus',
      source: 'derived',
      bus: 'i2c',
      operation: 'scan',
      summary: 'No known I2C target acknowledged',
    }
  }

  const found = Array.from(
    new Set(
      Array.from(normalized.matchAll(/\b0x[0-9a-f]{2}\b/gi))
        .map((match) => match[0].toLowerCase())
        .filter((token) => i2cDevices[token] !== undefined),
    ),
  ).map((token) => i2cDevices[token])

  if (found.length === 0) {
    return null
  }

  const labels = found.map((target) => `${target.device} ${target.addressHex}`)
  const detailText = found
    .map((target) => `${target.device} at ${target.addressHex}: ${target.purpose} ${target.benchRole}`)
    .join(' ')

  return {
    id: '',
    atIso: '',
    severity: 'info',
    category: 'bus',
    title: 'I2C scan',
    detail: `Acknowledged shared-bus targets: ${labels.join(', ')}.`,
    decodedDetail: detailText,
    module: found.length === 1 ? found[0].module : 'bus',
    source: 'derived',
    bus: 'i2c',
    operation: 'scan',
    device: found.length === 1 ? found[0].device : undefined,
    addressHex: found.length === 1 ? found[0].addressHex : undefined,
    summary: `I2C scan: ${labels.join(', ')}`,
  }
}

function parseI2cTransfer(detail: string): SessionEvent | null {
  const match = detail.match(
    /^(read|write)\s+(0x[0-9a-f]+)\s+(reg|cmd)\s+(0x[0-9a-f]+)\s+(->|<-)\s+([^\s]+)$/i,
  )

  if (match === null) {
    return null
  }

  const [, operation, addressHexRaw, registerKind, registerHexRaw, , valueRaw] = match
  const addressHex = addressHexRaw.toLowerCase()
  const registerHex = registerHexRaw.toLowerCase()
  const valueHex = valueRaw.toLowerCase()
  const target = i2cDevices[addressHex]
  const decode = decodeRegisterDescription(target?.registers, registerHex, valueHex)
  const deviceLabel = target?.device ?? `I2C target ${addressHex.toUpperCase()}`
  const registerWord = registerKind.toLowerCase() === 'cmd' ? 'command' : 'register'
  const registerLabel =
    decode.registerName !== undefined
      ? `${decode.registerName} (${registerHex.toUpperCase()})`
      : `${registerWord} ${registerHex.toUpperCase()}`
  const targetDetail = target !== undefined ? describeTarget(target) : undefined
  const decodedDetail = [targetDetail, decode.registerSummary, decode.description]
    .filter((value): value is string => value !== undefined && value.length > 0)
    .join(' ')

  if (valueHex === 'no-ack') {
    return {
      id: '',
      atIso: '',
      severity: 'warn',
      category: 'bus',
      title: `${deviceLabel} ${operation}`,
      detail: `${deviceLabel} did not acknowledge ${registerLabel}.`,
      decodedDetail: decodedDetail.length > 0 ? decodedDetail : undefined,
      module: target?.module ?? 'bus',
      source: 'derived',
      bus: 'i2c',
      operation: operation as 'read' | 'write',
      device: target?.device ?? `I2C ${addressHex.toUpperCase()}`,
      addressHex: addressHex.toUpperCase(),
      registerHex: registerHex.toUpperCase(),
      registerName: decode.registerName,
      summary: `${deviceLabel} ${registerLabel} no-ack`,
    }
  }

  const valueLabel = valueHex.toUpperCase()
  return {
    id: '',
    atIso: '',
    severity: valueHex.startsWith('esp_') ? 'warn' : 'info',
    category: 'bus',
    title: `${deviceLabel} ${operation}`,
    detail:
      valueHex.startsWith('esp_')
        ? `${deviceLabel} ${registerLabel} ${operation} returned ${valueLabel}.`
        : `${deviceLabel} ${registerLabel} ${operation === 'read' ? 'read returned' : 'write staged'} ${valueLabel}.`,
    decodedDetail: decodedDetail.length > 0 ? decodedDetail : undefined,
    module: target?.module ?? 'bus',
    source: 'derived',
    bus: 'i2c',
    operation: operation as 'read' | 'write',
    device: target?.device ?? `I2C ${addressHex.toUpperCase()}`,
    addressHex: addressHex.toUpperCase(),
    registerHex: registerHex.toUpperCase(),
    registerName: decode.registerName,
    valueHex: valueLabel,
    summary: `${deviceLabel} ${registerLabel} ${operation} ${valueLabel}`,
  }
}

function parseSpiTransfer(detail: string): SessionEvent | null {
  const match = detail.match(/^(imu)\s+reg\s+(0x[0-9a-f]+)\s+(->|<-)\s+(0x[0-9a-f]+|unavailable)$/i)
  if (match === null) {
    return null
  }

  const [, deviceKey, registerHexRaw, arrow, valueRaw] = match
  const registerHex = registerHexRaw.toLowerCase()
  const valueHex = valueRaw.toLowerCase()
  const target = spiDevices[deviceKey.toLowerCase()]
  const decode = decodeRegisterDescription(target?.registers, registerHex, valueHex)
  const operation = arrow === '->' ? 'read' : 'write'
  const registerLabel =
    decode.registerName !== undefined
      ? `${decode.registerName} (${registerHex.toUpperCase()})`
      : registerHex.toUpperCase()
  const decodedDetail = [target !== undefined ? describeTarget(target) : undefined, decode.registerSummary, decode.description]
    .filter((value): value is string => value !== undefined && value.length > 0)
    .join(' ')

  return {
    id: '',
    atIso: '',
    severity: valueHex === 'unavailable' ? 'warn' : 'info',
    category: 'bus',
    title: `${target.device} ${operation}`,
    detail:
      valueHex === 'unavailable'
        ? `${target.device} did not respond to ${registerLabel}.`
        : `${target.device} ${registerLabel} ${operation === 'read' ? 'returned' : 'was written with'} ${valueHex.toUpperCase()}.`,
    decodedDetail: decodedDetail.length > 0 ? decodedDetail : undefined,
    module: target.module,
    source: 'derived',
    bus: 'spi',
    operation,
    device: target.device,
    registerHex: registerHex.toUpperCase(),
    registerName: decode.registerName,
    valueHex: valueHex === 'unavailable' ? undefined : valueHex.toUpperCase(),
    summary:
      valueHex === 'unavailable'
        ? `${target.device} ${registerLabel} unavailable`
        : `${target.device} ${registerLabel} ${operation} ${valueHex.toUpperCase()}`,
  }
}

function inferModuleFromCategory(event: SessionEvent): string | undefined {
  if (event.category === 'transport' || event.category === 'console' || event.category === 'reset') {
    return 'transport'
  }

  if (event.category === 'firmware' || event.operation === 'transfer') {
    return 'firmware'
  }

  if (event.category === 'fault' || event.category === 'safety') {
    return 'safety'
  }

  if (event.category === 'command') {
    return 'service'
  }

  if (event.category === 'telemetry' || event.category === 'boot') {
    return 'system'
  }

  return undefined
}

export function decodeBusText(detail: string): SessionEvent | null {
  return parseI2cScan(detail) ?? parseI2cTransfer(detail) ?? parseSpiTransfer(detail)
}

export function annotateSessionEvent(event: SessionEvent): SessionEvent {
  const decoded = decodeBusText(event.detail)

  if (decoded !== null) {
    return {
      ...event,
      ...decoded,
      id: event.id,
      atIso: event.atIso,
      severity: event.severity === 'critical' ? event.severity : decoded.severity,
      source: event.source ?? decoded.source,
    }
  }

  return {
    ...event,
    module: event.module ?? inferModuleFromCategory(event),
    summary: event.summary ?? event.title,
    source: event.source ?? 'firmware',
  }
}
