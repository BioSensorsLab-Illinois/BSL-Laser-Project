import { useEffect, useMemo, useRef, useState } from 'react'
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
  LedBlockedReason,
  NirBlockedReason,
  SbdnState,
  SessionEvent,
  TransportStatus,
  TriggerPhase,
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

/* ------------------------------------------------------------------ */
/* Reason-string tables — mirror the firmware reason tokens one-for-one */
/* ------------------------------------------------------------------ */

function nirReasonLabel(reason: NirBlockedReason): string {
  switch (reason) {
    case 'none':
      return 'NIR request is allowed.'
    case 'not-connected':
      return 'No controller is connected.'
    case 'fault-latched':
      return 'A fault is latched. Clear it before requesting NIR.'
    case 'deployment-off':
      return 'Enter deployment mode first.'
    case 'checklist-running':
      return 'Wait for the deployment checklist to finish.'
    case 'checklist-not-ready':
      return 'Deployment has not reached the ready posture yet.'
    case 'ready-not-idle':
      return 'Ready posture is asserted but the output is not idle.'
    case 'not-modulated-host':
      return 'Switch runtime mode to Host control to drive NIR from here.'
    case 'power-not-full':
      return 'Connect a full-power PD source before requesting NIR.'
    case 'rail-not-good':
      return 'LD or TEC rail is not reporting PGOOD.'
    case 'tec-not-settled':
      return 'TEC temperature has not settled yet.'
  }
}

function ledReasonLabel(reason: LedBlockedReason): string {
  switch (reason) {
    case 'none':
      return 'GPIO6 LED request is allowed.'
    case 'not-connected':
      return 'No controller is connected.'
    case 'deployment-off':
      return 'Enter deployment mode first.'
    case 'checklist-running':
      return 'Wait for the deployment checklist to finish.'
  }
}

function sbdnStateLabel(state: SbdnState): string {
  switch (state) {
    case 'on':
      return 'Operate (drive HIGH)'
    case 'off':
      return 'Shutdown (drive LOW)'
    case 'standby':
      return 'Standby (Hi-Z ~2.25 V)'
  }
}

