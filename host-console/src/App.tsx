import { startTransition, useEffect, useMemo, useState } from 'react'
import { Activity, Cable, Clock3, Cpu, History, ShieldCheck, Upload } from 'lucide-react'

import { BringupWorkbench } from './components/BringupWorkbench'
import { BusTrafficViewer } from './components/BusTrafficViewer'
import { CommandDeck } from './components/CommandDeck'
import { ConnectionWorkbench } from './components/ConnectionWorkbench'
import { EventTimeline } from './components/EventTimeline'
import { FirmwareWorkbench } from './components/FirmwareWorkbench'
import { InspectorRail } from './components/InspectorRail'
import { ModuleReadinessPanel } from './components/ModuleReadinessPanel'
import { OperateConsole } from './components/OperateConsole'
import { SafetyMatrix } from './components/SafetyMatrix'
import { StatusRail } from './components/StatusRail'
import { parseFirmwareFile } from './lib/firmware'
import { formatNumber, formatRelativeUptime } from './lib/format'
import { useDeviceSession } from './hooks/use-device-session'
import { useGlobalHoverHelp } from './hooks/use-global-hover-help'
import { useLiveSnapshot } from './hooks/use-live-snapshot'
import { formatEnumLabel } from './lib/presentation'
import type {
  CommandHistoryEntry,
  FirmwarePackageDescriptor,
  Severity,
} from './types'

type PageId = 'system' | 'operate' | 'integrate' | 'update' | 'history'
type IntegrateTab = 'bringup' | 'tools'
type HistoryTab = 'events' | 'comms' | 'commands'

const pages: Array<{
  id: PageId
  label: string
  detail: string
  icon: typeof ShieldCheck
}> = [
  { id: 'system', label: 'System', detail: 'Link, identity, readiness', icon: ShieldCheck },
  { id: 'operate', label: 'Operate', detail: 'Deployment and runtime', icon: Activity },
  { id: 'integrate', label: 'Integrate', detail: 'Service, tuning, tools', icon: Cpu },
  { id: 'update', label: 'Update', detail: 'Inspect and flash firmware', icon: Upload },
  { id: 'history', label: 'History', detail: 'Events, comms, commands', icon: History },
]

function CommandHistoryPanel({
  commands,
  onClear,
}: {
  commands: CommandHistoryEntry[]
  onClear: () => void
}) {
  return (
    <section className="panel-section">
      <div className="section-head">
        <div>
          <h3>Command history</h3>
          <p>Recent controller requests and their terminal state.</p>
        </div>
        <button type="button" className="action-button is-inline" onClick={onClear}>
          Clear history
        </button>
      </div>

      {commands.length > 0 ? (
        <div className="activity-list">
          {commands.map((command) => (
            <article key={command.id} className="activity-row">
              <div>
                <strong>{command.cmd}</strong>
                <small>{command.issuedAtIso}</small>
              </div>
              <p>{command.note}</p>
              <div className="status-badges">
                <span className={command.status === 'ack' ? 'status-badge is-on' : command.status === 'error' ? 'status-badge is-critical' : 'status-badge'}>
                  {command.status}
                </span>
                <span className="status-badge">{command.risk}</span>
              </div>
            </article>
          ))}
        </div>
      ) : (
        <p className="panel-note">No commands have been issued in this session.</p>
      )}
    </section>
  )
}

