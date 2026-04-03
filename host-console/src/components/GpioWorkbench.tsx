import { useEffect, useMemo, useState } from 'react'
import { Cpu, RefreshCcw, TriangleAlert } from 'lucide-react'

import { formatNumber } from '../lib/format'
import {
  gpioModulePins,
  type GpioModulePinMeta,
  type GpioModuleSignalClass,
} from '../lib/gpio-layout'
import type {
  CommandRisk,
  DeviceSnapshot,
  GpioOverrideMode,
  GpioPinReadback,
} from '../types'

type GpioWorkbenchProps = {
  snapshot: DeviceSnapshot
  connected: boolean
  onRunCommand: (
    label: string,
    cmd: string,
    risk: CommandRisk,
    note: string,
    args?: Record<string, number | string | boolean>,
  ) => Promise<boolean>
}

type GpioBias = 'floating' | 'pullup' | 'pulldown'

type GpioDraft = {
  mode: GpioOverrideMode
  levelHigh: boolean
  bias: GpioBias
}

type GpioAnalogReading = {
  label: string
  voltageV: number
  caption: string
  summary: string
  maxVoltageV: number
}

const gpioSignalLegend: GpioModuleSignalClass[] = [
  'bus',
  'power',
  'analog',
  'control',
  'usb',
  'strap',
  'debug',
]

function inferBias(pullupEnabled: boolean, pulldownEnabled: boolean): GpioBias {
  if (pullupEnabled) {
    return 'pullup'
  }

  if (pulldownEnabled) {
    return 'pulldown'
  }

  return 'floating'
}

function makeDraftFromPin(pin: GpioPinReadback): GpioDraft {
  if (pin.overrideActive) {
    return {
      mode: pin.overrideMode,
      levelHigh: pin.overrideLevelHigh,
      bias: inferBias(pin.overridePullupEnabled, pin.overridePulldownEnabled),
    }
  }

  return {
    mode: 'firmware',
    levelHigh: pin.levelHigh,
    bias: inferBias(pin.pullupEnabled, pin.pulldownEnabled),
  }
}

function formatActualMode(
  pin: GpioPinReadback,
  meta?: GpioModulePinMeta,
  analogReading?: GpioAnalogReading | null,
) {
  if (meta?.signalClass === 'analog') {
    return analogReading !== null && analogReading !== undefined
      ? 'ADC telemetry input'
      : 'Analog telemetry pad'
  }

  if (pin.inputEnabled && pin.outputEnabled) {
    return pin.openDrainEnabled ? 'Input + open-drain output' : 'Input + output'
  }

  if (pin.outputEnabled) {
    return pin.openDrainEnabled ? 'Open-drain output' : 'Output only'
  }

  if (pin.inputEnabled) {
    return 'Input only'
  }

  return 'Not actively configured'
}

function formatCompactMode(
  pin: GpioPinReadback,
  meta?: GpioModulePinMeta,
  analogReading?: GpioAnalogReading | null,
) {
  if (meta?.signalClass === 'analog') {
    return analogReading !== null && analogReading !== undefined ? 'ADC' : 'ANA'
  }

  if (pin.inputEnabled && pin.outputEnabled) {
    return pin.openDrainEnabled ? 'IN/OD' : 'I/O'
  }

  if (pin.outputEnabled) {
    return pin.openDrainEnabled ? 'OD' : 'OUT'
  }

  if (pin.inputEnabled) {
    return 'IN'
  }

  return 'OFF'
}

function getModeTone(pin: GpioPinReadback, meta?: GpioModulePinMeta) {
  if (meta?.signalClass === 'analog') {
    return 'input'
  }

  if (pin.inputEnabled && pin.outputEnabled) {
    return 'bidirectional'
  }

  if (pin.outputEnabled) {
    return 'output'
  }

  if (pin.inputEnabled) {
    return 'input'
  }

  return 'idle'
}

function formatBias(pin: GpioPinReadback, meta?: GpioModulePinMeta) {
  if (meta?.signalClass === 'analog') {
    return 'Analog / high-Z'
  }

  if (pin.pullupEnabled) {
    return 'Pull-up'
  }

  if (pin.pulldownEnabled) {
    return 'Pull-down'
  }

  return 'Floating'
}

