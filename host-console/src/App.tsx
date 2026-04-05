import { startTransition, useEffect, useMemo, useState } from 'react'
import { AnimatePresence, motion } from 'framer-motion'
import {
  Activity,
  Box,
  Cable,
  Command,
  Cpu,
  FlaskConical,
  Shield,
  Zap,
} from 'lucide-react'

import { BringupWorkbench } from './components/BringupWorkbench'
import { BusTrafficViewer } from './components/BusTrafficViewer'
import { CommandDeck } from './components/CommandDeck'
import { ConnectionStatusTag } from './components/ConnectionStatusTag'
import { ConnectionWorkbench } from './components/ConnectionWorkbench'
import { ControlWorkbench } from './components/ControlWorkbench'
import { DeploymentWorkbench } from './components/DeploymentWorkbench'
import { EventTimeline } from './components/EventTimeline'
import { FirmwareWorkbench } from './components/FirmwareWorkbench'
import { InspectorRail } from './components/InspectorRail'
import { ModuleReadinessPanel } from './components/ModuleReadinessPanel'
import { ProgressMeter } from './components/ProgressMeter'
import { SafetyMatrix } from './components/SafetyMatrix'
import { StatusRail } from './components/StatusRail'
import { deriveBenchEstimate } from './lib/bench-model'
import { parseFirmwareFile } from './lib/firmware'
import { formatNumber, formatRelativeUptime } from './lib/format'
import { useDeviceSession } from './hooks/use-device-session'
import { useGlobalHoverHelp } from './hooks/use-global-hover-help'
import { useLiveSnapshot } from './hooks/use-live-snapshot'
import {
  buildSafetyChecks,
  formatEnumLabel,
  formatTofValidityLabel,
  formatTofWindowSummary,
  getTofDisplayDistanceM,
  summarizeSafetyChecks,
  toneFromSystemState,
} from './lib/presentation'
import type {
  FirmwarePackageDescriptor,
  Severity,
  ThemeMode,
} from './types'

const HOST_THEME_STORAGE_KEY = 'bsl-host-theme'

type AppView = 'overview' | 'connection' | 'control' | 'deployment' | 'bringup' | 'events' | 'firmware' | 'service'
type EventLogView = 'system' | 'comms'

const navItems: Array<{
  id: AppView
  label: string
  icon: typeof Shield
  detail: string
}> = [
  {
    id: 'overview',
    label: 'Overview',
    icon: Shield,
    detail: 'State, rails, interlocks',
  },
  {
    id: 'connection',
    label: 'Connection',
    icon: Cable,
    detail: 'Transport and Wi-Fi',
  },
  {
    id: 'deployment',
    label: 'Deployment',
    icon: Zap,
    detail: 'Checklist and target',
  },
  {
    id: 'control',
    label: 'Control',
    icon: FlaskConical,
    detail: 'Laser, TEC, PWM',
  },
  {
    id: 'bringup',
    label: 'Bring-up',
    icon: Cpu,
    detail: 'Modules and probes',
  },
  {
    id: 'events',
    label: 'Events',
    icon: Activity,
    detail: 'Logs and faults',
  },
  {
    id: 'firmware',
    label: 'Firmware',
    icon: Box,
    detail: 'Load and update',
  },
  {
    id: 'service',
    label: 'Tools',
    icon: Command,
    detail: 'Bus lab and utility',
  },
]

function readStoredTheme(): ThemeMode {
  if (typeof window === 'undefined') {
    return 'dark'
  }

  const stored = window.localStorage.getItem(HOST_THEME_STORAGE_KEY)
  return stored === 'light' ? 'light' : 'dark'
}

