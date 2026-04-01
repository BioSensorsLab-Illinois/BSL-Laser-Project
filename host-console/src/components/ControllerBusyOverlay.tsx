import { createPortal } from 'react-dom'

import { ProgressMeter } from './ProgressMeter'
import type { UiTone } from '../lib/presentation'

type ControllerBusyOverlayProps = {
  label: string
  detail: string
  percent: number
  tone: UiTone
  footer: string
  confirmLabel?: string
  onConfirm?: () => void
}

export function ControllerBusyOverlay({
  label,
  detail,
  percent,
  tone,
  footer,
  confirmLabel,
  onConfirm,
}: ControllerBusyOverlayProps) {
  if (typeof document === 'undefined') {
    return null
  }

  return createPortal(
    <div
      className={onConfirm ? 'controller-busy-overlay is-confirm' : 'controller-busy-overlay'}
      role={onConfirm ? 'alertdialog' : 'status'}
      aria-live="assertive"
      aria-modal={onConfirm ? 'true' : undefined}
    >
      <div className={`controller-busy-dialog is-${tone}`}>
        <div className="controller-busy-dialog__head">
          <strong>{label}</strong>
          <span>{percent}%</span>
        </div>
        <p className="controller-busy-dialog__detail">{detail}</p>
        <ProgressMeter value={percent} tone={tone} />
        <small>{footer}</small>
        {onConfirm ? (
          <div className="controller-busy-dialog__actions">
            <button
              type="button"
              className="action-button is-inline"
              onClick={onConfirm}
            >
              {confirmLabel ?? 'Confirm'}
            </button>
          </div>
        ) : null}
      </div>
    </div>,
    document.body,
  )
}
