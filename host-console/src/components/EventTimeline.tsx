/* 2026-04-17 (Uncodixfy polish): framer-motion removed. Event rows
 * used staggered slide-in (`initial={{opacity:0,y:10}} animate=`) and
 * the expanded-detail drawer used a height-morphing AnimatePresence.
 * Both are banned transform + shape-morph animations. Plain native
 * `<article>` + `<div>` with no entrance animation; the expand is a
 * simple CSS display toggle driven by the `expanded` state. */
import { useDeferredValue, useMemo, useState } from 'react'
import {
  Activity,
  AlertTriangle,
  ArrowDownWideNarrow,
  ArrowUpWideNarrow,
  ChevronDown,
  ChevronRight,
  CircleCheckBig,
  Cpu,
  Filter,
  Info,
  Radio,
  Search,
  ShieldAlert,
  Waves,
} from 'lucide-react'

import { formatShortTime } from '../lib/format'
import type { CommandHistoryEntry, EventSource, SessionEvent, Severity } from '../types'

type EventTimelineProps = {
  events: SessionEvent[]
  commands?: CommandHistoryEntry[]
  query: string
  severity: Severity | 'all'
  moduleFilter: string
  onQueryChange: (value: string) => void
  onSeverityChange: (value: Severity | 'all') => void
  onModuleFilterChange: (value: string) => void
  onClearSessionHistory?: () => void
  compact?: boolean
  mode?: 'all' | 'system'
}

type FocusMode = 'all' | 'faults' | 'bus' | 'service' | 'firmware' | 'transport'
type SortMode = 'latest' | 'oldest'

const severityOptions: Array<Severity | 'all'> = ['all', 'ok', 'info', 'warn', 'critical']
const focusOptions: Array<{
  value: FocusMode
  label: string
  icon: typeof Activity
}> = [
  { value: 'all', label: 'All', icon: Activity },
  { value: 'faults', label: 'Faults', icon: ShieldAlert },
  { value: 'bus', label: 'Bus', icon: Waves },
  { value: 'service', label: 'Service', icon: Cpu },
  { value: 'firmware', label: 'Firmware', icon: Radio },
  { value: 'transport', label: 'Transport', icon: Activity },
]

function severityIcon(severity: Severity) {
  if (severity === 'ok') {
    return <CircleCheckBig size={16} />
  }

  if (severity === 'warn' || severity === 'critical') {
    return <AlertTriangle size={16} />
  }

  return <Info size={16} />
}

function severityLabel(severity: Severity): string {
  if (severity === 'critical') {
    return 'Critical'
  }

  if (severity === 'warn') {
    return 'Warning'
  }

  if (severity === 'ok') {
    return 'OK'
  }

  return 'Info'
}

function titleCaseToken(value: string): string {
  const upper = new Set(['IMU', 'TOF', 'USB', 'PD', 'TEC', 'LD', 'NIR', 'I2C', 'SPI'])
  return value
    .replace(/([a-z])([A-Z])/g, '$1 $2')
    .split(/[_\s-]+/)
    .filter(Boolean)
    .map((token) => {
      const normalized = token.toUpperCase()
      return upper.has(normalized)
        ? normalized
        : token.charAt(0).toUpperCase() + token.slice(1).toLowerCase()
    })
    .join(' ')
}

function formatSourceLabel(source: EventSource | undefined): string {
  if (source === undefined) {
    return 'Unknown'
  }

  if (source === 'derived') {
    return 'Decoded'
  }

  return titleCaseToken(source)
}

function matchesFocus(event: SessionEvent, focus: FocusMode): boolean {
  if (focus === 'all') {
    return true
  }

  if (focus === 'faults') {
    return (
      event.severity === 'warn' ||
      event.severity === 'critical' ||
      event.category === 'fault' ||
      event.category === 'safety' ||
      event.module === 'safety'
    )
  }

  if (focus === 'bus') {
    return event.bus !== undefined || event.category === 'bus'
  }

  if (focus === 'service') {
    return event.module === 'service' || event.category === 'service' || event.category === 'command'
  }

  if (focus === 'firmware') {
    return event.module === 'firmware' || event.operation === 'transfer' || event.category === 'firmware'
  }

  return event.module === 'transport' || event.category === 'transport' || event.category === 'reset' || event.category === 'console'
}