function App() {
  const [activeView, setActiveView] = useState<AppView>('overview')
  const [eventQuery, setEventQuery] = useState('')
  const [eventSeverity, setEventSeverity] = useState<Severity | 'all'>('all')
  const [eventModuleFilter, setEventModuleFilter] = useState('all')
  const [eventLogView, setEventLogView] = useState<EventLogView>('system')
  const [packageDescriptor, setPackageDescriptor] = useState<FirmwarePackageDescriptor | null>(null)
  const [themeMode, setThemeMode] = useState<ThemeMode>(() => readStoredTheme())

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

  const stateTone = toneFromSystemState(liveSnapshot.session.state)
  const benchEstimate = useMemo(() => deriveBenchEstimate(liveSnapshot), [liveSnapshot])
  const safetyChecks = useMemo(() => buildSafetyChecks(liveSnapshot), [liveSnapshot])
  const safetySummary = useMemo(() => summarizeSafetyChecks(safetyChecks), [safetyChecks])
  const tofDisplayDistance = useMemo(() => getTofDisplayDistanceM(liveSnapshot), [liveSnapshot])
  const sessionFacts = useMemo(
    () => [
      {
        label: 'Firmware build',
        value: liveSnapshot.identity.firmwareVersion,
        detail: `Protocol ${liveSnapshot.identity.protocolVersion}`,
      },
      {
        label: 'Hardware rev',
        value: liveSnapshot.identity.hardwareRevision,
        detail: `Serial ${liveSnapshot.identity.serialNumber}`,
      },
      {
        label: 'Boot reason',
        value: formatEnumLabel(liveSnapshot.session.bootReason),
        detail: `Uptime ${formatRelativeUptime(liveSnapshot.session.uptimeSeconds)}`,
      },
      {
        label: 'Power source',
        value: liveSnapshot.pd.sourceIsHostOnly ? 'USB host only' : formatEnumLabel(liveSnapshot.session.powerTier),
        detail: `${formatNumber(liveSnapshot.pd.sourceVoltageV, 1)} V · ${formatNumber(liveSnapshot.pd.sourceCurrentA, 2)} A`,
      },
    ],
    [
      liveSnapshot.identity.firmwareVersion,
      liveSnapshot.identity.hardwareRevision,
      liveSnapshot.identity.protocolVersion,
      liveSnapshot.identity.serialNumber,
      liveSnapshot.pd.sourceCurrentA,
      liveSnapshot.pd.sourceIsHostOnly,
      liveSnapshot.pd.sourceVoltageV,
      liveSnapshot.session.bootReason,
      liveSnapshot.session.powerTier,
      liveSnapshot.session.uptimeSeconds,
    ],
  )
  const workflowSteps = useMemo(
    () => [
      {
        label: 'Link live',
        pass: transportStatus === 'connected',
        detail:
          transportStatus === 'connected'
            ? transportKind === 'mock'
              ? 'Mock rig active'
              : transportKind === 'wifi'
                ? 'Wireless controller link active'
                : 'Web Serial attached'
            : 'Start or connect the selected transport',
      },
      {
        label: 'Outputs safe',
        pass: !liveSnapshot.laser.nirEnabled && !liveSnapshot.laser.alignmentEnabled,
        detail:
          !liveSnapshot.laser.nirEnabled && !liveSnapshot.laser.alignmentEnabled
            ? 'No optical output requested'
            : liveSnapshot.laser.nirEnabled
              ? 'NIR laser request is active'
              : 'Green alignment laser is active',
      },
      {
        label: 'Fault state',
        pass: !liveSnapshot.fault.latched,
        detail: liveSnapshot.fault.latched ? liveSnapshot.fault.activeCode : 'No latched fault',
      },
      {
        label: 'Gate health',
        pass: safetySummary.percent >= 75,
        detail: `${safetySummary.passCount}/${safetySummary.total} interlocks passing`,
      },
    ],
    [liveSnapshot.fault.activeCode, liveSnapshot.fault.latched, liveSnapshot.laser.alignmentEnabled, liveSnapshot.laser.nirEnabled, safetySummary, transportKind, transportStatus],
  )

  async function handlePickFirmware(file: File) {
    const parsed = await parseFirmwareFile(file)
    setPackageDescriptor(parsed)
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

  const connectionHealthy = transportStatus === 'connected'
  const deviceLinkLost = !connectionHealthy
  const contentDimmed = deviceLinkLost && activeView !== 'connection'

  async function handleToggleServiceMode() {
    if (transportStatus !== 'connected') {
      return
    }

    if (snapshot.deployment.active) {
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
    if (transportStatus !== 'connected') {
      return
    }

    if (snapshot.deployment.active) {
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

  useEffect(() => {
    document.documentElement.dataset.theme = themeMode
    window.localStorage.setItem(HOST_THEME_STORAGE_KEY, themeMode)
  }, [themeMode])

  return (
    <div className={deviceLinkLost ? 'app-shell is-offline' : 'app-shell'}>
      <aside className={deviceLinkLost ? 'sidebar offline-dim' : 'sidebar'}>
        <div className="sidebar__brand">
          <div className="brand-mark">
            <Shield size={18} />
          </div>
          <div>
            <p className="eyebrow">BioSensors Lab @ UIUC</p>
            <h1>BSL-HTLS Gen2</h1>
          </div>
        </div>

        <p className="sidebar__lede">
          770-790 nm, 5 W tunable handheld laser for F-IGS.
        </p>

        <nav className="nav">
          {navItems.map((item) => {
            const Icon = item.icon
            return (
              <button
                key={item.id}
                type="button"
                className={activeView === item.id ? 'nav-link is-active' : 'nav-link'}
                onClick={() => startTransition(() => setActiveView(item.id))}
              >
                <Icon size={16} />
                <span>{item.label}</span>
                <small>{item.detail}</small>
              </button>
            )
          })}
        </nav>

        <div className="sidebar__footer">
          <div className="segmented">
            <button
              type="button"
              className={themeMode === 'dark' ? 'segmented__button is-active' : 'segmented__button'}
              onClick={() => setThemeMode('dark')}
            >
              Dark
            </button>
            <button
              type="button"
              className={themeMode === 'light' ? 'segmented__button is-active' : 'segmented__button'}
              onClick={() => setThemeMode('light')}
            >
              Light
            </button>
          </div>
          <div className={`state-pill is-${stateTone}`}>
            <Zap size={14} />
            <span>{liveSnapshot.session.state.replaceAll('_', ' ')}</span>
          </div>
          <p>Launcher: <code>./start-host-console.command</code></p>
        </div>
      </aside>

      <main className="workspace">
        {deviceLinkLost ? (
          <section className="offline-banner">
            <div>
              <p className="eyebrow">Controller link lost</p>
              <strong>
                {transportStatus === 'connecting'
                  ? 'Waiting for the ESP32 protocol handshake.'
                  : 'Live telemetry and bench controls are muted until the ESP32 reconnects.'}
              </strong>
            </div>
            <span className={`transport-chip is-${transportStatus}`}>
              <Cable size={14} />
              <span>{formatEnumLabel(transportStatus)}</span>
            </span>
          </section>
        ) : null}

        <header className={`hero-panel hero-panel--compact tone-${stateTone}`}>
          <motion.div
            className={deviceLinkLost ? 'hero-panel__main offline-dim' : 'hero-panel__main'}
            initial={{ opacity: 0, y: 16 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.4 }}
          >
            <div className="hero-summary" data-tone={safetySummary.tone}>
              <div className="hero-summary__head">
                <div>
                  <p className="eyebrow">System readiness</p>
                  <strong>{safetySummary.label}</strong>
                  <p className="hero-summary__copy">
                    {formatEnumLabel(liveSnapshot.session.state)} · {formatEnumLabel(liveSnapshot.session.powerTier)} · {liveSnapshot.fault.latched ? liveSnapshot.fault.activeCode : 'No latched fault'}
                  </p>
                </div>
                <div className={`state-pill is-${safetySummary.tone}`}>
                  <span>{safetySummary.percent}% ready</span>
                </div>
              </div>
              <ProgressMeter value={safetySummary.percent} tone={safetySummary.tone} />
              <div className="hero-workflow">
                {workflowSteps.map((step) => (
                  <div
                    key={step.label}
                    className={step.pass ? 'hero-workflow__step is-pass' : 'hero-workflow__step is-fail'}
                  >
                    <span>{step.pass ? 'OK' : 'HOLD'}</span>
                    <div>
                      <strong>{step.label}</strong>
                      <small>{step.detail}</small>
                    </div>
                  </div>
                ))}
              </div>
            </div>

            <div className="hero-facts">
              <div className="cutout-head">
                <div>
                  <p className="eyebrow">Session facts</p>
                  <strong>Identity and power</strong>
                </div>
              </div>
              <div className="hero-facts__grid">
                {sessionFacts.map((fact) => (
                  <div key={fact.label} className="hero-facts__item">
                    <span>{fact.label}</span>
                    <strong>{fact.value}</strong>
                    <small>{fact.detail}</small>
                  </div>
                ))}
              </div>
            </div>
          </motion.div>
        </header>

        <div className={deviceLinkLost ? 'offline-dim' : undefined}>
          <StatusRail snapshot={liveSnapshot} telemetryStore={telemetryStore} />
        </div>

        <AnimatePresence mode="wait">
          <motion.div
            key={activeView}
            initial={{ opacity: 0, y: 16 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -12 }}
            transition={{ duration: 0.24 }}
            className={contentDimmed ? 'workspace__content offline-dim' : 'workspace__content'}
          >
            {activeView === 'overview' ? (
              <>
                <SafetyMatrix snapshot={liveSnapshot} />

                <ModuleReadinessPanel snapshot={liveSnapshot} telemetryStore={telemetryStore} />

                <section className="telemetry-grid">
                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                          <p className="eyebrow">Laser path</p>
                          <h2>Beam status</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>Green alignment laser</dt>
                    <dd>{liveSnapshot.laser.alignmentEnabled ? 'On' : 'Safe off'}</dd>
                      </div>
                      <div>
                        <dt>NIR laser</dt>
                    <dd>{liveSnapshot.laser.nirEnabled ? 'On' : 'Safe off'}</dd>
                      </div>
                      <div>
                        <dt>Driver standby</dt>
                    <dd>{liveSnapshot.laser.driverStandby ? 'asserted' : 'released'}</dd>
                      </div>
                      <div>
                        <dt>Measured current</dt>
                    <dd>{formatNumber(liveSnapshot.laser.measuredCurrentA, 3)} A</dd>
                      </div>
                      <div>
                        <dt>Commanded current</dt>
                    <dd>{formatNumber(liveSnapshot.laser.commandedCurrentA, 3)} A</dd>
                      </div>
                      <div>
                        <dt>Optical estimate</dt>
                        <dd>{formatNumber(benchEstimate.averageOpticalPowerW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Power and rails</p>
                        <h2>Power</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>PD contract</dt>
                        <dd>{liveSnapshot.pd.contractValid ? 'Negotiated' : 'Unavailable'}</dd>
                      </div>
                      <div>
                        <dt>Source</dt>
                        <dd>
                          {formatNumber(liveSnapshot.pd.sourceVoltageV, 1)} V / {formatNumber(liveSnapshot.pd.sourceCurrentA, 1)} A
                        </dd>
                      </div>
                      <div>
                        <dt>TEC rail</dt>
                        <dd>{liveSnapshot.rails.tec.enabled ? 'enabled' : 'off'} · {liveSnapshot.rails.tec.pgood ? 'pgood' : 'bad'}</dd>
                      </div>
                      <div>
                        <dt>LD rail</dt>
                        <dd>{liveSnapshot.rails.ld.enabled ? 'enabled' : 'off'} · {liveSnapshot.rails.ld.pgood ? 'pgood' : 'bad'}</dd>
                      </div>
                      <div>
                        <dt>Total estimated draw</dt>
                        <dd>{formatNumber(benchEstimate.totalEstimatedInputPowerW, 2)} W</dd>
                      </div>
                      <div>
                        <dt>PD headroom</dt>
                        <dd>{formatNumber(benchEstimate.pdHeadroomW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Thermal loop</p>
                        <h2>TEC and lambda</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>Target temp</dt>
                        <dd>{formatNumber(liveSnapshot.tec.targetTempC, 1)} °C</dd>
                      </div>
                      <div>
                        <dt>Actual temp</dt>
                        <dd>{formatNumber(liveSnapshot.tec.tempC, 2)} °C</dd>
                      </div>
                      <div>
                        <dt>Target wavelength</dt>
                        <dd>{formatNumber(liveSnapshot.tec.targetLambdaNm, 1)} nm</dd>
                      </div>
                      <div>
                        <dt>TEC voltage</dt>
                        <dd>{formatNumber(liveSnapshot.tec.voltageV, 2)} V</dd>
                      </div>
                      <div>
                        <dt>TEC current</dt>
                        <dd>{formatNumber(liveSnapshot.tec.currentA, 2)} A</dd>
                      </div>
                      <div>
                        <dt>Cooling estimate</dt>
                        <dd>{formatNumber(benchEstimate.tecCoolingPowerW, 2)} W</dd>
                      </div>
                    </dl>
                  </article>

                  <article className="panel-section">
                    <div className="panel-section__head">
                      <div>
                        <p className="eyebrow">Sensors</p>
                        <h2>IMU and range</h2>
                      </div>
                    </div>
                    <dl className="telemetry-list">
                      <div>
                        <dt>IMU freshness</dt>
                        <dd>{liveSnapshot.imu.valid && liveSnapshot.imu.fresh ? 'Fresh and valid' : 'Stale or invalid'}</dd>
                      </div>
                      <div>
                        <dt>Beam pitch</dt>
                        <dd>{formatNumber(liveSnapshot.imu.beamPitchDeg, 1)}°</dd>
                      </div>
                      <div>
                        <dt>Pitch limit</dt>
                        <dd>{formatNumber(liveSnapshot.imu.beamPitchLimitDeg, 1)}°</dd>
                      </div>
                      <div>
                        <dt>ToF validity</dt>
                        <dd>{formatTofValidityLabel(liveSnapshot)}</dd>
                      </div>
                      <div>
                        <dt>Distance</dt>
                        <dd>
                          {tofDisplayDistance !== null
                            ? `${formatNumber(tofDisplayDistance, 2)} m`
                            : 'No live sample'}
                        </dd>
                      </div>
                      <div>
                        <dt>Allowed window</dt>
                        <dd>{formatTofWindowSummary(liveSnapshot)}</dd>
                      </div>
                    </dl>
                  </article>
                </section>

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

            {activeView === 'connection' ? (
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
            ) : null}

            {activeView === 'control' ? (
              <ControlWorkbench
                snapshot={liveSnapshot}
                telemetryStore={telemetryStore}
                transportKind={transportKind}
                transportStatus={transportStatus}
                transportRecovering={transportRecovering}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}

            {activeView === 'deployment' ? (
              <DeploymentWorkbench
                snapshot={liveSnapshot}
                telemetryStore={telemetryStore}
                events={events}
                transportStatus={transportStatus}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}

            {activeView === 'bringup' ? (
              <BringupWorkbench
                snapshot={liveSnapshot}
                telemetryStore={telemetryStore}
                transportStatus={liveSnapshot.deployment.active ? 'disconnected' : transportStatus}
                transportRecovering={transportRecovering}
                deploymentLocked={liveSnapshot.deployment.active}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}

            {activeView === 'events' ? (
              <>
                <section className="panel-section log-workspace-switcher">
                  <div className="panel-section__head">
                    <div>
                      <p className="eyebrow">Log workspace</p>
                      <h2>Separate system and comms views</h2>
                    </div>
                    <p className="panel-note">
                      Switch between controller/system history and the SPI or I2C traffic stream instead of stacking both on one page.
                    </p>
                  </div>
                  <div className="segmented log-workspace-switcher__tabs">
                    <button
                      type="button"
                      className={eventLogView === 'system' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setEventLogView('system')}
                    >
                      System log
                    </button>
                    <button
                      type="button"
                      className={eventLogView === 'comms' ? 'segmented__button is-active' : 'segmented__button'}
                      onClick={() => setEventLogView('comms')}
                    >
                      Comms log
                    </button>
                  </div>
                </section>

                {eventLogView === 'system' ? (
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
              ) : (
                  <BusTrafficViewer events={events} commands={commands} />
                )}
              </>
            ) : null}

            {activeView === 'firmware' ? (
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

            {activeView === 'service' ? (
              <CommandDeck
                snapshot={liveSnapshot}
                transportKind={transportKind}
                transportStatus={liveSnapshot.deployment.active ? 'disconnected' : transportStatus}
                deploymentLocked={liveSnapshot.deployment.active}
                onIssueCommandAwaitAck={issueCommandAwaitAck}
              />
            ) : null}
          </motion.div>
        </AnimatePresence>
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
        deviceLinkLost={deviceLinkLost}
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

      <ConnectionStatusTag
        snapshot={liveSnapshot}
        transportKind={transportKind}
        transportStatus={transportStatus}
        transportDetail={transportDetail}
      />
    </div>
  )
}

export default App
