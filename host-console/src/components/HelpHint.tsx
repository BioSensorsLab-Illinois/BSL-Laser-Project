import { CircleHelp } from 'lucide-react'

type HelpHintProps = {
  text: string
}

export function HelpHint({ text }: HelpHintProps) {
  const helpText = text.trim()

  return (
    <span
      className="help-hint"
      tabIndex={0}
      role="note"
      aria-label={helpText.length > 0 ? helpText : undefined}
      title={helpText.length > 0 ? helpText : undefined}
      data-hover-help={helpText.length > 0 ? helpText : undefined}
    >
      <CircleHelp size={14} />
    </span>
  )
}
