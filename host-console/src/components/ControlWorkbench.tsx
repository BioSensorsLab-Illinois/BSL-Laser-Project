import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import {
  Activity,
  Crosshair,
  ScanLine,
  ShieldAlert,
  Sparkles,
  Thermometer,
  Waves,
  Zap,
} from 'lucide-react'

import {
  currentFromOpticalPowerW,
  deriveBenchEstimate,
} from '../lib/bench-model'
import {
  mergeObservedBringupModules,
  moduleMeta,
} from '../lib/bringup'
import { formatNumber } from '../lib/format'
import type { RealtimeTelemetryStore } from '../lib/live-telemetry'
import {
  clampTecTempC,
  clampTecWavelengthNm,
  estimateTempFromWavelengthNm,
  estimateTecVoltageFromTempC,
  estimateWavelengthFromTempC,
  tecCalibrationPoints,
} from '../lib/tec-calibration'
import { useLiveSnapshot } from '../hooks/use-live-snapshot'
import { HelpHint } from './HelpHint'
import type {
  BenchTargetMode,
  BringupModuleStatus,
  DeviceSnapshot,
  ModuleKey,
  TransportKind,
  TransportStatus,
} from '../types'

type ControlWorkbenchProps = {
  snapshot: DeviceSnapshot
  telemetryStore: RealtimeTelemetryStore
  transportKind: TransportKind
  transportStatus: TransportStatus
  transportRecovering: boolean
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

type ModuleAvailability = {
  available: boolean
  state: 'ready' | 'unavailable' | 'issue'
  label: string
  detail: string
}

function joinLabels(labels: string[]): string {
  return labels.join(', ')
}

function describeModuleAvailability(
  label: string,
  dependencies: Array<{ key: ModuleKey; label: string; status: BringupModuleStatus }>,
): ModuleAvailability {
  const missing = dependencies
    .filter((dependency) => !dependency.status.expectedPresent)
    .map((dependency) => dependency.label)
  const undetected = dependencies
    .filter(
      (dependency) =>
        dependency.status.expectedPresent &&
        moduleMeta[dependency.key].validationMode === 'probe' &&
        !dependency.status.detected,
    )
    .map((dependency) => dependency.label)
  const unhealthy = dependencies
    .filter(
      (dependency) =>
        dependency.status.expectedPresent &&
        moduleMeta[dependency.key].validationMode === 'probe' &&
        dependency.status.detected &&
        !dependency.status.healthy,
    )
    .map((dependency) => dependency.label)

  if (missing.length > 0) {
    return {
      available: false,
      state: 'unavailable',
      label: `${label} unavailable`,
      detail: `${joinLabels(missing)} not marked installed on this bench build.`,
    }
  }

  if (undetected.length > 0) {
    return {
      available: false,
      state: 'issue',
      label: `${label} awaiting probe`,
      detail: `Waiting for a successful probe from ${joinLabels(undetected)}.`,
    }
  }

  if (unhealthy.length > 0) {
    return {
      available: false,
      state: 'issue',
      label: `${label} needs attention`,
      detail: `${joinLabels(unhealthy)} reported an unhealthy state.`,
    }
  }

  return {
    available: true,
    state: 'ready',
    label: `${label} ready`,
    detail: 'Installed dependencies are available for staged control.',
  }
}

function availabilityBadgeClass(availability: ModuleAvailability): string {
  if (availability.state === 'ready') {
    return 'status-badge is-on'
  }

  if (availability.state === 'issue') {
    return 'status-badge is-warn'
  }

  return 'status-badge is-off'
}

export function ControlWorkbench({
  snapshot,
  telemetryStore,
  transportKind,
  transportStatus,
  transportRecovering,
  onIssueCommandAwaitAck,
}: ControlWorkbenchProps) {
  const liveSnapshot = useLiveSnapshot(snapshot, telemetryStore)
  const commandReady = transportStatus === 'connected'
  const hasLiveControllerSnapshot = snapshot.identity.firmwareVersion !== 'unavailable'
  const connected =
    commandReady ||
    (hasLiveControllerSnapshot &&
      (transportRecovering || transportStatus === 'connecting'))
  const estimate = useMemo(() => deriveBenchEstimate(liveSnapshot), [liveSnapshot])
  const liveSbdnPin = useMemo(
    () => liveSnapshot.gpioInspector.pins.find((pin) => pin.gpioNum === 13),
    [liveSnapshot.gpioInspector.pins],
  )
  const livePcnPin = useMemo(
    () => liveSnapshot.gpioInspector.pins.find((pin) => pin.gpioNum === 21),
    [liveSnapshot.gpioInspector.pins],
  )
  const controlModules = useMemo(
    () =>
      mergeObservedBringupModules(
        liveSnapshot.bringup.modules,
        liveSnapshot,
        connected,
      ),
    [connected, liveSnapshot],
  )
  const [autoFollowPower, setAutoFollowPower] = useState(false)
  const [laserPowerW, setLaserPowerW] = useState(() => formatNumber(estimate.commandedOpticalPowerW, 2))
  const [targetMode, setTargetMode] = useState<BenchTargetMode>(snapshot.bench.targetMode)
  const [targetTempC, setTargetTempC] = useState(() => formatNumber(snapshot.tec.targetTempC, 1))
  const [targetLambdaNm, setTargetLambdaNm] = useState(() => formatNumber(snapshot.tec.targetLambdaNm, 1))
  const [modulationEnabled, setModulationEnabled] = useState(snapshot.bench.modulationEnabled)
  const [modulationFrequencyHz, setModulationFrequencyHz] = useState(
    String(snapshot.bench.modulationFrequencyHz),
  )
  const [modulationDutyPct, setModulationDutyPct] = useState(
    String(snapshot.bench.modulationDutyCyclePct),
  )
  const lastAutoPowerRequestRef = useRef<string | null>(null)
  const lastSyncedTecDraftRef = useRef({
    targetMode: snapshot.bench.targetMode,
    targetTempC: formatNumber(snapshot.tec.targetTempC, 1),
    targetLambdaNm: formatNumber(snapshot.tec.targetLambdaNm, 1),
  })
  const lastSyncedModulationDraftRef = useRef({
    enabled: snapshot.bench.modulationEnabled,
    frequencyHz: String(snapshot.bench.modulationFrequencyHz),
    dutyPct: String(snapshot.bench.modulationDutyCyclePct),
  })

  const deploymentReady =
    liveSnapshot.deployment.active &&
    liveSnapshot.deployment.ready &&
    !liveSnapshot.deployment.running
  const modulatedHostMode = liveSnapshot.bench.runtimeMode === 'modulated_host'
  const writesDisabled = !commandReady || !deploymentReady
  const laserAvailability = describeModuleAvailability('Laser path', [
    { key: 'laserDriver', label: 'Laser driver', status: controlModules.laserDriver },
    { key: 'dac', label: 'DAC', status: controlModules.dac },
  ])
  const tecAvailability = describeModuleAvailability('TEC target', [
    { key: 'tec', label: 'TEC loop', status: controlModules.tec },
    { key: 'dac', label: 'DAC', status: controlModules.dac },
  ])
  const modulationAvailability = describeModuleAvailability('PCN modulation', [
    { key: 'laserDriver', label: 'Laser driver', status: controlModules.laserDriver },
  ])
  const laserInteractDisabled =
    writesDisabled || !laserAvailability.available || !modulatedHostMode
  const tecInteractDisabled = writesDisabled || !tecAvailability.available
  const modulationInteractDisabled =
    writesDisabled || !modulationAvailability.available || !modulatedHostMode
  const tempRange = {
    min: tecCalibrationPoints[0].tempC,
    max: tecCalibrationPoints[tecCalibrationPoints.length - 1].tempC,
  }
  const wavelengthRange = {
    min: tecCalibrationPoints[0].wavelengthNm,
    max: tecCalibrationPoints[tecCalibrationPoints.length - 1].wavelengthNm,
  }
  const deploymentOpticalCapW =
    liveSnapshot.deployment.active && liveSnapshot.deployment.maxOpticalPowerW > 0
      ? liveSnapshot.deployment.maxOpticalPowerW
      : 5
  const deploymentCurrentCapA =
    liveSnapshot.deployment.active && liveSnapshot.deployment.maxLaserCurrentA > 0
      ? liveSnapshot.deployment.maxLaserCurrentA
      : liveSnapshot.safety.maxLaserCurrentA
  const requestedPowerW = Math.min(
    deploymentOpticalCapW,
    Math.max(0, parseNumber(laserPowerW, estimate.commandedOpticalPowerW)),
  )
  const requestedTempC =
    targetMode === 'temp'
      ? clampTecTempC(parseNumber(targetTempC, liveSnapshot.tec.targetTempC))
      : estimateTempFromWavelengthNm(parseNumber(targetLambdaNm, liveSnapshot.tec.targetLambdaNm))
  const requestedLambdaNm =
    targetMode === 'lambda'
      ? clampTecWavelengthNm(parseNumber(targetLambdaNm, liveSnapshot.tec.targetLambdaNm))
      : estimateWavelengthFromTempC(parseNumber(targetTempC, liveSnapshot.tec.targetTempC))
  const requestedDutyPct = Math.min(
    100,
    Math.max(0, parseNumber(modulationDutyPct, liveSnapshot.bench.modulationDutyCyclePct)),
  )
  const requestedFrequencyHz = Math.min(
    4000,
    Math.max(0, parseNumber(modulationFrequencyHz, liveSnapshot.bench.modulationFrequencyHz)),
  )
  const nirRequested = liveSnapshot.bench.requestedNirEnabled
  const nirActive = liveSnapshot.laser.nirEnabled
  const alignmentActive = liveSnapshot.laser.alignmentEnabled
  const sbdnHigh = (liveSbdnPin?.outputEnabled ?? false) && (liveSbdnPin?.levelHigh ?? false)
  const pcnHigh = (livePcnPin?.outputEnabled ?? false) && (livePcnPin?.levelHigh ?? false)
  const nirActionCmd = nirRequested || nirActive ? 'laser_output_disable' : 'laser_output_enable'
  const nirActionLabel = nirRequested || nirActive ? 'Disable NIR Laser' : 'Enable NIR Laser'
  const nirActionTitle = nirActive
    ? 'Drop the NIR output request and return the beam path safe.'
      : 'Request NIR output through the normal runtime safety gate. If bring-up service control is still active, the request will stay staged until service mode is exited.'
  const deploymentBlockedByUsbOnly =
    liveSnapshot.pd.sourceIsHostOnly || liveSnapshot.pd.sourceVoltageV < 9
  const runtimeModeNote = modulatedHostMode
    ? 'Host-owned NIR output and PCN modulation are available once deployment is ready.'
    : 'Binary trigger mode is selected. Host runtime output controls stay off while the physical trigger path remains blocked pending source-backed wiring.'

  const issueRuntimeControlCommand = useCallback(
    async (
      cmd: string,
      risk: 'read' | 'write' | 'service' | 'firmware',
      note: string,
      args?: Record<string, number | string | boolean>,
    ) => onIssueCommandAwaitAck(cmd, risk, note, args),
    [onIssueCommandAwaitAck],
  )

  useEffect(() => {
    const next = {
      targetMode: liveSnapshot.bench.targetMode,
      targetTempC: formatNumber(liveSnapshot.tec.targetTempC, 1),
      targetLambdaNm: formatNumber(liveSnapshot.tec.targetLambdaNm, 1),
    }
    const previous = lastSyncedTecDraftRef.current

    if (targetMode === previous.targetMode) {
      setTargetMode(next.targetMode)
    }
    if (targetTempC === previous.targetTempC) {
      setTargetTempC(next.targetTempC)
    }
    if (targetLambdaNm === previous.targetLambdaNm) {
      setTargetLambdaNm(next.targetLambdaNm)
    }

    lastSyncedTecDraftRef.current = next
  }, [
    liveSnapshot.bench.targetMode,
    liveSnapshot.tec.targetLambdaNm,
    liveSnapshot.tec.targetTempC,
    targetLambdaNm,
    targetMode,
    targetTempC,
  ])

  useEffect(() => {
    const next = {
      enabled: liveSnapshot.bench.modulationEnabled,
      frequencyHz: String(liveSnapshot.bench.modulationFrequencyHz),
      dutyPct: String(liveSnapshot.bench.modulationDutyCyclePct),
    }
    const previous = lastSyncedModulationDraftRef.current

    if (modulationEnabled === previous.enabled) {
      setModulationEnabled(next.enabled)
    }
    if (modulationFrequencyHz === previous.frequencyHz) {
      setModulationFrequencyHz(next.frequencyHz)
    }
    if (modulationDutyPct === previous.dutyPct) {
      setModulationDutyPct(next.dutyPct)
    }

    lastSyncedModulationDraftRef.current = next
  }, [
    liveSnapshot.bench.modulationDutyCyclePct,
    liveSnapshot.bench.modulationEnabled,
    liveSnapshot.bench.modulationFrequencyHz,
    modulationDutyPct,
    modulationEnabled,
    modulationFrequencyHz,
  ])

  useEffect(() => {
    if (!autoFollowPower || writesDisabled) {
      lastAutoPowerRequestRef.current = null
      return
    }

    const desiredCurrentA = currentFromOpticalPowerW(requestedPowerW)
    if (Math.abs(desiredCurrentA - liveSnapshot.laser.commandedCurrentA) < 0.02) {
      lastAutoPowerRequestRef.current = null
      return
    }

    const requestKey = desiredCurrentA.toFixed(3)
    if (lastAutoPowerRequestRef.current === requestKey) {
      return
    }

    const timerId = window.setTimeout(() => {
      lastAutoPowerRequestRef.current = requestKey
      void issueRuntimeControlCommand(
        'set_laser_power',
        'write',
        'Auto-follow laser power from the host slider without a separate apply step.',
        {
          optical_power_w: requestedPowerW,
          current_a: desiredCurrentA,
        },
      )
    }, 280)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [
    autoFollowPower,
    issueRuntimeControlCommand,
    liveSnapshot.laser.commandedCurrentA,
    requestedPowerW,
    writesDisabled,
  ])

  return (
    <section className="panel-section">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Bench control</p>
          <h2>Laser and TEC bench control</h2>
        </div>
        <p className="panel-note">
          Host requests are staged here. Firmware still owns beam permission and any ambiguous condition resolves beam-off.
        </p>
      </div>

      <div className="control-banner">
        <div className="control-banner__copy">
          <strong>
            {transportKind === 'mock' ? 'Mock rig is writable.' : 'Live board writes stay locally armed.'}
          </strong>
          <p>
            {transportKind === 'mock'
              ? 'Use this to rehearse the control flow before the full hardware stack is populated.'
              : !liveSnapshot.deployment.active
                ? 'Enter deployment mode and complete the deployment checklist before runtime control is allowed.'
                : !deploymentReady
                  ? liveSnapshot.deployment.running
                    ? 'Deployment checklist is still running. Control writes stay locked until the controller reports ready.'
                    : 'Deployment mode is active, but runtime control remains locked until the checklist succeeds.'
                : 'Runtime control requests can be sent without service mode. The GUI expresses intent only; the firmware can still block output.'}
          </p>
        </div>

        <div className="status-badges">
          <span className={connected ? 'status-badge is-on' : 'status-badge'}>
            <Activity size={14} />
            Link {connected ? 'ready' : 'offline'}
          </span>
          <span className={liveSnapshot.deployment.active ? 'status-badge is-on' : 'status-badge'}>
            <ShieldAlert size={14} />
            Deployment {liveSnapshot.deployment.active ? (deploymentReady ? 'ready' : liveSnapshot.deployment.running ? 'running' : 'active') : 'off'}
          </span>
          <span className={liveSnapshot.rails.ld.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Zap size={14} />
            LD rail {liveSnapshot.rails.ld.pgood ? 'PGOOD' : 'waiting'}
          </span>
          <span className={liveSnapshot.rails.tec.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Thermometer size={14} />
            TEC rail {liveSnapshot.rails.tec.pgood ? 'PGOOD' : 'waiting'}
          </span>
        </div>

        <div className="note-strip">
          <span>
            {deploymentReady
              ? `Deployment current cap ${formatNumber(deploymentCurrentCapA, 2)} A / optical cap ${formatNumber(deploymentOpticalCapW, 2)} W.`
              : 'Runtime output actions are locked until deployment succeeds in the current session.'}
          </span>
          <span>
            {deploymentBlockedByUsbOnly
              ? 'USB-only Phase 1 bench detected. Power-dependent checklist steps stay blocked until a real PD source is available.'
              : runtimeModeNote}
          </span>
        </div>
      </div>

      <div className="control-layout">
        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <div className="control-module__title">
              <ShieldAlert size={16} />
              <strong>Runtime mode</strong>
            </div>
            <HelpHint text="The controller now separates binary trigger mode from host-owned modulated mode. Only modulated_host accepts host runtime output requests." />
          </div>

          <div className="control-module__banner">
            <div className="control-module__availability">
              <strong>{liveSnapshot.bench.runtimeMode === 'modulated_host' ? 'modulated_host active' : 'binary_trigger active'}</strong>
              <p>{runtimeModeNote}</p>
            </div>
            <span className={liveSnapshot.bench.runtimeMode === 'modulated_host' ? 'status-badge is-on' : 'status-badge is-warn'}>
              {liveSnapshot.bench.runtimeMode}
            </span>
          </div>

          <div className="segmented is-three">
            <button
              type="button"
              className={liveSnapshot.bench.runtimeMode === 'binary_trigger' ? 'segmented__button is-active' : 'segmented__button'}
              disabled={!commandReady || (!liveSnapshot.bench.runtimeModeSwitchAllowed && liveSnapshot.bench.runtimeMode !== 'binary_trigger')}
              title={
                liveSnapshot.bench.runtimeModeSwitchAllowed || liveSnapshot.bench.runtimeMode === 'binary_trigger'
                  ? 'Use the binary trigger path. Host runtime output buttons remain disabled.'
                  : liveSnapshot.bench.runtimeModeLockReason
              }
              onClick={() =>
                issueRuntimeControlCommand(
                  'set_runtime_mode',
                  'write',
                  'Switch the controller runtime mode to binary_trigger.',
                  { mode: 'binary_trigger' },
                )
              }
            >
              Binary trigger
            </button>
            <button
              type="button"
              className={liveSnapshot.bench.runtimeMode === 'modulated_host' ? 'segmented__button is-active' : 'segmented__button'}
              disabled={!commandReady || (!liveSnapshot.bench.runtimeModeSwitchAllowed && liveSnapshot.bench.runtimeMode !== 'modulated_host')}
              title={
                liveSnapshot.bench.runtimeModeSwitchAllowed || liveSnapshot.bench.runtimeMode === 'modulated_host'
                  ? 'Use host-owned runtime output and PCN modulation.'
                  : liveSnapshot.bench.runtimeModeLockReason
              }
              onClick={() =>
                issueRuntimeControlCommand(
                  'set_runtime_mode',
                  'write',
                  'Switch the controller runtime mode to modulated_host.',
                  { mode: 'modulated_host' },
                )
              }
            >
              Modulated host
            </button>
            <button
              type="button"
              className="segmented__button"
              disabled
              title={liveSnapshot.bench.runtimeModeLockReason || 'Mode changes are only allowed while the laser path is safe-off.'}
            >
              {liveSnapshot.bench.runtimeModeSwitchAllowed ? 'Safe to switch' : 'Switch locked'}
            </button>
          </div>

          {!liveSnapshot.bench.runtimeModeSwitchAllowed ? (
            <p className="inline-help">{liveSnapshot.bench.runtimeModeLockReason}</p>
          ) : null}
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <div className="control-module__title">
              <Zap size={16} />
              <strong>Laser output</strong>
            </div>
            <HelpHint text="Set the high-current LISH operating point. Auto-follow can push updates as the slider moves so a separate Apply step is not needed." />
          </div>

          <div className="control-module__banner">
            <div className="control-module__availability">
              <strong>{laserAvailability.label}</strong>
              <p>
                {modulatedHostMode
                  ? laserAvailability.detail
                  : 'Host-owned laser controls are disabled until the controller is in modulated_host mode.'}
              </p>
            </div>
            <span className={availabilityBadgeClass(laserAvailability)}>{laserAvailability.state}</span>
          </div>

          <div className="control-module__body control-module__body--split">
            <div className={laserAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <label className="arming-toggle is-compact" title="Automatically send set_laser_power when the slider changes.">
                <input
                  type="checkbox"
                  checked={autoFollowPower}
                  disabled={laserInteractDisabled}
                  onChange={(event) => setAutoFollowPower(event.target.checked)}
                />
                <span>Auto-follow optical power setpoint as the slider moves.</span>
              </label>

              <div className="field">
                <div className="field__head">
                  <span>Optical power setpoint</span>
                  <strong>{formatNumber(requestedPowerW, 2)} W</strong>
                </div>
                <input
                  type="range"
                  min="0"
                  max={deploymentOpticalCapW}
                  step="0.1"
                  value={requestedPowerW}
                  disabled={laserInteractDisabled}
                  title="Drag to stage the desired optical output estimate."
                  onChange={(event) => setLaserPowerW(event.target.value)}
                />
                <div className="field__pair">
                  <input
                    type="number"
                    min="0"
                    max={deploymentOpticalCapW}
                    step="0.1"
                    value={laserPowerW}
                    disabled={laserInteractDisabled}
                    title="Type the target optical power in watts."
                    onChange={(event) => setLaserPowerW(event.target.value)}
                  />
                  <span className="inline-help">
                    ~{formatNumber(currentFromOpticalPowerW(requestedPowerW), 2)} A at 3.0 V on the diode, capped by a
                    {` ${formatNumber(deploymentCurrentCapA, 2)} A`} deployment limit.
                  </span>
                </div>
              </div>

              <div className="button-row control-module__actions">
                <button
                  type="button"
                  className="action-button is-inline"
                  title="Apply the current power setting once."
                  disabled={laserInteractDisabled}
                  onClick={() =>
                    issueRuntimeControlCommand(
                      'set_laser_power',
                      'write',
                      'Return ownership from bring-up service mode if needed, then update the high-state laser power setpoint for bench testing.',
                      {
                        optical_power_w: requestedPowerW,
                        current_a: currentFromOpticalPowerW(requestedPowerW),
                      },
                    )
                  }
                >
                  {autoFollowPower ? 'Apply once anyway' : 'Apply power'}
                </button>
                <button
                  type="button"
                  className="action-button is-inline is-accent"
                  title={nirActionTitle}
                  disabled={laserInteractDisabled}
                  onClick={() =>
                    issueRuntimeControlCommand(
                      nirActionCmd,
                      'write',
                      nirRequested || nirActive
                        ? 'Drop the NIR output request and return the beam path safe.'
                        : 'Return ownership from bring-up service mode if needed, then request NIR laser enable through the runtime safety gate.',
                    )
                  }
                >
                  {nirActionLabel}
                </button>
              </div>
              {!modulatedHostMode ? (
                <p className="inline-help">
                  Switch the controller to `modulated_host` to use host-owned NIR output and PCN modulation. Binary trigger mode is reserved for the physical trigger path.
                </p>
              ) : null}
            </div>

            <div className={laserAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <div className="status-badges is-stack">
                <span className={liveSnapshot.safety.allowNir ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <ScanLine size={14} />
                  NIR laser {liveSnapshot.safety.allowNir ? 'permitted' : 'blocked'}
                </span>
                <span className={alignmentActive ? 'status-badge is-on' : 'status-badge'}>
                  <Crosshair size={14} />
                  Green path {alignmentActive ? 'active' : 'idle'}
                </span>
                <span className={liveSnapshot.bench.requestedNirEnabled ? 'status-badge is-on' : 'status-badge'}>
                  <Activity size={14} />
                  NIR request {liveSnapshot.bench.requestedNirEnabled ? 'staged' : 'clear'}
                </span>
                <span className={liveSnapshot.laser.telemetryValid ? liveSnapshot.laser.loopGood ? 'status-badge is-on' : 'status-badge is-warn' : 'status-badge'}>
                  <Sparkles size={14} />
                  Loop {liveSnapshot.laser.telemetryValid ? liveSnapshot.laser.loopGood ? 'good' : 'degraded' : 'off'}
                </span>
                <span className={liveSnapshot.rails.ld.enabled ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Zap size={14} />
                  LD rail {liveSnapshot.rails.ld.enabled ? 'enabled' : 'off'}
                </span>
                <span className={liveSnapshot.rails.ld.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Zap size={14} />
                  LD rail {liveSnapshot.rails.ld.pgood ? 'good' : 'not good'}
                </span>
                <span className={liveSnapshot.rails.tec.enabled ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Thermometer size={14} />
                  TEC rail {liveSnapshot.rails.tec.enabled ? 'enabled' : 'off'}
                </span>
                <span className={liveSnapshot.rails.tec.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Thermometer size={14} />
                  TEC rail {liveSnapshot.rails.tec.pgood ? 'good' : 'not good'}
                </span>
                <span className={sbdnHigh ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <ScanLine size={14} />
                  SBDN {sbdnHigh ? 'high' : 'not high'}
                </span>
                <span className={pcnHigh ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Waves size={14} />
                  PCN {pcnHigh ? 'high' : 'not high'}
                </span>
              </div>

              <div className="metric-grid is-two">
                <div className="metric-card">
                  <span>Commanded current</span>
                  <strong>{formatNumber(liveSnapshot.laser.commandedCurrentA, 2)} A</strong>
                  <small>host requested high-state current</small>
                </div>
                <div className="metric-card">
                  <span>Measured current</span>
                  <strong>{liveSnapshot.laser.telemetryValid ? `${formatNumber(liveSnapshot.laser.measuredCurrentA, 2)} A` : 'OFF / INVALID'}</strong>
                  <small>{liveSnapshot.laser.telemetryValid ? 'driver monitor readback' : 'LD telemetry only becomes valid when LD rail PGOOD is high and SBDN is high'}</small>
                </div>
                <div className="metric-card">
                  <span>Optical estimate</span>
                  <strong>{formatNumber(estimate.averageOpticalPowerW, 2)} W</strong>
                  <small>{liveSnapshot.laser.nirEnabled ? 'beam request active' : nirRequested ? 'request staged, waiting on prerequisites' : 'beam path safe'}</small>
                </div>
                <div className="metric-card">
                  <span>Driver temp</span>
                  <strong>{liveSnapshot.laser.telemetryValid ? `${formatNumber(liveSnapshot.laser.driverTempC, 1)} °C` : 'OFF / INVALID'}</strong>
                  <small>{liveSnapshot.laser.driverStandby ? 'standby asserted' : 'standby released'}</small>
                </div>
              </div>
            </div>
          </div>
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <div className="control-module__title">
              <Thermometer size={16} />
              <strong>TEC target</strong>
            </div>
            <HelpHint text="Choose wavelength-first or temperature-first targeting. The host shows both the estimated lambda and the TMS voltage for faster bench iteration." />
          </div>

          <div className="control-module__banner">
            <div className="control-module__availability">
              <strong>{tecAvailability.label}</strong>
              <p>{tecAvailability.detail}</p>
            </div>
            <span className={availabilityBadgeClass(tecAvailability)}>{tecAvailability.state}</span>
          </div>

          <div className="control-module__body control-module__body--split">
            <div className={tecAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <div className="segmented is-three">
                <button
                  type="button"
                  className={targetMode === 'lambda' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={tecInteractDisabled}
                  title="Stage wavelength and let the host estimate temperature."
                  onClick={() => setTargetMode('lambda')}
                >
                  Wavelength
                </button>
                <button
                  type="button"
                  className={targetMode === 'temp' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={tecInteractDisabled}
                  title="Stage temperature directly."
                  onClick={() => setTargetMode('temp')}
                >
                  Temperature
                </button>
                <button
                  type="button"
                  className="segmented__button"
                  disabled={tecInteractDisabled}
                  title="Overwrite the editor with the latest live controller targets."
                  onClick={() => {
                    setTargetMode(liveSnapshot.bench.targetMode)
                    setTargetLambdaNm(formatNumber(liveSnapshot.tec.targetLambdaNm, 1))
                    setTargetTempC(formatNumber(liveSnapshot.tec.targetTempC, 1))
                  }}
                >
                  Sync live
                </button>
              </div>

              {targetMode === 'lambda' ? (
                <div className="field">
                  <div className="field__head">
                    <span>Target wavelength</span>
                    <strong>{formatNumber(requestedLambdaNm, 1)} nm</strong>
                  </div>
                  <input
                    type="range"
                    min={wavelengthRange.min}
                    max={wavelengthRange.max}
                    step="0.1"
                    value={requestedLambdaNm}
                    disabled={tecInteractDisabled}
                    title="Drag to stage the wavelength target."
                    onChange={(event) => setTargetLambdaNm(event.target.value)}
                  />
                  <div className="field__pair">
                    <input
                      type="number"
                      min={wavelengthRange.min}
                      max={wavelengthRange.max}
                      step="0.1"
                      value={targetLambdaNm}
                      disabled={tecInteractDisabled}
                      title="Type the target wavelength in nanometers."
                      onChange={(event) => setTargetLambdaNm(event.target.value)}
                    />
                    <span className="inline-help">
                      maps to ~{formatNumber(requestedTempC, 1)} °C and {formatNumber(estimateTecVoltageFromTempC(requestedTempC), 3)} V on TMS.
                    </span>
                  </div>
                </div>
              ) : (
                <div className="field">
                  <div className="field__head">
                    <span>Target TEC temperature</span>
                    <strong>{formatNumber(requestedTempC, 1)} °C</strong>
                  </div>
                  <input
                    type="range"
                    min={tempRange.min}
                    max={tempRange.max}
                    step="0.1"
                    value={requestedTempC}
                    disabled={tecInteractDisabled}
                    title="Drag to stage the TEC target temperature."
                    onChange={(event) => setTargetTempC(event.target.value)}
                  />
                  <div className="field__pair">
                    <input
                      type="number"
                      min={tempRange.min}
                      max={tempRange.max}
                      step="0.1"
                      value={targetTempC}
                      disabled={tecInteractDisabled}
                      title="Type the target TEC temperature in degrees Celsius."
                      onChange={(event) => setTargetTempC(event.target.value)}
                    />
                    <span className="inline-help">
                      estimates {formatNumber(requestedLambdaNm, 1)} nm and {formatNumber(estimateTecVoltageFromTempC(requestedTempC), 3)} V.
                    </span>
                  </div>
                </div>
              )}

              <button
                type="button"
                className="action-button is-inline"
                title="Apply the current TEC target using the selected target mode."
                disabled={tecInteractDisabled}
                onClick={() =>
                  targetMode === 'lambda'
                    ? issueRuntimeControlCommand(
                        'set_target_lambda',
                        'write',
                        'Return ownership from bring-up service mode if needed, then set the wavelength target from the host bench panel.',
                        { lambda_nm: requestedLambdaNm },
                      )
                    : issueRuntimeControlCommand(
                        'set_target_temp',
                        'write',
                        'Return ownership from bring-up service mode if needed, then set the TEC temperature target from the host bench panel.',
                        { temp_c: requestedTempC },
                      )
                }
              >
                Apply TEC target
              </button>
            </div>

            <div className={tecAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <div className="metric-grid is-two">
                <div className="metric-card">
                  <span>Actual lambda</span>
                  <strong>{liveSnapshot.tec.telemetryValid ? `${formatNumber(liveSnapshot.tec.actualLambdaNm, 2)} nm` : 'OFF / INVALID'}</strong>
                  <small>{liveSnapshot.tec.telemetryValid ? `${formatNumber(liveSnapshot.safety.lambdaDriftNm, 2)} nm drift from target` : 'TEC telemetry is only valid when TEC rail PGOOD is high'}</small>
                </div>
                <div className="metric-card">
                  <span>TEC temp</span>
                  <strong>{liveSnapshot.tec.telemetryValid ? `${formatNumber(liveSnapshot.tec.tempC, 2)} °C` : 'OFF / INVALID'}</strong>
                  <small>{liveSnapshot.tec.telemetryValid ? (liveSnapshot.tec.tempGood ? 'settled' : 'still settling') : 'No valid TEC readback while the rail is off'}</small>
                </div>
                <div className="metric-card">
                  <span>Temp ADC</span>
                  <strong>{liveSnapshot.tec.telemetryValid ? `${formatNumber(liveSnapshot.tec.tempAdcVoltageV, 3)} V` : 'OFF / INVALID'}</strong>
                  <small>trip at {formatNumber(liveSnapshot.safety.tecTempAdcTripV, 3)} V</small>
                </div>
                <div className="metric-card">
                  <span>TEC rail</span>
                  <strong>{liveSnapshot.rails.tec.pgood ? 'PGOOD' : 'Waiting'}</strong>
                  <small>{liveSnapshot.tec.telemetryValid ? `${formatNumber(liveSnapshot.tec.voltageV, 2)} V at ${formatNumber(liveSnapshot.tec.currentA, 2)} A` : 'Rail off or not yet valid'}</small>
                </div>
              </div>
            </div>
          </div>
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <div className="control-module__title">
              <Waves size={16} />
              <strong>PCN modulation</strong>
            </div>
            <HelpHint text="Bench PWM on the PCN pin. On this board LISL is fixed at ground, so modulation only stages enable, frequency, and duty. When modulation is off, PCN returns to normal digital high or low control." />
          </div>

          <div className="control-module__banner">
            <div className="control-module__availability">
              <strong>{modulationAvailability.label}</strong>
              <p>
                {modulatedHostMode
                  ? modulationAvailability.detail
                  : 'PCN modulation is disabled until the controller is in modulated_host mode.'}
              </p>
            </div>
            <span className={availabilityBadgeClass(modulationAvailability)}>{modulationAvailability.state}</span>
          </div>

          <div className={modulationAvailability.available ? 'control-module__body' : 'control-module__body is-muted'}>
            <label className="arming-toggle is-compact" title="Enable PCN-based high/low current modulation.">
              <input
                  type="checkbox"
                  checked={modulationEnabled}
                  disabled={modulationInteractDisabled}
                onChange={(event) => setModulationEnabled(event.target.checked)}
              />
              <span>Use PWM on PCN for LD bench modulation. LISL stays fixed low on this board.</span>
            </label>

            <div className="field-grid">
              <label className="field">
                <span>Frequency (DC to 4 kHz)</span>
                <input
                  type="number"
                  min="0"
                  max="4000"
                  step="1"
                  value={modulationFrequencyHz}
                  disabled={modulationInteractDisabled}
                  title="Set the PCN modulation frequency. Use 0 for a DC/static leg selection."
                  onChange={(event) => setModulationFrequencyHz(event.target.value)}
                />
                <span className="inline-help">
                  {requestedFrequencyHz === 0
                    ? 'DC mode. Duty below 50% drives LISL/low, 50% or above drives LISH/high.'
                    : 'PWM mode. GPIO21 is temporarily owned by the hardware PWM block during modulation.'}
                </span>
              </label>
            </div>

            <div className="field">
              <div className="field__head">
                <span>Duty cycle</span>
                <strong>{formatNumber(requestedDutyPct, 0)}%</strong>
              </div>
              <input
                type="range"
                min="0"
                max="100"
                step="1"
                value={requestedDutyPct}
                disabled={modulationInteractDisabled}
                title="Drag to stage the modulation duty cycle."
                onChange={(event) => setModulationDutyPct(event.target.value)}
              />
            </div>

            <button
              type="button"
              className="action-button is-inline"
              title="Apply the staged PCN modulation profile."
              disabled={modulationInteractDisabled}
              onClick={() =>
                issueRuntimeControlCommand(
                  'configure_modulation',
                  'write',
                  'Return ownership from bring-up service mode if needed, then configure PCN PWM modulation from the host bench panel.',
                  {
                    enabled: modulationEnabled,
                    frequency_hz: requestedFrequencyHz,
                    duty_cycle_pct: requestedDutyPct,
                  },
                )
              }
            >
              Apply modulation
            </button>
            {!modulatedHostMode ? (
              <p className="inline-help">
                Binary trigger mode reserves the laser path for the physical trigger hardware. Switch back to `modulated_host` before using PCN modulation.
              </p>
            ) : null}
          </div>
        </article>

        <article className="panel-cutout control-module">
          <div className="cutout-head">
            <div className="control-module__title">
              <Sparkles size={16} />
              <strong>Operating estimate</strong>
            </div>
            <HelpHint text="Power estimates assume 90% driver and TEC efficiency. Laser optical power uses the 5 W diode model at 3 V and 5 A full load." />
          </div>

          <div className="metric-grid">
            <div className={laserAvailability.available ? 'metric-card' : 'metric-card is-muted'}>
              <span>Optical output</span>
              <strong>{formatNumber(estimate.averageOpticalPowerW, 2)} W</strong>
              <small>{liveSnapshot.laser.nirEnabled ? 'average live output' : 'beam path off'}</small>
            </div>
            <div className={laserAvailability.available ? 'metric-card' : 'metric-card is-muted'}>
              <span>Laser electrical</span>
              <strong>{formatNumber(estimate.laserElectricalPowerW, 2)} W</strong>
              <small>diode side at 3.0 V nominal</small>
            </div>
            <div className={laserAvailability.available ? 'metric-card' : 'metric-card is-muted'}>
              <span>Laser input draw</span>
              <strong>{formatNumber(estimate.laserInputPowerW, 2)} W</strong>
              <small>assuming 90% driver efficiency</small>
            </div>
            <div className={tecAvailability.available ? 'metric-card' : 'metric-card is-muted'}>
              <span>TEC electrical</span>
              <strong>{formatNumber(estimate.tecElectricalPowerW, 2)} W</strong>
              <small>{formatNumber(liveSnapshot.tec.currentA, 2)} A × {formatNumber(liveSnapshot.tec.voltageV, 2)} V</small>
            </div>
            <div className={tecAvailability.available ? 'metric-card' : 'metric-card is-muted'}>
              <span>TEC cooling power</span>
              <strong>{formatNumber(estimate.tecCoolingPowerW, 2)} W</strong>
              <small>assuming 90% conversion efficiency</small>
            </div>
            <div className="metric-card">
              <span>Total wall draw</span>
              <strong>{formatNumber(estimate.totalEstimatedInputPowerW, 2)} W</strong>
              <small>{formatNumber(estimate.pdHeadroomW, 2)} W PD headroom remaining</small>
            </div>
          </div>

          <div className="status-badges">
            <span className={laserAvailability.available ? liveSnapshot.laser.nirEnabled ? 'status-badge is-on' : 'status-badge' : 'status-badge is-off'}>
              <ScanLine size={14} />
              NIR laser {liveSnapshot.laser.nirEnabled ? 'on' : 'off'}
            </span>
            <span className={laserAvailability.available ? alignmentActive ? 'status-badge is-on' : 'status-badge' : 'status-badge is-off'}>
              <Crosshair size={14} />
              Green laser {alignmentActive ? 'on' : 'off'}
            </span>
            <span className={tecAvailability.available ? liveSnapshot.tec.tempGood ? 'status-badge is-on' : 'status-badge is-warn' : 'status-badge is-off'}>
              <Thermometer size={14} />
              TEC {liveSnapshot.tec.tempGood ? 'settled' : 'settling'}
            </span>
            <span className={tecAvailability.available ? liveSnapshot.safety.lambdaDriftBlocked ? 'status-badge is-warn' : 'status-badge is-on' : 'status-badge is-off'}>
              <Sparkles size={14} />
              Lambda drift {liveSnapshot.safety.lambdaDriftBlocked ? 'tripped' : 'stable'}
            </span>
            <span className={tecAvailability.available ? liveSnapshot.safety.tecTempAdcBlocked ? 'status-badge is-warn' : 'status-badge is-on' : 'status-badge is-off'}>
              <Thermometer size={14} />
              Temp ADC {liveSnapshot.safety.tecTempAdcBlocked ? 'high' : 'clear'}
            </span>
            <span className={liveSnapshot.fault.latched ? 'status-badge is-critical' : 'status-badge is-on'}>
              <ShieldAlert size={14} />
              Fault {liveSnapshot.fault.latched ? liveSnapshot.fault.activeCode : liveSnapshot.fault.activeCode === 'none' ? 'clear' : liveSnapshot.fault.activeCode}
            </span>
          </div>
        </article>

      </div>

      <div className="command-footer">
        <div className="inline-token">
          <span>Target mode: {targetMode === 'lambda' ? 'wavelength-first' : 'temperature-first'}</span>
        </div>
        <div className="inline-token">
          <span>TEC target voltage estimate: {formatNumber(estimate.targetTecVoltageV, 3)} V</span>
        </div>
        <div className="inline-token">
          <span>PD headroom: {formatNumber(estimate.pdHeadroomW, 2)} W</span>
        </div>
      </div>
    </section>
  )
}
