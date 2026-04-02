import { useMemo, useState } from 'react'
import { Cpu, RefreshCcw, TriangleAlert } from 'lucide-react'

import { gpioModulePins } from '../lib/gpio-layout'
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

function formatActualMode(pin: GpioPinReadback) {
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

function formatBias(pin: GpioPinReadback) {
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

  const [draft, setDraft] = useState<GpioDraft>(() => makeDraftFromPin(selectedPin))

  const serviceWriteReady = connected && snapshot.bringup.serviceModeActive
  const anyOverrideActive = snapshot.gpioInspector.anyOverrideActive

  function selectGpio(gpioNum: number) {
    const nextPin = pinLookup.get(gpioNum)
    setSelectedGpio(gpioNum)
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

    if (ok && draft.mode === 'firmware') {
      setDraft(makeDraftFromPin(selectedPin))
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
      setDraft(makeDraftFromPin(selectedPin))
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
      setDraft(makeDraftFromPin(selectedPin))
    }
  }

  function renderPinButton(gpioNum: number) {
    const meta = gpioModulePins.find((pin) => pin.gpioNum === gpioNum)
    const pin = pinLookup.get(gpioNum)

    if (meta === undefined || pin === undefined) {
      return null
    }

    const isSelected = gpioNum === selectedGpio
    const actualMode = pin.outputEnabled ? 'OUT' : pin.inputEnabled ? 'IN' : 'IDLE'

    return (
      <button
        key={gpioNum}
        type="button"
        className={[
          'gpio-pin-button',
          isSelected ? 'is-selected' : '',
          pin.overrideActive ? 'is-overridden' : '',
          pin.levelHigh ? 'is-high' : 'is-low',
          meta.riskNote !== undefined ? 'is-risky' : '',
        ]
          .filter(Boolean)
          .join(' ')}
        title={`${meta.netName}: ${meta.detail}${meta.riskNote ? ` ${meta.riskNote}` : ''}`}
        onClick={() => selectGpio(gpioNum)}
      >
        <div className="gpio-pin-button__head">
          <strong>GPIO{gpioNum}</strong>
          <span>pin {meta.modulePin}</span>
        </div>
        <span className="gpio-pin-button__label">{meta.label}</span>
        <div className="gpio-pin-button__tokens">
          <span className={`gpio-mini-token ${pin.levelHigh ? 'is-high' : 'is-low'}`}>
            {pin.levelHigh ? 'HIGH' : 'LOW'}
          </span>
          <span className="gpio-mini-token">{actualMode}</span>
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
            {snapshot.gpioInspector.activeOverrideCount} override
            {snapshot.gpioInspector.activeOverrideCount === 1 ? '' : 's'}
          </span>
        </div>
      </div>

      <div className="gpio-workbench__layout">
        <div className="gpio-module-visual">
          <div className="gpio-module-visual__column">
            {leftPins.map((pin) => renderPinButton(pin.gpioNum))}
          </div>

          <div className="gpio-module-visual__chip">
            <div className="gpio-module-visual__badge">ESP32-S3-WROOM</div>
            <strong>Physical module map</strong>
            <p>
              Select a side pad to inspect actual readback and optionally apply a
              temporary service override.
            </p>
            <div className="gpio-module-visual__center-stats">
              <div>
                <span>Visible pads</span>
                <strong>{gpioModulePins.length}</strong>
              </div>
              <div>
                <span>Overrides</span>
                <strong>{snapshot.gpioInspector.activeOverrideCount}</strong>
              </div>
              <div>
                <span>Transport risk pins</span>
                <strong>
                  {
                    gpioModulePins.filter((pin) => pin.riskNote !== undefined).length
                  }
                </strong>
              </div>
            </div>
          </div>

          <div className="gpio-module-visual__column">
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
            <div className="metric-card">
              <span>Actual direction</span>
              <strong>{formatActualMode(selectedPin)}</strong>
              <small>
                {selectedPin.outputCapable ? 'output-capable pad' : 'input-only / limited pad'}
              </small>
            </div>
            <div className="metric-card">
              <span>Actual bias</span>
              <strong>{formatBias(selectedPin)}</strong>
              <small>
                {selectedPin.openDrainEnabled ? 'open-drain enabled' : 'push-pull or input path'}
              </small>
            </div>
            <div className="metric-card">
              <span>Actual level</span>
              <strong>{selectedPin.levelHigh ? 'High' : 'Low'}</strong>
              <small>live pin sample from the controller snapshot</small>
            </div>
            <div className="metric-card">
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

          <div className="field-grid gpio-override-grid">
            <label className="field">
              <span className="field-label">Override mode</span>
              <select
                value={draft.mode}
                title="Firmware returns ownership to the original control logic. Input and output claim the pin in service mode."
                onChange={(event) =>
                  setDraft((current) => ({
                    ...current,
                    mode: event.target.value as GpioOverrideMode,
                  }))
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
                  setDraft((current) => ({
                    ...current,
                    bias: event.target.value as GpioBias,
                  }))
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
                onClick={() =>
                  setDraft((current) => ({
                    ...current,
                    levelHigh: false,
                  }))
                }
              >
                Drive low
              </button>
              <button
                type="button"
                className={`segmented__button ${draft.levelHigh ? 'is-active' : ''}`}
                disabled={draft.mode !== 'output'}
                onClick={() =>
                  setDraft((current) => ({
                    ...current,
                    levelHigh: true,
                  }))
                }
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
