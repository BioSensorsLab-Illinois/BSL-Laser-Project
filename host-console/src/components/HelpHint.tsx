import { CircleHelp } from 'lucide-react'

type HelpHintProps = {
  text: string
}

export function HelpHint({ text }: HelpHintProps) {
  return (
    <span
      className="help-hint"
      tabIndex={0}
      role="note"
      aria-label={text}
      title={text}
    >
      <CircleHelp size={14} />
    </span>
  )
}
