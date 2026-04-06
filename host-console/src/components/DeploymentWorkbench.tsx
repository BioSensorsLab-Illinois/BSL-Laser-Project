import { useEffect, useMemo, useState } from 'react'
import { Activity, ShieldAlert, Thermometer, Zap } from 'lucide-react'

import { formatNumber } from '../lib/format'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import type {
  DeploymentTargetMode,
  DeviceSnapshot,
  SessionEvent,
  TransportStatus,
} from '../types'

type DeploymentWorkbenchProps = {
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

type SafetyDraft = {
  horizonThresholdDeg: string
  horizonHysteresisDeg: string
  tofMinRangeM: string
  tofMaxRangeM: string
  tofHysteresisM: string
  imuStaleMs: string
  tofStaleMs: string
  railGoodTimeoutMs: string
  lambdaDriftLimitNm: string
  lambdaDriftHysteresisNm: string
  lambdaDriftHoldMs: string
  ldOvertempLimitC: string
  tecTempAdcTripV: string
  tecTempAdcHysteresisV: string
  tecTempAdcHoldMs: string
  tecMinCommandC: string
  tecMaxCommandC: string
  tecReadyToleranceC: string
  maxLaserCurrentA: string
}

const deploymentStepDetails: Record<
  string,
  {
    summary: string
    validates: string
  }
> = {
  ownership_reclaim: {
    summary: 'Reclaims firmware ownership from bring-up paths, clears overrides, and forces a safe-off posture before any deployment logic advances.',
    validates: 'Service mode is no longer active, GPIO overrides are gone, and PWM or direct bench-drive paths are no longer holding hardware state.',
  },
  pd_inspect: {
    summary: 'Interrogates the live USB-PD sink contract without rewriting PDOs and confirms that a source capable of at least 9 V is present.',
    validates: 'A live PD contract exists and the negotiated source voltage is high enough for deployment rail sequencing.',
  },
  power_cap: {
    summary: 'Derives the runtime laser current cap from the negotiated PD budget so the runtime page cannot request more than the source can sustain.',
    validates: 'The remaining deployment power headroom stays positive after TEC and peripheral reserve are removed.',
  },
  outputs_off: {
    summary: 'Checks that every controlled output is still in the expected off posture before peripheral initialization continues.',
    validates: 'SBDN, PCN, LD enable, TEC enable, ERM enable, green enable, ToF illumination, and PWM are all in the safe deployment posture.',
  },
  stabilize_3v3: {
    summary: 'Waits for the local low-voltage supply and board peripherals to settle before DAC and bus devices are trusted.',
    validates: 'Only elapsed stabilization time; this step prevents races right after deployment entry.',
  },
  dac_safe_zero: {
    summary: 'Initializes the DAC, drives LD command to 0 V, sets the TEC target channel to the default deployment point, and reads back the written registers.',
    validates: 'The DAC is reachable, configured, alarm-free, and the LD/TEC channel readback matches the expected safe initialization.',
  },
  peripherals_verify: {
    summary: 'Verifies the IMU, ToF, and haptic peripherals with live readback before rail sequencing is allowed.',
    validates: 'The IMU identity/config registers, ToF boot and sensor IDs, and haptic reachability are all present and sane.',
  },
  rail_sequence: {
    summary: 'Enables the TEC rail first, then the LD rail, and waits for the expected power-good indications in order.',
    validates: 'TEC PGOOD appears before LD PGOOD and each rail satisfies the configured rail-good timeout.',
  },
  tec_settle: {
    summary: 'Holds the deployment target until TEC good, analog plausibility, and thermal drift checks indicate a stable runtime starting point.',
    validates: 'TEC good is asserted and the analog thermal channels remain plausible and within the configured deployment envelope.',
  },
  ready_posture: {
    summary: 'Temporarily drives the laser driver into its active-current posture to validate LPGD, then returns to the low-current idle posture used by the Control page.',
    validates: 'SBDN stays ON, PCN goes high long enough to confirm LPGD, then firmware returns PCN low while keeping both rails up for Control-page handoff.',
  },
}

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function makeSafetyDraft(snapshot: DeviceSnapshot): SafetyDraft {
  return {
    horizonThresholdDeg: formatNumber(snapshot.safety.horizonThresholdDeg, 1),
    horizonHysteresisDeg: formatNumber(snapshot.safety.horizonHysteresisDeg, 1),
    tofMinRangeM: formatNumber(snapshot.safety.tofMinRangeM, 3),
    tofMaxRangeM: formatNumber(snapshot.safety.tofMaxRangeM, 3),
    tofHysteresisM: formatNumber(snapshot.safety.tofHysteresisM, 3),
    imuStaleMs: String(snapshot.safety.imuStaleMs),
    tofStaleMs: String(snapshot.safety.tofStaleMs),
    railGoodTimeoutMs: String(snapshot.safety.railGoodTimeoutMs),
    lambdaDriftLimitNm: formatNumber(snapshot.safety.lambdaDriftLimitNm, 2),
    lambdaDriftHysteresisNm: formatNumber(snapshot.safety.lambdaDriftHysteresisNm, 2),
    lambdaDriftHoldMs: String(snapshot.safety.lambdaDriftHoldMs),
    ldOvertempLimitC: formatNumber(snapshot.safety.ldOvertempLimitC, 1),
    tecTempAdcTripV: formatNumber(snapshot.safety.tecTempAdcTripV, 3),
    tecTempAdcHysteresisV: formatNumber(snapshot.safety.tecTempAdcHysteresisV, 3),
    tecTempAdcHoldMs: String(snapshot.safety.tecTempAdcHoldMs),
    tecMinCommandC: formatNumber(snapshot.safety.tecMinCommandC, 1),
    tecMaxCommandC: formatNumber(snapshot.safety.tecMaxCommandC, 1),
    tecReadyToleranceC: formatNumber(snapshot.safety.tecReadyToleranceC, 2),
    maxLaserCurrentA: formatNumber(snapshot.safety.maxLaserCurrentA, 2),
  }
}

function rowClass(status: string): string {
  if (status === 'passed') {
    return 'is-pass'
  }
  if (status === 'failed') {
    return 'is-fail'
  }
  if (status === 'in_progress') {
    return 'is-warn'
  }
  return ''
}

export function DeploymentWorkbench({
  snapshot,
  telemetryStore,
  events,
  transportStatus,
  onIssueCommandAwaitAck,
}: DeploymentWorkbenchProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const connected = transportStatus === 'connected'
  const deployment = liveSnapshot.deployment
  const usbOnlyPowerBlocked =
    liveSnapshot.pd.sourceIsHostOnly || liveSnapshot.pd.sourceVoltageV < 9
  const [targetMode, setTargetMode] = useState<DeploymentTargetMode>(deployment.targetMode)
  const [targetTempC, setTargetTempC] = useState(() => formatNumber(deployment.targetTempC, 1))
  const [targetLambdaNm, setTargetLambdaNm] = useState(() =>
    formatNumber(deployment.targetLambdaNm, 1),
  )
  const [targetDirty, setTargetDirty] = useState(false)
  const [safetyDraft, setSafetyDraft] = useState<SafetyDraft>(() =>
    makeSafetyDraft(liveSnapshot),
  )
  const [safetyDirty, setSafetyDirty] = useState(false)

  useEffect(() => {
    if (!targetDirty || !deployment.active) {
      setTargetMode(deployment.targetMode)
      setTargetTempC(formatNumber(deployment.targetTempC, 1))
      setTargetLambdaNm(formatNumber(deployment.targetLambdaNm, 1))
      setTargetDirty(false)
    }
  }, [
    deployment.active,
    deployment.targetLambdaNm,
    deployment.targetMode,
    deployment.targetTempC,
    targetDirty,
  ])

  useEffect(() => {
    if (!safetyDirty || !deployment.active) {
      setSafetyDraft(makeSafetyDraft(liveSnapshot))
      setSafetyDirty(false)
    }
  }, [deployment.active, liveSnapshot, safetyDirty])

  const checklistSteps = useMemo(
    () => deployment.steps,
    [deployment.steps],
  )
  const deploymentLogEvents = useMemo(() => {
    const keywords = ['deployment', 'pgood', 'rail', 'loop-good', 'loop good', 'tec rail', 'ld rail']

    return events
      .filter((event) => {
        const haystack = `${event.category} ${event.title} ${event.detail}`.toLowerCase()
        return (
          event.category === 'deploy' ||
          event.category === 'fault' ||
          keywords.some((keyword) => haystack.includes(keyword))
        )
      })
      .slice(0, 6)
  }, [events])

  async function enterDeploymentMode() {
    await onIssueCommandAwaitAck(
      'enter_deployment_mode',
      'service',
      'Enter deployment mode and reclaim ownership safely from bring-up service paths.',
      undefined,
      { timeoutMs: 3000 },
    )
  }

  async function exitDeploymentMode() {
    await onIssueCommandAwaitAck(
      'exit_deployment_mode',
      'service',
      'Exit deployment mode and return the controller to non-deployed safe supervision.',
      undefined,
      { timeoutMs: 3000 },
    )
  }

  async function runDeploymentSequence() {
    await onIssueCommandAwaitAck(
      'run_deployment_sequence',
      'service',
      'Start the deployment checklist and follow each step live as firmware advances through the sequence.',
      undefined,
      { timeoutMs: 4000 },
    )
  }

  async function applyDeploymentTarget() {
    const result = await onIssueCommandAwaitAck(
      'set_deployment_target',
      'write',
      'Set the deployment temperature or wavelength target for checklist execution and ready posture.',
      targetMode === 'lambda'
        ? { target_mode: 'lambda', lambda_nm: parseNumber(targetLambdaNm, deployment.targetLambdaNm) }
        : { target_mode: 'temp', temp_c: parseNumber(targetTempC, deployment.targetTempC) },
      { timeoutMs: 3000 },
    )
    if (result.ok) {
      setTargetDirty(false)
    }
  }

  async function applyDeploymentSafety() {
    const result = await onIssueCommandAwaitAck(
      'set_deployment_safety',
      'write',
      'Apply deployment safety thresholds and timeouts live in firmware.',
      {
        horizon_threshold_deg: parseNumber(
          safetyDraft.horizonThresholdDeg,
          liveSnapshot.safety.horizonThresholdDeg,
        ),
        horizon_hysteresis_deg: parseNumber(
          safetyDraft.horizonHysteresisDeg,
          liveSnapshot.safety.horizonHysteresisDeg,
        ),
        tof_min_range_m: parseNumber(safetyDraft.tofMinRangeM, liveSnapshot.safety.tofMinRangeM),
        tof_max_range_m: parseNumber(safetyDraft.tofMaxRangeM, liveSnapshot.safety.tofMaxRangeM),
        tof_hysteresis_m: parseNumber(
          safetyDraft.tofHysteresisM,
          liveSnapshot.safety.tofHysteresisM,
        ),
        imu_stale_ms: parseNumber(safetyDraft.imuStaleMs, liveSnapshot.safety.imuStaleMs),
        tof_stale_ms: parseNumber(safetyDraft.tofStaleMs, liveSnapshot.safety.tofStaleMs),
        rail_good_timeout_ms: parseNumber(
          safetyDraft.railGoodTimeoutMs,
          liveSnapshot.safety.railGoodTimeoutMs,
        ),
        lambda_drift_limit_nm: parseNumber(
          safetyDraft.lambdaDriftLimitNm,
          liveSnapshot.safety.lambdaDriftLimitNm,
        ),
        lambda_drift_hysteresis_nm: parseNumber(
          safetyDraft.lambdaDriftHysteresisNm,
          liveSnapshot.safety.lambdaDriftHysteresisNm,
        ),
        lambda_drift_hold_ms: parseNumber(
          safetyDraft.lambdaDriftHoldMs,
          liveSnapshot.safety.lambdaDriftHoldMs,
        ),
        ld_overtemp_limit_c: parseNumber(
          safetyDraft.ldOvertempLimitC,
          liveSnapshot.safety.ldOvertempLimitC,
        ),
        tec_temp_adc_trip_v: parseNumber(
          safetyDraft.tecTempAdcTripV,
          liveSnapshot.safety.tecTempAdcTripV,
        ),
        tec_temp_adc_hysteresis_v: parseNumber(
          safetyDraft.tecTempAdcHysteresisV,
          liveSnapshot.safety.tecTempAdcHysteresisV,
        ),
        tec_temp_adc_hold_ms: parseNumber(
          safetyDraft.tecTempAdcHoldMs,
          liveSnapshot.safety.tecTempAdcHoldMs,
        ),
        tec_min_command_c: parseNumber(
          safetyDraft.tecMinCommandC,
          liveSnapshot.safety.tecMinCommandC,
        ),
        tec_max_command_c: parseNumber(
          safetyDraft.tecMaxCommandC,
          liveSnapshot.safety.tecMaxCommandC,
        ),
        tec_ready_tolerance_c: parseNumber(
          safetyDraft.tecReadyToleranceC,
          liveSnapshot.safety.tecReadyToleranceC,
        ),
        max_laser_current_a: parseNumber(
          safetyDraft.maxLaserCurrentA,
          liveSnapshot.safety.maxLaserCurrentA,
        ),
      },
      { timeoutMs: 3500 },
    )
    if (result.ok) {
      setSafetyDirty(false)
    }
  }

  return (
    <section className="panel-section">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Inline deployment</p>
          <h2>Pre-enable checklist</h2>
        </div>
        <p className="panel-note">
          Firmware still owns deployment sequencing. This section now lives on the Control page so checklist state, failure reason, target edits, and runtime controls stay in one place.
        </p>
      </div>

      <div className="control-banner">
        <div className="control-banner__copy">
          <strong>
            {deployment.ready
              ? 'Deployment ready for runtime control.'
              : deployment.running
                ? 'Deployment checklist running.'
                : deployment.active
                  ? 'Deployment mode active.'
                  : 'Deployment mode off.'}
          </strong>
          <p>
            {deployment.failed && deployment.failureReason.trim().length > 0
              ? deployment.failureReason
              : deployment.ready
                ? 'Control-page runtime actions are unlocked in this session. Bring-up and service writes stay locked.'
                : 'Deployment mode is the only supported path from validated bring-up hardware into normal runtime operation.'}
          </p>
          {usbOnlyPowerBlocked ? (
            <p>
              USB-only Phase 1 bench detected. Power-independent checks can still be exercised, but PD, TEC rail, LD rail, and final ready posture remain intentionally blocked here.
            </p>
          ) : null}
        </div>
        <div className="status-badges">
          <span className={connected ? 'status-badge is-on' : 'status-badge'}>
            <Activity size={14} />
            Link {connected ? 'ready' : 'offline'}
          </span>
          <span className={deployment.active ? 'status-badge is-on' : 'status-badge'}>
            <ShieldAlert size={14} />
            Deployment {deployment.active ? 'active' : 'off'}
          </span>
          <span className={deployment.ready ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Zap size={14} />
            Runtime {deployment.ready ? 'unlocked' : 'locked'}
          </span>
          <span className={liveSnapshot.tec.tempGood ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Thermometer size={14} />
            TEC {liveSnapshot.tec.tempGood ? 'settled' : 'waiting'}
          </span>
        </div>
        <div className="button-row">
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || deployment.active}
            onClick={() => {
              void enterDeploymentMode()
            }}
          >
            Enter deployment mode
          </button>
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || !deployment.active || deployment.running}
            onClick={() => {
              void exitDeploymentMode()
            }}
          >
            {deployment.ready ? 'Power down and exit deployment' : 'Exit deployment mode'}
          </button>
          <button
            type="button"
            className="action-button is-inline is-accent"
            disabled={!connected || !deployment.active || deployment.running}
            onClick={() => {
              void runDeploymentSequence()
            }}
          >
            Run deployment checklist
          </button>
        </div>
      </div>

      <div className="control-layout">
        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <strong>Deployment target</strong>
          </div>
          <div className="segmented is-three">
            <button
              type="button"
              className={targetMode === 'temp' ? 'segmented__button is-active' : 'segmented__button'}
              disabled={!deployment.active || deployment.running || deployment.ready}
              onClick={() => {
                setTargetMode('temp')
                setTargetDirty(true)
              }}
            >
              Temperature
            </button>
            <button
              type="button"
              className={targetMode === 'lambda' ? 'segmented__button is-active' : 'segmented__button'}
              disabled={!deployment.active || deployment.running || deployment.ready}
              onClick={() => {
                setTargetMode('lambda')
                setTargetDirty(true)
              }}
            >
              Wavelength
            </button>
            <button
              type="button"
              className="segmented__button"
              onClick={() => {
                setTargetMode(deployment.targetMode)
                setTargetTempC(formatNumber(deployment.targetTempC, 1))
                setTargetLambdaNm(formatNumber(deployment.targetLambdaNm, 1))
                setTargetDirty(false)
              }}
            >
              Sync live
            </button>
          </div>
          <div className="field-grid">
            <label className="field">
              <span>Target temp (C)</span>
              <input
                type="number"
                value={targetTempC}
                disabled={!deployment.active || deployment.running || deployment.ready}
                onChange={(event) => {
                  setTargetTempC(event.target.value)
                  setTargetDirty(true)
                }}
              />
            </label>
            <label className="field">
              <span>Target lambda (nm)</span>
              <input
                type="number"
                value={targetLambdaNm}
                disabled={!deployment.active || deployment.running || deployment.ready}
                onChange={(event) => {
                  setTargetLambdaNm(event.target.value)
                  setTargetDirty(true)
                }}
              />
            </label>
          </div>
          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline"
              disabled={!connected || !deployment.active || deployment.running || deployment.ready}
              onClick={() => {
                void applyDeploymentTarget()
              }}
            >
              Apply deployment target
            </button>
          </div>
          <div className="metric-grid is-two">
            <div className="metric-card">
              <span>Current target</span>
              <strong>{formatNumber(deployment.targetTempC, 1)} °C</strong>
              <small>{formatNumber(deployment.targetLambdaNm, 1)} nm</small>
            </div>
            <div className="metric-card">
              <span>Deployment cap</span>
              <strong>{formatNumber(deployment.maxLaserCurrentA, 2)} A</strong>
              <small>{formatNumber(deployment.maxOpticalPowerW, 2)} W optical cap</small>
            </div>
          </div>
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <strong>Checklist</strong>
          </div>
          <p className="panel-note">
            Open any row for the exact action and validation gate. The checklist stays blocking and sequential in firmware; this page only reflects progress.
          </p>
          <div className="checklist">
            {checklistSteps.map((step) => (
              <details
                key={step.key}
                className={`deployment-step ${rowClass(step.status)}`.trim()}
                open={step.status === 'failed' || step.status === 'in_progress'}
              >
                <summary className={`check-row deployment-step__summary ${rowClass(step.status)}`.trim()}>
                  <div className="check-row__copy">
                    <strong className="check-row__label">{step.label}</strong>
                    <span className="check-row__note">
                      {deploymentStepDetails[step.key]?.summary ?? step.key.replaceAll('_', ' ')}
                    </span>
                  </div>
                  <em>{step.status.replaceAll('_', ' ')}</em>
                </summary>
                <div className="deployment-step__body">
                  <p>{deploymentStepDetails[step.key]?.validates ?? 'No additional validation note recorded for this deployment step.'}</p>
                  {step.status === 'failed' ? (
                    <div className="deployment-step__failure">
                      <strong>Failure detail</strong>
                      <p>
                        {deployment.failureCode !== 'none'
                          ? `${deployment.failureCode}: ${deployment.failureReason || 'The controller reported a deployment failure without a reason string.'}`
                          : deployment.failureReason || 'The controller reported this checklist step as failed, but no failure string was attached.'}
                      </p>
                    </div>
                  ) : null}
                </div>
              </details>
            ))}
          </div>
        </article>
      </div>

      <article className="panel-cutout">
        <div className="cutout-head">
          <strong>Safety parameters</strong>
        </div>
        <div className="field-grid">
          {(
            [
              ['Horizon threshold (deg)', 'horizonThresholdDeg'],
              ['Horizon hysteresis (deg)', 'horizonHysteresisDeg'],
              ['ToF min range (m)', 'tofMinRangeM'],
              ['ToF max range (m)', 'tofMaxRangeM'],
              ['ToF hysteresis (m)', 'tofHysteresisM'],
              ['IMU stale (ms)', 'imuStaleMs'],
              ['ToF stale (ms)', 'tofStaleMs'],
              ['Rail good timeout (ms)', 'railGoodTimeoutMs'],
              ['Lambda drift limit (nm)', 'lambdaDriftLimitNm'],
              ['Lambda drift hysteresis (nm)', 'lambdaDriftHysteresisNm'],
              ['Lambda drift hold (ms)', 'lambdaDriftHoldMs'],
              ['LD overtemp limit (C)', 'ldOvertempLimitC'],
              ['TEC temp ADC trip (V)', 'tecTempAdcTripV'],
              ['TEC temp ADC hysteresis (V)', 'tecTempAdcHysteresisV'],
              ['TEC temp ADC hold (ms)', 'tecTempAdcHoldMs'],
              ['TEC min command (C)', 'tecMinCommandC'],
              ['TEC max command (C)', 'tecMaxCommandC'],
              ['TEC ready tolerance (C)', 'tecReadyToleranceC'],
              ['Max laser current (A)', 'maxLaserCurrentA'],
            ] as Array<[string, keyof SafetyDraft]>
          ).map(([label, key]) => (
            <label key={key} className="field">
              <span>{label}</span>
              <input
                type="text"
                value={safetyDraft[key]}
                disabled={!deployment.active || deployment.running}
                onChange={(event) => {
                  setSafetyDraft((current) => ({
                    ...current,
                    [key]: event.target.value,
                  }))
                  setSafetyDirty(true)
                }}
              />
            </label>
          ))}
        </div>
        <div className="button-row is-compact">
          <button
            type="button"
            className="action-button is-inline"
            disabled={!deployment.active || deployment.running}
            onClick={() => {
              setSafetyDraft(makeSafetyDraft(liveSnapshot))
              setSafetyDirty(false)
            }}
          >
            Sync live safety
          </button>
        </div>
        <div className="button-row is-compact">
          <button
            type="button"
            className="action-button is-inline"
            disabled={!connected || !deployment.active || deployment.running}
            onClick={() => {
              void applyDeploymentSafety()
            }}
          >
            Apply deployment safety
          </button>
        </div>
      </article>

      <article className="panel-cutout">
        <div className="cutout-head">
          <strong>Read-only deployment telemetry</strong>
        </div>
        <div className="metric-grid">
          <div className="metric-card">
            <span>PD source</span>
            <strong>{formatNumber(liveSnapshot.pd.sourceVoltageV, 1)} V</strong>
            <small>{formatNumber(liveSnapshot.pd.sourceCurrentA, 2)} A · {formatNumber(liveSnapshot.pd.negotiatedPowerW, 1)} W</small>
          </div>
          <div className="metric-card">
            <span>Rails</span>
            <strong>{liveSnapshot.rails.tec.pgood ? 'TEC good' : 'TEC bad'} / {liveSnapshot.rails.ld.pgood ? 'LD good' : 'LD bad'}</strong>
            <small>{liveSnapshot.rails.tec.enabled ? 'TEC enabled' : 'TEC off'} · {liveSnapshot.rails.ld.enabled ? 'LD enabled' : 'LD off'}</small>
          </div>
          <div className="metric-card">
            <span>TEC state</span>
            <strong>{liveSnapshot.tec.tempGood ? 'settled' : 'settling'}</strong>
            <small>{formatNumber(liveSnapshot.tec.tempC, 2)} °C · {formatNumber(liveSnapshot.tec.actualLambdaNm, 2)} nm</small>
          </div>
          <div className="metric-card">
            <span>Loop and standby</span>
            <strong>{liveSnapshot.laser.loopGood ? 'loop good' : 'loop bad'}</strong>
            <small>{liveSnapshot.laser.driverStandby ? 'standby asserted' : 'operate asserted'}</small>
          </div>
          <div className="metric-card">
            <span>Interlocks</span>
            <strong>{liveSnapshot.safety.allowNir ? 'NIR permitted' : 'NIR blocked'}</strong>
            <small>horizon {liveSnapshot.safety.horizonBlocked ? 'blocked' : 'clear'} · range {liveSnapshot.safety.distanceBlocked ? 'blocked' : 'clear'}</small>
          </div>
          <div className="metric-card">
            <span>Deployment failure</span>
            <strong>{deployment.failureCode}</strong>
            <small>{deployment.failureReason || 'No deployment failure recorded.'}</small>
          </div>
        </div>
      </article>

      <article className="panel-cutout">
        <div className="cutout-head">
          <strong>Recent deployment logs</strong>
        </div>
        {deploymentLogEvents.length > 0 ? (
          <div className="deployment-log-list">
            {deploymentLogEvents.map((event) => (
              <div key={event.id} className="deployment-log-entry">
                <div>
                  <strong>{event.title}</strong>
                  <span>{event.category}</span>
                </div>
                <p>{event.detail}</p>
              </div>
            ))}
          </div>
        ) : (
          <p className="panel-note">
            No deployment-specific logs captured in this session yet.
          </p>
        )}
      </article>
    </section>
  )
}