function App() {
  const [page, setPage] = useState<PageId>('system')
  const [integrateTab, setIntegrateTab] = useState<IntegrateTab>('bringup')
  const [historyTab, setHistoryTab] = useState<HistoryTab>('events')
  const [eventQuery, setEventQuery] = useState('')
  const [eventSeverity, setEventSeverity] = useState<Severity | 'all'>('all')
  const [eventModuleFilter, setEventModuleFilter] = useState('all')
  const [packageDescriptor, setPackageDescriptor] = useState<FirmwarePackageDescriptor | null>(null)

  const {
    transportKind,
    wifiUrl,
    setWifiUrl,
    setTransportKind,
    transportStatus,
    transportDetail,
    transportRecovering,
    snapshot,
    telemetryStore,
    events,
    commands,
    firmwareProgress,
    sessionAutosave,
    supportsFirmwareTransfer,
    connect,
    disconnect,
    issueCommand,
    issueCommandAwaitAck,
    beginFirmwareTransfer,
    exportSession,
    clearSessionHistory,
    configureSessionAutosave,
    disableSessionAutosave,
  } = useDeviceSession()

  useGlobalHoverHelp()
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)

  useEffect(() => {
    document.documentElement.dataset.theme = 'light'
  }, [])

  const headerFacts = useMemo(
    () => [
      {
        label: 'State',
        value: formatEnumLabel(liveSnapshot.session.state),
        detail: formatEnumLabel(liveSnapshot.session.powerTier),
      },
      {
        label: 'Firmware',
        value: liveSnapshot.identity.firmwareVersion,
        detail: liveSnapshot.identity.protocolVersion,
      },
      {
        label: 'Uptime',
        value: formatRelativeUptime(liveSnapshot.session.uptimeSeconds),
        detail: formatEnumLabel(liveSnapshot.session.bootReason),
      },
      {
        label: 'Power source',
        value: `${formatNumber(liveSnapshot.pd.sourceVoltageV, 1)} V / ${formatNumber(liveSnapshot.pd.sourceCurrentA, 2)} A`,
        detail: liveSnapshot.pd.sourceIsHostOnly ? 'USB host only' : 'External PD source',
      },
    ],
    [
      liveSnapshot.identity.firmwareVersion,
      liveSnapshot.identity.protocolVersion,
      liveSnapshot.pd.sourceCurrentA,
      liveSnapshot.pd.sourceIsHostOnly,
      liveSnapshot.pd.sourceVoltageV,
      liveSnapshot.session.bootReason,
      liveSnapshot.session.powerTier,
      liveSnapshot.session.state,
      liveSnapshot.session.uptimeSeconds,
    ],
  )

  async function handlePickFirmware(file: File) {
    setPackageDescriptor(await parseFirmwareFile(file))
  }

  async function handlePrepareDevice() {
    if (snapshot.session.state !== 'SERVICE_MODE') {
      await issueCommand(
        'enter_service_mode',
        'service',
        'Preparing controller for maintenance or firmware work.',
      )
      return
    }

    await issueCommand('get_status', 'read', 'Refreshing state after service-mode request.')
  }

  async function handleBeginTransfer() {
    if (packageDescriptor === null) {
      return
    }

    await beginFirmwareTransfer(packageDescriptor)
  }

  async function handleToggleServiceMode() {
    if (transportStatus !== 'connected' || snapshot.deployment.active) {
      return
    }

    await issueCommand(
      snapshot.bringup.serviceModeActive ? 'exit_service_mode' : 'enter_service_mode',
      'service',
      snapshot.bringup.serviceModeActive
        ? 'Leaving guarded write session.'
        : 'Entering guarded write session with outputs held safe.',
    )
  }

  async function handleSetInterlocksDisabled(enabled: boolean) {
    if (transportStatus !== 'connected' || snapshot.deployment.active) {
      return
    }

    await issueCommandAwaitAck(
      'set_interlocks_disabled',
      'service',
      enabled
        ? 'Disabling all beam interlocks under explicit bench override.'
        : 'Restoring normal controller interlock supervision.',
      { enabled },
      { timeoutMs: 3200 },
    )
  }

  return (
    <div className="console-shell">
      <aside className="console-sidebar">
        <div className="console-brand">
          <div className="brand-mark">
            <ShieldCheck size={18} />
          </div>
          <div>
            <strong>BSL-HTLS Gen2</strong>
            <p>Powered-ready console rewrite</p>
          </div>
        </div>

        <nav className="console-nav">
          {pages.map((entry) => {
            const Icon = entry.icon
            return (
              <button
                key={entry.id}
                type="button"
                className={page === entry.id ? 'console-nav__item is-active' : 'console-nav__item'}
                onClick={() => startTransition(() => setPage(entry.id))}
              >
                <Icon size={16} />
                <div>
                  <strong>{entry.label}</strong>
                  <span>{entry.detail}</span>
                </div>
              </button>
            )
          })}
        </nav>

        <div className="console-sidebar__status">
          <span className={transportStatus === 'connected' ? 'status-badge is-on' : 'status-badge'}>
            <Cable size={14} />
            {formatEnumLabel(transportStatus)}
          </span>
          <span className={liveSnapshot.deployment.ready ? 'status-badge is-on' : liveSnapshot.deployment.failed ? 'status-badge is-critical' : 'status-badge'}>
            <Clock3 size={14} />
            {liveSnapshot.deployment.ready ? 'Ready' : liveSnapshot.deployment.failed ? 'Failed' : 'Not ready'}
          </span>
        </div>
      </aside>

      <main className="console-main">
        <header className="console-header">
          <div className="console-header__title">
            <h1>{pages.find((entry) => entry.id === page)?.label}</h1>
            <p>{pages.find((entry) => entry.id === page)?.detail}</p>
          </div>
          <div className="console-header__facts">
            {headerFacts.map((fact) => (
              <div key={fact.label} className="console-fact">
                <span>{fact.label}</span>
                <strong>{fact.value}</strong>
                <small>{fact.detail}</small>
              </div>
            ))}
          </div>
        </header>

        {/*
         * USB-Debug Mock app-wide banner. Rendered whenever the firmware (or
         * mock transport) reports `bench.usbDebugMock.active`. Required by
         * AGENT.md "USB-Only Debug Power" — every TEC/LD readout below this
         * banner is synthesized while the mock is active.
         */}
        {liveSnapshot.bench.usbDebugMock.active && (
          <div className="usb-debug-mock-banner" role="status" aria-live="polite">
            <strong>USB DEBUG MOCK ACTIVE</strong>
            <span>
              Telemetry below is synthesized — TEC and LD rails are NOT
              actually powered. Only use for online testing on a USB-only
              session. Real PD power will auto-disable the mock and latch a
              SYSTEM_MAJOR fault.
            </span>
          </div>
        )}
        {liveSnapshot.bench.usbDebugMock.pdConflictLatched && (
          <div className="usb-debug-mock-banner is-critical" role="alert">
            <strong>USB DEBUG MOCK PD-CONFLICT LATCHED</strong>
            <span>
              The mock auto-disabled because real PD power was detected.
              Clear faults from the System or Integrate workspace before
              re-enabling.
            </span>
          </div>
        )}

        <StatusRail snapshot={liveSnapshot} telemetryStore={telemetryStore} />

        <div className="console-page">
          {page === 'system' ? (
            <>
              <ConnectionWorkbench
                snapshot={liveSnapshot}
                transportKind={transportKind}
                transportStatus={transportStatus}
                transportDetail={transportDetail}
                wifiUrl={wifiUrl}
                onSetWifiUrl={setWifiUrl}
                onSetTransportKind={setTransportKind}
                onConnect={connect}
                onDisconnect={disconnect}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
              <SafetyMatrix snapshot={liveSnapshot} />
              <ModuleReadinessPanel snapshot={liveSnapshot} telemetryStore={telemetryStore} />
              <EventTimeline
                events={events}
                commands={commands}
                query={eventQuery}
                severity={eventSeverity}
                moduleFilter={eventModuleFilter}
                onQueryChange={setEventQuery}
                onSeverityChange={setEventSeverity}
                onModuleFilterChange={setEventModuleFilter}
                compact
              />
            </>
          ) : null}

          {page === 'operate' ? (
            <OperateConsole
              snapshot={liveSnapshot}
              telemetryStore={telemetryStore}
              events={events}
              transportStatus={transportStatus}
              onIssueCommandAwaitAck={issueCommandAwaitAck}
            />
          ) : null}

          {page === 'integrate' ? (
            <>
              <section className="panel-section">
                <div className="section-head">
                  <div>
                    <h3>Integrate workspace</h3>
                    <p>Persistent safety policy, service-mode tuning, module configuration, and direct controller tools.</p>
                  </div>
                  <div className="segmented">
                    <button
                      type="button"
                      className={integrateTab === 'bringup' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setIntegrateTab('bringup')}
                    >
                      Bring-up
                    </button>
                    <button
                      type="button"
                      className={integrateTab === 'tools' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setIntegrateTab('tools')}
                    >
                      Commands
                    </button>
                  </div>
                </div>
              </section>

              {integrateTab === 'bringup' ? (
                <BringupWorkbench
                  snapshot={liveSnapshot}
                  telemetryStore={telemetryStore}
                  transportStatus={transportStatus}
                  transportRecovering={transportRecovering}
                  deploymentLocked={liveSnapshot.deployment.active}
                  onIssueCommandAwaitAck={issueCommandAwaitAck}
                />
              ) : (
                <CommandDeck
                  snapshot={liveSnapshot}
                  transportKind={transportKind}
                  transportStatus={liveSnapshot.deployment.active ? 'disconnected' : transportStatus}
                  deploymentLocked={liveSnapshot.deployment.active}
                  onIssueCommandAwaitAck={issueCommandAwaitAck}
                />
              )}
            </>
          ) : null}

          {page === 'update' ? (
            <FirmwareWorkbench
              snapshot={liveSnapshot}
              packageDescriptor={packageDescriptor}
              firmwareProgress={firmwareProgress}
              connected={transportStatus === 'connected'}
              supportsFirmwareTransfer={supportsFirmwareTransfer}
              transportKind={transportKind}
              onPickFile={handlePickFirmware}
              onBeginTransfer={handleBeginTransfer}
              onPrepareDevice={handlePrepareDevice}
            />
          ) : null}

          {page === 'history' ? (
            <>
              <section className="panel-section">
                <div className="section-head">
                  <div>
                    <h3>History workspace</h3>
                    <p>Switch between controller events, raw comms traffic, and command results.</p>
                  </div>
                  <div className="segmented">
                    <button
                      type="button"
                      className={historyTab === 'events' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setHistoryTab('events')}
                    >
                      Events
                    </button>
                    <button
                      type="button"
                      className={historyTab === 'comms' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setHistoryTab('comms')}
                    >
                      Comms
                    </button>
                    <button
                      type="button"
                      className={historyTab === 'commands' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setHistoryTab('commands')}
                    >
                      Commands
                    </button>
                  </div>
                </div>
              </section>

              {historyTab === 'events' ? (
                <EventTimeline
                  events={events}
                  commands={commands}
                  query={eventQuery}
                  severity={eventSeverity}
                  moduleFilter={eventModuleFilter}
                  onQueryChange={setEventQuery}
                  onSeverityChange={setEventSeverity}
                  onModuleFilterChange={setEventModuleFilter}
                  onClearSessionHistory={clearSessionHistory}
                  mode="system"
                />
              ) : null}

              {historyTab === 'comms' ? (
                <BusTrafficViewer events={events} commands={commands} />
              ) : null}

              {historyTab === 'commands' ? (
                <CommandHistoryPanel commands={commands} onClear={clearSessionHistory} />
              ) : null}
            </>
          ) : null}
        </div>
      </main>

      <InspectorRail
        snapshot={liveSnapshot}
        commands={commands}
        transportKind={transportKind}
        transportStatus={transportStatus}
        transportDetail={transportDetail}
        wifiUrl={wifiUrl}
        firmwareProgress={firmwareProgress}
        sessionAutosave={sessionAutosave}
        deviceLinkLost={transportStatus !== 'connected'}
        onExportSession={exportSession}
        onClearSessionHistory={clearSessionHistory}
        onConfigureSessionAutosave={configureSessionAutosave}
        onDisableSessionAutosave={disableSessionAutosave}
        onSetWifiUrl={setWifiUrl}
        onSetTransportKind={setTransportKind}
        onConnect={connect}
        onDisconnect={disconnect}
        onToggleServiceMode={handleToggleServiceMode}
        onSetInterlocksDisabled={handleSetInterlocksDisabled}
      />
    </div>
  )
}

export default App
