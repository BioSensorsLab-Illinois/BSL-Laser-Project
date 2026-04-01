import { useEffect, useMemo, useRef, useState } from 'react'
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
import { formatNumber } from '../lib/format'
import {
  clampTecTempC,
  clampTecWavelengthNm,
  estimateTempFromWavelengthNm,
  estimateTecVoltageFromTempC,
  estimateWavelengthFromTempC,
  tecCalibrationPoints,
} from '../lib/tec-calibration'
import { HelpHint } from './HelpHint'
import type {
  BenchTargetMode,
  BringupModuleStatus,
  DeviceSnapshot,
  TransportKind,
  TransportStatus,
} from '../types'

type ControlWorkbenchProps = {
  snapshot: DeviceSnapshot
  transportKind: TransportKind
  transportStatus: TransportStatus
  onIssueCommand: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
  ) => Promise<void>
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
  dependencies: Array<{ label: string; status: BringupModuleStatus }>,
): ModuleAvailability {
  const missing = dependencies
    .filter((dependency) => !dependency.status.expectedPresent)
    .map((dependency) => dependency.label)
  const undetected = dependencies
    .filter(
      (dependency) => dependency.status.expectedPresent && !dependency.status.detected,
    )
    .map((dependency) => dependency.label)
  const unhealthy = dependencies
    .filter(
      (dependency) =>
        dependency.status.expectedPresent &&
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
    detail: 'Installed modules are detected and healthy.',
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
  transportKind,
  transportStatus,
  onIssueCommand,
}: ControlWorkbenchProps) {
  const estimate = useMemo(() => deriveBenchEstimate(snapshot), [snapshot])
  const [serviceArmed, setServiceArmed] = useState(false)
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
  const [lowStateCurrentA, setLowStateCurrentA] = useState(
    formatNumber(snapshot.bench.lowStateCurrentA, 2),
  )
  const lastAutoPowerRequestRef = useRef<string | null>(null)

  const connected = transportStatus === 'connected'
  const writesDisabled = !connected || !serviceArmed
  const laserAvailability = describeModuleAvailability('Laser path', [
    { label: 'Laser driver', status: snapshot.bringup.modules.laserDriver },
    { label: 'DAC', status: snapshot.bringup.modules.dac },
  ])
  const tecAvailability = describeModuleAvailability('TEC target', [
    { label: 'TEC loop', status: snapshot.bringup.modules.tec },
    { label: 'DAC', status: snapshot.bringup.modules.dac },
  ])
  const modulationAvailability = describeModuleAvailability('PCN modulation', [
    { label: 'Laser driver', status: snapshot.bringup.modules.laserDriver },
  ])
  const laserInteractDisabled = writesDisabled || !laserAvailability.available
  const tecInteractDisabled = writesDisabled || !tecAvailability.available
  const modulationInteractDisabled = writesDisabled || !modulationAvailability.available
  const tempRange = {
    min: tecCalibrationPoints[0].tempC,
    max: tecCalibrationPoints[tecCalibrationPoints.length - 1].tempC,
  }
  const wavelengthRange = {
    min: tecCalibrationPoints[0].wavelengthNm,
    max: tecCalibrationPoints[tecCalibrationPoints.length - 1].wavelengthNm,
  }
  const requestedPowerW = Math.min(5, Math.max(0, parseNumber(laserPowerW, estimate.commandedOpticalPowerW)))
  const requestedTempC =
    targetMode === 'temp'
      ? clampTecTempC(parseNumber(targetTempC, snapshot.tec.targetTempC))
      : estimateTempFromWavelengthNm(parseNumber(targetLambdaNm, snapshot.tec.targetLambdaNm))
  const requestedLambdaNm =
    targetMode === 'lambda'
      ? clampTecWavelengthNm(parseNumber(targetLambdaNm, snapshot.tec.targetLambdaNm))
      : estimateWavelengthFromTempC(parseNumber(targetTempC, snapshot.tec.targetTempC))
  const requestedDutyPct = Math.min(
    99,
    Math.max(1, parseNumber(modulationDutyPct, snapshot.bench.modulationDutyCyclePct)),
  )
  const requestedFrequencyHz = Math.min(
    50000,
    Math.max(10, parseNumber(modulationFrequencyHz, snapshot.bench.modulationFrequencyHz)),
  )
  const requestedLowStateCurrentA = Math.min(
    currentFromOpticalPowerW(requestedPowerW),
    Math.max(0, parseNumber(lowStateCurrentA, snapshot.bench.lowStateCurrentA)),
  )

  useEffect(() => {
    if (!autoFollowPower || writesDisabled) {
      lastAutoPowerRequestRef.current = null
      return
    }

    const desiredCurrentA = currentFromOpticalPowerW(requestedPowerW)
    if (Math.abs(desiredCurrentA - snapshot.laser.commandedCurrentA) < 0.02) {
      lastAutoPowerRequestRef.current = null
      return
    }

    const requestKey = desiredCurrentA.toFixed(3)
    if (lastAutoPowerRequestRef.current === requestKey) {
      return
    }

    const timerId = window.setTimeout(() => {
      lastAutoPowerRequestRef.current = requestKey
      void onIssueCommand(
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
    onIssueCommand,
    requestedPowerW,
    snapshot.laser.commandedCurrentA,
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
            {transportKind === 'mock' ? 'Mock rig is writable.' : 'Live board writes stay service-gated.'}
          </strong>
          <p>
            {transportKind === 'mock'
              ? 'Use this to rehearse the control flow before the full hardware stack is populated.'
              : 'Use service mode for writes. The GUI expresses intent only; the firmware can still block output.'}
          </p>
        </div>

        <div className="status-badges">
          <span className={connected ? 'status-badge is-on' : 'status-badge'}>
            <Activity size={14} />
            Link {connected ? 'ready' : 'offline'}
          </span>
          <span className={snapshot.bringup.serviceModeActive ? 'status-badge is-on' : 'status-badge'}>
            <ShieldAlert size={14} />
            Service {snapshot.bringup.serviceModeActive ? 'active' : 'required'}
          </span>
          <span className={snapshot.rails.ld.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Zap size={14} />
            LD rail {snapshot.rails.ld.pgood ? 'PGOOD' : 'waiting'}
          </span>
          <span className={snapshot.rails.tec.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
            <Thermometer size={14} />
            TEC rail {snapshot.rails.tec.pgood ? 'PGOOD' : 'waiting'}
          </span>
        </div>

        <div className="button-row">
          <button
            type="button"
            className="action-button is-inline"
            title="Request service mode so bench-control mutations are accepted."
            disabled={!connected || snapshot.bringup.serviceModeActive}
            onClick={() =>
              onIssueCommand(
                'enter_service_mode',
                'service',
                'Enter service mode before bench-side control and tuning.',
              )
            }
          >
            Enter service mode
          </button>
          <button
            type="button"
            className="action-button is-inline"
            title="Leave service mode and return to normal safe supervision."
            disabled={!connected || !snapshot.bringup.serviceModeActive}
            onClick={() =>
              onIssueCommand(
                'exit_service_mode',
                'service',
                'Exit service mode and return to normal safe supervision.',
              )
            }
          >
            Exit service mode
          </button>
        </div>

        <label className="arming-toggle is-compact" title="Local arming gate before service writes are sent from this GUI.">
          <input
            type="checkbox"
            checked={serviceArmed}
            onChange={(event) => setServiceArmed(event.target.checked)}
          />
          <span>Bench terminated, eyewear on, interlocks verified, and ready to send service commands.</span>
        </label>
      </div>

      <div className="control-layout">
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
              <p>{laserAvailability.detail}</p>
            </div>
            <span className={availabilityBadgeClass(laserAvailability)}>{laserAvailability.state}</span>
          </div>

          <div className="control-module__body control-module__body--split">
            <div className={laserAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <label className="arming-toggle is-compact" title="Automatically send set_laser_power when the slider changes.">
                <input
                  type="checkbox"
                  checked={autoFollowPower}
                  disabled={!laserAvailability.available}
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
                  max="5"
                  step="0.1"
                  value={requestedPowerW}
                  disabled={!laserAvailability.available}
                  title="Drag to stage the desired optical output estimate."
                  onChange={(event) => setLaserPowerW(event.target.value)}
                />
                <div className="field__pair">
                  <input
                    type="number"
                    min="0"
                    max="5"
                    step="0.1"
                    value={laserPowerW}
                    disabled={!laserAvailability.available}
                    title="Type the target optical power in watts."
                    onChange={(event) => setLaserPowerW(event.target.value)}
                  />
                  <span className="inline-help">
                    ~{formatNumber(currentFromOpticalPowerW(requestedPowerW), 2)} A at 3.0 V on the diode, capped by a
                    {` ${formatNumber(snapshot.safety.maxLaserCurrentA, 2)} A`} safety limit.
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
                    onIssueCommand(
                      'set_laser_power',
                      'write',
                      'Update the high-state laser power setpoint for bench testing.',
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
                  title="Request NIR output through the firmware safety gate."
                  disabled={laserInteractDisabled}
                  onClick={() =>
                    onIssueCommand(
                      'laser_output_enable',
                      'service',
                      'Request NIR laser enable through the firmware gate.',
                    )
                  }
                >
                  Enable NIR Laser
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  title="Force the host-side NIR request low."
                  disabled={!connected || !laserAvailability.available}
                  onClick={() =>
                    onIssueCommand(
                      'laser_output_disable',
                      'write',
                      'Drop the NIR output request and return the beam path safe.',
                    )
                  }
                >
                  Laser safe off
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  title="Request the green alignment laser for terminated bench aiming checks."
                  disabled={laserInteractDisabled}
                  onClick={() =>
                    onIssueCommand(
                      'enable_alignment',
                      'service',
                      'Request the green alignment laser for terminated bench aiming checks.',
                    )
                  }
                >
                  Green laser on
                </button>
                <button
                  type="button"
                  className="action-button is-inline"
                  title="Drop any host-requested green alignment laser output."
                  disabled={!connected || !laserAvailability.available}
                  onClick={() =>
                    onIssueCommand(
                      'disable_alignment',
                      'write',
                      'Drop any host-requested green alignment laser output.',
                    )
                  }
                >
                  Green laser off
                </button>
              </div>
            </div>

            <div className={laserAvailability.available ? 'control-module__column' : 'control-module__column is-muted'}>
              <div className="status-badges is-stack">
                <span className={snapshot.safety.allowNir ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <ScanLine size={14} />
                  NIR laser {snapshot.safety.allowNir ? 'permitted' : 'blocked'}
                </span>
                <span className={snapshot.laser.loopGood ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Sparkles size={14} />
                  Loop {snapshot.laser.loopGood ? 'good' : 'degraded'}
                </span>
                <span className={snapshot.rails.ld.pgood ? 'status-badge is-on' : 'status-badge is-warn'}>
                  <Zap size={14} />
                  LD rail {snapshot.rails.ld.pgood ? 'good' : 'not good'}
                </span>
              </div>

              <div className="metric-grid is-two">
                <div className="metric-card">
                  <span>Commanded current</span>
                  <strong>{formatNumber(snapshot.laser.commandedCurrentA, 2)} A</strong>
                  <small>host requested high-state current</small>
                </div>
                <div className="metric-card">
                  <span>Measured current</span>
                  <strong>{formatNumber(snapshot.laser.measuredCurrentA, 2)} A</strong>
                  <small>driver monitor readback</small>
                </div>
                <div className="metric-card">
                  <span>Optical estimate</span>
                  <strong>{formatNumber(estimate.averageOpticalPowerW, 2)} W</strong>
                  <small>{snapshot.laser.nirEnabled ? 'beam request active' : 'beam path safe'}</small>
                </div>
                <div className="metric-card">
                  <span>Driver temp</span>
                  <strong>{formatNumber(snapshot.laser.driverTempC, 1)} °C</strong>
                  <small>{snapshot.laser.driverStandby ? 'standby asserted' : 'standby released'}</small>
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
                  disabled={!tecAvailability.available}
                  title="Stage wavelength and let the host estimate temperature."
                  onClick={() => setTargetMode('lambda')}
                >
                  Wavelength
                </button>
                <button
                  type="button"
                  className={targetMode === 'temp' ? 'segmented__button is-active' : 'segmented__button'}
                  disabled={!tecAvailability.available}
                  title="Stage temperature directly."
                  onClick={() => setTargetMode('temp')}
                >
                  Temperature
                </button>
                <button
                  type="button"
                  className="segmented__button"
                  disabled={!tecAvailability.available}
                  title="Overwrite the editor with the latest live controller targets."
                  onClick={() => {
                    setTargetMode(snapshot.bench.targetMode)
                    setTargetLambdaNm(formatNumber(snapshot.tec.targetLambdaNm, 1))
                    setTargetTempC(formatNumber(snapshot.tec.targetTempC, 1))
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
                    disabled={!tecAvailability.available}
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
                      disabled={!tecAvailability.available}
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
                    disabled={!tecAvailability.available}
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
                      disabled={!tecAvailability.available}
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
                    ? onIssueCommand(
                        'set_target_lambda',
                        'write',
                        'Set the wavelength target from the host bench panel.',
                        { lambda_nm: requestedLambdaNm },
                      )
                    : onIssueCommand(
                        'set_target_temp',
                        'write',
                        'Set the TEC temperature target from the host bench panel.',
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
                  <strong>{formatNumber(snapshot.tec.actualLambdaNm, 2)} nm</strong>
                  <small>{formatNumber(snapshot.safety.lambdaDriftNm, 2)} nm drift from target</small>
                </div>
                <div className="metric-card">
                  <span>TEC temp</span>
                  <strong>{formatNumber(snapshot.tec.tempC, 2)} °C</strong>
                  <small>{snapshot.tec.tempGood ? 'settled' : 'still settling'}</small>
                </div>
                <div className="metric-card">
                  <span>Temp ADC</span>
                  <strong>{formatNumber(snapshot.tec.tempAdcVoltageV, 3)} V</strong>
                  <small>trip at {formatNumber(snapshot.safety.tecTempAdcTripV, 3)} V</small>
                </div>
                <div className="metric-card">
                  <span>TEC rail</span>
                  <strong>{snapshot.rails.tec.pgood ? 'PGOOD' : 'Waiting'}</strong>
                  <small>{formatNumber(snapshot.tec.voltageV, 2)} V at {formatNumber(snapshot.tec.currentA, 2)} A</small>
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
            <HelpHint text="Bench PWM by switching between LISH and LISL using the PCN pin. This is staged through the firmware so the hard safety path still owns SBDN." />
          </div>

          <div className="control-module__banner">
            <div className="control-module__availability">
              <strong>{modulationAvailability.label}</strong>
              <p>{modulationAvailability.detail}</p>
            </div>
            <span className={availabilityBadgeClass(modulationAvailability)}>{modulationAvailability.state}</span>
          </div>

          <div className={modulationAvailability.available ? 'control-module__body' : 'control-module__body is-muted'}>
            <label className="arming-toggle is-compact" title="Enable PCN-based high/low current modulation.">
              <input
                type="checkbox"
                checked={modulationEnabled}
                disabled={!modulationAvailability.available}
                onChange={(event) => setModulationEnabled(event.target.checked)}
              />
              <span>Switch between LISH and LISL using the PCN pin for bench PWM testing.</span>
            </label>

            <div className="field-grid">
              <label className="field">
                <span>Frequency (Hz)</span>
                <input
                  type="number"
                  min="10"
                  max="50000"
                  step="10"
                  value={modulationFrequencyHz}
                  disabled={!modulationAvailability.available}
                  title="Set the PCN modulation frequency in hertz."
                  onChange={(event) => setModulationFrequencyHz(event.target.value)}
                />
              </label>
              <label className="field">
                <span>LISL current (A)</span>
                <input
                  type="number"
                  min="0"
                  max={currentFromOpticalPowerW(requestedPowerW)}
                  step="0.05"
                  value={lowStateCurrentA}
                  disabled={!modulationAvailability.available}
                  title="Set the low-state current used when PCN selects LISL."
                  onChange={(event) => setLowStateCurrentA(event.target.value)}
                />
              </label>
            </div>

            <div className="field">
              <div className="field__head">
                <span>Duty cycle</span>
                <strong>{formatNumber(requestedDutyPct, 0)}%</strong>
              </div>
              <input
                type="range"
                min="1"
                max="99"
                step="1"
                value={requestedDutyPct}
                disabled={!modulationAvailability.available}
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
                onIssueCommand(
                  'configure_modulation',
                  'write',
                  'Configure PCN-based high/low current modulation from the host bench panel.',
                  {
                    enabled: modulationEnabled,
                    frequency_hz: requestedFrequencyHz,
                    duty_cycle_pct: requestedDutyPct,
                    low_current_a: requestedLowStateCurrentA,
                  },
                )
              }
            >
              Apply modulation
            </button>
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
              <small>{snapshot.laser.nirEnabled ? 'average live output' : 'beam path off'}</small>
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
              <small>{formatNumber(snapshot.tec.currentA, 2)} A × {formatNumber(snapshot.tec.voltageV, 2)} V</small>
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
            <span className={laserAvailability.available ? snapshot.laser.nirEnabled ? 'status-badge is-on' : 'status-badge' : 'status-badge is-off'}>
              <ScanLine size={14} />
              NIR laser {snapshot.laser.nirEnabled ? 'on' : 'off'}
            </span>
            <span className={laserAvailability.available ? snapshot.laser.alignmentEnabled ? 'status-badge is-on' : 'status-badge' : 'status-badge is-off'}>
              <Crosshair size={14} />
              Green laser {snapshot.laser.alignmentEnabled ? 'on' : 'off'}
            </span>
            <span className={tecAvailability.available ? snapshot.tec.tempGood ? 'status-badge is-on' : 'status-badge is-warn' : 'status-badge is-off'}>
              <Thermometer size={14} />
              TEC {snapshot.tec.tempGood ? 'settled' : 'settling'}
            </span>
            <span className={tecAvailability.available ? snapshot.safety.lambdaDriftBlocked ? 'status-badge is-warn' : 'status-badge is-on' : 'status-badge is-off'}>
              <Sparkles size={14} />
              Lambda drift {snapshot.safety.lambdaDriftBlocked ? 'tripped' : 'stable'}
            </span>
            <span className={tecAvailability.available ? snapshot.safety.tecTempAdcBlocked ? 'status-badge is-warn' : 'status-badge is-on' : 'status-badge is-off'}>
              <Thermometer size={14} />
              Temp ADC {snapshot.safety.tecTempAdcBlocked ? 'high' : 'clear'}
            </span>
            <span className={snapshot.fault.latched ? 'status-badge is-critical' : 'status-badge is-on'}>
              <ShieldAlert size={14} />
              Fault {snapshot.fault.latched ? snapshot.fault.activeCode : snapshot.fault.activeCode === 'none' ? 'clear' : snapshot.fault.activeCode}
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