function formatOverrideBias(draft: GpioDraft) {
  if (draft.bias === 'pullup') {
    return {
      pullup_enabled: true,
      pulldown_enabled: false,
    }
  }

  if (draft.bias === 'pulldown') {
    return {
      pullup_enabled: false,
      pulldown_enabled: true,
    }
  }

  return {
    pullup_enabled: false,
    pulldown_enabled: false,
  }
}

function formatSignalClass(signalClass: GpioModuleSignalClass) {
  switch (signalClass) {
    case 'bus':
      return 'Bus'
    case 'power':
      return 'Power'
    case 'analog':
      return 'Analog'
    case 'control':
      return 'Control'
    case 'strap':
      return 'Boot / strap'
    case 'usb':
      return 'USB'
    case 'debug':
      return 'Debug'
    default:
      return 'GPIO'
  }
}

function deriveAnalogReading(
  meta: GpioModulePinMeta,
  snapshot: DeviceSnapshot,
): GpioAnalogReading | null {
  if (meta.analogTelemetry === 'tecTempAdc') {
    return {
      label: 'TEC temperature ADC',
      voltageV: snapshot.tec.tempAdcVoltageV,
      caption:
        'Live TEC temperature monitor sample from the controller snapshot. Meter range is shown against the 0 V to 3.3 V ADC span.',
      summary: `${formatNumber(snapshot.tec.tempC, 1)} C equivalent`,
      maxVoltageV: 3.3,
    }
  }

  if (meta.analogTelemetry === 'ldCurrentMonitor') {
    return {
      label: 'Laser current monitor',
      voltageV: snapshot.laser.currentMonitorVoltageV,
      caption:
        'Raw LD LIO ADC telemetry from the controller snapshot. This is the analog current-monitor node, not a digital GPIO high/low state.',
      summary: `${formatNumber(snapshot.laser.measuredCurrentA, 2)} A derived`,
      maxVoltageV: 2.5,
    }
  }

  if (meta.analogTelemetry === 'ldDriverTemp') {
    return {
      label: 'Laser driver temperature monitor',
      voltageV: snapshot.laser.driverTempVoltageV,
      caption:
        'Raw LD TMO ADC telemetry from the controller snapshot. This is only meaningful when the LD rail is good and the driver is not forced off.',
      summary: `${formatNumber(snapshot.laser.driverTempC, 1)} C derived`,
      maxVoltageV: 2.5,
    }
  }

  return null
}

