import { createPortal } from 'react-dom'

import type { UiTone } from '../lib/presentation'

type ConfirmActionDialogProps = {
  title: string
  detail: string
  confirmLabel: string
  cancelLabel?: string
  tone?: UiTone
  bullets?: string[]
  onConfirm: () => void
  onCancel: () => void
}

export function ConfirmActionDialog({
  title,
  detail,
  confirmLabel,
  cancelLabel = 'Cancel',
  tone = 'warning',
  bullets = [],
  onConfirm,
  onCancel,
}: ConfirmActionDialogProps) {
  if (typeof document === 'undefined') {
    return null
  }

  return createPortal(
    <div className="confirm-action-overlay" role="dialog" aria-modal="true" aria-live="assertive">
      <div className={`confirm-action-dialog is-${tone}`}>
        <div className="confirm-action-dialog__head">
          <strong>{title}</strong>
          <span>{tone}</span>
        </div>
        <p>{detail}</p>
        {bullets.length > 0 ? (
          <ul className="confirm-action-dialog__list">
            {bullets.map((bullet) => (
              <li key={bullet}>{bullet}</li>
            ))}
          </ul>
        ) : null}
        <div className="button-row">
          <button
            type="button"
            className="action-button is-inline"
            onClick={onCancel}
          >
            {cancelLabel}
          </button>
          <button
            type="button"
            className={`action-button is-inline ${tone === 'critical' ? 'is-danger' : 'is-accent'}`}
            onClick={onConfirm}
          >
            {confirmLabel}
          </button>
        </div>
      </div>
    </div>,
    document.body,
  )
}
