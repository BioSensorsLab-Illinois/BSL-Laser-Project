import { ShieldCheck, ShieldX } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import {
  buildSafetyChecks,
  summarizeSafetyChecks,
} from '../lib/presentation'
import type { DeviceSnapshot } from '../types'

type SafetyMatrixProps = {
  snapshot: DeviceSnapshot
}

export function SafetyMatrix({ snapshot }: SafetyMatrixProps) {
  const rows = buildSafetyChecks(snapshot)
  const summary = summarizeSafetyChecks(rows)
  const failingCount = rows.length - summary.passCount

  return (
    <section className="panel-section">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Interlocks</p>
          <h2>Beam permission matrix</h2>
        </div>
        <p className="panel-note">
          Host shows controller truth only.
        </p>
      </div>

      <div className="safety-compact-summary" data-tone={summary.tone}>
        <div className="safety-compact-summary__copy">
          <strong>{summary.label}</strong>
          <span>
            {summary.passCount}/{summary.total} checks passing
            {failingCount > 0 ? ` • ${failingCount} need review` : ' • all gates aligned'}
          </span>
        </div>
        <div className="safety-compact-summary__meta">
          <span className="inline-token">{summary.passCount} pass</span>
          <span className={failingCount > 0 ? 'inline-token' : 'inline-token'}>
            {failingCount} fail
          </span>
          <div className={`state-pill is-${summary.tone}`}>
            <span>{summary.percent}% ready</span>
          </div>
        </div>
      </div>

      <ProgressMeter value={summary.percent} tone={summary.tone} />

      <div className="safety-grid">
        {rows.map((row) => (
          <article
            key={row.label}
            className={`safety-card ${row.pass ? 'is-pass' : 'is-fail'}`}
            data-tone={row.tone}
          >
            <div className="safety-card__head">
              <div className="safety-card__label">
                {row.pass ? <ShieldCheck size={16} /> : <ShieldX size={16} />}
                <span>{row.label}</span>
              </div>
              <span className={row.pass ? 'status-badge is-on' : 'status-badge is-warn'}>
                {row.pass ? 'pass' : 'hold'}
              </span>
            </div>
            <strong className="safety-card__detail">{row.detail}</strong>
          </article>
        ))}
      </div>
    </section>
  )
}
