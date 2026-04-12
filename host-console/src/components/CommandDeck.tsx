import { useEffect, useMemo, useRef, useState } from 'react'
import {
  Cable,
  ChevronRight,
  RefreshCcw,
  Settings2,
  ShieldAlert,
} from 'lucide-react'

import { ControllerBusyOverlay } from './ControllerBusyOverlay'
import { GpioWorkbench } from './GpioWorkbench'
import {
  decodeBusText,
  describeI2cSelection,
  describeSpiSelection,
  listKnownI2cTargets,
  listKnownSpiTargets,
} from '../lib/event-decode'
import type { UiTone } from '../lib/presentation'
import { commandTemplates } from '../lib/protocol'
import { transportLabel } from '../lib/wireless'
import type { CommandRisk, DeviceSnapshot, TransportKind, TransportStatus } from '../types'

type CommandDeckProps = {
  snapshot: DeviceSnapshot
  transportKind: TransportKind
  transportStatus: TransportStatus
  deploymentLocked: boolean
  onIssueCommandAwaitAck: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
    options?: {
      logHistory?: boolean
      timeoutMs?: number
    },
  ) => Promise<{
    ok: boolean
    note: string
  }>
}

type ProvisioningBoardKey = 'mainPcb' | 'tofPcb' | 'bmsPcb' | 'pdPcb' | 'buttonPcb'

type ProvisioningBoardEntry = {
  hardwareRevision: string
  serialNumber: string
}

type ToolsDraft = {
  i2cKnownTarget: string
  i2cAddress: string
  i2cRegister: string
  i2cValue: string
  spiKnownTarget: string
  spiDevice: string
  spiRegister: string
  spiValue: string
  boardProvisioning: Record<ProvisioningBoardKey, ProvisioningBoardEntry>
  worksheetBoardNotes: string
}

const TOOLS_DRAFT_STORAGE_KEY = 'bsl-tools-draft-v2'
const TOOLS_ACK_TIMEOUT_MS = 2600
const TOOLS_SUCCESS_DISMISS_MS = 260

type ToolsOperationState = {
  label: string
  detail: string
  percent: number
  tone: UiTone
  requiresConfirm?: boolean
}

const PROVISIONING_BOARDS: Array<{
  key: ProvisioningBoardKey
  label: string
  detail: string
}> = [
  {
    key: 'mainPcb',
    label: 'Main PCB',
    detail: 'ESP32-S3 controller main assembly',
  },
  {
    key: 'tofPcb',
    label: 'ToF PCB',
    detail: 'distance interlock sensor board',
  },
  {
    key: 'bmsPcb',
    label: 'BMS PCB',
    detail: 'battery and power management board',
  },
  {
    key: 'pdPcb',
    label: 'PD PCB',
    detail: 'USB-PD sink and contract board',
  },
  {
    key: 'buttonPcb',
    label: 'Button PCB',
    detail: 'trigger and operator input board',
  },
]

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function pause(ms: number) {
  return new Promise((resolve) => window.setTimeout(resolve, ms))
}

function makeBoardProvisioningDefaults(
  snapshot: DeviceSnapshot,
): Record<ProvisioningBoardKey, ProvisioningBoardEntry> {
  const liveSerial =
    snapshot.identity.serialNumber === 'pending' ? '' : snapshot.identity.serialNumber
  const liveHardware =
    snapshot.identity.hardwareRevision === 'rev-?' ? '' : snapshot.identity.hardwareRevision

  return {
    mainPcb: {
      hardwareRevision: liveHardware,
      serialNumber: liveSerial,
    },
    tofPcb: {
      hardwareRevision: '',
      serialNumber: '',
    },
    bmsPcb: {
      hardwareRevision: '',
      serialNumber: '',
    },
    pdPcb: {
      hardwareRevision: '',
      serialNumber: '',
    },
    buttonPcb: {
      hardwareRevision: '',
      serialNumber: '',
    },
  }
}

function makeDefaultToolsDraft(snapshot: DeviceSnapshot): ToolsDraft {
  return {
    i2cKnownTarget: 'dac',
    i2cAddress: '0x48',
    i2cRegister: '0x00',
    i2cValue: '0x00',
    spiKnownTarget: 'imu',
    spiDevice: 'imu',
    spiRegister: '0x0f',
    spiValue: '0x00',
    boardProvisioning: makeBoardProvisioningDefaults(snapshot),
    worksheetBoardNotes: '',
  }
}

