import { gpioModulePins, makeDefaultGpioInspectorStatus } from './gpio-layout'
import type { GpioInspectorStatus, GpioOverrideMode, GpioPinReadback } from '../types'

const GPIO_FLAG_OUTPUT_CAPABLE = 1 << 0
const GPIO_FLAG_INPUT_ENABLED = 1 << 1
const GPIO_FLAG_OUTPUT_ENABLED = 1 << 2
const GPIO_FLAG_OPEN_DRAIN_ENABLED = 1 << 3
const GPIO_FLAG_PULLUP_ENABLED = 1 << 4
const GPIO_FLAG_PULLDOWN_ENABLED = 1 << 5
const GPIO_FLAG_LEVEL_HIGH = 1 << 6
const GPIO_FLAG_OVERRIDE_ACTIVE = 1 << 7

const GPIO_OVERRIDE_MODE_MASK = 0x3
const GPIO_OVERRIDE_LEVEL_HIGH = 1 << 2
const GPIO_OVERRIDE_PULLUP_ENABLED = 1 << 3
const GPIO_OVERRIDE_PULLDOWN_ENABLED = 1 << 4

const defaultInspector = makeDefaultGpioInspectorStatus()
const defaultPinByGpio = new Map(defaultInspector.pins.map((pin) => [pin.gpioNum, pin]))
const gpioOrder = gpioModulePins.map((pin) => pin.gpioNum)
const gpioOrderIndex = new Map(gpioOrder.map((gpioNum, index) => [gpioNum, index]))

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

function asNumber(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null
}

function asBoolean(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback
}

function asOverrideMode(
  value: unknown,
  fallback: GpioOverrideMode,
): GpioOverrideMode {
  return value === 'firmware' || value === 'input' || value === 'output'
    ? value
    : fallback
}

function makeBasePin(gpioNum: number): GpioPinReadback {
  const seeded = defaultPinByGpio.get(gpioNum)
  if (seeded !== undefined) {
    return { ...seeded }
  }

  const meta = gpioModulePins.find((pin) => pin.gpioNum === gpioNum)
  return {
    gpioNum,
    modulePin: meta?.modulePin ?? 0,
    outputCapable: false,
    inputEnabled: false,
    outputEnabled: false,
    openDrainEnabled: false,
    pullupEnabled: false,
    pulldownEnabled: false,
    levelHigh: false,
    overrideActive: false,
    overrideMode: 'firmware',
    overrideLevelHigh: false,
    overridePullupEnabled: false,
    overridePulldownEnabled: false,
  }
}

function decodeOverrideMode(modeCode: number): GpioOverrideMode {
  switch (modeCode & GPIO_OVERRIDE_MODE_MASK) {
    case 1:
      return 'input'
    case 2:
      return 'output'
    default:
      return 'firmware'
  }
}

function normalizeLegacyPin(raw: unknown): GpioPinReadback | null {
  if (!isObject(raw)) {
    return null
  }

  const gpioNum = asNumber(raw.gpioNum)
  if (gpioNum === null) {
    return null
  }

  const base = makeBasePin(gpioNum)
  return {
    ...base,
    modulePin: asNumber(raw.modulePin) ?? base.modulePin,
    outputCapable: asBoolean(raw.outputCapable, base.outputCapable),
    inputEnabled: asBoolean(raw.inputEnabled, base.inputEnabled),
    outputEnabled: asBoolean(raw.outputEnabled, base.outputEnabled),
    openDrainEnabled: asBoolean(raw.openDrainEnabled, base.openDrainEnabled),
    pullupEnabled: asBoolean(raw.pullupEnabled, base.pullupEnabled),
    pulldownEnabled: asBoolean(raw.pulldownEnabled, base.pulldownEnabled),
    levelHigh: asBoolean(raw.levelHigh, base.levelHigh),
    overrideActive: asBoolean(raw.overrideActive, base.overrideActive),
    overrideMode: asOverrideMode(raw.overrideMode, base.overrideMode),
    overrideLevelHigh: asBoolean(raw.overrideLevelHigh, base.overrideLevelHigh),
    overridePullupEnabled: asBoolean(
      raw.overridePullupEnabled,
      base.overridePullupEnabled,
    ),
    overridePulldownEnabled: asBoolean(
      raw.overridePulldownEnabled,
      base.overridePulldownEnabled,
    ),
  }
}

