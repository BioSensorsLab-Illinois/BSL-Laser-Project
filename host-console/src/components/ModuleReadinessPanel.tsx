import { Layers3 } from 'lucide-react'

import { ProgressMeter } from './ProgressMeter'
import { moduleKeys, moduleMeta } from '../lib/bringup'
import type { UiTone } from '../lib/presentation'
import type { DeviceSnapshot, ModuleKey } from '../types'

type ModuleReadinessPanelProps = {
  modules: DeviceSnapshot['bringup']['modules']
}

type ModuleReadinessSummary = {
  label: string
  detail: string
  progress: number
  tone: UiTone
  state: 'idle' | 'planned' | 'attention' | 'ready'
}

function summarizeModule(
  key: ModuleKey,
  status: DeviceSnapshot['bringup']['modules'][ModuleKey],
): ModuleReadinessSummary {
  if (!status.expectedPresent) {
    return {
      label: status.detected ? 'Unexpected response' : 'Not declared',
      detail: status.detected
        ? `${moduleMeta[key].label} responded even though it is not declared installed.`
        : 'Skipped on this bench stage until you mark the module installed.',
      progress: 0,
      tone: 'critical',
      state: status.detected ? 'attention' : 'idle',
    }
  }

  if (status.healthy) {
    return {
      label: 'Ready',
      detail: status.debugEnabled
        ? 'Detected, healthy, and writable from bring-up tools.'
        : 'Detected and healthy on the current bench setup.',
      progress: 100,
      tone: 'steady',
      state: 'ready',
    }
  }

  if (status.detected) {
    return {
      label: 'Needs review',
      detail: status.expectedPresent
        ? 'Hardware responded, but the firmware does not trust it yet.'
        : `${moduleMeta[key].label} responded even though it is not declared installed.`,
      progress: status.debugEnabled ? 72 : 62,
      tone: 'warning',
      state: 'attention',
    }
  }

  return {
    label: 'Awaiting probe',
    detail: status.debugEnabled
      ? 'Declared installed. Run a probe or register read next.'
      : 'Declared installed, but debug tools are still off.',
    progress: status.debugEnabled ? 44 : 30,
    tone: 'warning',
    state: 'planned',
  }
}

export function ModuleReadinessPanel({
  modules,
}: ModuleReadinessPanelProps) {
  const summaries = moduleKeys.map((key) => {
    const status = modules[key]
    return {
      key,
      meta: moduleMeta[key],
      status,
      summary: summarizeModule(key, status),
    }
  })

  const readyCount = summaries.filter((entry) => entry.status.healthy).length
  const declaredCount = summaries.filter((entry) => entry.status.expectedPresent).length
  const detectedCount = summaries.filter((entry) => entry.status.detected).length

  return (
    <section className="panel-section module-readiness-panel">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Sub-module readiness</p>
          <h2>Board blocks</h2>
        </div>
      </div>

      <div className="module-readiness-panel__summary">
        <div className="cutout-head">
          <Layers3 size={16} />
          <strong>Bench declaration vs live proof</strong>
        </div>
        <p className="inline-help">
          This list separates modules you plan to bring up from modules the firmware has
          actually seen. A block only reads as ready after it is detected and healthy.
        </p>
        <div className="status-badges">
          <span className="status-badge is-steady">{readyCount} ready</span>
          <span className="status-badge">{detectedCount} detected</span>
          <span className="status-badge is-warning">{declaredCount} declared installed</span>
        </div>
      </div>

      <div className="module-grid module-readiness-panel__grid">
        {summaries.map(({ key, meta, status, summary }) => (
          <article
            key={key}
            className="module-card module-readiness-card"
            data-readiness={summary.state}
          >
            <div className="module-card__head">
              <div>
                <p className="eyebrow">{meta.transport}</p>
                <h3>{meta.label}</h3>
              </div>
              <div className="module-readiness-card__state">
                <div className="module-state-dot" data-readiness={summary.state} />
                <span className="module-readiness-card__label">{summary.label}</span>
              </div>
            </div>

            <p className="module-readiness-card__detail">{summary.detail}</p>
            <ProgressMeter value={summary.progress} tone={summary.tone} compact />

            <div className="module-readiness-card__flags">
              <span
                className={status.expectedPresent ? 'module-flag is-on' : 'module-flag'}
              >
                Installed
              </span>
              <span
                className={status.expectedPresent && status.debugEnabled ? 'module-flag is-on' : 'module-flag'}
              >
                Tools
              </span>
              <span className={status.expectedPresent && status.detected ? 'module-flag is-on' : 'module-flag'}>
                Detected
              </span>
              <span className={status.expectedPresent && status.healthy ? 'module-flag is-on' : 'module-flag'}>
                Healthy
              </span>
            </div>
          </article>
        ))}
      </div>
    </section>
  )
}
