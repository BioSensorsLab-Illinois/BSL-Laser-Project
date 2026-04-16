import { AlertTriangle, ShieldCheck } from 'lucide-react'

import type { DeviceSnapshot, TransportStatus } from '../types'

type UsbDebugMockPanelProps = {
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

/**
 * USB-Debug Mock Panel — operator control for the firmware mock layer that
 * synthesizes TEC/LD rail PGOOD and telemetry so a USB-only session can
 * exercise the full deployment / runtime / fault paths without real bench
 * power.
 *
 * Hard-isolated per AGENT.md "USB-Only Debug Power":
 *   - Explicit opt-in only.
 *   - Auto-disables on real PD (any tier above programming_only).
 *   - Auto-disables on any other fault, on service-mode exit, or on
 *     explicit operator disable.
 *   - NEVER drives any GPIO.
 *
 * Lives under the Integrate workspace because it is service-mode-only and
 * because Integrate is where bring-up tools live. Operate intentionally
 * does NOT carry this control — only the read-only banner.
 */
export function UsbDebugMockPanel({
  snapshot,
  transportStatus,
  onIssueCommandAwaitAck,
}: UsbDebugMockPanelProps) {
  const connected = transportStatus === 'connected'
  const mock = snapshot.bench.usbDebugMock
  const serviceModeActive = snapshot.bringup.serviceModeActive
  const powerTier = snapshot.session.powerTier
  const programmingOnly = powerTier === 'programming_only'
  const faultLatched = snapshot.fault.latched
  const pdConflictLatched = mock.pdConflictLatched

  const enableBlocker = !connected
    ? 'No controller is connected.'
    : !serviceModeActive
      ? 'Enter service mode first (Bring-up workbench).'
      : !programmingOnly
        ? `Power tier is ${powerTier}. Mock only engages on USB-only (programming_only) power.`
        : pdConflictLatched
          ? 'Clear the latched usb_debug_mock_pd_conflict fault first.'
          : faultLatched
            ? 'Clear the latched fault first.'
            : null

  const enableAllowed = enableBlocker === null && !mock.active

  async function issue(cmd: string, note: string) {
    await onIssueCommandAwaitAck(cmd, 'service', note, undefined, { timeoutMs: 3000 })
  }

  return (
    <section className="panel-section">
      <div className="section-head">
        <div>
          <h3>USB Debug Mock</h3>
          <p>
            Synthesized telemetry for online testing on USB-only power.
            Activate when TEC and LD rails physically cannot come up because
            USB cannot supply enough current. Mock is hard-isolated and
            auto-disables on any real-power or fault transition.
          </p>
        </div>
        <span className={mock.active ? 'status-badge is-warn' : 'status-badge'}>
          {mock.active ? <AlertTriangle size={14} /> : <ShieldCheck size={14} />}
          {mock.active ? 'Mock active' : 'Mock off'}
        </span>
      </div>

      <dl className="summary-list">
        <div>
          <dt>Service mode</dt>
          <dd>{serviceModeActive ? 'Active' : 'Inactive'}</dd>
        </div>
        <div>
          <dt>Power tier</dt>
          <dd>{powerTier.replaceAll('_', ' ')}</dd>
        </div>
        <div>
          <dt>PD-conflict latched</dt>
          <dd>{pdConflictLatched ? 'Yes — clear faults first' : 'No'}</dd>
        </div>
        <div>
          <dt>Last disable reason</dt>
          <dd>{mock.lastDisableReason || '—'}</dd>
        </div>
      </dl>

      {mock.active && (
        <div className="inline-alert">
          <strong>Mock is currently substituting telemetry.</strong>
          <p>
            TEC and LD rail PGOOD, TEC temperature, LD current monitor, and
            related telemetry are synthesized. Real GPIOs are NOT driven by
            the mock — they remain governed by the normal control path. The
            moment real PD power is detected, the mock will disable itself
            and latch a SYSTEM_MAJOR fault.
          </p>
        </div>
      )}

      {enableBlocker !== null && !mock.active && (
        <p className="inline-help" role="status">
          {enableBlocker}
        </p>
      )}

      <div className="button-row is-compact">
        <button
          type="button"
          className="action-button is-inline is-accent"
          disabled={!enableAllowed}
          title={enableBlocker ?? undefined}
          onClick={() =>
            void issue(
              'service.usb_debug_mock_enable',
              'Enable the USB-debug mock layer (synthesized rail PGOOD + TEC/LD telemetry).',
            )
          }
        >
          Enable mock
        </button>
        <button
          type="button"
          className="action-button is-inline"
          disabled={!connected || !mock.active}
          title={
            !connected
              ? 'No controller is connected.'
              : !mock.active
                ? 'Mock is not active.'
                : undefined
          }
          onClick={() =>
            void issue(
              'service.usb_debug_mock_disable',
              'Disable the USB-debug mock layer; revert to real telemetry reads.',
            )
          }
        >
          Disable mock
        </button>
      </div>
    </section>
  )
}