function sortEvents(events: SessionEvent[], sortMode: SortMode): SessionEvent[] {
  const sorted = [...events].sort((left, right) => {
    const leftTime = Date.parse(left.atIso)
    const rightTime = Date.parse(right.atIso)
    return sortMode === 'latest' ? rightTime - leftTime : leftTime - rightTime
  })

  return sorted
}

function eventToneClass(severity: Severity): string {
  return `is-${severity}`
}

function uniqueTokens(tokens: Array<string | undefined>): string[] {
  const seen = new Set<string>()
  const ordered: string[] = []

  for (const token of tokens) {
    if (token === undefined) {
      continue
    }

    const normalized = token.trim().toLowerCase()
    if (normalized.length === 0 || seen.has(normalized)) {
      continue
    }

    seen.add(normalized)
    ordered.push(token)
  }

  return ordered
}

function moduleFromCommand(cmd: string): string {
  if (cmd.startsWith('i2c_') || cmd.startsWith('spi_')) {
    return 'bus'
  }

  if (cmd.startsWith('set_target_') || cmd.includes('tec')) {
    return 'tec'
  }

  if (cmd.includes('pd') || cmd === 'refresh_pd_status') {
    return 'pd'
  }

  if (cmd.includes('imu')) {
    return 'imu'
  }

  if (cmd.includes('tof')) {
    return 'tof'
  }

  if (cmd.includes('dac')) {
    return 'dac'
  }

  if (cmd.includes('haptic')) {
    return 'haptic'
  }

  if (cmd.includes('alignment') || cmd.includes('laser') || cmd.includes('modulation')) {
    return 'laser'
  }

  if (cmd.includes('service') || cmd.includes('fault') || cmd === 'reboot') {
    return 'service'
  }

  return 'system'
}

