import { useMemo, useState } from 'react'
import {
  AlertTriangle,
  CheckCircle2,
  Clock3,
  LoaderCircle,
  Thermometer,
  Zap,
} from 'lucide-react'

import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import { formatNumber } from '../lib/format'
import { formatEnumLabel } from '../lib/presentation'
import {
  clampTecTempC,
  clampTecWavelengthNm,
  estimateTempFromWavelengthNm,
  estimateWavelengthFromTempC,
} from '../lib/tec-calibration'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import type {
  DeploymentSecondaryEffect,
  DeviceSnapshot,
  SessionEvent,
  TransportStatus,
} from '../types'

type OperateConsoleProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
  events: SessionEvent[]
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

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function clampCurrentA(value: number): number {
  return Math.max(0, Math.min(5.2, value))
}

function stepTone(status: DeviceSnapshot['deployment']['steps'][number]['status']): string {
  switch (status) {
    case 'passed':
      return 'is-on'
    case 'failed':
      return 'is-critical'
    case 'in_progress':
      return 'is-warn'
    default:
      return ''
  }
}

function effectTone(effect: DeploymentSecondaryEffect): string {
  return effect.code === 'unexpected_state' ? 'status-badge is-warn' : 'status-badge'
}

function ledOwnerLabel(owner: DeviceSnapshot['bench']['appliedLedOwner']): string {
  switch (owner) {
    case 'integrate_service':
      return 'Integrate service'
    case 'operate_runtime':
      return 'Operate'
    case 'deployment':
      return 'Deployment hold'
    default:
      return 'None'
  }
}

