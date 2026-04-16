import { useEffect, useState } from 'react'
import { CircleDot, Cpu, Lightbulb, ShieldCheck, TriangleAlert } from 'lucide-react'

import type { DeviceSnapshot, TransportStatus, TriggerPhase } from '../types'

type ButtonBoardPanelProps = {
  snapshot: DeviceSnapshot
  transportStatus: TransportStatus
  onIssueCommandAwaitAck: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
    options?: {
      logHistory?: boolean
      timeoutMs?: number
    },
  ) => Promise<{ ok: boolean; note: string }>
}

const TRIGGER_PHASE_LABEL: Record<TriggerPhase, string> = {
  off: 'Off (pre-deployment)',
  ready: 'Ready — awaiting trigger',
  armed: 'Armed — stage 1 held',
  firing: 'Firing — NIR active',
  interlock: 'Interlock active',
  lockout: 'Lockout — release trigger',
  unrecoverable: 'Unrecoverable fault',
}

/**
 * Button Board Panel — Integrate workspace.
 *
 * Renders live MCP23017 + TLC59116 reachability, the four button states
 * (stage1 / stage2 / side1 / side2) with active-state CSS chips, the
 * GPIO7 ISR fire counter, the firmware-computed RGB target color/blink,
 * and a service-mode-only test path that drives an arbitrary RGB color
 * for up to 30 s via integrate.rgb_led.set.
 *
 * Hardware reference: the button board uses
 *   - MCP23017 @ 0x20 (GPA0=stage1, GPA1=stage2, GPA2=side1, GPA3=side2)
 *   - TLC59116 @ 0x60 (OUT0=B, OUT1=R, OUT2=G — non-standard channel order)
 * INTA is wired to ESP32 GPIO7 (open-drain, active-low). See
 * docs/firmware-pinmap.md "Button board" section.
 *
 * Lives under Integrate (bring-up workbench). Operate gets a separate
 * read-only trigger-state card; this panel is the only place where the
 * RGB test command is reachable.
 */