export function EventTimeline({
  events,
  commands = [],
  query,
  severity,
  moduleFilter,
  onQueryChange,
  onSeverityChange,
  onModuleFilterChange,
  onClearSessionHistory,
  compact = false,
  mode = 'all',
}: EventTimelineProps) {
  const [focusMode, setFocusMode] = useState<FocusMode>('all')
  const [sourceFilter, setSourceFilter] = useState<'all' | EventSource>('all')
  const [sortMode, setSortMode] = useState<SortMode>('latest')
  const [expandedEventId, setExpandedEventId] = useState<string | null>(null)
  const deferredQuery = useDeferredValue(query)
  const effectiveFocusMode: FocusMode =
    mode === 'system' && focusMode === 'bus' ? 'all' : focusMode
  const failedCommands = useMemo(
    () => commands.filter((entry) => entry.status === 'error').slice(0, 6),
    [commands],
  )

  const commandFailureEvents = useMemo(
    () =>
      failedCommands.map<SessionEvent>((entry) => ({
        id: `command-failure-${entry.id}`,
        atIso: entry.issuedAtIso,
        severity: 'warn',
        category: 'command',
        title: 'Host command failed',
        detail: `${entry.cmd}: ${entry.note}`,
        module: moduleFromCommand(entry.cmd),
        source: 'host',
        summary: entry.note,
      })),
    [failedCommands],
  )

  const timelineEvents = useMemo(() => {
    const merged = [...events, ...commandFailureEvents]
    const filtered =
      mode === 'system'
        ? merged.filter((event) => event.bus === undefined && event.category !== 'bus')
        : merged

    return filtered.sort(
      (left, right) => Date.parse(right.atIso) - Date.parse(left.atIso),
    )
  }, [commandFailureEvents, events, mode])

  const moduleOptions = useMemo(() => {
    const modules = new Set<string>(['all'])

    for (const event of timelineEvents) {
      if (event.module !== undefined && event.module.length > 0) {
        modules.add(event.module)
      }
    }

    return Array.from(modules)
  }, [timelineEvents])

  const sourceOptions = useMemo(() => {
    const options = new Set<'all' | EventSource>(['all'])

    for (const event of timelineEvents) {
      if (event.source !== undefined) {
        options.add(event.source)
      }
    }

    return Array.from(options)
  }, [timelineEvents])

  const availableFocusOptions = useMemo(
    () => focusOptions.filter((option) => !(mode === 'system' && option.value === 'bus')),
    [mode],
  )

  const filteredEvents = useMemo(() => {
    const search = deferredQuery.trim().toLowerCase()

    const matched = timelineEvents.filter((event) => {
      const severityMatch = severity === 'all' || event.severity === severity
      const moduleMatch = moduleFilter === 'all' || event.module === moduleFilter
      const sourceMatch = sourceFilter === 'all' || event.source === sourceFilter
      const focusMatch = matchesFocus(event, effectiveFocusMode)
      const queryMatch =
        search.length === 0 ||
        event.title.toLowerCase().includes(search) ||
        event.detail.toLowerCase().includes(search) ||
        event.category.toLowerCase().includes(search) ||
        event.summary?.toLowerCase().includes(search) === true ||
        event.device?.toLowerCase().includes(search) === true ||
        event.addressHex?.toLowerCase().includes(search) === true ||
        event.registerHex?.toLowerCase().includes(search) === true ||
        event.registerName?.toLowerCase().includes(search) === true ||
        event.valueHex?.toLowerCase().includes(search) === true ||
        event.decodedDetail?.toLowerCase().includes(search) === true

      return severityMatch && moduleMatch && sourceMatch && focusMatch && queryMatch
    })

    return sortEvents(matched, sortMode)
  }, [deferredQuery, effectiveFocusMode, moduleFilter, severity, sortMode, sourceFilter, timelineEvents])

  const visibleEvents = compact ? filteredEvents.slice(0, 8) : filteredEvents

  const summary = useMemo(() => {
    const total = timelineEvents.length
    const critical = timelineEvents.filter((event) => event.severity === 'critical').length
    const warnings = timelineEvents.filter((event) => event.severity === 'warn').length
    const bus = timelineEvents.filter((event) => event.bus !== undefined || event.category === 'bus').length
    const modules = new Set(timelineEvents.map((event) => event.module).filter((value): value is string => value !== undefined))
    const latest = timelineEvents[0]

    return {
      total,
      critical,
      warnings,
      bus,
      modules: modules.size,
      latest,
      matches: filteredEvents.length,
    }
  }, [filteredEvents.length, timelineEvents])

  return (
    <section className={`panel-section ${compact ? 'event-surface is-compact' : 'event-surface'}`}>
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Observability</p>
          <h2>
            {compact ? 'Recent events' : mode === 'system' ? 'System log' : 'Event workspace'}
          </h2>
        </div>
        <p className="panel-note">
          {compact
            ? 'Latest decoded activity from transport, safety, and buses.'
            : mode === 'system'
              ? 'Controller, safety, service, and transport history without the SPI/I2C traffic flood.'
              : 'Triage faults, bus traffic, and service actions with decoded context and expandable detail.'}
        </p>
      </div>

      {!compact && onClearSessionHistory !== undefined ? (
        <div className="button-row">
          <button
            type="button"
            className="action-button is-inline"
            onClick={onClearSessionHistory}
          >
            Clear session log
          </button>
        </div>
      ) : null}

      {!compact ? (
        <div className="event-overview">
          <button
            type="button"
            className="event-kpi"
            data-tone={summary.critical > 0 ? 'critical' : 'steady'}
            onClick={() => {
              setFocusMode('faults')
              onSeverityChange(summary.critical > 0 ? 'critical' : 'warn')
            }}
          >
            <span>Open issues</span>
            <strong>{summary.critical + summary.warnings}</strong>
            <small>{summary.critical} critical • {summary.warnings} warnings</small>
          </button>

          {mode === 'all' ? (
            <button
              type="button"
              className="event-kpi"
              data-tone={summary.bus > 0 ? 'warning' : 'steady'}
              onClick={() => {
                setFocusMode('bus')
                onSeverityChange('all')
              }}
            >
              <span>Bus activity</span>
              <strong>{summary.bus}</strong>
              <small>I2C and SPI decoded events in this session</small>
            </button>
          ) : null}

          <button
            type="button"
            className="event-kpi"
            data-tone="steady"
            onClick={() => {
              setFocusMode('all')
              onModuleFilterChange('all')
              onSeverityChange('all')
              setSourceFilter('all')
            }}
          >
            <span>Visible results</span>
            <strong>{summary.matches}</strong>
            <small>{summary.total} total events across {summary.modules} modules</small>
          </button>

          <div className="event-kpi event-kpi--info" data-tone="steady">
            <span>Latest event</span>
            <strong>{summary.latest ? formatShortTime(summary.latest.atIso) : '—'}</strong>
            <small>{summary.latest ? summary.latest.summary ?? summary.latest.title : 'No events yet'}</small>
          </div>
        </div>
      ) : null}

      {!compact && failedCommands.length > 0 ? (
        <div className="event-command-failures">
          <div className="panel-section__head">
            <div>
              <p className="eyebrow">Command failures</p>
              <h2>Recent rejected host actions</h2>
            </div>
            <p className="panel-note">
              These come directly from command history, so a rejected service write is visible here even if you never opened the inspector rail.
            </p>
          </div>
          <div className="command-history">
            {failedCommands.map((command) => (
              <div key={command.id} className="history-row is-error">
                <div className="event-command-failure__copy">
                  <strong>{command.cmd}</strong>
                  <span>{formatShortTime(command.issuedAtIso)}</span>
                  <p className="event-command-failure__note">{command.note}</p>
                </div>
                <div className="history-row__meta-group">
                  <span className={`history-row__risk is-${command.risk}`}>{command.risk}</span>
                  <em>error</em>
                </div>
              </div>
            ))}
          </div>
        </div>
      ) : null}

      <div className={`event-toolbar ${compact ? 'is-compact' : ''}`}>
        <label className="search-field">
          <Search size={14} />
          <input
            value={query}
            onChange={(event) => onQueryChange(event.target.value)}
            placeholder="Search fault, register, bus address, command…"
          />
        </label>

        {!compact ? (
          <div className="event-toolbar__row">
            <span className="event-toolbar__label">
              <Filter size={14} />
              Focus
            </span>
            <div className="chip-row">
              {availableFocusOptions.map((option) => {
                const Icon = option.icon
                return (
                  <button
                    key={option.value}
                    type="button"
                    className={option.value === effectiveFocusMode ? 'chip is-active' : 'chip'}
                    onClick={() => setFocusMode(option.value)}
                  >
                    <Icon size={14} />
                    {option.label}
                  </button>
                )
              })}
            </div>
          </div>
        ) : null}

        <div className="event-toolbar__row">
          <span className="event-toolbar__label">Severity</span>
          <div className="chip-row">
            {severityOptions.map((option) => (
              <button
                key={option}
                type="button"
                className={option === severity ? 'chip is-active' : 'chip'}
                onClick={() => onSeverityChange(option)}
              >
                {option}
              </button>
            ))}
          </div>
        </div>

        {!compact ? (
          <>
            <div className="event-toolbar__row">
              <span className="event-toolbar__label">Module</span>
              <div className="chip-row">
                {moduleOptions.map((option) => (
                  <button
                    key={option}
                    type="button"
                    className={option === moduleFilter ? 'chip is-active' : 'chip'}
                    onClick={() => onModuleFilterChange(option)}
                  >
                    {option === 'all' ? 'All modules' : titleCaseToken(option)}
                  </button>
                ))}
              </div>
            </div>

            <div className="event-toolbar__row event-toolbar__row--split">
              <div>
                <span className="event-toolbar__label">Source</span>
                <div className="chip-row">
                  {sourceOptions.map((option) => (
                    <button
                      key={option}
                      type="button"
                      className={option === sourceFilter ? 'chip is-active' : 'chip'}
                      onClick={() => setSourceFilter(option)}
                    >
                      {option === 'all' ? 'All sources' : formatSourceLabel(option)}
                    </button>
                  ))}
                </div>
              </div>

              <div className="event-sort">
                <span className="event-toolbar__label">Sort</span>
                <div className="segmented">
                  <button
                    type="button"
                    className={sortMode === 'latest' ? 'segmented__button is-active' : 'segmented__button'}
                    onClick={() => setSortMode('latest')}
                  >
                    <ArrowDownWideNarrow size={14} />
                    Latest first
                  </button>
                  <button
                    type="button"
                    className={sortMode === 'oldest' ? 'segmented__button is-active' : 'segmented__button'}
                    onClick={() => setSortMode('oldest')}
                  >
                    <ArrowUpWideNarrow size={14} />
                    Oldest first
                  </button>
                </div>
              </div>
            </div>
          </>
        ) : null}
      </div>

      <div className={`event-stream ${compact ? 'is-compact' : ''}`}>
        {visibleEvents.map((event, index) => {
          const renderKey = `${event.id}-${event.atIso}-${index}`
          const expanded = expandedEventId === renderKey
          const metadataTokens = uniqueTokens([
            event.module !== undefined ? titleCaseToken(event.module) : undefined,
            event.category !== undefined ? titleCaseToken(event.category) : undefined,
            event.bus !== undefined ? event.bus.toUpperCase() : undefined,
            event.operation !== undefined ? titleCaseToken(event.operation) : undefined,
            event.device,
            event.addressHex,
            event.registerName,
            event.registerHex,
            event.valueHex,
          ])

          return (
            <article
              key={renderKey}
              className={`event-card ${eventToneClass(event.severity)}`}
            >
              <button
                type="button"
                className="event-card__toggle"
                onClick={() => setExpandedEventId(expanded ? null : renderKey)}
              >
                <div className={`event-card__icon ${eventToneClass(event.severity)}`}>
                  {severityIcon(event.severity)}
                </div>

                <div className="event-card__body">
                  <div className="event-card__meta">
                    <span>{formatShortTime(event.atIso)}</span>
                    <span>{severityLabel(event.severity)}</span>
                    {event.source !== undefined ? <span>{formatSourceLabel(event.source)}</span> : null}
                    {event.module !== undefined ? <span>{titleCaseToken(event.module)}</span> : null}
                  </div>

                  <div className="event-card__headline">
                    <strong>{event.summary ?? event.title}</strong>
                    <span className={`status-badge ${event.severity === 'critical' ? 'is-critical' : event.severity === 'warn' ? 'is-warn' : 'is-on'}`}>
                      {severityLabel(event.severity)}
                    </span>
                  </div>

                  <p className="event-card__detail">{event.detail}</p>

                  {event.bus !== undefined && event.decodedDetail !== undefined ? (
                    <p className="event-card__decoded-preview">{event.decodedDetail}</p>
                  ) : null}

                  {metadataTokens.length > 0 ? (
                    <div className="event-card__tokens">
                      {metadataTokens
                        .slice(0, compact ? 4 : metadataTokens.length)
                        .map((token, tokenIndex) => (
                        <span key={`${renderKey}-${token}-${tokenIndex}`} className="inline-token">
                          {token}
                        </span>
                      ))}
                    </div>
                  ) : null}
                </div>

                <div className="event-card__expand">
                  {expanded ? <ChevronDown size={18} /> : <ChevronRight size={18} />}
                </div>
              </button>

              {expanded ? (
                <div className="event-card__details">
                  <div className="event-card__detail-grid">
                    <div>
                      <span>Title</span>
                      <strong>{event.title}</strong>
                    </div>
                    <div>
                      <span>Category</span>
                      <strong>{titleCaseToken(event.category)}</strong>
                    </div>
                    <div>
                      <span>Source</span>
                      <strong>{formatSourceLabel(event.source)}</strong>
                    </div>
                    <div>
                      <span>Time</span>
                      <strong>{formatShortTime(event.atIso)}</strong>
                    </div>
                    {event.device !== undefined ? (
                      <div>
                        <span>Device</span>
                        <strong>{event.device}</strong>
                      </div>
                    ) : null}
                    {event.addressHex !== undefined ? (
                      <div>
                        <span>Address</span>
                        <strong>{event.addressHex}</strong>
                      </div>
                    ) : null}
                    {event.registerName !== undefined || event.registerHex !== undefined ? (
                      <div>
                        <span>Register</span>
                        <strong>{event.registerName ?? event.registerHex}</strong>
                      </div>
                    ) : null}
                    {event.valueHex !== undefined ? (
                      <div>
                        <span>Value</span>
                        <strong>{event.valueHex}</strong>
                      </div>
                    ) : null}
                  </div>

                  {event.decodedDetail !== undefined ? (
                    <div className="event-card__decoded">
                      <span className="event-toolbar__label">Decoded from board and datasheet map</span>
                      <p>{event.decodedDetail}</p>
                    </div>
                  ) : null}
                </div>
              ) : null}
            </article>
          )
        })}

        {visibleEvents.length === 0 ? (
          <div className="timeline-empty">
            <Activity size={18} />
            <span>No events match the current filter.</span>
          </div>
        ) : null}
      </div>
    </section>
  )
}