function decodeCompactPin(raw: unknown): GpioPinReadback | null {
  if (!Array.isArray(raw) || raw.length < 3) {
    return null
  }

  const gpioNum = asNumber(raw[0])
  const liveFlags = asNumber(raw[1])
  const overrideFlags = asNumber(raw[2])

  if (gpioNum === null || liveFlags === null || overrideFlags === null) {
    return null
  }

  const base = makeBasePin(gpioNum)

  return {
    ...base,
    outputCapable: (liveFlags & GPIO_FLAG_OUTPUT_CAPABLE) !== 0,
    inputEnabled: (liveFlags & GPIO_FLAG_INPUT_ENABLED) !== 0,
    outputEnabled: (liveFlags & GPIO_FLAG_OUTPUT_ENABLED) !== 0,
    openDrainEnabled: (liveFlags & GPIO_FLAG_OPEN_DRAIN_ENABLED) !== 0,
    pullupEnabled: (liveFlags & GPIO_FLAG_PULLUP_ENABLED) !== 0,
    pulldownEnabled: (liveFlags & GPIO_FLAG_PULLDOWN_ENABLED) !== 0,
    levelHigh: (liveFlags & GPIO_FLAG_LEVEL_HIGH) !== 0,
    overrideActive: (liveFlags & GPIO_FLAG_OVERRIDE_ACTIVE) !== 0,
    overrideMode: decodeOverrideMode(overrideFlags),
    overrideLevelHigh: (overrideFlags & GPIO_OVERRIDE_LEVEL_HIGH) !== 0,
    overridePullupEnabled: (overrideFlags & GPIO_OVERRIDE_PULLUP_ENABLED) !== 0,
    overridePulldownEnabled: (overrideFlags & GPIO_OVERRIDE_PULLDOWN_ENABLED) !== 0,
  }
}

function sortPins(pins: GpioPinReadback[]): GpioPinReadback[] {
  return [...pins].sort((left, right) => {
    const leftIndex = gpioOrderIndex.get(left.gpioNum)
    const rightIndex = gpioOrderIndex.get(right.gpioNum)

    if (leftIndex !== undefined && rightIndex !== undefined) {
      return leftIndex - rightIndex
    }

    if (leftIndex !== undefined) {
      return -1
    }

    if (rightIndex !== undefined) {
      return 1
    }

    return left.gpioNum - right.gpioNum
  })
}

function finalizePins(pins: GpioPinReadback[]): GpioInspectorStatus {
  const normalizedPins = sortPins(pins)
  const activeOverrideCount = normalizedPins.filter((pin) => pin.overrideActive).length

  return {
    anyOverrideActive: activeOverrideCount > 0,
    activeOverrideCount,
    pins: normalizedPins,
  }
}

export function normalizeGpioInspector(raw: unknown): GpioInspectorStatus | undefined {
  if (!isObject(raw) || !Array.isArray(raw.pins)) {
    return undefined
  }

  const pins = raw.pins
    .map((pin) => (Array.isArray(pin) ? decodeCompactPin(pin) : normalizeLegacyPin(pin)))
    .filter((pin): pin is GpioPinReadback => pin !== null)

  if (pins.length === 0) {
    return undefined
  }

  return finalizePins(pins)
}

export function mergeGpioInspector(
  current: GpioInspectorStatus,
  incoming: GpioInspectorStatus | undefined,
): GpioInspectorStatus {
  if (incoming === undefined) {
    return finalizePins(current.pins)
  }

  const pinsByGpio = new Map(current.pins.map((pin) => [pin.gpioNum, { ...pin }]))

  for (const pin of incoming.pins) {
    const existing = pinsByGpio.get(pin.gpioNum)
    pinsByGpio.set(
      pin.gpioNum,
      existing === undefined
        ? { ...pin }
        : {
            ...existing,
            ...pin,
          },
    )
  }

  return finalizePins([...pinsByGpio.values()])
}