export function OperateConsole({
  snapshot,
  telemetryStore,
  events,
  transportStatus,
  onIssueCommandAwaitAck,
}: OperateConsoleProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const connected = transportStatus === 'connected'
  const deployment = liveSnapshot.deployment
  const readyTruth = deployment.readyTruth
  const runtimeMode = liveSnapshot.bench.runtimeMode

  const liveRuntimeTempC = formatNumber(liveSnapshot.tec.targetTempC, 1)
  const liveRuntimeLambdaNm = formatNumber(liveSnapshot.tec.targetLambdaNm, 1)
  const liveRequestedCurrent = formatNumber(liveSnapshot.bench.requestedCurrentA, 2)
  const liveModulationFrequency = String(liveSnapshot.bench.modulationFrequencyHz)
  const liveModulationDuty = String(liveSnapshot.bench.modulationDutyCyclePct)
  const liveLedBrightness = String(
    liveSnapshot.bench.requestedLedDutyCyclePct || liveSnapshot.bench.illuminationDutyCyclePct,
  )

  const [targetDraft, setTargetDraft] = useState(() => ({
    dirty: false,
    tempC: liveRuntimeTempC,
    lambdaNm: liveRuntimeLambdaNm,
    source: liveSnapshot.bench.targetMode === 'temp' ? ('temp' as const) : ('lambda' as const),
  }))
  const [currentDraft, setCurrentDraft] = useState(() => ({
    dirty: false,
    value: liveRequestedCurrent,
  }))
  const [modulationDraft, setModulationDraft] = useState(() => ({
    dirty: false,
    enabled: liveSnapshot.bench.modulationEnabled,
    frequencyHz: liveModulationFrequency,
    dutyPct: liveModulationDuty,
  }))
  const [ledDraft, setLedDraft] = useState(() => ({
    dirty: false,
    value: liveLedBrightness,
  }))

  const readyIdle = deployment.ready && deployment.readyIdle && deployment.readyQualified
  const hostOutputUnlocked = connected && readyIdle && runtimeMode === 'modulated_host'
  const auxControlsUnlocked = connected && deployment.active && !deployment.running

  const runtimeTempC = targetDraft.dirty ? targetDraft.tempC : liveRuntimeTempC
  const runtimeLambdaNm = targetDraft.dirty ? targetDraft.lambdaNm : liveRuntimeLambdaNm
  const targetCommitSource = targetDraft.dirty
    ? targetDraft.source
    : liveSnapshot.bench.targetMode === 'temp'
      ? 'temp'
      : 'lambda'
  const laserCurrentA = currentDraft.dirty ? currentDraft.value : liveRequestedCurrent
  const modulationEnabled = modulationDraft.dirty
    ? modulationDraft.enabled
    : liveSnapshot.bench.modulationEnabled
  const modulationFrequencyHz = modulationDraft.dirty
    ? modulationDraft.frequencyHz
    : liveModulationFrequency
  const modulationDutyPct = modulationDraft.dirty
    ? modulationDraft.dutyPct
    : liveModulationDuty
  const ledBrightnessPct = ledDraft.dirty ? ledDraft.value : liveLedBrightness

  const requestedCurrentA = clampCurrentA(
    parseNumber(laserCurrentA, liveSnapshot.bench.requestedCurrentA),
  )
  const requestedLedBrightnessPct = Math.max(
    0,
    Math.min(
      100,
      parseNumber(
        ledBrightnessPct,
        liveSnapshot.bench.requestedLedDutyCyclePct || liveSnapshot.bench.illuminationDutyCyclePct,
      ),
    ),
  )

  const pdSnapshotLabel =
    liveSnapshot.pd.lastUpdatedMs === 0
      ? 'No passive PD observation yet'
      : liveSnapshot.pd.snapshotFresh
        ? `Passive status fresh from ${liveSnapshot.pd.source.replaceAll('_', ' ')}`
        : `Passive status cached from ${liveSnapshot.pd.source.replaceAll('_', ' ')}`

  const activeStep = deployment.steps.find((step) => step.status === 'in_progress')
    ?? deployment.steps.find((step) => step.key === deployment.currentStep)
    ?? deployment.steps.find((step) => step.status === 'failed')
    ?? deployment.steps.find((step) => step.status === 'passed')
    ?? null

  const deploymentTimeline = useMemo(
    () =>
      deployment.steps
        .filter((step) => step.status !== 'inactive')
        .map((step) => ({
          ...step,
          atMs:
            step.completedAtMs > 0
              ? step.completedAtMs
              : step.startedAtMs > 0
                ? step.startedAtMs
                : 0,
        }))
        .sort((left, right) => right.atMs - left.atMs),
    [deployment.steps],
  )

  const deploymentEventTail = useMemo(
    () =>
      events
        .filter((event) => event.category === 'deploy' || event.category === 'deployment' || event.module === 'deployment')
        .slice(0, 6),
    [events],
  )

  const statusLabel = !deployment.active
    ? 'Deployment off'
    : deployment.running
      ? 'Preparing'
      : deployment.failed
        ? 'Faulted'
        : readyIdle
          ? 'Ready, output off'
          : 'Deployment entered'

  const statusDetail = !deployment.active
    ? 'Enter deployment mode to reclaim ownership, hold GPIO6 low, and unlock aux controls for the powered bench.'
    : deployment.running
      ? 'The checklist owns the power-up sequence. Wait for it to stop before changing aux outputs or runtime control.'
      : deployment.failed
        ? deployment.primaryFailureReason || deployment.failureReason || 'The controller reported a deployment failure.'
        : readyIdle
          ? 'TEC and LD are qualified, the output path is idle, and host runtime control is available.'
          : 'Ownership is reclaimed and aux controls are available. Run the checklist when you are ready to qualify runtime operation.'

  const alignmentBlockedBy = !deployment.active
    ? 'Deployment is off.'
    : deployment.running
      ? 'Checklist is running.'
      : liveSnapshot.bench.requestedAlignmentEnabled && !liveSnapshot.laser.alignmentEnabled
        ? !liveSnapshot.safety.allowAlignment
          ? 'Safety or current power tier denies green output.'
          : 'Request is present but output is not yet applied.'
        : 'None'

  const ledBlockedBy = !deployment.active
    ? 'Deployment is off.'
    : deployment.running
      ? 'Checklist is running.'
      : liveSnapshot.bench.requestedLedEnabled && !liveSnapshot.bench.appliedLedPinHigh
        ? liveSnapshot.bench.appliedLedOwner === 'deployment'
          ? 'Deployment is holding GPIO6 low.'
          : 'GPIO6 request is present but the pin is not observed high.'
        : 'None'

  async function issue(
    cmd: string,
    note: string,
    args?: Record<string, number | string | boolean>,
    timeoutMs = 3200,
  ) {
    await onIssueCommandAwaitAck(cmd, 'write', note, args, { timeoutMs })
  }

  function updateRuntimeTemp(nextValue: string) {
    const parsed = Number(nextValue)
    setTargetDraft((current) => ({
      ...current,
      dirty: true,
      source: 'temp',
      tempC: nextValue,
      lambdaNm: Number.isFinite(parsed)
        ? formatNumber(estimateWavelengthFromTempC(clampTecTempC(parsed)), 1)
        : current.lambdaNm,
    }))
  }

  function updateRuntimeLambda(nextValue: string) {
    const parsed = Number(nextValue)
    setTargetDraft((current) => ({
      ...current,
      dirty: true,
      source: 'lambda',
      lambdaNm: nextValue,
      tempC: Number.isFinite(parsed)
        ? formatNumber(estimateTempFromWavelengthNm(clampTecWavelengthNm(parsed)), 1)
        : current.tempC,
    }))
  }

  function commitRuntimeTarget(source = targetCommitSource) {
    if (!readyIdle) {
      return
    }

    const tempC = clampTecTempC(parseNumber(runtimeTempC, liveSnapshot.tec.targetTempC))
    const lambdaNm = clampTecWavelengthNm(
      parseNumber(runtimeLambdaNm, liveSnapshot.tec.targetLambdaNm),
    )

    setTargetDraft({
      dirty: true,
      tempC: formatNumber(tempC, 1),
      lambdaNm: formatNumber(lambdaNm, 1),
      source,
    })

    void issue(
      'operate.set_target',
      source === 'lambda'
        ? 'Update the runtime wavelength setpoint.'
        : 'Update the runtime temperature setpoint.',
      source === 'lambda'
        ? { target_mode: 'lambda', lambda_nm: lambdaNm }
        : { target_mode: 'temp', temp_c: tempC },
    ).finally(() => {
      setTargetDraft((current) => ({
        ...current,
        dirty: false,
      }))
    })
  }

  function commitRequestedCurrent() {
    if (!hostOutputUnlocked) {
      return
    }

    setCurrentDraft({
      dirty: true,
      value: formatNumber(requestedCurrentA, 2),
    })

    void issue(
      'operate.set_output',
      liveSnapshot.bench.requestedNirEnabled
        ? 'Update the live constant NIR current request.'
        : 'Update the stored constant NIR current while output stays off.',
      {
        enabled: liveSnapshot.bench.requestedNirEnabled,
        current_a: requestedCurrentA,
      },
    ).finally(() => {
      setCurrentDraft((current) => ({
        ...current,
        dirty: false,
      }))
    })
  }

  function commitModulation(nextEnabled = modulationEnabled) {
    if (!hostOutputUnlocked) {
      return
    }

    const frequencyHz = Math.max(
      0,
      parseNumber(modulationFrequencyHz, liveSnapshot.bench.modulationFrequencyHz),
    )
    const dutyCyclePct = Math.max(
      0,
      Math.min(100, parseNumber(modulationDutyPct, liveSnapshot.bench.modulationDutyCyclePct)),
    )

    setModulationDraft({
      dirty: true,
      enabled: nextEnabled,
      frequencyHz: String(frequencyHz),
      dutyPct: String(dutyCyclePct),
    })

    void issue(
      'operate.set_modulation',
      'Update host modulation parameters.',
      {
        enabled: nextEnabled,
        frequency_hz: frequencyHz,
        duty_cycle_pct: dutyCyclePct,
      },
    ).finally(() => {
      setModulationDraft((current) => ({
        ...current,
        dirty: false,
      }))
    })
  }

  function commitLedBrightness() {
    if (!auxControlsUnlocked) {
      return
    }

    setLedDraft({
      dirty: true,
      value: String(requestedLedBrightnessPct),
    })

    void issue(
      'operate.set_led',
      liveSnapshot.bench.requestedLedEnabled
        ? 'Update the GPIO6 LED brightness request.'
        : 'Update the stored GPIO6 LED brightness while the output stays off.',
      {
        enabled: liveSnapshot.bench.requestedLedEnabled && requestedLedBrightnessPct > 0,
        duty_cycle_pct: requestedLedBrightnessPct,
        frequency_hz: 20000,
      },
    ).finally(() => {
      setLedDraft((current) => ({
        ...current,
        dirty: false,
      }))
    })
  }

  return (
    <section className="operate-console">
      <section className="panel-section">
        <div className="workspace-titlebar">
          <div>
            <h2>Operate</h2>
            <p>{statusDetail}</p>
          </div>
          <div className="status-badges">
            <span className={readyIdle ? 'status-badge is-on' : deployment.failed ? 'status-badge is-critical' : 'status-badge is-warn'}>
              {readyIdle ? <CheckCircle2 size={14} /> : deployment.running ? <LoaderCircle size={14} /> : <AlertTriangle size={14} />}
              {statusLabel}
            </span>
            <span className={readyTruth.tecRailPgoodFiltered ? 'status-badge is-on' : 'status-badge'}>
              <Thermometer size={14} />
              TEC {readyTruth.tecRailPgoodFiltered ? 'qualified' : 'not qualified'}
            </span>
            <span className={readyTruth.ldRailPgoodFiltered ? 'status-badge is-on' : 'status-badge'}>
              <Zap size={14} />
              LD {readyTruth.ldRailPgoodFiltered ? 'qualified' : 'not qualified'}
            </span>
          </div>
        </div>

        <div className="operate-toolbar">
          <button
            type="button"
            className="action-button is-inline is-accent"
            disabled={!connected || deployment.active}
            onClick={() => {
              void issue(
                'deployment.enter',
                'Enter deployment mode, reclaim ownership, and hold GPIO6 low by default.',
                undefined,
                4000,
              )
            }}
          >
            Enter deployment
          </button>
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || !deployment.active || deployment.running}
            onClick={() => {
              void issue(
                'deployment.exit',
                'Exit deployment mode and return to non-deployed supervision.',
                undefined,
                4000,
              )
            }}
          >
            Exit deployment
          </button>
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || !deployment.active || deployment.running}
            onClick={() => {
              void issue(
                'deployment.run',
                'Run the powered deployment checklist with live step telemetry.',
                undefined,
                4000,
              )
            }}
          >
            Run checklist
          </button>
        </div>
      </section>

      <div className="operate-wizard">
        <section className="panel-section operate-wizard__rail">
          <div className="section-head">
            <div>
              <h3>Checklist</h3>
              <p>Fixed 25 C deployment target. The controller advances these rows live.</p>
            </div>
            <span className={deployment.running ? 'status-badge is-warn' : readyIdle ? 'status-badge is-on' : 'status-badge'}>
              <Clock3 size={14} />
              {activeStep ? activeStep.label : 'Waiting'}
            </span>
          </div>

          <div className="operate-step-rail">
            {deployment.steps.map((step, index) => (
              <article
                key={step.key}
                className={`operate-step-card ${stepTone(step.status)}`.trim()}
                data-active={step.key === activeStep?.key}
              >
                <span className="operate-step-card__index">{index + 1}</span>
                <div className="operate-step-card__body">
                  <strong>{step.label}</strong>
                  <small>{step.status.replaceAll('_', ' ')}</small>
                </div>
              </article>
            ))}
          </div>

          {deployment.primaryFailureReason ? (
            <div className="inline-alert is-critical">
              <strong>{deployment.primaryFailureCode}</strong>
              <p>{deployment.primaryFailureReason}</p>
            </div>
          ) : null}
        </section>

        <div className="operate-main">
          <section className="panel-section operate-deployment-card">
            <div className="section-head">
              <div>
                <h3>Deployment status</h3>
                <p>Live deployment telemetry from the controller, including current step, cap, and passive PD state.</p>
              </div>
            </div>

            <div className="operate-focus-card">
              <div className="operate-focus-card__header">
                <div>
                  <strong>{activeStep?.label ?? 'No active checklist step'}</strong>
                  <p>
                    Phase {deployment.phase.replaceAll('_', ' ')}.
                    {' '}
                    {readyIdle
                      ? 'Runtime output is idle and available.'
                      : deployment.running
                        ? 'The checklist currently owns the power-up sequence.'
                        : 'Ownership is reclaimed and the controller is waiting for the next action.'}
                  </p>
                </div>
                <span className={readyIdle ? 'status-badge is-on' : deployment.failed ? 'status-badge is-critical' : 'status-badge'}>
                  {statusLabel}
                </span>
              </div>

              <div className="operate-focus-card__metrics">
                <div>
                  <span>Sequence</span>
                  <strong>{deployment.sequenceId}</strong>
                </div>
                <div>
                  <span>Current step</span>
                  <strong>{deployment.currentStepIndex}</strong>
                </div>
                <div>
                  <span>Current cap</span>
                  <strong>{formatNumber(deployment.maxLaserCurrentA, 2)} A</strong>
                </div>
                <div>
                  <span>PD status</span>
                  <strong>{pdSnapshotLabel}</strong>
                </div>
              </div>
            </div>
          </section>

          <div className="operate-control-grid">
            <section className="panel-section">
              <div className="section-head">
                <div>
                  <h3>Runtime setpoint</h3>
                  <p>Temperature and wavelength stay linked. Runtime updates commit on slider release, blur, or Enter.</p>
                </div>
              </div>

              <div className="segmented is-compact">
                <button
                  type="button"
                  className={runtimeMode === 'binary_trigger' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={!connected || (!liveSnapshot.bench.runtimeModeSwitchAllowed && runtimeMode !== 'binary_trigger')}
                  onClick={() => {
                    void issue('operate.set_mode', 'Switch NIR control source to trigger buttons.', { mode: 'binary_trigger' })
                  }}
                >
                  Trigger buttons
                </button>
                <button
                  type="button"
                  className={runtimeMode === 'modulated_host' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={!connected || (!liveSnapshot.bench.runtimeModeSwitchAllowed && runtimeMode !== 'modulated_host')}
                  onClick={() => {
                    void issue('operate.set_mode', 'Switch NIR control source to host control.', { mode: 'modulated_host' })
                  }}
                >
                  Host control
                </button>
              </div>

              {!liveSnapshot.bench.runtimeModeSwitchAllowed ? (
                <p className="inline-help">{liveSnapshot.bench.runtimeModeLockReason}</p>
              ) : null}

              <div className="operate-target-grid">
                <div className="operate-slider-field">
                  <label className="field">
                    <span>Temperature (C)</span>
                    <input
                      type="range"
                      min="5"
                      max="65"
                      step="0.1"
                      value={clampTecTempC(parseNumber(runtimeTempC, liveSnapshot.tec.targetTempC))}
                      disabled={!readyIdle}
                      onChange={(event) => updateRuntimeTemp(event.target.value)}
                      onMouseUp={() => commitRuntimeTarget('temp')}
                      onTouchEnd={() => commitRuntimeTarget('temp')}
                    />
                  </label>
                  <label className="field">
                    <span>Requested temp</span>
                    <input
                      type="number"
                      min="5"
                      max="65"
                      step="0.1"
                      value={runtimeTempC}
                      disabled={!readyIdle}
                      onChange={(event) => updateRuntimeTemp(event.target.value)}
                      onBlur={() => commitRuntimeTarget('temp')}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter') {
                          commitRuntimeTarget('temp')
                        }
                      }}
                    />
                  </label>
                </div>

                <div className="operate-slider-field">
                  <label className="field">
                    <span>Wavelength (nm)</span>
                    <input
                      type="range"
                      min="771.2"
                      max="790"
                      step="0.1"
                      value={clampTecWavelengthNm(parseNumber(runtimeLambdaNm, liveSnapshot.tec.targetLambdaNm))}
                      disabled={!readyIdle}
                      onChange={(event) => updateRuntimeLambda(event.target.value)}
                      onMouseUp={() => commitRuntimeTarget('lambda')}
                      onTouchEnd={() => commitRuntimeTarget('lambda')}
                    />
                  </label>
                  <label className="field">
                    <span>Requested wavelength</span>
                    <input
                      type="number"
                      min="771.2"
                      max="790"
                      step="0.1"
                      value={runtimeLambdaNm}
                      disabled={!readyIdle}
                      onChange={(event) => updateRuntimeLambda(event.target.value)}
                      onBlur={() => commitRuntimeTarget('lambda')}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter') {
                          commitRuntimeTarget('lambda')
                        }
                      }}
                    />
                  </label>
                </div>
              </div>
            </section>

            <section className="panel-section">
              <div className="section-head">
                <div>
                  <h3>Constant NIR</h3>
                  <p>One stored current request plus one explicit output toggle. Modulation settings sit underneath.</p>
                </div>
              </div>

              <div className="compact-grid">
                <div className="field field--static">
                  <span>Requested current</span>
                  <strong>{formatNumber(liveSnapshot.bench.requestedCurrentA, 2)} A</strong>
                </div>
                <div className="field field--static">
                  <span>Applied current</span>
                  <strong>{formatNumber(liveSnapshot.laser.commandedCurrentA, 2)} A</strong>
                </div>
              </div>

              <label className="field">
                <span>Current request</span>
                <input
                  type="range"
                  min="0"
                  max="5.2"
                  step="0.05"
                  value={requestedCurrentA}
                  disabled={!hostOutputUnlocked}
                  onChange={(event) => {
                    setCurrentDraft({ dirty: true, value: event.target.value })
                  }}
                  onMouseUp={commitRequestedCurrent}
                  onTouchEnd={commitRequestedCurrent}
                />
              </label>

              <div className="compact-grid">
                <label className="field">
                  <span>Current (A)</span>
                  <input
                    type="number"
                    min="0"
                    max="5.2"
                    step="0.05"
                    value={laserCurrentA}
                    disabled={!hostOutputUnlocked}
                    onChange={(event) => {
                      setCurrentDraft({ dirty: true, value: event.target.value })
                    }}
                    onBlur={commitRequestedCurrent}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') {
                        commitRequestedCurrent()
                      }
                    }}
                  />
                </label>
                <div className="field field--static">
                  <span>Checklist cap</span>
                  <strong>{formatNumber(deployment.maxLaserCurrentA, 2)} A</strong>
                </div>
              </div>

              <div className="button-row is-compact">
                <button
                  type="button"
                  className="action-button is-inline is-accent"
                  disabled={!hostOutputUnlocked}
                  onClick={() => {
                    void issue(
                      'operate.set_output',
                      liveSnapshot.bench.requestedNirEnabled
                        ? 'Disable host-controlled NIR output.'
                        : 'Enable host-controlled NIR output.',
                      {
                        enabled: !liveSnapshot.bench.requestedNirEnabled,
                        current_a: requestedCurrentA,
                      },
                    )
                  }}
                >
                  {liveSnapshot.bench.requestedNirEnabled ? 'Output off' : 'Output on'}
                </button>
              </div>

              <div className="compact-grid">
                <label className="field">
                  <span>Modulation freq (Hz)</span>
                  <input
                    type="number"
                    value={modulationFrequencyHz}
                    disabled={!hostOutputUnlocked}
                    onChange={(event) => {
                      setModulationDraft((current) => ({
                        ...current,
                        dirty: true,
                        frequencyHz: event.target.value,
                      }))
                    }}
                    onBlur={() => commitModulation()}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') {
                        commitModulation()
                      }
                    }}
                  />
                </label>
                <label className="field">
                  <span>Duty (%)</span>
                  <input
                    type="number"
                    value={modulationDutyPct}
                    disabled={!hostOutputUnlocked}
                    onChange={(event) => {
                      setModulationDraft((current) => ({
                        ...current,
                        dirty: true,
                        dutyPct: event.target.value,
                      }))
                    }}
                    onBlur={() => commitModulation()}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') {
                        commitModulation()
                      }
                    }}
                  />
                </label>
              </div>

              <label className="arming-toggle is-compact">
                <input
                  type="checkbox"
                  checked={modulationEnabled}
                  disabled={!hostOutputUnlocked}
                  onChange={(event) => {
                    const nextEnabled = event.target.checked
                    setModulationDraft((current) => ({
                      ...current,
                      dirty: true,
                      enabled: nextEnabled,
                    }))
                    commitModulation(nextEnabled)
                  }}
                />
                <span>Enable PCN modulation</span>
              </label>
            </section>

            <section className="panel-section">
              <div className="section-head">
                <div>
                  <h3>Aux lights</h3>
                  <p>Operate owns the normal request path after deployment entry. Integrate remains the hazardous service override path.</p>
                </div>
              </div>

              <div className="operate-aux-grid">
                <article className="operate-aux-card">
                  <strong>Green laser</strong>
                  <dl className="summary-list">
                    <div>
                      <dt>Requested</dt>
                      <dd>{liveSnapshot.bench.requestedAlignmentEnabled ? 'On' : 'Off'}</dd>
                    </div>
                    <div>
                      <dt>Applied</dt>
                      <dd>{liveSnapshot.laser.alignmentEnabled ? 'On' : 'Off'}</dd>
                    </div>
                    <div>
                      <dt>Owner</dt>
                      <dd>{deployment.active ? 'Operate' : 'None'}</dd>
                    </div>
                    <div>
                      <dt>Blocked by</dt>
                      <dd>{alignmentBlockedBy}</dd>
                    </div>
                  </dl>

                  <div className="button-row is-compact">
                    <button
                      type="button"
                      className="action-button is-inline"
                      disabled={!auxControlsUnlocked}
                      onClick={() => {
                        void issue(
                          'operate.set_alignment',
                          liveSnapshot.bench.requestedAlignmentEnabled
                            ? 'Disable the normal green-laser request.'
                            : 'Enable the normal green-laser request.',
                          { enabled: !liveSnapshot.bench.requestedAlignmentEnabled },
                        )
                      }}
                    >
                      {liveSnapshot.bench.requestedAlignmentEnabled ? 'Request off' : 'Request on'}
                    </button>
                  </div>
                </article>

                <article className="operate-aux-card">
                  <strong>GPIO6 LED</strong>
                  <dl className="summary-list">
                    <div>
                      <dt>Requested</dt>
                      <dd>
                        {liveSnapshot.bench.requestedLedEnabled
                          ? `${liveSnapshot.bench.requestedLedDutyCyclePct}%`
                          : 'Off'}
                      </dd>
                    </div>
                    <div>
                      <dt>Applied</dt>
                      <dd>{liveSnapshot.bench.appliedLedPinHigh ? 'On' : 'Off'}</dd>
                    </div>
                    <div>
                      <dt>Owner</dt>
                      <dd>{ledOwnerLabel(liveSnapshot.bench.appliedLedOwner)}</dd>
                    </div>
                    <div>
                      <dt>Blocked by</dt>
                      <dd>{ledBlockedBy}</dd>
                    </div>
                  </dl>

                  <label className="field">
                    <span>Brightness (%)</span>
                    <input
                      type="range"
                      min="0"
                      max="100"
                      step="1"
                      value={requestedLedBrightnessPct}
                      disabled={!auxControlsUnlocked}
                      onChange={(event) => {
                        setLedDraft({ dirty: true, value: event.target.value })
                      }}
                      onMouseUp={commitLedBrightness}
                      onTouchEnd={commitLedBrightness}
                    />
                  </label>

                  <div className="compact-grid">
                    <label className="field">
                      <span>Brightness</span>
                      <input
                        type="number"
                        min="0"
                        max="100"
                        step="1"
                        value={ledBrightnessPct}
                        disabled={!auxControlsUnlocked}
                        onChange={(event) => {
                          setLedDraft({ dirty: true, value: event.target.value })
                        }}
                        onBlur={commitLedBrightness}
                        onKeyDown={(event) => {
                          if (event.key === 'Enter') {
                            commitLedBrightness()
                          }
                        }}
                      />
                    </label>
                    <div className="field field--static">
                      <span>Observed pin</span>
                      <strong>{liveSnapshot.bench.appliedLedPinHigh ? 'High' : 'Low'}</strong>
                    </div>
                  </div>

                  <div className="button-row is-compact">
                    <button
                      type="button"
                      className="action-button is-inline"
                      disabled={!auxControlsUnlocked}
                      onClick={() => {
                        const nextEnabled = !liveSnapshot.bench.requestedLedEnabled
                        void issue(
                          'operate.set_led',
                          nextEnabled
                            ? 'Enable the GPIO6 LED request.'
                            : 'Disable the GPIO6 LED request.',
                          {
                            enabled: nextEnabled && requestedLedBrightnessPct > 0,
                            duty_cycle_pct: nextEnabled ? requestedLedBrightnessPct : 0,
                            frequency_hz: 20000,
                          },
                        )
                      }}
                    >
                      {liveSnapshot.bench.requestedLedEnabled ? 'LED off' : 'LED on'}
                    </button>
                  </div>
                </article>
              </div>
            </section>

            <section className="panel-section">
              <div className="section-head">
                <div>
                  <h3>Ready truth</h3>
                  <p>Passive PD only here. Explicit PD refreshes or writes remain Integrate-only.</p>
                </div>
              </div>

              <div className="operate-truth-grid">
                <div>
                  <span>TEC PGOOD</span>
                  <strong>{readyTruth.tecRailPgoodRaw ? 'raw high' : 'raw low'}</strong>
                  <small>{readyTruth.tecRailPgoodFiltered ? 'filtered good' : 'filtered bad'}</small>
                </div>
                <div>
                  <span>LD PGOOD</span>
                  <strong>{readyTruth.ldRailPgoodRaw ? 'raw high' : 'raw low'}</strong>
                  <small>{readyTruth.ldRailPgoodFiltered ? 'filtered good' : 'filtered bad'}</small>
                </div>
                <div>
                  <span>TEMPGD</span>
                  <strong>{readyTruth.tecTempGood ? 'high' : 'low'}</strong>
                  <small>{readyTruth.tecAnalogPlausible ? 'analog plausible' : 'analog suspect'}</small>
                </div>
                <div>
                  <span>Driver path</span>
                  <strong>{readyTruth.driverLoopGood ? 'loop good' : 'loop bad'}</strong>
                  <small>{readyTruth.sbdnHigh ? 'SBDN high' : 'SBDN low'} / {readyTruth.pcnLow ? 'PCN low' : 'PCN high'}</small>
                </div>
                <div>
                  <span>Idle bias</span>
                  <strong>{formatNumber(readyTruth.idleBiasCurrentA, 3)} A</strong>
                  <small>Threshold lives on Integrate</small>
                </div>
                <div>
                  <span>PD source</span>
                  <strong>{pdSnapshotLabel}</strong>
                  <small>{formatEnumLabel(liveSnapshot.session.powerTier)}</small>
                </div>
              </div>
            </section>

            <section className="panel-section operate-causal-log__panel">
              <div className="section-head">
                <div>
                  <h3>Deployment causal log</h3>
                  <p>Primary fault first, secondary effects after, then the checklist timeline.</p>
                </div>
              </div>

              <div className="operate-causal-log">
                <article className="operate-causal-log__block">
                  <strong>Primary fault</strong>
                  <p>
                    {deployment.primaryFailureReason
                      ? `${deployment.primaryFailureCode}: ${deployment.primaryFailureReason}`
                      : deployment.running
                        ? 'No primary fault recorded while the checklist is running.'
                        : 'No deployment fault is currently recorded.'}
                  </p>
                </article>

                <article className="operate-causal-log__block">
                  <strong>Secondary effects</strong>
                  {deployment.secondaryEffects.length > 0 ? (
                    <div className="status-stack">
                      {deployment.secondaryEffects.map((effect) => (
                        <span key={`${effect.code}-${effect.atMs}`} className={effectTone(effect)}>
                          {effect.code}: {effect.reason}
                        </span>
                      ))}
                    </div>
                  ) : (
                    <p>No secondary effects are attached to the current deployment state.</p>
                  )}
                </article>

                <article className="operate-causal-log__block">
                  <strong>Checklist timeline</strong>
                  {deploymentTimeline.length > 0 ? (
                    <div className="activity-list">
                      {deploymentTimeline.map((step) => (
                        <article key={`${step.key}-${step.startedAtMs}-${step.completedAtMs}`} className="activity-row">
                          <div>
                            <strong>{step.label}</strong>
                            <small>
                              start {step.startedAtMs} ms
                              {step.completedAtMs > 0 ? ` / end ${step.completedAtMs} ms` : ''}
                            </small>
                          </div>
                          <p>{step.status.replaceAll('_', ' ')}</p>
                        </article>
                      ))}
                    </div>
                  ) : deploymentEventTail.length > 0 ? (
                    <div className="activity-list">
                      {deploymentEventTail.map((event) => (
                        <article key={event.id} className="activity-row">
                          <div>
                            <strong>{event.title}</strong>
                            <small>{event.atIso}</small>
                          </div>
                          <p>{event.detail}</p>
                        </article>
                      ))}
                    </div>
                  ) : (
                    <p className="panel-note">No deployment-causal records have been recorded in this session.</p>
                  )}
                </article>
              </div>
            </section>
          </div>
        </div>
      </div>
    </section>
  )
}