export function GpioWorkbench({
  snapshot,
  connected,
  onRunCommand,
}: GpioWorkbenchProps) {
  const pinLookup = useMemo(() => {
    const next = new Map<number, GpioPinReadback>()
    for (const pin of snapshot.gpioInspector.pins) {
      next.set(pin.gpioNum, pin)
    }
    return next
  }, [snapshot.gpioInspector.pins])

  const leftPins = useMemo(
    () => gpioModulePins.filter((pin) => pin.side === 'left'),
    [],
  )
  const rightPins = useMemo(
    () => gpioModulePins.filter((pin) => pin.side === 'right'),
    [],
  )

  const [selectedGpio, setSelectedGpio] = useState(gpioModulePins[0]?.gpioNum ?? 4)
  const selectedPin =
    pinLookup.get(selectedGpio) ??
    snapshot.gpioInspector.pins[0] ??
    ({
      gpioNum: selectedGpio,
      modulePin: 0,
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
    } satisfies GpioPinReadback)
  const selectedMeta =
    gpioModulePins.find((pin) => pin.gpioNum === selectedPin.gpioNum) ?? gpioModulePins[0]
  const selectedAnalogReading =
    selectedMeta !== undefined ? deriveAnalogReading(selectedMeta, snapshot) : null
  const selectedAnalogMaxVoltageV = selectedAnalogReading?.maxVoltageV ?? 3.3
  const selectedAnalogPercent =
    selectedAnalogReading !== null
      ? Math.max(
          0,
          Math.min(100, (selectedAnalogReading.voltageV / selectedAnalogMaxVoltageV) * 100),
        )
      : 0

  const [draft, setDraft] = useState<GpioDraft>(() => makeDraftFromPin(selectedPin))
  const [draftDirty, setDraftDirty] = useState(false)

  const serviceWriteReady = connected && snapshot.bringup.serviceModeActive
  const activeOverrideCount = useMemo(
    () => snapshot.gpioInspector.pins.filter((pin) => pin.overrideActive).length,
    [snapshot.gpioInspector.pins],
  )
  const anyOverrideActive = activeOverrideCount > 0

  const selectedPinSyncKey = [
    selectedPin.gpioNum,
    selectedPin.levelHigh ? 1 : 0,
    selectedPin.overrideActive ? 1 : 0,
    selectedPin.overrideMode,
    selectedPin.overrideLevelHigh ? 1 : 0,
    selectedPin.overridePullupEnabled ? 1 : 0,
    selectedPin.overridePulldownEnabled ? 1 : 0,
    selectedPin.pullupEnabled ? 1 : 0,
    selectedPin.pulldownEnabled ? 1 : 0,
  ].join(':')
  const liveDraft = useMemo(() => makeDraftFromPin(selectedPin), [selectedPinSyncKey])

  useEffect(() => {
    if (!draftDirty) {
      setDraft(liveDraft)
    }
  }, [draftDirty, liveDraft])

  function selectGpio(gpioNum: number) {
    const nextPin = pinLookup.get(gpioNum)
    setSelectedGpio(gpioNum)
    setDraftDirty(false)
    if (nextPin !== undefined) {
      setDraft(makeDraftFromPin(nextPin))
    }
  }

  async function applyOverride() {
    if (selectedMeta === undefined) {
      return
    }

    const bias = formatOverrideBias(draft)
    const ok = await onRunCommand(
      draft.mode === 'firmware'
        ? `Release GPIO${selectedPin.gpioNum} to firmware`
        : `Apply GPIO${selectedPin.gpioNum} override`,
      'set_gpio_override',
      'service',
      draft.mode === 'firmware'
        ? `Release GPIO${selectedPin.gpioNum} so original firmware logic regains ownership.`
        : `Apply a service-only GPIO override to GPIO${selectedPin.gpioNum}.`,
      {
        gpio: selectedPin.gpioNum,
        mode: draft.mode,
        level_high: draft.levelHigh,
        ...bias,
      },
    )

    if (ok) {
      setDraftDirty(false)
    }
  }

  async function releaseSelectedPin() {
    const ok = await onRunCommand(
      `Release GPIO${selectedPin.gpioNum}`,
      'set_gpio_override',
      'service',
      `Release GPIO${selectedPin.gpioNum} so original firmware logic regains ownership.`,
      {
        gpio: selectedPin.gpioNum,
        mode: 'firmware',
        level_high: false,
        pullup_enabled: false,
        pulldown_enabled: false,
      },
    )

    if (ok) {
      setDraftDirty(false)
    }
  }

  async function resetAllOverrides() {
    const ok = await onRunCommand(
      'Reset all GPIO control',
      'clear_gpio_overrides',
      'service',
      'Release every GPIO override and hand control back to the original firmware logic.',
    )

    if (ok) {
      setDraftDirty(false)
    }
  }

  function renderPinButton(gpioNum: number) {
    const meta = gpioModulePins.find((pin) => pin.gpioNum === gpioNum)
    const pin = pinLookup.get(gpioNum)

    if (meta === undefined || pin === undefined) {
      return null
    }

    const isSelected = gpioNum === selectedGpio
    const analogReading = deriveAnalogReading(meta, snapshot)
    const actualMode = formatCompactMode(pin, meta, analogReading)
    const pinStateClass = pin.outputEnabled
      ? pin.levelHigh
        ? 'is-driven-high'
        : 'is-driven-low'
      : pin.levelHigh
        ? 'is-sensed-high'
        : 'is-sensed-low'

    return (
      <button
        key={gpioNum}
        type="button"
        className={[
          'gpio-pin-button',
          `gpio-pin-button--${meta.signalClass}`,
          isSelected ? 'is-selected' : '',
          pin.overrideActive ? 'is-overridden' : '',
          pin.levelHigh ? 'is-high' : 'is-low',
          pinStateClass,
          meta.riskNote !== undefined ? 'is-risky' : '',
        ]
          .filter(Boolean)
          .join(' ')}
        title={`${meta.netName}: ${meta.detail}${meta.riskNote ? ` ${meta.riskNote}` : ''}`}
        onClick={() => selectGpio(gpioNum)}
      >
        <div className="gpio-pin-button__head">
          <strong>GPIO{gpioNum}</strong>
          <span>P{meta.modulePin}</span>
        </div>
        <div className="gpio-pin-button__body">
          <span className="gpio-pin-button__label">{meta.label}</span>
          <span className="gpio-pin-button__net">{meta.netName}</span>
        </div>
        <div className="gpio-pin-button__tokens">
          <span
            className={`gpio-state-dot ${
              meta.signalClass === 'analog' ? 'is-analog' : pin.levelHigh ? 'is-high' : 'is-low'
            }`}
            aria-hidden="true"
          />
          <span className={`gpio-mini-token is-mode-${getModeTone(pin, meta)}`}>{actualMode}</span>
          {analogReading !== null ? (
            <span className="gpio-mini-token is-analog">
              {formatNumber(analogReading.voltageV, 2)}V
            </span>
          ) : null}
          {pin.overrideActive ? <span className="gpio-mini-token is-override">OVR</span> : null}
        </div>
      </button>
    )
  }

  return (
    <article className="panel-cutout tools-panel tools-panel--wide gpio-workbench">
      <div className="cutout-head">
        <Cpu size={16} />
        <strong>ESP32-S3 module GPIO inspector</strong>
      </div>

      <div className="gpio-workbench__summary">
        <p className="panel-note">
          This view shows live GPIO truth from the controller snapshot: level, mode,
          pull state, and whether service-mode overrides currently own the pin. Override
          writes are service-only and should stay on the bench, never in normal runtime.
        </p>
        <div className="status-badges">
          <span className={`status-badge ${connected ? '' : 'is-off'}`}>
            {connected ? 'Board linked' : 'Board offline'}
          </span>
          <span className={`status-badge ${serviceWriteReady ? '' : 'is-off'}`}>
            {serviceWriteReady ? 'Service write enabled' : 'Read-only until service mode'}
          </span>
          <span className={`status-badge ${anyOverrideActive ? 'is-warn' : ''}`}>
            {activeOverrideCount} override
            {activeOverrideCount === 1 ? '' : 's'}
          </span>
        </div>
      </div>

      <div className="gpio-workbench__layout">
        <div className="gpio-module-visual">
          <div className="gpio-module-visual__column gpio-module-visual__column--left">
            {leftPins.map((pin) => renderPinButton(pin.gpioNum))}
          </div>

          <div className="gpio-module-visual__chip">
            <div className="gpio-module-visual__antenna" />
            <div className="gpio-module-visual__package">
              <div className="gpio-module-visual__badge">ESP32-S3-WROOM</div>
              <strong>Castellated live map</strong>
              <p>
                Compact pad view sized closer to the module footprint. Select a pad
                to inspect live state and stage a temporary service override.
              </p>
              <div className="gpio-module-visual__legend">
                {gpioSignalLegend.map((signalClass) => (
                  <span
                    key={signalClass}
                    className={`gpio-detail-chip is-${signalClass}`}
                  >
                    {formatSignalClass(signalClass)}
                  </span>
                ))}
              </div>
              <div className="gpio-module-visual__center-stats">
                <div>
                  <span>Visible pads</span>
                  <strong>{gpioModulePins.length}</strong>
                </div>
                <div>
                  <span>Overrides</span>
                  <strong>{activeOverrideCount}</strong>
                </div>
                <div>
                  <span>Risk pins</span>
                  <strong>
                    {gpioModulePins.filter((pin) => pin.riskNote !== undefined).length}
                  </strong>
                </div>
              </div>
            </div>
          </div>

          <div className="gpio-module-visual__column gpio-module-visual__column--right">
            {rightPins.map((pin) => renderPinButton(pin.gpioNum))}
          </div>
        </div>

        <div className="gpio-detail-card">
          <div className="gpio-detail-card__head">
            <div>
              <span className="eyebrow">Selected GPIO</span>
              <h3>
                GPIO{selectedPin.gpioNum} · module pin {selectedPin.modulePin}
              </h3>
            </div>
            <div className="status-badges">
              <span className={`status-badge ${selectedPin.levelHigh ? '' : 'is-off'}`}>
                {selectedPin.levelHigh ? 'Level high' : 'Level low'}
              </span>
              <span className={`status-badge ${selectedPin.overrideActive ? 'is-warn' : 'is-off'}`}>
                {selectedPin.overrideActive ? 'Override active' : 'Firmware-owned'}
              </span>
            </div>
          </div>

          <div className="gpio-detail-card__identity">
            <div>
              <strong>{selectedMeta?.label ?? `GPIO${selectedPin.gpioNum}`}</strong>
              <p>{selectedMeta?.detail ?? 'No board annotation available for this pad.'}</p>
              <div className="gpio-detail-card__chips">
                <span className={`gpio-detail-chip is-${selectedMeta?.signalClass ?? 'control'}`}>
                  {formatSignalClass(selectedMeta?.signalClass ?? 'control')}
                </span>
                <span
                  className={`gpio-detail-chip ${
                    selectedMeta?.signalClass === 'analog'
                      ? 'is-analog'
                      : selectedPin.levelHigh
                        ? 'is-high'
                        : 'is-low'
                  }`}
                >
                  {selectedMeta?.signalClass === 'analog'
                    ? selectedAnalogReading !== null
                      ? `${formatNumber(selectedAnalogReading.voltageV, 3)} V ADC`
                      : 'Analog telemetry'
                    : selectedPin.levelHigh
                      ? 'Level high'
                      : 'Level low'}
                </span>
                <span
                  className={`gpio-detail-chip is-mode-${getModeTone(
                    selectedPin,
                    selectedMeta,
                  )}`}
                >
                  {formatCompactMode(selectedPin, selectedMeta, selectedAnalogReading)}
                </span>
                {selectedPin.overrideActive ? (
                  <span className="gpio-detail-chip is-override">Service override</span>
                ) : null}
              </div>
            </div>
            <div className="gpio-detail-card__net">
              <span className="eyebrow">Board net</span>
              <strong>{selectedMeta?.netName ?? `GPIO${selectedPin.gpioNum}`}</strong>
            </div>
          </div>

          {selectedMeta?.riskNote !== undefined ? (
            <div className="gpio-risk-banner">
              <TriangleAlert size={16} />
              <span>{selectedMeta.riskNote}</span>
            </div>
          ) : null}

          <div className="metric-grid is-two gpio-detail-grid">
            <div
              className={`metric-card gpio-detail-metric is-mode-${getModeTone(
                selectedPin,
                selectedMeta,
              )}`}
            >
              <span>Actual direction</span>
              <strong>
                {formatActualMode(selectedPin, selectedMeta, selectedAnalogReading)}
              </strong>
              <small>
                {selectedMeta?.signalClass === 'analog'
                  ? 'sampled through the controller ADC path'
                  : selectedPin.outputCapable
                    ? 'output-capable pad'
                    : 'input-only / limited pad'}
              </small>
            </div>
            <div className="metric-card gpio-detail-metric">
              <span>Actual bias</span>
              <strong>{formatBias(selectedPin, selectedMeta)}</strong>
              <small>
                {selectedMeta?.signalClass === 'analog'
                  ? 'ADC path does not use the digital pull-state summary here'
                  : selectedPin.openDrainEnabled
                    ? 'open-drain enabled'
                    : 'push-pull or input path'}
              </small>
            </div>
            <div
              className={`metric-card gpio-detail-metric ${
                selectedMeta?.signalClass === 'analog'
                  ? 'is-analog'
                  : selectedPin.levelHigh
                    ? 'is-high'
                    : 'is-low'
              }`}
            >
              <span>{selectedMeta?.signalClass === 'analog' ? 'Analog sample' : 'Actual level'}</span>
              <strong>
                {selectedMeta?.signalClass === 'analog'
                  ? selectedAnalogReading !== null
                    ? `${formatNumber(selectedAnalogReading.voltageV, 3)} V`
                    : 'No ADC sample'
                  : selectedPin.levelHigh
                    ? 'High'
                    : 'Low'}
              </strong>
              <small>
                {selectedMeta?.signalClass === 'analog'
                  ? 'live ADC telemetry from the controller snapshot'
                  : 'live pin sample from the controller snapshot'}
              </small>
            </div>
            <div className={`metric-card gpio-detail-metric ${selectedPin.overrideActive ? 'is-override' : ''}`}>
              <span>Override owner</span>
              <strong>
                {selectedPin.overrideActive
                  ? selectedPin.overrideMode.replace('_', ' ')
                  : 'firmware'}
              </strong>
              <small>
                {selectedPin.overrideActive
                  ? 'service-mode override is active'
                  : 'normal firmware logic owns this pin'}
              </small>
            </div>
          </div>

          {selectedMeta?.signalClass === 'analog' ? (
            <div className="gpio-analog-card">
              <div className="gpio-analog-card__head">
                <div>
                  <span className="eyebrow">ADC range meter</span>
                  <strong>
                    {selectedAnalogReading?.label ?? 'Analog telemetry input'}
                  </strong>
                </div>
                <strong>
                  {selectedAnalogReading !== null
                    ? `${formatNumber(selectedAnalogReading.voltageV, 3)} V`
                    : 'No live ADC voltage'}
                </strong>
              </div>

              <div
                className="gpio-analog-card__meter"
                role="progressbar"
                aria-label="ADC voltage"
                aria-valuemin={0}
                aria-valuemax={selectedAnalogMaxVoltageV}
                aria-valuenow={selectedAnalogReading?.voltageV ?? 0}
              >
                <span style={{ width: `${selectedAnalogPercent}%` }} />
              </div>

              <div className="gpio-analog-card__scale" aria-hidden="true">
                <span>0.0 V</span>
                <span>{formatNumber(selectedAnalogMaxVoltageV / 2, 2)} V</span>
                <span>{formatNumber(selectedAnalogMaxVoltageV, 2)} V</span>
              </div>

              {selectedAnalogReading !== null ? (
                <div className="gpio-detail-card__chips">
                  <span className="gpio-detail-chip is-analog">
                    {selectedAnalogReading.summary}
                  </span>
                </div>
              ) : null}

              <small>
                {selectedAnalogReading?.caption ??
                  'This pad is marked as an analog telemetry input, but the current snapshot does not publish a raw 0-3.3 V ADC sample for it yet.'}
              </small>
            </div>
          ) : null}

          <div className="field-grid gpio-override-grid">
            <label className="field">
              <span className="field-label">Override mode</span>
              <select
                value={draft.mode}
                title="Firmware returns ownership to the original control logic. Input and output claim the pin in service mode."
                onChange={(event) =>
                  {
                    setDraftDirty(true)
                    setDraft((current) => ({
                      ...current,
                      mode: event.target.value as GpioOverrideMode,
                    }))
                  }
                }
              >
                <option value="firmware">Firmware control</option>
                <option value="input">Force input</option>
                <option value="output" disabled={!selectedPin.outputCapable}>
                  Force output
                </option>
              </select>
            </label>

            <label className="field">
              <span className="field-label">Bias / pull</span>
              <select
                value={draft.bias}
                title="Apply a pull-up or pull-down during the service override."
                onChange={(event) =>
                  {
                    setDraftDirty(true)
                    setDraft((current) => ({
                      ...current,
                      bias: event.target.value as GpioBias,
                    }))
                  }
                }
              >
                <option value="floating">Floating</option>
                <option value="pullup">Pull-up</option>
                <option value="pulldown">Pull-down</option>
              </select>
            </label>
          </div>

          <div className="gpio-output-toggle">
            <span className="field-label">Output drive</span>
            <div className="segmented gpio-output-toggle__buttons" role="group" aria-label="GPIO output drive">
              <button
                type="button"
                className={`segmented__button ${!draft.levelHigh ? 'is-active' : ''}`}
                disabled={draft.mode !== 'output'}
                onClick={() => {
                  setDraftDirty(true)
                  setDraft((current) => ({
                    ...current,
                    levelHigh: false,
                  }))
                }}
              >
                Drive low
              </button>
              <button
                type="button"
                className={`segmented__button ${draft.levelHigh ? 'is-active' : ''}`}
                disabled={draft.mode !== 'output'}
                onClick={() => {
                  setDraftDirty(true)
                  setDraft((current) => ({
                    ...current,
                    levelHigh: true,
                  }))
                }}
              >
                Drive high
              </button>
            </div>
            <small className="inline-help">
              Output drive is only applied when the selected override mode is
              <strong> Force output</strong>.
            </small>
          </div>

          <div className="button-row is-compact gpio-detail-card__actions">
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!serviceWriteReady || (draft.mode === 'output' && !selectedPin.outputCapable)}
              title="Apply the selected service-only GPIO override."
              onClick={() => {
                void applyOverride()
              }}
            >
              Apply selected override
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!serviceWriteReady || !selectedPin.overrideActive}
              title="Release only the selected GPIO back to firmware ownership."
              onClick={() => {
                void releaseSelectedPin()
              }}
            >
              Release selected pin
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!serviceWriteReady || !anyOverrideActive}
              title="Clear every active GPIO override and restore normal firmware ownership."
              onClick={() => {
                void resetAllOverrides()
              }}
            >
              <RefreshCcw size={14} />
              Reset all to firmware
            </button>
          </div>

          <div className="note-strip gpio-note-strip">
            <span>
              Actual status above is live controller readback. The override editor below is
              only a staged service request until you apply it.
            </span>
          </div>
        </div>
      </div>
    </article>
  )
}