function readStoredToolsDraft(snapshot: DeviceSnapshot): ToolsDraft {
  if (typeof window === 'undefined') {
    return makeDefaultToolsDraft(snapshot)
  }

  const fallback = makeDefaultToolsDraft(snapshot)
  const raw = window.localStorage.getItem(TOOLS_DRAFT_STORAGE_KEY)

  if (raw === null) {
    return fallback
  }

  try {
    const parsed = JSON.parse(raw) as Partial<ToolsDraft> & {
      worksheetSerial?: string
      worksheetHardware?: string
    }

    const defaultBoardProvisioning = fallback.boardProvisioning
    const storedBoardProvisioning = parsed.boardProvisioning ?? defaultBoardProvisioning

    return {
      ...fallback,
      ...parsed,
      boardProvisioning: {
        mainPcb: {
          hardwareRevision:
            storedBoardProvisioning.mainPcb?.hardwareRevision ??
            parsed.worksheetHardware ??
            defaultBoardProvisioning.mainPcb.hardwareRevision,
          serialNumber:
            storedBoardProvisioning.mainPcb?.serialNumber ??
            parsed.worksheetSerial ??
            defaultBoardProvisioning.mainPcb.serialNumber,
        },
        tofPcb: {
          hardwareRevision:
            storedBoardProvisioning.tofPcb?.hardwareRevision ??
            defaultBoardProvisioning.tofPcb.hardwareRevision,
          serialNumber:
            storedBoardProvisioning.tofPcb?.serialNumber ??
            defaultBoardProvisioning.tofPcb.serialNumber,
        },
        bmsPcb: {
          hardwareRevision:
            storedBoardProvisioning.bmsPcb?.hardwareRevision ??
            defaultBoardProvisioning.bmsPcb.hardwareRevision,
          serialNumber:
            storedBoardProvisioning.bmsPcb?.serialNumber ??
            defaultBoardProvisioning.bmsPcb.serialNumber,
        },
        pdPcb: {
          hardwareRevision:
            storedBoardProvisioning.pdPcb?.hardwareRevision ??
            defaultBoardProvisioning.pdPcb.hardwareRevision,
          serialNumber:
            storedBoardProvisioning.pdPcb?.serialNumber ??
            defaultBoardProvisioning.pdPcb.serialNumber,
        },
        buttonPcb: {
          hardwareRevision:
            storedBoardProvisioning.buttonPcb?.hardwareRevision ??
            defaultBoardProvisioning.buttonPcb.hardwareRevision,
          serialNumber:
            storedBoardProvisioning.buttonPcb?.serialNumber ??
            defaultBoardProvisioning.buttonPcb.serialNumber,
        },
      },
    }
  } catch {
    return fallback
  }
}

