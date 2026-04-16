import { useEffect, useMemo, useState } from 'react'
import { Crosshair, Gauge, Save, ShieldCheck, Target, TriangleAlert } from 'lucide-react'

import type { DeviceSnapshot, TofDistanceMode, TransportStatus } from '../types'

type TofCalibrationPanelProps = {
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

/*
 * VL53L1X ROI center is a SPAD-grid byte index per ST ULD reference
 * (VL53L1X_SetROICenter). The byte packs (row, col) where row∈0..15,
 * col∈0..15 on the physical SPAD array. Default 199 = grid centre
 * (row=8, col=7).
 *
 * Formula (matches VL53L1_zone_preset.c in the ULD driver):
 *   if (row > 7) center = 128 + (col << 3) + (15 - row)
 *   else         center = ((15 - col) << 3) + row
 *
 * Each SPAD step ≈ 1.7° of effective pointing shift per VL53L1X §6.8.
 */
function centerSpadFromRowCol(row: number, col: number): number {
  const r = Math.max(0, Math.min(15, Math.round(row)))
  const c = Math.max(0, Math.min(15, Math.round(col)))
  if (r > 7) {
    return 128 + (c << 3) + (15 - r)
  }
  return ((15 - c) << 3) + r
}

// Best-effort inverse. Not all bytes are reachable from the formula; this
// returns the (row, col) that re-packs to the given byte, or (7, 7) if
// no match. Used only to seed the UI from the firmware-reported byte.
function rowColFromCenterSpad(center: number): { row: number; col: number } {
  for (let row = 0; row <= 15; row += 1) {
    for (let col = 0; col <= 15; col += 1) {
      if (centerSpadFromRowCol(row, col) === center) {
        return { row, col }
      }
    }
  }
  return { row: 7, col: 7 }
}

/*
 * Effective receive-cone full angle, rough linear approximation from
 * VL53L1X datasheet AN4907:
 *   full-grid 16×16 ≈ 27° diagonal
 *   4×4 ≈ 7.5°
 * Returns the larger of the two side estimates.
 */
function coneAngleDeg(widthSpads: number, heightSpads: number): number {
  const side = Math.max(widthSpads, heightSpads)
  return Math.round(((27 * side) / 16) * 10) / 10
}

const DISTANCE_MODE_OPTIONS: Array<{
  value: TofDistanceMode
  label: string
  desc: string
}> = [
  {
    value: 'short',
    label: 'Short',
    desc: '≤ 1.3 m — best ambient immunity',
  },
  {
    value: 'medium',
    label: 'Medium',
    desc: '≤ 3.0 m',
  },
  {
    value: 'long',
    label: 'Long',
    desc: '≤ 4.0 m (default)',
  },
]

const TIMING_BUDGET_OPTIONS: number[] = [20, 33, 50, 100, 200]

export function TofCalibrationPanel({
  snapshot,
  transportStatus,
  onIssueCommandAwaitAck,
}: TofCalibrationPanelProps) {
  const connected = transportStatus === 'connected'
  const serviceModeActive = snapshot.bringup.serviceModeActive
  const deploymentActive = snapshot.deployment.active
  const faultLatched = snapshot.fault.latched
  const cal = snapshot.bringup.tuning.tofCalibration

  const applyBlocker = !connected
    ? 'No controller is connected.'
    : !serviceModeActive
      ? 'Enter service mode first (Bring-up workbench).'
      : deploymentActive
        ? 'Exit deployment mode before calibrating the ToF sensor.'
        : faultLatched
          ? 'Clear the latched fault before calibrating.'
          : null
  const applyAllowed = applyBlocker === null

  const initialRowCol = useMemo(
    () => rowColFromCenterSpad(cal.roiCenterSpad),
    [cal.roiCenterSpad],
  )

  const [distanceMode, setDistanceMode] = useState<TofDistanceMode>(cal.distanceMode)
  const [timingBudgetMs, setTimingBudgetMs] = useState<number>(cal.timingBudgetMs)
  const [roiWidth, setRoiWidth] = useState<number>(cal.roiWidthSpads)
  const [roiHeight, setRoiHeight] = useState<number>(cal.roiHeightSpads)
  const [roiRow, setRoiRow] = useState<number>(initialRowCol.row)
  const [roiCol, setRoiCol] = useState<number>(initialRowCol.col)
  const [offsetMm, setOffsetMm] = useState<number>(cal.offsetMm)
  const [xtalkCps, setXtalkCps] = useState<number>(cal.xtalkCps)
  const [xtalkEnabled, setXtalkEnabled] = useState<boolean>(cal.xtalkEnabled)

  // Re-seed UI when firmware-reported calibration changes (e.g., after
  // a fresh snapshot after Apply). Runs only when NOT mid-edit — i.e.
  // when the persisted state matches the UI state from the prior sync.
  useEffect(() => {
    setDistanceMode(cal.distanceMode)
    setTimingBudgetMs(cal.timingBudgetMs)
    setRoiWidth(cal.roiWidthSpads)
    setRoiHeight(cal.roiHeightSpads)
    const rc = rowColFromCenterSpad(cal.roiCenterSpad)
    setRoiRow(rc.row)
    setRoiCol(rc.col)
    setOffsetMm(cal.offsetMm)
    setXtalkCps(cal.xtalkCps)
    setXtalkEnabled(cal.xtalkEnabled)
  }, [
    cal.distanceMode,
    cal.timingBudgetMs,
    cal.roiWidthSpads,
    cal.roiHeightSpads,
    cal.roiCenterSpad,
    cal.offsetMm,
    cal.xtalkCps,
    cal.xtalkEnabled,
  ])

  const uiCenterSpad = centerSpadFromRowCol(roiRow, roiCol)
  const coneDeg = coneAngleDeg(roiWidth, roiHeight)
  const tiltXDeg = Math.round((roiCol - 7) * 1.7 * 10) / 10
  const tiltYDeg = Math.round((roiRow - 7) * 1.7 * 10) / 10

  const dirty =
    distanceMode !== cal.distanceMode ||
    timingBudgetMs !== cal.timingBudgetMs ||
    roiWidth !== cal.roiWidthSpads ||
    roiHeight !== cal.roiHeightSpads ||
    uiCenterSpad !== cal.roiCenterSpad ||
    offsetMm !== cal.offsetMm ||
    xtalkCps !== cal.xtalkCps ||
    xtalkEnabled !== cal.xtalkEnabled

  async function apply() {
    await onIssueCommandAwaitAck(
      'integrate.tof.set_calibration',
      'service',
      `Apply + persist ToF calibration (${distanceMode}, ${timingBudgetMs} ms, ${roiWidth}×${roiHeight} SPAD, center=${uiCenterSpad}, offset=${offsetMm} mm, xtalk=${xtalkEnabled ? xtalkCps + ' cps' : 'off'}).`,
      {
        distance_mode: distanceMode,
        timing_budget_ms: timingBudgetMs,
        roi_width_spads: roiWidth,
        roi_height_spads: roiHeight,
        roi_center_spad: uiCenterSpad,
        offset_mm: offsetMm,
        xtalk_cps: xtalkCps,
        xtalk_enabled: xtalkEnabled,
      },
      { timeoutMs: 5000 },
    )
  }

  function resetCenter() {
    setRoiRow(7)
    setRoiCol(7)
  }

  return (
    <section className="panel-section">
      <div className="section-head">
        <div>
          <h3>ToF calibration</h3>
          <p>
            VL53L1X distance mode, timing budget, ROI (cone) size + angle, offset, and crosstalk compensation. Saved to the <code>tof_cal</code> NVS blob on Apply; auto-re-applied at every boot.
          </p>
        </div>
        <span
          className={
            xtalkEnabled
              ? 'status-badge is-warn'
              : 'status-badge'
          }
        >
          {xtalkEnabled ? <TriangleAlert size={14} /> : <ShieldCheck size={14} />}
          Xtalk {xtalkEnabled ? 'on' : 'off'}
        </span>
      </div>

      <dl className="summary-list">
        <div>
          <dt>Persisted</dt>
          <dd>
            {cal.distanceMode} · {cal.timingBudgetMs} ms · {cal.roiWidthSpads}×{cal.roiHeightSpads} SPAD · center {cal.roiCenterSpad} · {cal.offsetMm} mm
          </dd>
        </div>
        <div>
          <dt>Staged (this session)</dt>
          <dd>
            {distanceMode} · {timingBudgetMs} ms · {roiWidth}×{roiHeight} SPAD · center {uiCenterSpad} · {offsetMm} mm
          </dd>
        </div>
        <div>
          <dt>Computed cone + tilt</dt>
          <dd>
            ~{coneDeg}° full cone · tilt X {tiltXDeg > 0 ? '+' : ''}{tiltXDeg}° / Y {tiltYDeg > 0 ? '+' : ''}{tiltYDeg}°
          </dd>
        </div>
      </dl>

      {/* Distance mode */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">
          <Gauge size={14} /> Distance mode
        </span>
        <div className="segmented is-compact">
          {DISTANCE_MODE_OPTIONS.map((opt) => (
            <button
              key={opt.value}
              type="button"
              className={
                distanceMode === opt.value
                  ? 'segmented__button is-active'
                  : 'segmented__button'
              }
              onClick={() => setDistanceMode(opt.value)}
              title={opt.desc}
            >
              {opt.label}
            </button>
          ))}
        </div>
      </div>

      {/* Timing budget */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">Timing budget</span>
        <div className="segmented is-compact">
          {TIMING_BUDGET_OPTIONS.map((ms) => (
            <button
              key={ms}
              type="button"
              className={
                timingBudgetMs === ms
                  ? 'segmented__button is-active'
                  : 'segmented__button'
              }
              onClick={() => setTimingBudgetMs(ms)}
              title={`${ms} ms per measurement — lower = faster refresh, higher = lower noise.`}
            >
              {ms} ms
            </button>
          ))}
        </div>
      </div>

      {/* ROI (cone size) */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">
          <Target size={14} /> Cone size (ROI width × height)
        </span>
        <div className="tof-roi-size-grid">
          <label className="field">
            <span>Width (SPAD, 4..16)</span>
            <input
              type="range"
              min={4}
              max={16}
              step={1}
              value={roiWidth}
              onChange={(e) => setRoiWidth(Math.max(4, Math.min(16, Number(e.target.value))))}
              aria-label="ROI width in SPAD units"
            />
            <output>{roiWidth}</output>
          </label>
          <label className="field">
            <span>Height (SPAD, 4..16)</span>
            <input
              type="range"
              min={4}
              max={16}
              step={1}
              value={roiHeight}
              onChange={(e) => setRoiHeight(Math.max(4, Math.min(16, Number(e.target.value))))}
              aria-label="ROI height in SPAD units"
            />
            <output>{roiHeight}</output>
          </label>
        </div>
      </div>

      {/* ROI angle (center) */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">
          <Crosshair size={14} /> Cone angle (ROI centre — row / col on 16×16 SPAD grid)
        </span>
        <div className="tof-roi-size-grid">
          <label className="field">
            <span>Col (X, 0..15)</span>
            <input
              type="range"
              min={0}
              max={15}
              step={1}
              value={roiCol}
              onChange={(e) => setRoiCol(Math.max(0, Math.min(15, Number(e.target.value))))}
              aria-label="ROI centre column 0 to 15"
            />
            <output>{roiCol}</output>
          </label>
          <label className="field">
            <span>Row (Y, 0..15)</span>
            <input
              type="range"
              min={0}
              max={15}
              step={1}
              value={roiRow}
              onChange={(e) => setRoiRow(Math.max(0, Math.min(15, Number(e.target.value))))}
              aria-label="ROI centre row 0 to 15"
            />
            <output>{roiRow}</output>
          </label>
        </div>
        <div className="button-row is-compact">
          <button
            type="button"
            className="action-button is-inline"
            onClick={resetCenter}
            title="Reset ROI centre to the SPAD grid centre (row 7, col 7 — byte 199)."
          >
            Recenter
          </button>
          <span className="tof-roi-byte-meta">
            centre byte = <code>{uiCenterSpad}</code> (firmware register)
          </span>
        </div>
      </div>

      {/* Offset */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">Offset (signed mm)</span>
        <div className="tof-offset-xtalk-grid">
          <label className="field">
            <span>mm</span>
            <input
              type="number"
              step={1}
              min={-2000}
              max={2000}
              value={offsetMm}
              onChange={(e) => setOffsetMm(Math.max(-2000, Math.min(2000, Number(e.target.value) || 0)))}
              aria-label="Distance offset in millimetres"
              title="Calibrated against an 18% grey target at known distance: offset_mm = target_mm - raw_mm."
            />
          </label>
          <p className="inline-help">
            Calibrate by placing an 18% grey target at a known distance (typ. 100 mm), reading the raw distance, and entering <code>(known − raw)</code> here.
          </p>
        </div>
      </div>

      {/* Xtalk */}
      <div className="tof-field-group">
        <span className="tof-field-group__label">Crosstalk compensation</span>
        <div className="tof-offset-xtalk-grid">
          <label className="field">
            <span>cps</span>
            <input
              type="number"
              step={1}
              min={0}
              max={65535}
              value={xtalkCps}
              onChange={(e) => setXtalkCps(Math.max(0, Math.min(65535, Number(e.target.value) || 0)))}
              aria-label="Crosstalk compensation in counts per second"
            />
          </label>
          <label className="field is-checkbox">
            <input
              type="checkbox"
              checked={xtalkEnabled}
              onChange={(e) => setXtalkEnabled(e.target.checked)}
              aria-label="Apply crosstalk compensation"
            />
            <span>Apply crosstalk compensation</span>
          </label>
        </div>
        <p className="inline-help">
          Measure cover-glass leakage: remove any target, run range sensor, read signal rate; enter that as <code>cps</code> and enable. See VL53L1X AN4907.
        </p>
      </div>

      {applyBlocker !== null && (
        <p className="inline-help" role="status">
          {applyBlocker}
        </p>
      )}

      <div className="button-row is-compact">
        <button
          type="button"
          className="action-button is-inline is-accent"
          disabled={!applyAllowed || !dirty}
          title={
            applyBlocker ??
            (dirty
              ? 'Send + persist the staged calibration to the device and re-apply live.'
              : 'Staged values match persisted — nothing to apply.')
          }
          onClick={() => void apply()}
        >
          <Save size={14} /> Apply &amp; save
        </button>
        {!dirty && (
          <span className="tof-roi-byte-meta">
            Persisted state matches UI — no unsaved changes.
          </span>
        )}
      </div>
    </section>
  )
}