function sbdnStateTone(state: SbdnState): string {
  return state === 'on' ? 'is-warn' : state === 'off' ? 'is-critical' : ''
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

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function clampCurrentA(value: number): number {
  return Math.max(0, Math.min(5.2, value))
}

/* ------------------------------------------------------------------ */
/*                           OperateConsole                            */
/* ------------------------------------------------------------------ */

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
  const bench = liveSnapshot.bench
  const laser = liveSnapshot.laser
  const fault = liveSnapshot.fault
  const readiness = bench.hostControlReadiness

  const runtimeMode = bench.runtimeMode
  const readyIdle = deployment.ready && deployment.readyIdle && deployment.readyQualified

  /* -------- canonical enable gates, driven by firmware reasons -------- */

  const nirEnabled = connected && readiness.nirBlockedReason === 'none'
  const nirDisabledReason = !connected
    ? 'No controller is connected.'
    : nirReasonLabel(readiness.nirBlockedReason)

  const ledEnabled = connected && readiness.ledBlockedReason === 'none'
  const ledDisabledReason = !connected
    ? 'No controller is connected.'
    : ledReasonLabel(readiness.ledBlockedReason)

  /* Green laser is never software-gated. Firmware honors the request
   * unconditionally; hardware rail availability is the only physical gate. */
  const greenEnabled = connected
  const greenDisabledReason = connected
    ? 'Green alignment is always available from the host.'
    : 'No controller is connected.'

  /* -------- draft state for inputs (so sliders don't fight the live snapshot) -------- */

  const liveRuntimeTempC = formatNumber(liveSnapshot.tec.targetTempC, 1)
  const liveRuntimeLambdaNm = formatNumber(liveSnapshot.tec.targetLambdaNm, 1)
  const liveRequestedCurrent = formatNumber(bench.requestedCurrentA, 2)
  const liveModulationFrequency = String(bench.modulationFrequencyHz)
  const liveModulationDuty = String(bench.modulationDutyCyclePct)
  /*
   * LED brightness comes from the firmware's button_runtime channel
   * (which is the shared source-of-truth for both side buttons and the
   * GUI slider since 2026-04-17). This makes the slider reflect side-
   * button adjustments AND avoids the snap-back bug where the slider
   * value diverged from the actual LED brightness when deployment-armed.
   * Falls back to bench storage when the button board telemetry is stale.
   */
  const liveLedBrightness = String(
    liveSnapshot.buttonBoard.ledBrightnessPct ||
      bench.requestedLedDutyCyclePct ||
      bench.illuminationDutyCyclePct,
  )

  const [targetDraft, setTargetDraft] = useState(() => ({
    dirty: false,
    tempC: liveRuntimeTempC,
    lambdaNm: liveRuntimeLambdaNm,
    source: bench.targetMode === 'temp' ? ('temp' as const) : ('lambda' as const),
  }))
  const [currentDraft, setCurrentDraft] = useState(() => ({
    dirty: false,
    value: liveRequestedCurrent,
  }))
  const [modulationDraft, setModulationDraft] = useState(() => ({
    dirty: false,
    enabled: bench.modulationEnabled,
    frequencyHz: liveModulationFrequency,
    dutyPct: liveModulationDuty,
  }))
  const [ledDraft, setLedDraft] = useState(() => ({
    dirty: false,
    value: liveLedBrightness,
  }))

  const runtimeTempC = targetDraft.dirty ? targetDraft.tempC : liveRuntimeTempC
  const runtimeLambdaNm = targetDraft.dirty ? targetDraft.lambdaNm : liveRuntimeLambdaNm
  const laserCurrentA = currentDraft.dirty ? currentDraft.value : liveRequestedCurrent
  const modulationEnabled = modulationDraft.dirty
    ? modulationDraft.enabled
    : bench.modulationEnabled
  const modulationFrequencyHz = modulationDraft.dirty
    ? modulationDraft.frequencyHz
    : liveModulationFrequency
  const modulationDutyPct = modulationDraft.dirty
    ? modulationDraft.dutyPct
    : liveModulationDuty
  const ledBrightnessPct = ledDraft.dirty ? ledDraft.value : liveLedBrightness

  const requestedCurrentA = clampCurrentA(
    parseNumber(laserCurrentA, bench.requestedCurrentA),
  )
  const requestedLedBrightnessPct = Math.max(
    0,
    Math.min(
      100,
      parseNumber(
        ledBrightnessPct,
        liveSnapshot.buttonBoard.ledBrightnessPct ||
          bench.requestedLedDutyCyclePct ||
          bench.illuminationDutyCyclePct,
      ),
    ),
  )

  /* -------- debounced LED commit (fixes the hardware flicker during drag) -------- */

  const ledCommitTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  useEffect(() => {
    return () => {
      if (ledCommitTimer.current) {
        clearTimeout(ledCommitTimer.current)
      }
    }
  }, [])

  /*
   * 2026-04-17 (audit round 3): runtime-control gate that matches what
   * the firmware actually enforces. `operate.set_target`,
   * `operate.set_output`, `operate.set_modulation` are all rejected by
   * the firmware unless `deployment.active && deployment.ready` (see
   * `laser_controller_comms_is_runtime_control_command`). Before this
   * round the UI kept the sliders enabled whenever `connected`, so the
   * operator could type a new setpoint, blur, and see the draft "30.0"
   * sticky on screen — thinking the commit had succeeded — when in
   * fact the firmware had returned `"Complete the deployment checklist
   * successfully before using runtime control commands."` The reveal
   * came on navigation: `OperateConsole` unmounted, the draft died,
   * the remount sourced from the untouched live snapshot, and the old
   * value reappeared. Now the gate is explicit and the operator can
   * see why the input is disabled before typing.
   */
  const runtimeControlAllowed =
    connected && deployment.active && deployment.ready
  const runtimeControlBlockReason = !connected
    ? 'No controller is connected.'
    : !deployment.active
      ? 'Enter deployment mode first. Runtime setpoints take effect only after the checklist qualifies TEC + LD.'
      : !deployment.ready
        ? 'The deployment checklist must reach the ready posture before runtime setpoints can be changed.'
        : undefined

  /*
   * Last commit-failure error surfaced to the operator. The commit
   * functions below await the ack, and on `!result.ok` they set this
   * string + clear the associated draft so the displayed value snaps
   * back to the firmware's live truth immediately (not only on the
   * next page nav). Null clears the banner. A 6 s auto-clear timer is
   * armed on every set to keep the banner from lingering forever.
   */
  const [runtimeCommitError, setRuntimeCommitErrorRaw] = useState<string | null>(null)
  const runtimeCommitErrorTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  function setRuntimeCommitError(msg: string | null) {
    if (runtimeCommitErrorTimer.current) {
      clearTimeout(runtimeCommitErrorTimer.current)
      runtimeCommitErrorTimer.current = null
    }
    setRuntimeCommitErrorRaw(msg)
    if (msg) {
      runtimeCommitErrorTimer.current = setTimeout(() => {
        setRuntimeCommitErrorRaw(null)
        runtimeCommitErrorTimer.current = null
      }, 6000)
    }
  }
  useEffect(() => {
    return () => {
      if (runtimeCommitErrorTimer.current) {
        clearTimeout(runtimeCommitErrorTimer.current)
      }
    }
  }, [])

  /* -------- active step for the focus card -------- */

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
        .filter(
          (event) =>
            event.category === 'deploy' ||
            event.category === 'deployment' ||
            event.module === 'deployment',
        )
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
    ? 'Deployment is not active. The controller auto-deploys on boot; re-enter manually if needed.'
    : deployment.running
      ? 'The checklist owns the power-up sequence. Wait for it to stop before changing runtime control.'
      : deployment.failed
        ? deployment.primaryFailureReason ||
          deployment.failureReason ||
          'The controller reported a deployment failure.'
        : readyIdle
          ? 'TEC and LD are qualified, the output path is idle, and host runtime control is available.'
          : 'Ownership is reclaimed. Run the checklist when you are ready to qualify runtime operation.'

  const pdSnapshotLabel =
    liveSnapshot.pd.lastUpdatedMs === 0
      ? 'No passive PD observation yet'
      : liveSnapshot.pd.snapshotFresh
        ? `Passive status fresh from ${liveSnapshot.pd.source.replaceAll('_', ' ')}`
        : `Passive status cached from ${liveSnapshot.pd.source.replaceAll('_', ' ')}`

  /* -------- helpers to send commands -------- */

  async function issue(
    cmd: string,
    note: string,
    args?: Record<string, number | string | boolean>,
    timeoutMs = 3200,
  ): Promise<{ ok: boolean; note: string }> {
    return onIssueCommandAwaitAck(cmd, 'write', note, args, { timeoutMs })
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

  async function commitRuntimeTarget(
    source: 'temp' | 'lambda' = targetDraft.source,
  ) {
    /*
     * 2026-04-17 (audit round 3): the firmware rejects
     * `operate.set_target` unless `deployment.active && deployment.ready`
     * (see `laser_controller_comms_is_runtime_control_command`). The
     * previous version of this function kept the draft sticky on
     * commit and fired `void issue(...)` without awaiting the ack —
     * so a rejection was invisible to the operator. The symptom the
     * user reported was: type 30.0, blur, display shows 30.0, switch
     * pages, come back → display snaps back to 28.7 because the
     * commit was never applied. Now the input is pre-disabled when
     * the gate is closed; we await the ack; and on `!ok` we drop the
     * draft dirty flag so the displayed value snaps back to the
     * firmware truth immediately AND surface the firmware error text
     * to the operator. On success the draft stays sticky so the user
     * can see what they committed until the next telemetry echo
     * overwrites it.
     */
    if (!runtimeControlAllowed) {
      setRuntimeCommitError(
        runtimeControlBlockReason ||
          'Runtime setpoint commit is not available right now.',
      )
      setTargetDraft((current) => ({ ...current, dirty: false }))
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

    const result = await issue(
      'operate.set_target',
      source === 'lambda'
        ? 'Update the runtime wavelength setpoint.'
        : 'Update the runtime temperature setpoint.',
      source === 'lambda'
        ? { target_mode: 'lambda', lambda_nm: lambdaNm }
        : { target_mode: 'temp', temp_c: tempC },
    )
    if (!result.ok) {
      setRuntimeCommitError(result.note || 'Runtime setpoint commit rejected.')
      setTargetDraft((current) => ({ ...current, dirty: false }))
    } else {
      setRuntimeCommitError(null)
    }
  }

  async function commitRequestedCurrent() {
    /*
     * 2026-04-17 (audit round 3): mirror the gate + await pattern of
     * commitRuntimeTarget. `operate.set_output` is rejected pre-ready
     * just like `operate.set_target`, so the same snap-back symptom
     * applied to LD current. Now the slider is pre-disabled when the
     * firmware gate is closed; the ack is awaited; on failure the
     * draft is cleared (displays the truth) and the error is
     * surfaced; on success the draft stays sticky until the next
     * telemetry echo.
     */
    if (!runtimeControlAllowed) {
      setRuntimeCommitError(
        runtimeControlBlockReason ||
          'Runtime setpoint commit is not available right now.',
      )
      setCurrentDraft((current) => ({ ...current, dirty: false }))
      return
    }

    setCurrentDraft({ dirty: true, value: formatNumber(requestedCurrentA, 2) })
    const result = await issue(
      'operate.set_output',
      bench.requestedNirEnabled
        ? 'Update the live constant NIR current request.'
        : 'Update the stored constant NIR current while output stays off.',
      { enabled: bench.requestedNirEnabled, current_a: requestedCurrentA },
    )
    if (!result.ok) {
      setRuntimeCommitError(result.note || 'NIR current commit rejected.')
      setCurrentDraft((current) => ({ ...current, dirty: false }))
    } else {
      setRuntimeCommitError(null)
    }
  }

  async function commitModulation(nextEnabled = modulationEnabled) {
    /*
     * 2026-04-17 (audit round 3): same gate + await pattern. If the
     * firmware would reject, the draft is cleared up-front so the
     * displayed value reflects truth; error surfaced.
     */
    if (!runtimeControlAllowed) {
      setRuntimeCommitError(
        runtimeControlBlockReason ||
          'Modulation commit is not available right now.',
      )
      setModulationDraft((current) => ({ ...current, dirty: false }))
      return
    }

    const frequencyHz = Math.max(
      0,
      parseNumber(modulationFrequencyHz, bench.modulationFrequencyHz),
    )
    const dutyCyclePct = Math.max(
      0,
      Math.min(100, parseNumber(modulationDutyPct, bench.modulationDutyCyclePct)),
    )

    setModulationDraft({
      dirty: true,
      enabled: nextEnabled,
      frequencyHz: String(frequencyHz),
      dutyPct: String(dutyCyclePct),
    })

    const result = await issue(
      'operate.set_modulation',
      'Update host modulation parameters.',
      {
        enabled: nextEnabled,
        frequency_hz: frequencyHz,
        duty_cycle_pct: dutyCyclePct,
      },
    )
    setModulationDraft((current) => ({ ...current, dirty: false }))
    if (!result.ok) {
      setRuntimeCommitError(result.note || 'Modulation commit rejected.')
    } else {
      setRuntimeCommitError(null)
    }
  }

  /* LED commit — debounced 200ms trailing, so dragging the slider fires
   * one command at release instead of spamming the firmware. */
  function scheduleLedCommit(delayMs = 200) {
    if (!ledEnabled) return
    if (ledCommitTimer.current) clearTimeout(ledCommitTimer.current)
    ledCommitTimer.current = setTimeout(() => {
      ledCommitTimer.current = null
      setLedDraft((current) => ({ ...current, dirty: true }))
      const duty = requestedLedBrightnessPct
      void issue(
        'operate.set_led',
        bench.requestedLedEnabled
          ? 'Update the GPIO6 LED brightness request.'
          : 'Update the stored GPIO6 LED brightness while the output stays off.',
        {
          enabled: bench.requestedLedEnabled && duty > 0,
          duty_cycle_pct: duty,
          frequency_hz: 20000,
        },
      ).finally(() => {
        setLedDraft((current) => ({ ...current, dirty: false }))
      })
    }, delayMs)
  }

  function toggleLed() {
    if (!ledEnabled) return
    if (ledCommitTimer.current) {
      clearTimeout(ledCommitTimer.current)
      ledCommitTimer.current = null
    }
    const nextEnabled = !bench.requestedLedEnabled
    /*
     * 2026-04-17 (audit round 2, S3): clear the brightness-draft
     * dirty flag on the next telemetry cycle. Without this the
     * toggleLed path left `ledDraft.dirty=true` (set by an in-flight
     * scheduleLedCommit) after flushing the pending timer, so the
     * slider snapped back to the firmware-echoed brightness on the
     * next snapshot and the operator's typed value was lost.
     */
    void issue(
      'operate.set_led',
      nextEnabled ? 'Enable the GPIO6 LED request.' : 'Disable the GPIO6 LED request.',
      {
        enabled: nextEnabled && requestedLedBrightnessPct > 0,
        duty_cycle_pct: nextEnabled ? requestedLedBrightnessPct : 0,
        frequency_hz: 20000,
      },
    ).finally(() => {
      setLedDraft((current) => ({ ...current, dirty: false }))
    })
  }

  /* -------- render -------- */

  return (
    <section className="operate-v3">
      {/* Header ------------------------------------------------------- */}
      <section className="panel-section">
        <div className="workspace-titlebar">
          <div>
            <h2>Operate</h2>
            <p>{statusDetail}</p>
          </div>
          <div className="status-badges">
            <span
              className={
                readyIdle
                  ? 'status-badge is-on'
                  : deployment.failed
                    ? 'status-badge is-critical'
                    : 'status-badge is-warn'
              }
            >
              {readyIdle ? (
                <CheckCircle2 size={14} />
              ) : deployment.running ? (
                <LoaderCircle size={14} />
              ) : (
                <AlertTriangle size={14} />
              )}
              {statusLabel}
            </span>
            <span
              className={
                readyTruth.tecRailPgoodFiltered ? 'status-badge is-on' : 'status-badge'
              }
            >
              <Thermometer size={14} />
              TEC {readyTruth.tecRailPgoodFiltered ? 'qualified' : 'not qualified'}
            </span>
            <span
              className={
                readyTruth.ldRailPgoodFiltered ? 'status-badge is-on' : 'status-badge'
              }
            >
              <Zap size={14} />
              LD {readyTruth.ldRailPgoodFiltered ? 'qualified' : 'not qualified'}
            </span>
            <span className={`status-badge ${sbdnStateTone(readiness.sbdnState)}`.trim()}>
              SBDN {readiness.sbdnState}
            </span>
          </div>
        </div>

        <div className="operate-v3__toolbar">
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || deployment.running || deployment.active}
            title={
              !connected
                ? 'No controller connected.'
                : deployment.running
                  ? 'Deployment checklist is running — wait for it to complete.'
                  : deployment.active
                    ? 'Deployment mode is already active. Use "Exit deployment" first if you want to restart it.'
                    : undefined
            }
            onClick={() =>
              void issue(
                'deployment.enter',
                'Enter deployment mode, clear faults, reclaim ownership, and hold GPIO6 low.',
                undefined,
                4000,
              )
            }
          >
            Enter deployment
          </button>
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || !deployment.active || deployment.running}
            title={
              !connected
                ? 'No controller connected.'
                : !deployment.active
                  ? 'Deployment is not active.'
                  : deployment.running
                    ? 'Deployment checklist is running — wait for it to complete.'
                    : undefined
            }
            onClick={() =>
              void issue(
                'deployment.exit',
                'Exit deployment mode and return to non-deployed supervision.',
                undefined,
                4000,
              )
            }
          >
            Exit deployment
          </button>
          <button
            type="button"
            className="action-button is-inline is-accent"
            disabled={!connected || !deployment.active || deployment.running}
            title={
              !connected
                ? 'No controller connected.'
                : !deployment.active
                  ? 'Enter deployment mode before running the checklist.'
                  : deployment.running
                    ? 'Deployment checklist is already running.'
                    : undefined
            }
            onClick={() =>
              void issue(
                'deployment.run',
                'Re-run the deployment checklist, clearing any prior faults.',
                undefined,
                4000,
              )
            }
          >
            Run checklist
          </button>
          <button
            type="button"
            className="action-button is-inline"
            /*
             * Enable on EITHER an active or a latched fault. Was disabled
             * unless `latched` — but the user reported a deadlock: trying
             * to enter deployment was rejected by the firmware because of
             * a transient fault that wasn't latched, and the Clear button
             * was grayed out so they couldn't break the cycle.
             * (2026-04-17)
             */
            disabled={
              !connected ||
              (!fault.latched && fault.activeCode === 'none')
            }
            title={
              !connected
                ? 'No controller connected.'
                : !fault.latched && fault.activeCode === 'none'
                  ? 'No active or latched fault to clear.'
                  : undefined
            }
            onClick={() =>
              void issue('clear_faults', 'Clear all latched faults so deployment can proceed.')
            }
          >
            Clear faults
          </button>
          {/*
            * 2026-04-16 user feature: persist the current bench NIR
            * current and TEC temp/lambda as the deployment defaults.
            * On the next power-up the headless 5-second auto-deploy
            * fires at these saved values. NVS write — operator action.
            */}
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || deployment.running}
            title={
              !connected
                ? 'No controller connected.'
                : deployment.running
                  ? 'Deployment checklist is running — wait for it to complete before saving defaults.'
                  : 'Persist the current NIR current + TEC target as the deployment defaults. Next boot starts the 5-second auto-deploy at these values.'
            }
            onClick={() =>
              void issue(
                'operate.save_deployment_defaults',
                'Save the current NIR current and TEC target as the on-board deployment defaults (NVS).',
              )
            }
          >
            Save deployment defaults
          </button>
        </div>

        {(fault.latched || fault.activeCode !== 'none') && (
          <div
            className="operate-v3__fault-banner"
            data-tone={fault.latched ? 'critical' : 'warn'}
          >
            <AlertTriangle size={16} />
            <div>
              <strong>
                {fault.latched
                  ? `Latched: ${fault.latchedCode} (${fault.latchedClass})`
                  : `Active: ${fault.activeCode} (${fault.activeClass})`}
              </strong>
              {/*
                * Surface the firmware-supplied reason string so the operator
                * can see WHY a fault tripped — added 2026-04-16 to address
                * the "unexpected_state (safety_latched)" opacity bug. Falls
                * back gracefully when older firmware omits the field.
                */}
              {(() => {
                const reason = fault.latched
                  ? fault.latchedReason
                  : fault.activeReason
                if (!reason || reason.length === 0) return null
                return <span className="operate-v3__fault-reason"> — {reason}</span>
              })()}
              {fault.latched &&
                fault.activeCode !== 'none' &&
                fault.activeCode !== fault.latchedCode && (
                  <span> | Also active: {fault.activeCode}</span>
                )}
              <span> | Trips: {fault.tripCounter}</span>
              {fault.triggerDiag !== null && (
                <span className="operate-v3__fault-diag">
                  {' '}| {fault.triggerDiag.expr} (LD rail{' '}
                  {(fault.triggerDiag.ldPgoodForMs / 1000).toFixed(1)} s ·
                  SBDN{' '}
                  {(fault.triggerDiag.sbdnNotOffForMs / 1000).toFixed(1)} s)
                </span>
              )}
            </div>
          </div>
        )}
      </section>

      {/* Two-column body ----------------------------------------------- */}
      <div className="operate-v3__body">
        {/* Left column: checklist + status + setpoint */}
        <div className="operate-v3__main-left">
          <aside className="panel-section">
            <div className="section-head">
              <div>
                <h3>Checklist</h3>
                <p>
                  {deployment.targetMode === 'lambda'
                    ? `${formatNumber(deployment.targetLambdaNm, 1)} nm deployment target. Rows advance live.`
                    : `${formatNumber(deployment.targetTempC, 1)} °C deployment target. Rows advance live.`}
                </p>
              </div>
              <span
                className={
                  deployment.running
                    ? 'status-badge is-warn'
                    : readyIdle
                      ? 'status-badge is-on'
                      : 'status-badge'
                }
              >
                <Clock3 size={14} />
                {activeStep ? activeStep.label : 'Waiting'}
              </span>
            </div>

            <ol className="operate-v3__steps">
              {deployment.steps.map((step, index) => (
                <li
                  key={step.key}
                  className={`operate-v3__step ${stepTone(step.status)}`.trim()}
                  data-active={step.key === activeStep?.key}
                >
                  <span className="operate-v3__step-index">{index + 1}</span>
                  <div>
                    <strong>{step.label}</strong>
                    <small>{step.status.replaceAll('_', ' ')}</small>
                  </div>
                </li>
              ))}
            </ol>

            {deployment.primaryFailureReason && (
              <div className="inline-alert is-critical">
                <strong>{deployment.primaryFailureCode}</strong>
                <p>{deployment.primaryFailureReason}</p>
              </div>
            )}
          </aside>

          {/* Deployment status card */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>Deployment focus</h3>
                <p>Live telemetry from the controller.</p>
              </div>
            </div>

            <div className="operate-v3__focus">
              <div className="operate-v3__focus-head">
                <div>
                  <strong>{activeStep?.label ?? 'No active checklist step'}</strong>
                  <p>
                    Phase {deployment.phase.replaceAll('_', ' ')}.{' '}
                    {readyIdle
                      ? 'Runtime output is idle and available.'
                      : deployment.running
                        ? 'The checklist currently owns the power-up sequence.'
                        : 'Ownership is reclaimed.'}
                  </p>
                </div>
                <span
                  className={
                    readyIdle
                      ? 'status-badge is-on'
                      : deployment.failed
                        ? 'status-badge is-critical'
                        : 'status-badge'
                  }
                >
                  {statusLabel}
                </span>
              </div>

              <dl className="operate-v3__metrics">
                <div>
                  <dt>Sequence</dt>
                  <dd>{deployment.sequenceId}</dd>
                </div>
                <div>
                  <dt>Current step</dt>
                  <dd>{deployment.currentStepIndex}</dd>
                </div>
                <div>
                  <dt>Current cap</dt>
                  <dd>{formatNumber(deployment.maxLaserCurrentA, 2)} A</dd>
                </div>
                <div>
                  <dt>PD</dt>
                  <dd>{pdSnapshotLabel}</dd>
                </div>
              </dl>
            </div>
          </section>

          {/* Runtime setpoint */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>Runtime setpoint</h3>
                <p>Temperature and wavelength stay linked. Commit on slider release, blur, or Enter.</p>
                <p className="inline-help">
                  Buttons always control NIR after deployment. The toggle below adds a parallel
                  host slider for bench testing.
                </p>
              </div>
            </div>

            <div className="segmented is-compact">
              <button
                type="button"
                className={
                  runtimeMode === 'binary_trigger'
                    ? 'segmented__button is-active'
                    : 'segmented__button'
                }
                disabled={
                  !connected ||
                  !liveSnapshot.buttonBoard.mcpReachable ||
                  (!bench.runtimeModeSwitchAllowed && runtimeMode !== 'binary_trigger')
                }
                title={
                  !connected
                    ? 'No controller connected.'
                    : !liveSnapshot.buttonBoard.mcpReachable
                      ? 'Button board (MCP23017 @ 0x20) is not reachable. Confirm the J2 connector before selecting trigger buttons.'
                      : !bench.runtimeModeSwitchAllowed && runtimeMode !== 'binary_trigger'
                        ? bench.runtimeModeLockReason
                        : undefined
                }
                onClick={() =>
                  void issue('operate.set_mode', 'Switch NIR control source to trigger buttons.', {
                    mode: 'binary_trigger',
                  })
                }
              >
                Trigger buttons
              </button>
              <button
                type="button"
                className={
                  runtimeMode === 'modulated_host'
                    ? 'segmented__button is-active'
                    : 'segmented__button'
                }
                disabled={
                  !connected ||
                  (!bench.runtimeModeSwitchAllowed && runtimeMode !== 'modulated_host')
                }
                title={
                  !connected
                    ? 'No controller connected.'
                    : !bench.runtimeModeSwitchAllowed && runtimeMode !== 'modulated_host'
                      ? bench.runtimeModeLockReason
                      : undefined
                }
                onClick={() =>
                  void issue('operate.set_mode', 'Switch NIR control source to host control.', {
                    mode: 'modulated_host',
                  })
                }
              >
                Host control
              </button>
            </div>

            {!bench.runtimeModeSwitchAllowed && (
              <p className="inline-help">{bench.runtimeModeLockReason}</p>
            )}

            {/*
              * Show the trigger card whenever the button board is reachable —
              * buttons are now the post-deployment trigger surface regardless
              * of the runtime_mode toggle (Bug 2 fix 2026-04-17).
              */}
            {liveSnapshot.buttonBoard.mcpReachable && (
              <TriggerControlCard snapshot={liveSnapshot} />
            )}

            <div className="operate-v3__target-grid">
              <div className="operate-v3__slider-field">
                <label className="field">
                  <span>Temperature (°C)</span>
                  <input
                    type="range"
                    min="5"
                    max="65"
                    step="0.1"
                    value={clampTecTempC(
                      parseNumber(runtimeTempC, liveSnapshot.tec.targetTempC),
                    )}
                    disabled={!runtimeControlAllowed}
                    title={runtimeControlBlockReason}
                    onChange={(event) => updateRuntimeTemp(event.target.value)}
                    onMouseUp={() => { void commitRuntimeTarget('temp') }}
                    onTouchEnd={() => { void commitRuntimeTarget('temp') }}
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
                    disabled={!runtimeControlAllowed}
                    title={runtimeControlBlockReason}
                    onChange={(event) => updateRuntimeTemp(event.target.value)}
                    onBlur={() => { void commitRuntimeTarget('temp') }}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') { void commitRuntimeTarget('temp') }
                    }}
                  />
                </label>
              </div>

              <div className="operate-v3__slider-field">
                <label className="field">
                  <span>Wavelength (nm)</span>
                  <input
                    type="range"
                    min="771.2"
                    max="790"
                    step="0.1"
                    value={clampTecWavelengthNm(
                      parseNumber(runtimeLambdaNm, liveSnapshot.tec.targetLambdaNm),
                    )}
                    disabled={!runtimeControlAllowed}
                    title={runtimeControlBlockReason}
                    onChange={(event) => updateRuntimeLambda(event.target.value)}
                    onMouseUp={() => { void commitRuntimeTarget('lambda') }}
                    onTouchEnd={() => { void commitRuntimeTarget('lambda') }}
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
                    disabled={!runtimeControlAllowed}
                    title={runtimeControlBlockReason}
                    onChange={(event) => updateRuntimeLambda(event.target.value)}
                    onBlur={() => { void commitRuntimeTarget('lambda') }}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') { void commitRuntimeTarget('lambda') }
                    }}
                  />
                </label>
              </div>
            </div>
            {!runtimeControlAllowed && runtimeControlBlockReason && (
              <p className="inline-help" role="status">
                {runtimeControlBlockReason}
              </p>
            )}
            {runtimeCommitError && (
              <p className="inline-help" role="alert" data-tone="critical">
                {runtimeCommitError}
              </p>
            )}
          </section>

          </div>

          {/* Right column: NIR + Aux */}
          <div className="operate-v3__main-right">
          {/* NIR output */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>NIR output</h3>
                <p>
                  SBDN is {sbdnStateLabel(readiness.sbdnState)}. PCN selects low (LISL) or high
                  (LISH) current; modulation drives PCN.
                </p>
              </div>
              <span className={`status-badge ${sbdnStateTone(readiness.sbdnState)}`.trim()}>
                {readiness.sbdnState.toUpperCase()}
              </span>
            </div>

            {!nirEnabled && (
              <p className="inline-help" role="status">
                {nirDisabledReason}
              </p>
            )}

            <div className="compact-grid">
              <div className="field field--static">
                <span>Requested current</span>
                <strong>{formatNumber(bench.requestedCurrentA, 2)} A</strong>
              </div>
              <div className="field field--static">
                <span>Applied current</span>
                <strong>{formatNumber(laser.commandedCurrentA, 2)} A</strong>
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
                /*
                 * 2026-04-17 (audit round 3): gate matches the firmware
                 * `is_runtime_control_command` reject path. Previously
                 * disabled only on `!connected`, which let the operator
                 * type + blur a new current value, see it stick on
                 * screen, then discover on page nav that the firmware
                 * had rejected the commit. Now pre-disabled with the
                 * real reason.
                 */
                disabled={!runtimeControlAllowed}
                title={runtimeControlBlockReason}
                onChange={(event) => setCurrentDraft({ dirty: true, value: event.target.value })}
                onMouseUp={() => { void commitRequestedCurrent() }}
                onTouchEnd={() => { void commitRequestedCurrent() }}
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
                  disabled={!runtimeControlAllowed}
                  title={runtimeControlBlockReason}
                  onChange={(event) => setCurrentDraft({ dirty: true, value: event.target.value })}
                  onBlur={() => { void commitRequestedCurrent() }}
                  onKeyDown={(event) => {
                    if (event.key === 'Enter') { void commitRequestedCurrent() }
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
                disabled={!nirEnabled}
                title={nirDisabledReason}
                onClick={() =>
                  void issue(
                    'operate.set_output',
                    bench.requestedNirEnabled
                      ? 'Disable host-controlled NIR output.'
                      : 'Enable host-controlled NIR output.',
                    {
                      enabled: !bench.requestedNirEnabled,
                      current_a: requestedCurrentA,
                    },
                  )
                }
              >
                {bench.requestedNirEnabled ? 'Output off' : 'Output on'}
              </button>
            </div>

            <div className="compact-grid">
              <label className="field">
                <span>Modulation freq (Hz)</span>
                <input
                  type="number"
                  value={modulationFrequencyHz}
                  disabled={!runtimeControlAllowed}
                  title={runtimeControlBlockReason}
                  onChange={(event) =>
                    setModulationDraft((current) => ({
                      ...current,
                      dirty: true,
                      frequencyHz: event.target.value,
                    }))
                  }
                  onBlur={() => { void commitModulation() }}
                  onKeyDown={(event) => {
                    if (event.key === 'Enter') { void commitModulation() }
                  }}
                />
              </label>
              <label className="field">
                <span>Duty (%)</span>
                <input
                  type="number"
                  value={modulationDutyPct}
                  disabled={!runtimeControlAllowed}
                  title={runtimeControlBlockReason}
                  onChange={(event) =>
                    setModulationDraft((current) => ({
                      ...current,
                      dirty: true,
                      dutyPct: event.target.value,
                    }))
                  }
                  onBlur={() => { void commitModulation() }}
                  onKeyDown={(event) => {
                    if (event.key === 'Enter') { void commitModulation() }
                  }}
                />
              </label>
            </div>

            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={modulationEnabled}
                disabled={!runtimeControlAllowed}
                title={runtimeControlBlockReason}
                onChange={(event) => {
                  const nextEnabled = event.target.checked
                  setModulationDraft((current) => ({
                    ...current,
                    dirty: true,
                    enabled: nextEnabled,
                  }))
                  void commitModulation(nextEnabled)
                }}
              />
              <span>Enable PCN modulation</span>
            </label>
          </section>

          {/* Aux lights: green (ungated) + GPIO6 LED */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>Aux lights</h3>
                <p>
                  Green alignment has no software interlock. LED still requires a stable deployment
                  context.
                </p>
              </div>
            </div>

            <div className="operate-v3__aux-grid">
              {/* Green laser — always available */}
              <article className="operate-v3__aux-card">
                <header>
                  <strong>Green laser</strong>
                  <span className="status-badge is-on">No interlock</span>
                </header>
                <dl className="summary-list">
                  <div>
                    <dt>Requested</dt>
                    <dd>{bench.requestedAlignmentEnabled ? 'On' : 'Off'}</dd>
                  </div>
                  <div>
                    <dt>Applied</dt>
                    <dd>{laser.alignmentEnabled ? 'On' : 'Off'}</dd>
                  </div>
                  <div>
                    <dt>Gate</dt>
                    <dd>None (hardware rail is the only gate)</dd>
                  </div>
                </dl>
                <div className="button-row is-compact">
                  <button
                    type="button"
                    className="action-button is-inline"
                    disabled={!greenEnabled}
                    title={greenDisabledReason}
                    onClick={() =>
                      void issue(
                        'operate.set_alignment',
                        bench.requestedAlignmentEnabled
                          ? 'Disable the green-laser request.'
                          : 'Enable the green-laser request.',
                        { enabled: !bench.requestedAlignmentEnabled },
                      )
                    }
                  >
                    {bench.requestedAlignmentEnabled ? 'Green off' : 'Green on'}
                  </button>
                </div>
              </article>

              {/* GPIO6 LED */}
              <article className="operate-v3__aux-card">
                <header>
                  <strong>GPIO6 LED</strong>
                  <span
                    className={
                      ledEnabled
                        ? 'status-badge is-on'
                        : 'status-badge is-warn'
                    }
                  >
                    {ledEnabled ? 'Available' : 'Blocked'}
                  </span>
                </header>
                <dl className="summary-list">
                  <div>
                    <dt>Requested</dt>
                    <dd>
                      {liveSnapshot.buttonBoard.ledOwned
                        ? `${liveSnapshot.buttonBoard.ledBrightnessPct}% (post-deployment)`
                        : bench.requestedLedEnabled
                          ? `${bench.requestedLedDutyCyclePct}%`
                          : 'Off'}
                    </dd>
                  </div>
                  <div>
                    <dt>Applied pin</dt>
                    <dd>{bench.appliedLedPinHigh ? 'High' : 'Low'}</dd>
                  </div>
                  <div>
                    <dt>Owner</dt>
                    <dd>
                      {bench.appliedLedOwner === 'none'
                        ? 'None'
                        : bench.appliedLedOwner.replaceAll('_', ' ')}
                    </dd>
                  </div>
                </dl>

                {!ledEnabled && (
                  <p className="inline-help" role="status">
                    {ledDisabledReason}
                  </p>
                )}

                <label className="field">
                  <span>Brightness (%)</span>
                  <input
                    type="range"
                    min="0"
                    max="100"
                    step="1"
                    value={requestedLedBrightnessPct}
                    disabled={!ledEnabled}
                    title={ledDisabledReason}
                    onChange={(event) => {
                      setLedDraft({ dirty: true, value: event.target.value })
                      scheduleLedCommit()
                    }}
                    onMouseUp={() => scheduleLedCommit(50)}
                    onTouchEnd={() => scheduleLedCommit(50)}
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
                      disabled={!ledEnabled}
                      title={ledDisabledReason}
                      onChange={(event) => setLedDraft({ dirty: true, value: event.target.value })}
                      onBlur={() => scheduleLedCommit(0)}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter') scheduleLedCommit(0)
                      }}
                    />
                  </label>
                  <div className="button-row is-compact">
                    <button
                      type="button"
                      className="action-button is-inline"
                      disabled={!ledEnabled}
                      title={ledDisabledReason}
                      onClick={toggleLed}
                    >
                      {bench.requestedLedEnabled ? 'LED off' : 'LED on'}
                    </button>
                  </div>
                </div>
              </article>
            </div>
          </section>

          </div>

          {/* Full-width below both columns */}
          {/* Ready truth */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>Ready truth</h3>
                <p>Passive PD and hardware readback only. No writes from here.</p>
              </div>
            </div>

            <dl className="operate-v3__truth">
              <div>
                <dt>TEC PGOOD</dt>
                <dd>{readyTruth.tecRailPgoodRaw ? 'raw high' : 'raw low'}</dd>
                <small>{readyTruth.tecRailPgoodFiltered ? 'filtered good' : 'filtered bad'}</small>
              </div>
              <div>
                <dt>LD PGOOD</dt>
                <dd>{readyTruth.ldRailPgoodRaw ? 'raw high' : 'raw low'}</dd>
                <small>{readyTruth.ldRailPgoodFiltered ? 'filtered good' : 'filtered bad'}</small>
              </div>
              <div>
                <dt>TEMPGD</dt>
                <dd>{readyTruth.tecTempGood ? 'high' : 'low'}</dd>
                <small>{readyTruth.tecAnalogPlausible ? 'analog plausible' : 'analog suspect'}</small>
              </div>
              <div>
                <dt>SBDN commanded</dt>
                <dd>{sbdnStateLabel(readiness.sbdnState)}</dd>
                <small>{readyTruth.sbdnHigh ? 'pin observed high' : 'pin observed low'}</small>
              </div>
              <div>
                <dt>PCN</dt>
                <dd>{readyTruth.pcnLow ? 'low (LISL)' : 'high (LISH)'}</dd>
                <small>{readyTruth.driverLoopGood ? 'loop good' : 'loop bad'}</small>
              </div>
              <div>
                <dt>Idle bias</dt>
                <dd>{formatNumber(readyTruth.idleBiasCurrentA, 3)} A</dd>
                <small>Threshold lives on Integrate</small>
              </div>
            </dl>
          </section>

          {/* Causal log */}
          <section className="panel-section">
            <div className="section-head">
              <div>
                <h3>Deployment causal log</h3>
                <p>Primary fault → secondary effects → checklist timeline.</p>
              </div>
            </div>

            <div className="operate-v3__log">
              <article>
                <strong>Primary fault</strong>
                <p>
                  {deployment.primaryFailureReason
                    ? `${deployment.primaryFailureCode}: ${deployment.primaryFailureReason}`
                    : deployment.running
                      ? 'No primary fault recorded while the checklist is running.'
                      : 'No deployment fault is currently recorded.'}
                </p>
              </article>

              <article>
                <strong>Secondary effects</strong>
                {deployment.secondaryEffects.length > 0 ? (
                  <div className="status-stack">
                    {deployment.secondaryEffects.map((effect) => (
                      <span
                        key={`${effect.code}-${effect.atMs}`}
                        className={effectTone(effect)}
                      >
                        {effect.code}: {effect.reason}
                      </span>
                    ))}
                  </div>
                ) : (
                  <p>No secondary effects are attached to the current deployment state.</p>
                )}
              </article>

              <article>
                <strong>Checklist timeline</strong>
                {deploymentTimeline.length > 0 ? (
                  <div className="activity-list">
                    {deploymentTimeline.map((step) => (
                      <div
                        key={`${step.key}-${step.startedAtMs}-${step.completedAtMs}`}
                        className="activity-row"
                      >
                        <div>
                          <strong>{step.label}</strong>
                          <small>
                            start {step.startedAtMs} ms
                            {step.completedAtMs > 0 ? ` / end ${step.completedAtMs} ms` : ''}
                          </small>
                        </div>
                        <p>{step.status.replaceAll('_', ' ')}</p>
                      </div>
                    ))}
                  </div>
                ) : deploymentEventTail.length > 0 ? (
                  <div className="activity-list">
                    {deploymentEventTail.map((event) => (
                      <div key={event.id} className="activity-row">
                        <div>
                          <strong>{event.title}</strong>
                          <small>{event.atIso}</small>
                        </div>
                        <p>{event.detail}</p>
                      </div>
                    ))}
                  </div>
                ) : (
                  <p className="panel-note">
                    No deployment-causal records have been recorded in this session.
                  </p>
                )}
              </article>
            </div>
          </section>
      </div>
    </section>
  )
}

const TRIGGER_PHASE_DESCRIPTION: Record<TriggerPhase, string> = {
  off: 'Pre-deployment / no trigger source.',
  ready: 'Deployment ready. Awaiting stage 1 press.',
  armed: 'Stage 1 held. NIR pre-armed.',
  firing: 'Stage 2 held. NIR emitting.',
  interlock: 'Interlock active. NIR forced safe.',
  lockout: 'Press-and-hold lockout. Release trigger to clear.',
  unrecoverable: 'Unrecoverable fault. Clear faults to recover.',
}

const TRIGGER_PHASE_TONE: Record<TriggerPhase, string> = {
  off: '',
  ready: 'is-on',
  armed: 'is-on',
  firing: 'is-warn',
  interlock: 'is-warn',
  lockout: 'is-warn',
  unrecoverable: 'is-critical',
}

/**
 * TriggerControlCard — Operate workspace, binary-trigger mode.
 *
 * Read-only visualization of the firmware-driven button state machine.
 * No actionable buttons here — the firmware is the safety authority. The
 * card lives in the Runtime setpoint section because it is part of the
 * "how do I drive the laser" decision.
 *
 * Renders:
 *   - The current trigger phase (off / ready / armed / firing / interlock
 *     / lockout / unrecoverable).
 *   - Live stage1 / stage2 / side1 / side2 chips with a CSS active-state
 *     class. No transforms — opacity and color transitions only per
 *     Uncodixfy.
 *   - A 32 px RGB indicator showing the firmware-computed color (with a
 *     `.is-blinking` opacity animation when the firmware blink bit is on).
 *   - The current LED brightness percent and ownership.
 */
function TriggerControlCard(props: { snapshot: DeviceSnapshot }) {
  const { snapshot } = props
  const board = snapshot.buttonBoard
  const buttons = snapshot.buttons
  const phase = board.triggerPhase
  const reachable = board.mcpReachable

  return (
    <div className="operate-trigger-card">
      <div className="operate-trigger-card__head">
        <div>
          <h4>Trigger control</h4>
          <p>
            Firmware-owned. LED auto-enables at 20% on ready. Stage 1 enables
            green alignment. Stage 2 fires NIR (PCN high, SBDN high). Side
            buttons adjust LED brightness ±5% (capped at {snapshot.safety.maxTofLedDutyCyclePct}%).
          </p>
        </div>
        <div className="operate-trigger-card__phase">
          <span className={`status-badge ${TRIGGER_PHASE_TONE[phase]}`.trim()}>
            {phase}
          </span>
        </div>
      </div>

      <p className="inline-help" role="status">
        {reachable
          ? TRIGGER_PHASE_DESCRIPTION[phase]
          : 'Button board MCP23017 @ 0x20 unreachable. Trigger inputs are not observable.'}
      </p>

      <div className="operate-trigger-card__chips">
        <div
          className={
            buttons.stage1Pressed
              ? 'button-chip is-pressed'
              : 'button-chip'
          }
          aria-pressed={buttons.stage1Pressed}
        >
          <span className="button-chip-label">Stage 1</span>
          <span className="button-chip-state">
            {buttons.stage1Pressed ? 'PRESSED' : 'idle'}
          </span>
        </div>
        <div
          className={
            buttons.stage2Pressed
              ? 'button-chip is-pressed'
              : 'button-chip'
          }
          aria-pressed={buttons.stage2Pressed}
        >
          <span className="button-chip-label">Stage 2</span>
          <span className="button-chip-state">
            {buttons.stage2Pressed ? 'PRESSED' : 'idle'}
          </span>
        </div>
        <div
          className={
            buttons.side1Pressed
              ? 'button-chip is-pressed'
              : 'button-chip'
          }
          aria-pressed={buttons.side1Pressed}
        >
          <span className="button-chip-label">Side 1 (+5%)</span>
          <span className="button-chip-state">
            {buttons.side1Pressed ? 'PRESSED' : 'idle'}
          </span>
        </div>
        <div
          className={
            buttons.side2Pressed
              ? 'button-chip is-pressed'
              : 'button-chip'
          }
          aria-pressed={buttons.side2Pressed}
        >
          <span className="button-chip-label">Side 2 (-5%)</span>
          <span className="button-chip-state">
            {buttons.side2Pressed ? 'PRESSED' : 'idle'}
          </span>
        </div>
      </div>

      <dl className="operate-trigger-card__metrics">
        <div>
          <dt>Status LED</dt>
          <dd>
            <span
              className={board.rgb.blink ? 'rgb-indicator is-blinking' : 'rgb-indicator'}
              style={{
                backgroundColor: board.rgb.enabled
                  ? `rgb(${board.rgb.r}, ${board.rgb.g}, ${board.rgb.b})`
                  : 'transparent',
                display: 'inline-block',
                verticalAlign: 'middle',
                marginRight: 8,
                width: 18,
                height: 18,
              }}
              aria-label={`Status LED rgb(${board.rgb.r}, ${board.rgb.g}, ${board.rgb.b})`}
            />
            {board.rgb.enabled
              ? `${board.rgb.r}, ${board.rgb.g}, ${board.rgb.b}${board.rgb.blink ? ' (blink)' : ''}`
              : 'Off'}
          </dd>
        </div>
        <div>
          <dt>Front LED</dt>
          <dd>
            {board.ledOwned
              ? `${board.ledBrightnessPct}% (button-driven)`
              : `${board.ledBrightnessPct}% (other source)`}
          </dd>
        </div>
        <div>
          <dt>NIR lockout</dt>
          <dd>{board.triggerLockout ? 'LATCHED' : 'Clear'}</dd>
        </div>
        {/*
          * 2026-04-16: surface the firmware-computed reason for why a
          * stage1+stage2 press isn't firing NIR. "none" means the gauntlet
          * is open; anything else identifies the gate that's blocking
          * (e.g. "fault: tof_out_of_range", "lockout-latched",
          * "ld-rail-not-good"). Replaces the previous "guess from chip
          * states" debugging path.
          */}
        <div>
          <dt>NIR block reason</dt>
          <dd>{board.nirButtonBlockReason || 'unknown'}</dd>
        </div>
      </dl>
    </div>
  )
}
