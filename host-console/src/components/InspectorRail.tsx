import { Cpu, Download, MemoryStick, Plug, TriangleAlert } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { formatRelativeUptime, formatShortTime } from '../lib/format'
import {
  buildSafetyChecks,
  formatEnumLabel,
  summarizeSafetyChecks,
} from '../lib/presentation'
import type {
  CommandHistoryEntry,
  DeviceSnapshot,
  FirmwareTransferProgress,
  TransportKind,
  TransportStatus,
} from '../types'

type InspectorRailProps = {
  snapshot: DeviceSnapshot
  commands: CommandHistoryEntry[]
  transportKind: TransportKind
  transportStatus: TransportStatus
  transportDetail: string
  firmwareProgress: FirmwareTransferProgress | null
  onExportSession: () => void
  onSetTransportKind: (kind: TransportKind) => Promise<void> | void
  onConnect: () => Promise<void> | void
  onDisconnect: () => Promise<void> | void
  onToggleServiceMode: () => Promise<void> | void
}

export function InspectorRail({
  snapshot,
  commands,
  transportKind,
  transportStatus,
  transportDetail,
  firmwareProgress,
  onExportSession,
  onSetTransportKind,
  onConnect,
  onDisconnect,
  onToggleServiceMode,
}: InspectorRailProps) {
  const recentCommands = commands.slice(0, 6)
  const safetySummary = summarizeSafetyChecks(buildSafetyChecks(snapshot))
  const connected = transportStatus === 'connected'
  const serviceModeLabel = snapshot.bringup.serviceModeActive
    ? 'Service active'
    : snapshot.bringup.serviceModeRequested
      ? 'Service requesting'
      : 'Service off'

  return (
    <aside className="inspector">
      <section className="inspector-block inspector-block--emphasis" data-tone={safetySummary.tone}>
        <div className="inspector-block__head">
          <TriangleAlert size={16} />
          <strong>Bench posture</strong>
        </div>
        <div className="inspector-summary">
          <div>
            <strong>{safetySummary.label}</strong>
            <span>
              {formatEnumLabel(snapshot.session.state)} • {safetySummary.passCount}/{safetySummary.total} checks
            </span>
          </div>
          <div className={`state-pill is-${safetySummary.tone}`}>
            <span>{safetySummary.percent}% ready</span>
          </div>
        </div>
        <ProgressMeter value={safetySummary.percent} tone={safetySummary.tone} />
      </section>

      <section className="inspector-block">
        <div className="inspector-block__head">
          <Cpu size={16} />
          <strong>Device</strong>
        </div>
        <dl className="key-grid">
          <div>
            <dt>Model</dt>
            <dd>{snapshot.identity.label}</dd>
          </div>
          <div>
            <dt>Firmware</dt>
            <dd>{snapshot.identity.firmwareVersion}</dd>
          </div>
          <div>
            <dt>Hardware</dt>
            <dd>{snapshot.identity.hardwareRevision}</dd>
          </div>
          <div>
            <dt>Serial</dt>
            <dd>{snapshot.identity.serialNumber}</dd>
          </div>
          <div>
            <dt>Uptime</dt>
            <dd>{formatRelativeUptime(snapshot.session.uptimeSeconds)}</dd>
          </div>
          <div>
            <dt>Boot</dt>
            <dd>{snapshot.session.bootReason}</dd>
          </div>
          <div>
            <dt>Lab</dt>
            <dd>BioSensors Lab @ UIUC</dd>
          </div>
          <div>
            <dt>Use</dt>
            <dd>F-IGS</dd>
          </div>
        </dl>
      </section>

      <section className="inspector-block">
        <div className="inspector-block__head">
          <Plug size={16} />
          <strong>Link and service</strong>
        </div>
        <div className="inspector-link-summary">
          <div className={`transport-chip is-${transportStatus}`}>
            <span>{transportKind === 'mock' ? 'mock rig' : 'web serial'}</span>
            <strong>{transportStatus}</strong>
          </div>
        </div>
        <div className="inspector-link-statuses">
          <span
            className={
              snapshot.bringup.serviceModeActive
                ? 'status-badge is-on'
                : snapshot.bringup.serviceModeRequested
                  ? 'status-badge is-warn'
                  : 'status-badge is-off'
            }
          >
            {serviceModeLabel}
          </span>
        </div>
        <div className="segmented inspector-link-transport">
          <button
            type="button"
            className={transportKind === 'mock' ? 'segmented__button is-active' : 'segmented__button'}
            onClick={() => {
              void onSetTransportKind('mock')
            }}
          >
            Mock rig
          </button>
          <button
            type="button"
            className={transportKind === 'serial' ? 'segmented__button is-active' : 'segmented__button'}
            onClick={() => {
              void onSetTransportKind('serial')
            }}
          >
            Web Serial
          </button>
        </div>
        <div className="button-row inspector-link-actions">
          <button
            type="button"
            className="action-button is-inline is-accent"
            onClick={() => {
              void onConnect()
            }}
          >
            {connected ? 'Refresh link' : 'Connect'}
          </button>
          <button
            type="button"
            className="action-button is-inline"
            disabled={transportStatus === 'disconnected'}
            onClick={() => {
              void onDisconnect()
            }}
          >
            Disconnect
          </button>
          <button
            type="button"
            className={snapshot.bringup.serviceModeActive ? 'action-button is-inline' : 'action-button is-inline is-accent'}
            disabled={!connected}
            onClick={() => {
              void onToggleServiceMode()
            }}
          >
            {snapshot.bringup.serviceModeActive ? 'Exit service' : 'Enter service'}
          </button>
        </div>
        <p className="inline-help">{transportDetail}</p>
        {firmwareProgress !== null ? (
          <>
            <ProgressMeter
              value={firmwareProgress.percent}
              tone={firmwareProgress.percent >= 100 ? 'steady' : 'warning'}
              compact
            />
            <p className="inline-help">
              Firmware phase: {firmwareProgress.phase} at {firmwareProgress.percent}%.
            </p>
          </>
        ) : null}
      </section>

      <section className="inspector-block">
        <div className="inspector-block__head">
          <TriangleAlert size={16} />
          <strong>Fault summary</strong>
        </div>
        <dl className="key-grid">
          <div>
            <dt>Active</dt>
            <dd>{snapshot.fault.activeCode}</dd>
          </div>
          <div>
            <dt>Latched</dt>
            <dd>{snapshot.fault.latched ? 'yes' : 'no'}</dd>
          </div>
          <div>
            <dt>Total trips</dt>
            <dd>{snapshot.fault.tripCounter}</dd>
          </div>
          <div>
            <dt>Last fault</dt>
            <dd>
              {snapshot.fault.lastFaultAtIso === null
                ? '—'
                : formatShortTime(snapshot.fault.lastFaultAtIso)}
            </dd>
          </div>
        </dl>
      </section>

      <section className="inspector-block">
        <div className="inspector-block__head">
          <MemoryStick size={16} />
          <strong>Recent commands</strong>
        </div>
        <div className="command-history">
          {recentCommands.length === 0 ? (
            <p className="inline-help">No host commands have been issued yet.</p>
          ) : (
            recentCommands.map((command) => (
              <div key={command.id} className={`history-row is-${command.status}`}>
                <div>
                  <strong>{command.cmd}</strong>
                  <span>{formatShortTime(command.issuedAtIso)}</span>
                </div>
                <div className="history-row__meta-group">
                  <span className={`history-row__risk is-${command.risk}`}>{command.risk}</span>
                  <em>{command.status}</em>
                </div>
              </div>
            ))
          )}
        </div>
      </section>

      <section className="inspector-block">
        <div className="inspector-block__head">
          <Download size={16} />
          <strong>Session archive</strong>
        </div>
        <p className="inline-help">
          Export snapshot, commands, and events as JSON.
        </p>
        <button type="button" className="action-button is-inline" onClick={onExportSession}>
          Export session bundle
        </button>
      </section>
    </aside>
  )
}
