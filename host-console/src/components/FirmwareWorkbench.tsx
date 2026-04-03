import { useMemo, useState } from 'react'
import { ArrowUpToLine, Box, Copy, FileCode2, ShieldEllipsis } from 'lucide-react'

import { FirmwareLoaderGuide } from './FirmwareLoaderGuide'
import { buildFirmwareChecklist } from '../lib/firmware'
import { formatBytes } from '../lib/format'
import type {
  DeviceSnapshot,
  FirmwarePackageDescriptor,
  FirmwareTransferProgress,
  TransportKind,
} from '../types'

type FirmwareWorkbenchProps = {
  snapshot: DeviceSnapshot
  packageDescriptor: FirmwarePackageDescriptor | null
  firmwareProgress: FirmwareTransferProgress | null
  connected: boolean
  supportsFirmwareTransfer: boolean
  transportKind: TransportKind
  onPickFile: (file: File) => Promise<void>
  onBeginTransfer: () => Promise<void>
  onPrepareDevice: () => Promise<void>
}

export function FirmwareWorkbench({
  snapshot,
  packageDescriptor,
  firmwareProgress,
  connected,
  supportsFirmwareTransfer,
  transportKind,
  onPickFile,
  onBeginTransfer,
  onPrepareDevice,
}: FirmwareWorkbenchProps) {
  const [shaCopied, setShaCopied] = useState(false)
  const checklist = useMemo(
    () => buildFirmwareChecklist(snapshot, connected, packageDescriptor),
    [connected, packageDescriptor, snapshot],
  )

  const readyToTransfer = checklist.every((item) => item.pass || !item.blocking)
  const deploymentLocked = snapshot.deployment.active
  const canWebFlash =
    !deploymentLocked &&
    readyToTransfer &&
    supportsFirmwareTransfer &&
    packageDescriptor?.webFlash.supported === true
  const serviceModeActive =
    snapshot.bringup.serviceModeActive || snapshot.session.state === 'SERVICE_MODE'
  const signature = packageDescriptor?.signature ?? null
  const blockingReasons = useMemo(() => {
    const reasons: string[] = []

    if (deploymentLocked) {
      reasons.push('Deployment mode is active. Exit deployment mode before starting a browser flash session.')
    }

    if (!supportsFirmwareTransfer) {
      reasons.push('Switch to a live Web Serial session to use browser flashing.')
    }

    checklist
      .filter((item) => item.blocking && !item.pass)
      .forEach((item) => {
        reasons.push(`${item.label}: ${item.note}`)
      })

    if (serviceModeActive && !deploymentLocked && readyToTransfer && supportsFirmwareTransfer) {
      reasons.push('Service mode is active, but that does not block browser flashing by itself.')
    }

    return reasons.filter((reason, index) => reasons.indexOf(reason) === index)
  }, [checklist, deploymentLocked, serviceModeActive, supportsFirmwareTransfer])

  function formatBuildUtc(value: string | undefined): string {
    if (value === undefined || value.length === 0) {
      return 'Unavailable'
    }

    const parsed = new Date(value)
    if (Number.isNaN(parsed.getTime())) {
      return value
    }

    return new Intl.DateTimeFormat(undefined, {
      year: 'numeric',
      month: 'short',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      timeZoneName: 'short',
    }).format(parsed)
  }

  async function copyShaToClipboard() {
    if (packageDescriptor === null) {
      return
    }

    try {
      await navigator.clipboard.writeText(packageDescriptor.sha256)
      setShaCopied(true)
      window.setTimeout(() => setShaCopied(false), 1200)
    } catch {
      setShaCopied(false)
    }
  }

  return (
    <section className="panel-section">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Firmware</p>
          <h2>Firmware update</h2>
        </div>
        <p className="panel-note">
          Review the image, confirm the board is safe, then flash over Web Serial when the package contains a local app binary.
        </p>
      </div>

      <div className="firmware-grid">
        <article className="panel-cutout firmware-drop">
          <div className="cutout-head">
            <ArrowUpToLine size={16} />
            <strong>1. Load image</strong>
          </div>

          <label className={packageDescriptor === null ? 'file-drop' : 'file-drop is-loaded'}>
            <input
              type="file"
              onChange={async (event) => {
                const file = event.target.files?.[0]

                if (file === undefined) {
                  return
                }

                await onPickFile(file)
              }}
            />
            <FileCode2 size={20} />
            {packageDescriptor === null ? (
              <>
                <strong>Select a manifest or firmware binary</strong>
                <span>Load a raw `.bin` or a manifest for review.</span>
              </>
            ) : (
              <div className="firmware-load-summary">
                <div className="firmware-load-summary__head">
                  <strong>{packageDescriptor.packageName}</strong>
                  <span className="status-badge">
                    {packageDescriptor.webFlash.supported ? 'Web flash ready' : 'Review only'}
                  </span>
                </div>
                <div className="firmware-load-summary__facts">
                  <span>{packageDescriptor.fileName}</span>
                  <span>{packageDescriptor.format === 'binary' ? 'Raw app binary' : 'Manifest package'}</span>
                  <span>Version {packageDescriptor.version}</span>
                  <span>{formatBytes(packageDescriptor.bytes)}</span>
                  <span>{packageDescriptor.board}</span>
                  <span>{signature !== null ? formatBuildUtc(signature.buildUtc) : 'Build date unavailable'}</span>
                </div>
              </div>
            )}
          </label>

          {packageDescriptor !== null ? (
            <div className="firmware-metadata">
              <div className="firmware-meta-card firmware-meta-card--sha">
                <span>SHA-256</span>
                <code>{packageDescriptor.sha256}</code>
                <button
                  type="button"
                  className="action-button is-inline is-compact"
                  title="Copy the full SHA-256 digest."
                  onClick={() => {
                    void copyShaToClipboard()
                  }}
                >
                  <Copy size={14} />
                  {shaCopied ? 'Copied' : 'Copy SHA'}
                </button>
              </div>
              <div className="firmware-meta-card">
                <span>BSL signature</span>
                <strong>{signature?.verified ? 'verified' : 'missing or invalid'}</strong>
                <small>
                  {signature !== null
                    ? `${signature.productName} / ${signature.boardName} / ${signature.protocolVersion}`
                    : 'Browser flashing requires an embedded BSL firmware signature block.'}
                </small>
              </div>
              <div className="firmware-meta-card">
                <span>Firmware version</span>
                <strong>{signature?.firmwareVersion ?? packageDescriptor.version}</strong>
                <small>
                  {signature !== null
                    ? `Embedded build for ${signature.productName}.`
                    : 'Raw package version inferred from the loaded file.'}
                </small>
              </div>
              <div className="firmware-meta-card">
                <span>Build date</span>
                <strong>{formatBuildUtc(signature?.buildUtc)}</strong>
                <small>
                  {signature !== null
                    ? 'UTC build timestamp embedded in the firmware signature.'
                    : 'No embedded build timestamp was found in this package.'}
                </small>
              </div>
              <div className="firmware-meta-card">
                <span>Web flash</span>
                <strong>{packageDescriptor.webFlash.supported ? 'ready' : 'review only'}</strong>
                <small>{packageDescriptor.webFlash.note}</small>
              </div>
              <div className="firmware-meta-card">
                <span>Segments</span>
                <strong>{packageDescriptor.segments.length}</strong>
                <small>{packageDescriptor.format === 'binary' ? 'raw app image' : 'manifest entries'}</small>
              </div>
            </div>
          ) : null}

          {signature !== null ? (
            <div className="firmware-signature-panel">
              <div className="firmware-signature-panel__head">
                <strong>Embedded firmware identity</strong>
                <span className="status-badge">
                  {signature.verified ? 'Signature verified' : 'Signature mismatch'}
                </span>
              </div>
              <div className="firmware-signature-grid">
                <div>
                  <span>Product</span>
                  <strong>{signature.productName}</strong>
                </div>
                <div>
                  <span>Firmware version</span>
                  <strong>{signature.firmwareVersion}</strong>
                </div>
                <div>
                  <span>Build UTC</span>
                  <strong>{formatBuildUtc(signature.buildUtc)}</strong>
                </div>
                <div>
                  <span>Board target</span>
                  <strong>{signature.boardName}</strong>
                </div>
                <div>
                  <span>Protocol</span>
                  <strong>{signature.protocolVersion}</strong>
                </div>
                <div>
                  <span>Scope</span>
                  <strong>{signature.hardwareScope}</strong>
                </div>
              </div>
            </div>
          ) : null}
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <ShieldEllipsis size={16} />
            <strong>2. Preflight</strong>
          </div>

          <div className="checklist">
            {checklist.map((item) => (
              <div
                key={item.label}
                className={
                  item.pass
                    ? 'check-row is-pass'
                    : item.blocking
                      ? 'check-row is-fail'
                      : 'check-row is-warn'
                }
              >
                <div className="check-row__copy">
                  <strong className="check-row__label">{item.label}</strong>
                  <span className="check-row__note">{item.note}</span>
                </div>
                <em>
                  {item.pass ? 'ready' : item.blocking ? 'hold' : 'warn'}
                </em>
              </div>
            ))}
          </div>

          <div className="button-row firmware-actions">
            <button
              type="button"
              className="action-button is-inline"
              disabled={serviceModeActive}
              title="Optional bench prep only. Browser flashing resets the controller and will end service mode when the ESP32 reboots."
              onClick={() => onPrepareDevice()}
            >
              {serviceModeActive ? 'Service mode active' : 'Optional: enter service mode'}
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={!canWebFlash}
              title={
                deploymentLocked
                  ? 'Exit deployment mode before starting a browser flash session.'
                  : supportsFirmwareTransfer && packageDescriptor?.webFlash.supported
                  ? 'Flash the currently loaded image over Web Serial.'
                  : packageDescriptor?.webFlash.note ?? 'Browser flashing is unavailable on this transport.'
              }
              onClick={() => onBeginTransfer()}
            >
              Flash over Web Serial
            </button>
          </div>

          <p className="inline-help">
            Warnings do not block browser flashing. Service mode is optional prep only and will clear when the ESP32 resets into and out of the flasher.
          </p>

          {!canWebFlash ? (
            <div className="firmware-blocked-note">
              <strong>Web Serial flash is currently blocked.</strong>
              <div className="segment-list">
                {blockingReasons.map((reason) => (
                  <div key={reason} className="segment-row">
                    <div>
                      <strong>Hold</strong>
                      <span>{reason}</span>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          ) : null}

          {deploymentLocked ? (
            <p className="inline-help">
              Deployment mode is active. Firmware metadata stays readable, but browser flashing is locked until deployment mode is exited.
            </p>
          ) : null}

          {!supportsFirmwareTransfer ? (
            <p className="inline-help">
              Switch to Web Serial to use browser flashing.
            </p>
          ) : null}

          {packageDescriptor !== null && !packageDescriptor.webFlash.supported ? (
            <p className="inline-help">{packageDescriptor.webFlash.note}</p>
          ) : null}
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Box size={16} />
            <strong>3. Flash detail</strong>
          </div>

          {packageDescriptor === null ? (
            <p className="inline-help">
              Load a firmware package to inspect addresses, segment sizes, and notes.
            </p>
          ) : (
            <>
              <div className="segment-list">
                {packageDescriptor.segments.map((segment) => (
                  <div key={`${segment.name}-${segment.address ?? 'na'}`} className="segment-row">
                    <div>
                      <strong>{segment.name}</strong>
                      <span>{segment.address ?? 'address pending'}</span>
                    </div>
                    <em>{formatBytes(segment.bytes)}</em>
                  </div>
                ))}
              </div>

              <div className="package-notes">
                <p>{packageDescriptor.webFlash.note}</p>
                {packageDescriptor.notes.map((note) => (
                  <p key={note}>{note}</p>
                ))}
              </div>
            </>
          )}
        </article>
      </div>

      {firmwareProgress !== null ? (
        <div className="firmware-progress">
          <div className="firmware-progress__head">
            <strong>{firmwareProgress.phase}</strong>
            <span>{firmwareProgress.percent}%</span>
          </div>
          <div className="progress-bar">
            <div
              className="progress-bar__fill"
              style={{ width: `${firmwareProgress.percent}%` }}
            />
          </div>
          <p>{firmwareProgress.detail}</p>
        </div>
      ) : null}

      <FirmwareLoaderGuide
        connected={connected}
        supportsFirmwareTransfer={supportsFirmwareTransfer}
        transportKind={transportKind}
      />
    </section>
  )
}