export function CommandDeck({
  snapshot,
  transportKind,
  transportStatus,
  deploymentLocked,
  onIssueCommandAwaitAck,
}: CommandDeckProps) {
  const connected = transportStatus === 'connected'
  const [serviceArmed, setServiceArmed] = useState(false)
  const [toolsDraft, setToolsDraft] = useState<ToolsDraft>(() =>
    readStoredToolsDraft(snapshot),
  )
  const [toolsNote, setToolsNote] = useState('Bus probes and maintenance results will appear here.')
  const [operation, setOperation] = useState<ToolsOperationState | null>(null)
  const operationBusyRef = useRef(false)
  const operationConfirmResolveRef = useRef<(() => void) | null>(null)

  const decodedI2cScan = useMemo(
    () => decodeBusText(snapshot.bringup.tools.lastI2cScan),
    [snapshot.bringup.tools.lastI2cScan],
  )
  const decodedI2cOp = useMemo(
    () => decodeBusText(snapshot.bringup.tools.lastI2cOp),
    [snapshot.bringup.tools.lastI2cOp],
  )
  const decodedSpiOp = useMemo(
    () => decodeBusText(snapshot.bringup.tools.lastSpiOp),
    [snapshot.bringup.tools.lastSpiOp],
  )
  const knownI2cTargets = useMemo(() => listKnownI2cTargets(), [])
  const knownSpiTargets = useMemo(() => listKnownSpiTargets(), [])
  const effectiveI2cAddress = useMemo(() => {
    if (toolsDraft.i2cKnownTarget === 'custom') {
      return toolsDraft.i2cAddress
    }

    const selected = knownI2cTargets.find((target) => target.key === toolsDraft.i2cKnownTarget)
    return selected?.addressHex ?? toolsDraft.i2cAddress
  }, [knownI2cTargets, toolsDraft.i2cAddress, toolsDraft.i2cKnownTarget])
  const effectiveSpiDevice = useMemo(() => {
    if (toolsDraft.spiKnownTarget === 'custom') {
      return toolsDraft.spiDevice
    }

    const selected = knownSpiTargets.find((target) => target.key === toolsDraft.spiKnownTarget)
    return selected?.key ?? toolsDraft.spiDevice
  }, [knownSpiTargets, toolsDraft.spiDevice, toolsDraft.spiKnownTarget])
  const selectedI2c = useMemo(
    () => describeI2cSelection(effectiveI2cAddress, toolsDraft.i2cRegister),
    [effectiveI2cAddress, toolsDraft.i2cRegister],
  )
  const selectedSpi = useMemo(
    () => describeSpiSelection(effectiveSpiDevice, toolsDraft.spiRegister),
    [effectiveSpiDevice, toolsDraft.spiRegister],
  )

  function dismissOperation() {
    const resolve = operationConfirmResolveRef.current
    operationConfirmResolveRef.current = null
    setOperation(null)
    resolve?.()
  }

  function waitForOperationConfirm() {
    return new Promise<void>((resolve) => {
      operationConfirmResolveRef.current = resolve
    })
  }

  async function holdOperationError(label: string, detail: string) {
    setToolsNote(detail)
    setOperation({
      label,
      detail,
      percent: 100,
      tone: 'critical',
      requiresConfirm: true,
    })
    await waitForOperationConfirm()
  }

  useEffect(() => {
    if (typeof window === 'undefined') {
      return
    }

    window.localStorage.setItem(TOOLS_DRAFT_STORAGE_KEY, JSON.stringify(toolsDraft))
  }, [toolsDraft])

  function patchDraft<Key extends keyof ToolsDraft>(key: Key, value: ToolsDraft[Key]) {
    setToolsDraft((current) => ({
      ...current,
      [key]: value,
    }))
  }

  function patchBoardProvisioning(
    board: ProvisioningBoardKey,
    field: keyof ProvisioningBoardEntry,
    value: string,
  ) {
    setToolsDraft((current) => ({
      ...current,
      boardProvisioning: {
        ...current.boardProvisioning,
        [board]: {
          ...current.boardProvisioning[board],
          [field]: value,
        },
      },
    }))
  }

  function syncWorksheetFromLive() {
    setToolsDraft((current) => ({
      ...current,
      boardProvisioning: {
        ...current.boardProvisioning,
        mainPcb: {
          hardwareRevision:
            snapshot.identity.hardwareRevision === 'rev-?'
              ? current.boardProvisioning.mainPcb.hardwareRevision
              : snapshot.identity.hardwareRevision,
          serialNumber:
            snapshot.identity.serialNumber === 'pending'
              ? current.boardProvisioning.mainPcb.serialNumber
              : snapshot.identity.serialNumber,
        },
      },
    }))
    setToolsNote('Copied the live controller identity into the Main PCB worksheet row.')
  }

  function clearWorksheet() {
    setToolsDraft((current) => ({
      ...current,
      boardProvisioning: makeBoardProvisioningDefaults({
        ...snapshot,
        identity: {
          ...snapshot.identity,
          hardwareRevision: 'rev-?',
          serialNumber: 'pending',
        },
      }),
      worksheetBoardNotes: '',
    }))
    setToolsNote('Cleared the local provisioning worksheet.')
  }

  async function runBusCommand(
    label: string,
    cmd: string,
    risk: CommandRisk,
    note: string,
    args?: Record<string, number | string | boolean>,
  ) {
    if (!connected) {
      setToolsNote('Board is offline. Connect the controller before running bus work or maintenance.')
      return false
    }

    if (operationBusyRef.current) {
      setToolsNote('A controller action is already running. Wait for it to finish.')
      return false
    }

    const busyTone: UiTone = risk === 'read' ? 'warning' : 'warning'
    operationBusyRef.current = true
    let clearOnExit = true
    setToolsNote(`Working: ${note}`)
    setOperation({
      label,
      detail: note,
      percent: 18,
      tone: busyTone,
    })

    try {
      await pause(70)
      setOperation({
        label,
        detail: 'Waiting for controller acknowledgement and peripheral readback...',
        percent: 58,
        tone: busyTone,
      })
      const result = await onIssueCommandAwaitAck(cmd, risk, note, args, {
        timeoutMs: TOOLS_ACK_TIMEOUT_MS,
      })

      setToolsNote(result.note)

      if (!result.ok) {
        await holdOperationError(label, result.note)
        clearOnExit = false
        return false
      }

      setOperation({
        label,
        detail: result.note,
        percent: 100,
        tone: 'steady',
      })
      await pause(TOOLS_SUCCESS_DISMISS_MS)
      return true
    } finally {
      operationBusyRef.current = false
      if (clearOnExit) {
        setOperation(null)
      }
    }
  }

  const fullyProvisionedBoards = useMemo(
    () =>
      PROVISIONING_BOARDS.filter(({ key }) => {
        const entry = toolsDraft.boardProvisioning[key]
        return entry.hardwareRevision.trim().length > 0 && entry.serialNumber.trim().length > 0
      }).length,
    [toolsDraft.boardProvisioning],
  )

  function renderCommandActionRow({
    id,
    label,
    description,
    actionLabel,
    disabled,
    onActivate,
  }: {
    id: string
    label: string
    description: string
    actionLabel: string
    disabled: boolean
    onActivate: () => void
  }) {
    return (
      <button
        key={id}
        type="button"
        className="command-list__item"
        disabled={disabled}
        title={description}
        aria-label={`${actionLabel} ${label}`}
        onClick={onActivate}
      >
        <div className="command-list__copy">
          <strong>{label}</strong>
          <p>{description}</p>
        </div>
        <span className="command-list__cue" aria-hidden="true">
          <span>{actionLabel}</span>
          <ChevronRight size={16} />
        </span>
      </button>
    )
  }

  return (
    <section
      className={operation !== null ? 'panel-section tools-section controller-busy-lock is-busy' : 'panel-section tools-section controller-busy-lock'}
      aria-busy={operation !== null}
    >
      <div className="controller-busy-lock__content">
        <div className="panel-section__head">
          <div>
            <p className="eyebrow">Maintenance tools</p>
            <h2>Provisioning, bus access, and bench actions</h2>
          </div>
          <p className="panel-note">
            Keep bench provisioning, bus work, and guarded maintenance separate from live
            beam control.
          </p>
        </div>

        {deploymentLocked ? (
          <div className="note-strip">
            <span>
              Deployment mode is active. Tooling stays visible for read-only review, but maintenance writes remain locked until deployment mode is exited.
            </span>
          </div>
        ) : null}

        <div className="command-grid tools-grid">
        <article className="panel-cutout tools-panel tools-panel--wide">
          <div className="cutout-head">
            <Settings2 size={16} />
            <strong>Provisioning worksheet</strong>
          </div>

          <div className="tools-provisioning-head">
            <p className="panel-note">
              Stage serial numbers and hardware revisions for each PCB here. The current
              bench image still exposes controller identity as readback only, so this
              worksheet persists locally until device-side writeback is added.
            </p>
            <div className="status-badges">
              <span className="status-badge is-off">Writeback unavailable</span>
              <span className="status-badge">
                {fullyProvisionedBoards} / {PROVISIONING_BOARDS.length} boards labeled
              </span>
            </div>
          </div>

          <div className="metric-grid is-two">
            <div className="metric-card">
              <span>Firmware</span>
              <strong>{snapshot.identity.firmwareVersion}</strong>
              <small>protocol {snapshot.identity.protocolVersion}</small>
            </div>
            <div className="metric-card">
              <span>Controller HW</span>
              <strong>{snapshot.identity.hardwareRevision}</strong>
              <small>live device readback</small>
            </div>
            <div className="metric-card">
              <span>Controller serial</span>
              <strong>{snapshot.identity.serialNumber}</strong>
              <small>live device readback</small>
            </div>
            <div className="metric-card">
              <span>Coverage</span>
              <strong>
                {fullyProvisionedBoards} / {PROVISIONING_BOARDS.length}
              </strong>
              <small>rows with both HW rev and serial</small>
            </div>
            <div className="metric-card">
              <span>Boot reason</span>
              <strong>{snapshot.session.bootReason}</strong>
              <small>{snapshot.session.state.replaceAll('_', ' ')}</small>
            </div>
            <div className="metric-card">
              <span>Write session</span>
              <strong>
                {snapshot.bringup.serviceModeActive ? 'service active' : 'read-only'}
              </strong>
              <small>device writeback path still pending firmware support</small>
            </div>
          </div>

          <div className="provisioning-sheet">
            {PROVISIONING_BOARDS.map((board) => {
              const entry = toolsDraft.boardProvisioning[board.key]

              return (
                <div key={board.key} className="provisioning-row">
                  <div className="provisioning-row__head">
                    <strong>{board.label}</strong>
                    <small>{board.detail}</small>
                  </div>
                  <label className="field">
                    <span className="field-label">HW rev</span>
                    <input
                      type="text"
                      value={entry.hardwareRevision}
                      title={`Worksheet hardware revision for ${board.label}.`}
                      onChange={(event) =>
                        patchBoardProvisioning(
                          board.key,
                          'hardwareRevision',
                          event.target.value,
                        )
                      }
                    />
                  </label>
                  <label className="field">
                    <span className="field-label">Serial</span>
                    <input
                      type="text"
                      value={entry.serialNumber}
                      title={`Worksheet serial number for ${board.label}.`}
                      onChange={(event) =>
                        patchBoardProvisioning(
                          board.key,
                          'serialNumber',
                          event.target.value,
                        )
                      }
                    />
                  </label>
                </div>
              )
            })}
          </div>

          <div className="field-grid tools-note-grid">
            <label className="field field--full">
              <span className="field-label">Bench notes</span>
              <input
                type="text"
                value={toolsDraft.worksheetBoardNotes}
                title="Optional local notes for this board set or provisioning run."
                onChange={(event) => patchDraft('worksheetBoardNotes', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline is-compact"
              title="Copy the live serial and hardware revision into the local worksheet."
              onClick={syncWorksheetFromLive}
            >
              Copy live identity to Main PCB
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              title="Clear the local identity worksheet."
              onClick={clearWorksheet}
            >
              Clear all worksheet rows
            </button>
          </div>
        </article>

        <GpioWorkbench
          snapshot={snapshot}
          connected={connected}
          onRunCommand={runBusCommand}
        />

        <article className="panel-cutout tools-panel">
          <div className="cutout-head">
            <Cable size={16} />
            <strong>I2C Bus Lab</strong>
          </div>
          <p className="panel-note">
            Use the known-target picker for shared bench devices, or switch to
            custom address entry when you are probing something unknown.
          </p>

          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Run an I2C discovery scan against the active bench bus."
              onClick={() => {
                void runBusCommand(
                  'I2C scan',
                  'i2c_scan',
                  'read',
                  'Run an I2C discovery scan against declared bring-up targets.',
                )
              }}
            >
              Scan I2C
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Refresh the full controller snapshot."
              onClick={() => {
                void runBusCommand(
                  'Refresh snapshot',
                  'get_status',
                  'read',
                  'Poll the controller for a fresh full snapshot.',
                )
              }}
            >
              Refresh snapshot
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Force an immediate STUSB4500 runtime status refresh."
              onClick={() => {
                void runBusCommand(
                  'Refresh PD status',
                  'refresh_pd_status',
                  'read',
                  'Force an immediate STUSB4500 contract and PDO refresh.',
                )
              }}
            >
              Refresh PD
            </button>
          </div>

          <div className="field-grid tools-field-grid">
            <label className="field">
              <span className="field-label">Known I2C target</span>
              <select
                value={toolsDraft.i2cKnownTarget}
                title="Pick a known shared-I2C device or switch to Custom address entry."
                onChange={(event) => patchDraft('i2cKnownTarget', event.target.value)}
              >
                {knownI2cTargets.map((target) => (
                  <option key={target.key} value={target.key}>
                    {target.device} {target.addressHex}
                  </option>
                ))}
                <option value="custom">Custom address</option>
              </select>
            </label>
            <label className="field">
              <span className="field-label">Custom I2C address</span>
              <input
                value={toolsDraft.i2cAddress}
                title="Enter the I2C target address in decimal or 0x-prefixed hex."
                disabled={toolsDraft.i2cKnownTarget !== 'custom'}
                onChange={(event) => patchDraft('i2cAddress', event.target.value)}
              />
            </label>
            <label className="field">
              <span className="field-label">I2C register</span>
              <input
                value={toolsDraft.i2cRegister}
                title="Enter the I2C register offset in decimal or 0x-prefixed hex."
                onChange={(event) => patchDraft('i2cRegister', event.target.value)}
              />
            </label>
            <label className="field">
              <span className="field-label">I2C value</span>
              <input
                value={toolsDraft.i2cValue}
                title="Enter the byte value for an I2C write."
                onChange={(event) => patchDraft('i2cValue', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Perform an I2C register read."
              onClick={() => {
                void runBusCommand(
                  'I2C register read',
                  'i2c_read',
                  'read',
                  'Perform an I2C register read.',
                  {
                    address: parseNumber(effectiveI2cAddress, 0x48),
                    reg: parseNumber(toolsDraft.i2cRegister, 0),
                  },
                )
              }}
            >
              I2C read
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected || !snapshot.bringup.serviceModeActive}
              title="Perform a guarded I2C register write."
              onClick={() => {
                void runBusCommand(
                  'I2C register write',
                  'i2c_write',
                  'service',
                  'Perform a service-mode I2C register write.',
                  {
                    address: parseNumber(effectiveI2cAddress, 0x48),
                    reg: parseNumber(toolsDraft.i2cRegister, 0),
                    value: parseNumber(toolsDraft.i2cValue, 0),
                  },
                )
              }}
            >
              I2C write
            </button>
          </div>

          <div className="tools-bus-grid">
            <div className="bus-selection-card">
              <span className="eyebrow">Selected I2C target</span>
              <strong>
                {selectedI2c.target !== undefined
                  ? `${selectedI2c.target.device} ${selectedI2c.target.addressHex}`
                  : toolsDraft.i2cKnownTarget === 'custom'
                    ? effectiveI2cAddress || 'Custom address'
                    : 'Unknown or undeclared target'}
              </strong>
              <p>
                {selectedI2c.target !== undefined
                  ? `${selectedI2c.target.purpose} ${selectedI2c.target.benchRole}`
                  : 'Use Custom address to probe an unknown shared-I2C target directly.'}
              </p>
              {selectedI2c.registerHex !== undefined ? (
                <p className="bus-selection-card__sub">
                  <strong>
                    {selectedI2c.registerName ?? selectedI2c.registerHex}
                  </strong>
                  {selectedI2c.registerSummary !== undefined
                    ? `: ${selectedI2c.registerSummary}.`
                    : ' is not in the current decoder map yet.'}
                  {selectedI2c.registerDetail !== undefined
                    ? ` ${selectedI2c.registerDetail}`
                    : ''}
                </p>
              ) : null}
            </div>

            <div className="bus-result-list">
              <div className="bus-result">
                <span className="eyebrow">Last I2C scan</span>
                <strong>{decodedI2cScan?.summary ?? 'No scan yet'}</strong>
                <p>{decodedI2cScan?.detail ?? snapshot.bringup.tools.lastI2cScan}</p>
                {decodedI2cScan?.decodedDetail !== undefined ? (
                  <p className="bus-result__note">{decodedI2cScan.decodedDetail}</p>
                ) : null}
              </div>
              <div className="bus-result">
                <span className="eyebrow">Last I2C transfer</span>
                <strong>{decodedI2cOp?.summary ?? 'No I2C transfer yet'}</strong>
                <p>{decodedI2cOp?.detail ?? snapshot.bringup.tools.lastI2cOp}</p>
                {decodedI2cOp?.decodedDetail !== undefined ? (
                  <p className="bus-result__note">{decodedI2cOp.decodedDetail}</p>
                ) : null}
              </div>
            </div>
          </div>

          <div className="note-strip">
            <span>{toolsNote}</span>
          </div>
        </article>

        <article className="panel-cutout tools-panel">
          <div className="cutout-head">
            <Cable size={16} />
            <strong>SPI Bus Lab</strong>
          </div>
          <p className="panel-note">
            Use the known-device picker for bench-supported SPI targets, or enter a
            custom device key when you are testing a new firmware path.
          </p>

          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Refresh the full controller snapshot."
              onClick={() => {
                void runBusCommand(
                  'Refresh snapshot',
                  'get_status',
                  'read',
                  'Poll the controller for a fresh full snapshot.',
                )
              }}
            >
              Refresh snapshot
            </button>
          </div>

          <div className="field-grid tools-field-grid">
            <label className="field">
              <span className="field-label">Known SPI target</span>
              <select
                value={toolsDraft.spiKnownTarget}
                title="Pick a known SPI device or switch to a custom device key."
                onChange={(event) => patchDraft('spiKnownTarget', event.target.value)}
              >
                {knownSpiTargets.map((target) => (
                  <option key={target.key} value={target.key}>
                    {target.device}
                  </option>
                ))}
                <option value="custom">Custom device</option>
              </select>
            </label>
            <label className="field">
              <span className="field-label">Custom SPI device key</span>
              <input
                value={toolsDraft.spiDevice}
                title="Enter the firmware SPI device key for an unknown or experimental target."
                disabled={toolsDraft.spiKnownTarget !== 'custom'}
                onChange={(event) => patchDraft('spiDevice', event.target.value)}
              />
            </label>
            <label className="field">
              <span className="field-label">SPI register</span>
              <input
                value={toolsDraft.spiRegister}
                title="Enter the SPI register offset in decimal or 0x-prefixed hex."
                onChange={(event) => patchDraft('spiRegister', event.target.value)}
              />
            </label>
            <label className="field">
              <span className="field-label">SPI value</span>
              <input
                value={toolsDraft.spiValue}
                title="Enter the byte value for an SPI write."
                onChange={(event) => patchDraft('spiValue', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row is-compact">
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected}
              title="Perform an SPI register read."
              onClick={() => {
                void runBusCommand(
                  'SPI register read',
                  'spi_read',
                  'read',
                  'Perform an SPI register read.',
                  {
                    device: effectiveSpiDevice,
                    reg: parseNumber(toolsDraft.spiRegister, 0x0f),
                  },
                )
              }}
            >
              SPI read
            </button>
            <button
              type="button"
              className="action-button is-inline is-compact"
              disabled={!connected || !snapshot.bringup.serviceModeActive}
              title="Perform a guarded SPI register write."
              onClick={() => {
                void runBusCommand(
                  'SPI register write',
                  'spi_write',
                  'service',
                  'Perform a service-mode SPI register write.',
                  {
                    device: effectiveSpiDevice,
                    reg: parseNumber(toolsDraft.spiRegister, 0x0f),
                    value: parseNumber(toolsDraft.spiValue, 0),
                  },
                )
              }}
            >
              SPI write
            </button>
          </div>

          <div className="tools-bus-grid">
            <div className="bus-selection-card">
              <span className="eyebrow">Selected SPI target</span>
              <strong>
                {selectedSpi.target !== undefined
                  ? selectedSpi.target.device
                  : toolsDraft.spiKnownTarget === 'custom'
                    ? effectiveSpiDevice || 'Custom device'
                    : 'Unknown SPI target'}
              </strong>
              <p>
                {selectedSpi.target !== undefined
                  ? `${selectedSpi.target.purpose} ${selectedSpi.target.benchRole}`
                  : 'Use Custom device when the current bench image exposes a new SPI target key.'}
              </p>
              {selectedSpi.registerHex !== undefined ? (
                <p className="bus-selection-card__sub">
                  <strong>
                    {selectedSpi.registerName ?? selectedSpi.registerHex}
                  </strong>
                  {selectedSpi.registerSummary !== undefined
                    ? `: ${selectedSpi.registerSummary}.`
                    : ' is not in the current decoder map yet.'}
                  {selectedSpi.registerDetail !== undefined
                    ? ` ${selectedSpi.registerDetail}`
                    : ''}
                </p>
              ) : null}
            </div>

            <div className="bus-result-list">
              <div className="bus-result">
                <span className="eyebrow">Last SPI transfer</span>
                <strong>{decodedSpiOp?.summary ?? 'No SPI transfer yet'}</strong>
                <p>{decodedSpiOp?.detail ?? snapshot.bringup.tools.lastSpiOp}</p>
                {decodedSpiOp?.decodedDetail !== undefined ? (
                  <p className="bus-result__note">{decodedSpiOp.decodedDetail}</p>
                ) : null}
              </div>
            </div>
          </div>

          <div className="note-strip">
            <span>{toolsNote}</span>
          </div>
        </article>

        <article className="panel-cutout tools-panel tools-panel--wide">
          <div className="cutout-head">
            <RefreshCcw size={16} />
            <strong>Bench operations</strong>
          </div>
          <div className="tools-ops-grid">
            <div className="tools-stack">
              <div className="tools-subsection">
                <div className="tools-subsection__head">
                  <strong>Read and synchronize</strong>
                  <span className="inline-token">safe reads</span>
                </div>
                <div className="command-list">
                  {commandTemplates
                    .filter((command) => command.risk === 'read')
                    .map((command) =>
                      renderCommandActionRow({
                        id: command.id,
                        label: command.label,
                        description: command.description,
                        actionLabel: 'Read',
                        disabled: !connected,
                        onActivate: () => {
                          void runBusCommand(
                            command.label,
                            command.command,
                            command.risk,
                            command.description,
                          )
                        },
                      }),
                    )}
                </div>
              </div>

              <div className="tools-subsection">
                <div className="tools-subsection__head">
                  <strong>Protected maintenance</strong>
                  <span className="inline-token">
                    {serviceArmed ? 'armed' : 'arm required'}
                  </span>
                </div>

                <label className="arming-toggle is-compact">
                  <input
                    type="checkbox"
                    checked={serviceArmed}
                    onChange={(event) => setServiceArmed(event.target.checked)}
                  />
                  <span>
                    I am on a controlled bench and understand these are maintenance-facing
                    commands.
                  </span>
                </label>

                <div className="command-list">
                  {commandTemplates
                    .filter((command) => command.risk !== 'read')
                    .map((command) =>
                      renderCommandActionRow({
                        id: command.id,
                        label: command.label,
                        description: command.description,
                        actionLabel: 'Run',
                        disabled: !connected || (!serviceArmed && command.risk === 'service'),
                        onActivate: () => {
                          void runBusCommand(
                            command.label,
                            command.command,
                            command.risk,
                            command.description,
                          )
                        },
                      }),
                    )}
                </div>
              </div>

              {transportKind === 'mock' ? (
                <div className="tools-subsection">
                  <div className="tools-subsection__head">
                    <strong>Mock fault scenarios</strong>
                    <span className="inline-token">mock rig only</span>
                  </div>
                  <div className="command-list">
                    {renderCommandActionRow({
                      id: 'mock-horizon-trip',
                      label: 'Inject horizon trip',
                      description: 'Move the beam pitch above the horizon threshold.',
                      actionLabel: 'Inject',
                      disabled: false,
                      onActivate: () => {
                        void runBusCommand(
                          'Inject horizon trip',
                          'simulate_horizon_trip',
                          'service',
                          'Inject a synthetic horizon interlock trip in the mock rig.',
                        )
                      },
                    })}
                    {renderCommandActionRow({
                      id: 'mock-distance-trip',
                      label: 'Inject distance trip',
                      description: 'Push the measured distance outside the safe window.',
                      actionLabel: 'Inject',
                      disabled: false,
                      onActivate: () => {
                        void runBusCommand(
                          'Inject distance trip',
                          'simulate_distance_trip',
                          'service',
                          'Inject a synthetic ToF out-of-range trip in the mock rig.',
                        )
                      },
                    })}
                    {renderCommandActionRow({
                      id: 'mock-pd-loss',
                      label: 'Inject PD loss',
                      description: 'Collapse the contract back to programming-only mode.',
                      actionLabel: 'Inject',
                      disabled: false,
                      onActivate: () => {
                        void runBusCommand(
                          'Inject PD loss',
                          'simulate_pd_drop',
                          'service',
                          'Inject a simulated PD contract collapse in the mock rig.',
                        )
                      },
                    })}
                  </div>
                </div>
              ) : null}
            </div>

            <div className="tools-stack">
              <div className="tools-subsection">
                <div className="tools-subsection__head">
                  <ShieldAlert size={16} />
                  <strong>Current gate status</strong>
                </div>
                <div className="metric-grid is-two">
                  <div className="metric-card">
                    <span>System state</span>
                    <strong>{snapshot.session.state.replaceAll('_', ' ')}</strong>
                    <small>firmware truth</small>
                  </div>
                  <div className="metric-card">
                    <span>Fault latch</span>
                    <strong>{snapshot.fault.latched ? snapshot.fault.latchedCode : 'clear'}</strong>
                    <small>{snapshot.fault.tripCounter} historical trips</small>
                  </div>
                  <div className="metric-card">
                    <span>Bench request</span>
                    <strong>
                      {snapshot.bench.requestedNirEnabled
                        ? 'NIR requested'
                        : 'beam request idle'}
                    </strong>
                    <small>
                      {snapshot.bench.modulationEnabled
                        ? 'PCN modulation staged'
                        : 'continuous-wave mode'}
                    </small>
                  </div>
                  <div className="metric-card">
                    <span>Transport</span>
                    <strong>{transportLabel(transportKind).toLowerCase()}</strong>
                    <small>
                      {snapshot.bringup.serviceModeActive
                        ? 'service path open'
                        : 'read-only session'}
                    </small>
                  </div>
                </div>
              </div>

              <div className="tools-subsection">
                <div className="tools-subsection__head">
                  <strong>Bench activity</strong>
                  <span className="inline-token">sticky local state</span>
                </div>
                <div className="tools-token-grid">
                  <div className="inline-token">
                    <span>Protocol: {snapshot.identity.protocolVersion}</span>
                  </div>
                  <div className="inline-token">
                    <span>
                      Main PCB serial:{' '}
                      {toolsDraft.boardProvisioning.mainPcb.serialNumber || 'unset'}
                    </span>
                  </div>
                  <div className="inline-token">
                    <span>
                      Notes:{' '}
                      {toolsDraft.worksheetBoardNotes.trim().length > 0
                        ? toolsDraft.worksheetBoardNotes
                        : 'none'}
                    </span>
                  </div>
                </div>
                <div className="note-strip">
                  <span>{toolsNote}</span>
                </div>
              </div>
            </div>
          </div>
        </article>
        </div>

        <div className="command-footer">
          <div className="inline-token">
            <Settings2 size={14} />
            <span>Live transport: {transportLabel(transportKind).toLowerCase()}</span>
          </div>
          <div className="inline-token">
            <span>Protocol: {snapshot.identity.protocolVersion}</span>
          </div>
          <div className="inline-token">
            <span>
              Provisioned boards: {fullyProvisionedBoards} / {PROVISIONING_BOARDS.length}
            </span>
          </div>
        </div>
      </div>

      {operation !== null ? (
        <ControllerBusyOverlay
          label={operation.label}
          detail={operation.detail}
          percent={operation.percent}
          tone={operation.tone}
          footer={
            operation.requiresConfirm
              ? 'Controller action failed. Review the message and confirm before tools unlock.'
              : 'Controller busy. Tool controls are locked until this action finishes.'
          }
          confirmLabel={operation.requiresConfirm ? 'Confirm and close' : undefined}
          onConfirm={operation.requiresConfirm ? dismissOperation : undefined}
        />
      ) : null}
    </section>
  )
}
