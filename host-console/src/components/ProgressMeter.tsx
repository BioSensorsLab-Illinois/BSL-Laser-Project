import { clampPercent } from '../lib/format'
import type { UiTone } from '../lib/presentation'

type ProgressMeterProps = {
  value: number
  tone?: UiTone
  compact?: boolean
}

export function ProgressMeter({
  value,
  tone = 'steady',
  compact = false,
}: ProgressMeterProps) {
  return (
    <div className={compact ? 'progress-meter is-compact' : 'progress-meter'}>
      <div
        className={`progress-meter__fill is-${tone}`}
        style={{ width: `${clampPercent(value)}%` }}
      />
    </div>
  )
}