export function ButtonBoardPanel({
  snapshot,
  transportStatus,
  onIssueCommandAwaitAck,
}: ButtonBoardPanelProps) {
  const connected = transportStatus === 'connected'
  const board = snapshot.buttonBoard
  const buttons = snapshot.buttons
  const serviceModeActive = snapshot.bringup.serviceModeActive
  const deploymentActive = snapshot.deployment.active
  const faultLatched = snapshot.fault.latched

  const testBlocker = !connected
    ? 'No controller is connected.'
    : !serviceModeActive
      ? 'Enter service mode first (Bring-up workbench).'
      : deploymentActive
        ? 'Exit deployment mode before driving the RGB LED test.'
        : faultLatched
          ? 'Clear the latched fault first.'
          : null
  const testAllowed = testBlocker === null

  const [r, setR] = useState(board.rgb.r)
  const [g, setG] = useState(board.rgb.g)
  const [b, setB] = useState(board.rgb.b)
  const [blink, setBlink] = useState(board.rgb.blink)

  // Sync the local sliders with firmware-published state when the test
  // expires or the operator clears it. Only update local state when not
  // actively dragging (testActive=false from firmware) so we don't fight
  // the user's input.
  useEffect(() => {
    if (!board.rgb.testActive) {
      setR(board.rgb.r)
      setG(board.rgb.g)
      setB(board.rgb.b)
      setBlink(board.rgb.blink)
    }
  }, [board.rgb.testActive, board.rgb.r, board.rgb.g, board.rgb.b, board.rgb.blink])

  async function issue(
    cmd: string,
    note: string,
    args?: Record<string, number | string | boolean>,
  ) {
    await onIssueCommandAwaitAck(cmd, 'service', note, args, { timeoutMs: 3000 })
  }

  const buttonsHealthy = board.mcpReachable && board.mcpConfigured
  const rgbHealthy = board.tlcReachable && board.tlcConfigured
  const headerStatusClass = buttonsHealthy && rgbHealthy
    ? 'status-badge'
    : 'status-badge is-warn'
  const headerStatusLabel = buttonsHealthy && rgbHealthy ? 'Reachable' : 'Unreachable'
  const HeaderIcon = buttonsHealthy && rgbHealthy ? ShieldCheck : TriangleAlert

  return (
    <section className="panel-section">
      <div className="section-head">
        <div>
          <h3>Button board</h3>
          <p>
            MCP23017 @ {board.mcpAddr} (4 buttons via INTA on GPIO7) and
            TLC59116 @ {board.tlcAddr} (RGB status LED, B/R/G on OUT0/1/2).
            Live state mirrors the firmware decision; the test command lets
            you bench-verify the RGB LED in service mode.
          </p>
        </div>
        <span className={headerStatusClass}>
          <HeaderIcon size={14} />
          {headerStatusLabel}
        </span>
      </div>

      <dl className="summary-list">
        <div>
          <dt>MCP23017 / 0x20</dt>
          <dd>
            {board.mcpReachable ? 'Reachable' : 'Unreachable'} ·{' '}
            {board.mcpConfigured ? 'Configured' : 'Unconfigured'}
            {board.mcpConsecFailures > 0 &&
              ` · ${board.mcpConsecFailures} consec. read failures`}
          </dd>
        </div>
        <div>
          <dt>TLC59116 / 0x60</dt>
          <dd>
            {board.tlcReachable ? 'Reachable' : 'Unreachable'} ·{' '}
            {board.tlcConfigured ? 'Configured' : 'Unconfigured'}
            {board.tlcLastError !== 0 && ` · err=${board.tlcLastError}`}
          </dd>
        </div>
        <div>
          <dt>INTA fires</dt>
          <dd>{board.isrFireCount.toLocaleString()}</dd>
        </div>
        <div>
          <dt>Trigger phase</dt>
          <dd>{TRIGGER_PHASE_LABEL[board.triggerPhase]}</dd>
        </div>
        <div>
          <dt>Front LED</dt>
          <dd>
            {board.ledOwned ? `Button-driven · ${board.ledBrightnessPct}%` : 'Operate / service owned'}
          </dd>
        </div>
        <div>
          <dt>NIR lockout</dt>
          <dd>{board.triggerLockout ? 'LATCHED — release trigger' : 'Clear'}</dd>
        </div>
      </dl>

      <div className="button-board-buttons">
        <ButtonChip label="Stage 1" pressed={buttons.stage1Pressed} hint="GPA0 / main trigger shallow" />
        <ButtonChip label="Stage 2" pressed={buttons.stage2Pressed} hint="GPA1 / main trigger deep" />
        <ButtonChip label="Side 1" pressed={buttons.side1Pressed} hint="GPA2 / +10% LED brightness" />
        <ButtonChip label="Side 2" pressed={buttons.side2Pressed} hint="GPA3 / -10% LED brightness" />
      </div>

      <div className="rgb-test-grid">
        <label className="field">
          <span>Red</span>
          <input
            type="range"
            min={0}
            max={255}
            value={r}
            onChange={(event) => setR(Number(event.target.value))}
            disabled={!testAllowed}
            title={testBlocker ?? undefined}
            aria-label="Red channel intensity 0 to 255"
          />
          <output>{r}</output>
        </label>
        <label className="field">
          <span>Green</span>
          <input
            type="range"
            min={0}
            max={255}
            value={g}
            onChange={(event) => setG(Number(event.target.value))}
            disabled={!testAllowed}
            title={testBlocker ?? undefined}
            aria-label="Green channel intensity 0 to 255"
          />
          <output>{g}</output>
        </label>
        <label className="field">
          <span>Blue</span>
          <input
            type="range"
            min={0}
            max={255}
            value={b}
            onChange={(event) => setB(Number(event.target.value))}
            disabled={!testAllowed}
            title={testBlocker ?? undefined}
            aria-label="Blue channel intensity 0 to 255"
          />
          <output>{b}</output>
        </label>
        <label className="field is-checkbox">
          <input
            type="checkbox"
            checked={blink}
            onChange={(event) => setBlink(event.target.checked)}
            disabled={!testAllowed}
            title={testBlocker ?? undefined}
            aria-label="Hardware blink (1 Hz, 50 percent duty)"
          />
          <span>Blink (1 Hz, 50% duty)</span>
        </label>
      </div>

      <div className="rgb-target-preview">
        <span
          className={board.rgb.blink ? 'rgb-indicator is-blinking' : 'rgb-indicator'}
          style={{
            backgroundColor: board.rgb.enabled
              ? `rgb(${board.rgb.r}, ${board.rgb.g}, ${board.rgb.b})`
              : 'transparent',
          }}
          aria-label={`Current RGB target r=${board.rgb.r} g=${board.rgb.g} b=${board.rgb.b}`}
        />
        <div>
          <strong>{board.rgb.testActive ? 'Test override active' : 'Firmware-computed'}</strong>
          <p>
            R {board.rgb.r} · G {board.rgb.g} · B {board.rgb.b} ·{' '}
            {board.rgb.blink ? 'blinking' : 'solid'} ·{' '}
            {board.rgb.enabled ? 'on' : 'off'}
          </p>
        </div>
      </div>

      {testBlocker !== null && (
        <p className="inline-help" role="status">
          {testBlocker}
        </p>
      )}

      <div className="button-row is-compact">
        <button
          type="button"
          className="action-button is-inline is-accent"
          disabled={!testAllowed}
          title={testBlocker ?? 'Drive RGB LED to the chosen color for 5 s.'}
          onClick={() =>
            void issue(
              'integrate.rgb_led.set',
              `Drive RGB LED test color r=${r} g=${g} b=${b} blink=${blink}.`,
              { r, g, b, blink, hold_ms: 5000 },
            )
          }
        >
          <Lightbulb size={14} />
          Send test color
        </button>
        <button
          type="button"
          className="action-button is-inline"
          disabled={!connected || !board.rgb.testActive}
          title={
            !connected
              ? 'No controller is connected.'
              : !board.rgb.testActive
                ? 'No RGB test override active.'
                : 'Revert to the firmware-computed color.'
          }
          onClick={() =>
            void issue(
              'integrate.rgb_led.clear',
              'Clear RGB LED test override and revert to firmware-computed state.',
            )
          }
        >
          Clear test
        </button>
        <span className="button-board-meta">
          <Cpu size={12} /> ESP32 GPIO7 = INTA · open-drain pull-up enabled
        </span>
      </div>
    </section>
  )
}

function ButtonChip(props: {
  label: string
  pressed: boolean
  hint: string
}) {
  return (
    <div
      className={
        props.pressed
          ? 'button-chip is-pressed'
          : 'button-chip'
      }
      title={props.hint}
      aria-pressed={props.pressed}
    >
      <CircleDot size={14} />
      <span className="button-chip-label">{props.label}</span>
      <span className="button-chip-state">{props.pressed ? 'PRESSED' : 'idle'}</span>
    </div>
  )
}
