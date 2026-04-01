import { useDeferredValue, useMemo, useState } from 'react'
import {
  ArrowDownLeft,
  ArrowUpRight,
  Cpu,
  Search,
  Waves,
} from 'lucide-react'

import {
  decodeBusText,
  describeI2cSelection,
  describeSpiSelection,
} from '../lib/event-decode'
import { formatShortTime } from '../lib/format'
import type { CommandHistoryEntry, SessionEvent } from '../types'

type BusTrafficViewerProps = {
  events: SessionEvent[]
  commands: CommandHistoryEntry[]
}

type BusDirectionFilter = 'all' | 'host-out' | 'device-in'
type BusFilter = 'all' | 'i2c' | 'spi'

type BusTrafficItem = {
  id: string
  atIso: string
  bus: 'i2c' | 'spi'
  direction: 'host-out' | 'device-in'
  module: string
  device: string
  title: string
  summary: string
  detail: string
  decodedDetail?: string
  addressHex?: string
  registerHex?: string
  registerName?: string
  valueHex?: string
  status?: string
}

function formatRegisterHex(value: number | string | undefined, width = 2): string | undefined {
  if (typeof value === 'string') {
    return value.toUpperCase()
  }

  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return undefined
  }

  return `0x${Math.max(0, Math.trunc(value)).toString(16).toUpperCase().padStart(width, '0')}`
}

function decodeBusCommand(entry: CommandHistoryEntry): BusTrafficItem | null {
  const args = entry.args ?? {}

  if (entry.cmd === 'i2c_scan') {
    return {
      id: `cmd-${entry.id}`,
      atIso: entry.issuedAtIso,
      bus: 'i2c',
      direction: 'host-out',
      module: 'bus',
      device: 'Shared I2C bus',
      title: 'Host I2C scan request',
      summary: 'Controller asked to probe the known shared I2C targets on GPIO4/GPIO5.',
      detail: entry.note,
      decodedDetail:
        'Expected shared-bus targets on this bench are STUSB4500 at 0x28, DAC80502 at 0x48, and DRV2605 at 0x5A.',
      status: entry.status,
    }
  }

  if (entry.cmd === 'i2c_read' || entry.cmd === 'i2c_write') {
    const info = describeI2cSelection(args.address as string | number | undefined, args.reg as string | number | undefined)
    const addressHex = formatRegisterHex(args.address as number | string | undefined)
    const registerHex = formatRegisterHex(args.reg as number | string | undefined)
    const valueHex = entry.cmd === 'i2c_write'
      ? formatRegisterHex(args.value as number | string | undefined, 4)
      : undefined
    const device = info.target?.device ?? `I2C ${addressHex ?? 'target'}`
    const registerLabel =
      info.registerName !== undefined
        ? `${info.registerName}${registerHex !== undefined ? ` (${registerHex})` : ''}`
        : registerHex ?? 'register'

    return {
      id: `cmd-${entry.id}`,
      atIso: entry.issuedAtIso,
      bus: 'i2c',
      direction: 'host-out',
      module: info.target?.module ?? 'bus',
      device,
      title: entry.cmd === 'i2c_read' ? 'Host I2C read request' : 'Host I2C write request',
      summary:
        entry.cmd === 'i2c_read'
          ? `Requested ${device} ${registerLabel} readback.`
          : `Requested ${device} ${registerLabel} write ${valueHex ?? ''}.`.trim(),
      detail: entry.note,
      decodedDetail: info.registerDetail ?? info.target?.benchRole,
      addressHex,
      registerHex,
      registerName: info.registerName,
      valueHex,
      status: entry.status,
    }
  }

  if (entry.cmd === 'spi_read' || entry.cmd === 'spi_write') {
    const info = describeSpiSelection(args.device as string | undefined, args.reg as string | number | undefined)
    const registerHex = formatRegisterHex(args.reg as number | string | undefined)
    const valueHex = entry.cmd === 'spi_write'
      ? formatRegisterHex(args.value as number | string | undefined, 2)
      : undefined
    const device = info.target?.device ?? String(args.device ?? 'SPI target')
    const registerLabel =
      info.registerName !== undefined
        ? `${info.registerName}${registerHex !== undefined ? ` (${registerHex})` : ''}`
        : registerHex ?? 'register'

    return {
      id: `cmd-${entry.id}`,
      atIso: entry.issuedAtIso,
      bus: 'spi',
      direction: 'host-out',
      module: info.target?.module ?? 'bus',
      device,
      title: entry.cmd === 'spi_read' ? 'Host SPI read request' : 'Host SPI write request',
      summary:
        entry.cmd === 'spi_read'
          ? `Requested ${device} ${registerLabel} readback.`
          : `Requested ${device} ${registerLabel} write ${valueHex ?? ''}.`.trim(),
      detail: entry.note,
      decodedDetail: info.registerDetail ?? info.target?.benchRole,
      registerHex,
      registerName: info.registerName,
      valueHex,
      status: entry.status,
    }
  }

  if (entry.cmd === 'refresh_pd_status') {
    return {
      id: `cmd-${entry.id}`,
      atIso: entry.issuedAtIso,
      bus: 'i2c',
      direction: 'host-out',
      module: 'pd',
      device: 'STUSB4500',
      title: 'Host PD refresh request',
      summary: 'Controller asked to refresh STUSB4500 CC, PDO, and RDO readback.',
      detail: entry.note,
      decodedDetail:
        'This refresh path re-reads live STUSB4500 register state such as CC_STATUS, DPM_PDO_NUMB, the runtime PDO block, and the negotiated RDO.',
      status: entry.status,
    }
  }

  return null
}

function eventToBusTrafficItem(event: SessionEvent): BusTrafficItem | null {
  if (event.bus === undefined) {
    const decoded = decodeBusText(event.detail)
    if (decoded === null || decoded.bus === undefined) {
      return null
    }

    return {
      id: event.id,
      atIso: event.atIso,
      bus: decoded.bus,
      direction: 'device-in',
      module: decoded.module ?? 'bus',
      device: decoded.device ?? decoded.module ?? 'bus',
      title: decoded.title,
      summary: decoded.summary ?? decoded.title,
      detail: decoded.detail,
      decodedDetail: decoded.decodedDetail,
      addressHex: decoded.addressHex,
      registerHex: decoded.registerHex,
      registerName: decoded.registerName,
      valueHex: decoded.valueHex,
      status: event.severity,
    }
  }

  return {
    id: event.id,
    atIso: event.atIso,
    bus: event.bus,
    direction: 'device-in',
    module: event.module ?? 'bus',
    device: event.device ?? event.module ?? 'bus',
    title: event.title,
    summary: event.summary ?? event.title,
    detail: event.detail,
    decodedDetail: event.decodedDetail,
    addressHex: event.addressHex,
    registerHex: event.registerHex,
    registerName: event.registerName,
    valueHex: event.valueHex,
    status: event.severity,
  }
}

export function BusTrafficViewer({
  events,
  commands,
}: BusTrafficViewerProps) {
  const [query, setQuery] = useState('')
  const [busFilter, setBusFilter] = useState<BusFilter>('all')
  const [directionFilter, setDirectionFilter] = useState<BusDirectionFilter>('all')
  const [moduleFilter, setModuleFilter] = useState('all')
  const [deviceFilter, setDeviceFilter] = useState('all')
  const deferredQuery = useDeferredValue(query)

  const busItems = useMemo(() => {
    const eventItems = events
      .map((event) => eventToBusTrafficItem(event))
      .filter((item): item is BusTrafficItem => item !== null)

    const commandItems = commands
      .map((entry) => decodeBusCommand(entry))
      .filter((item): item is BusTrafficItem => item !== null)

    return [...eventItems, ...commandItems].sort(
      (left, right) => Date.parse(right.atIso) - Date.parse(left.atIso),
    )
  }, [commands, events])

  const moduleOptions = useMemo(() => {
    return ['all', ...Array.from(new Set(busItems.map((item) => item.module))).sort()]
  }, [busItems])

  const deviceOptions = useMemo(() => {
    return ['all', ...Array.from(new Set(busItems.map((item) => item.device))).sort()]
  }, [busItems])

  const filteredItems = useMemo(() => {
    const search = deferredQuery.trim().toLowerCase()

    return busItems.filter((item) => {
      if (busFilter !== 'all' && item.bus !== busFilter) {
        return false
      }

      if (directionFilter !== 'all' && item.direction !== directionFilter) {
        return false
      }

      if (moduleFilter !== 'all' && item.module !== moduleFilter) {
        return false
      }

      if (deviceFilter !== 'all' && item.device !== deviceFilter) {
        return false
      }

      if (search.length === 0) {
        return true
      }

      const haystack = [
        item.title,
        item.summary,
        item.detail,
        item.decodedDetail,
        item.device,
        item.module,
        item.addressHex,
        item.registerHex,
        item.registerName,
        item.valueHex,
      ]
        .filter((value): value is string => value !== undefined)
        .join(' ')
        .toLowerCase()

      return haystack.includes(search)
    })
  }, [busFilter, busItems, deferredQuery, deviceFilter, directionFilter, moduleFilter])

  return (
    <section className="panel-section bus-traffic-panel">
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Decoded bus traffic</p>
          <h2>SPI and I2C communications</h2>
        </div>
        <div className="bus-traffic-panel__summary">
          <span>{filteredItems.length} shown</span>
          <span>{busItems.length} total</span>
        </div>
      </div>

      <p className="panel-note">
        Host-out rows are the GUI requests sent to the controller. Device-in rows are
        decoded peripheral responses and scan/readback text returned by firmware.
      </p>

      <div className="bus-traffic-toolbar">
        <label className="search-field">
          <Search size={15} />
          <input
            value={query}
            onChange={(event) => setQuery(event.target.value)}
            placeholder="Search register, address, module, or decode..."
          />
        </label>

        <select value={busFilter} onChange={(event) => setBusFilter(event.target.value as BusFilter)}>
          <option value="all">All buses</option>
          <option value="i2c">I2C only</option>
          <option value="spi">SPI only</option>
        </select>

        <select
          value={directionFilter}
          onChange={(event) => setDirectionFilter(event.target.value as BusDirectionFilter)}
        >
          <option value="all">In + out</option>
          <option value="host-out">Host out</option>
          <option value="device-in">Device in</option>
        </select>

        <select value={moduleFilter} onChange={(event) => setModuleFilter(event.target.value)}>
          {moduleOptions.map((option) => (
            <option key={option} value={option}>
              {option === 'all' ? 'All modules' : option}
            </option>
          ))}
        </select>

        <select value={deviceFilter} onChange={(event) => setDeviceFilter(event.target.value)}>
          {deviceOptions.map((option) => (
            <option key={option} value={option}>
              {option === 'all' ? 'All devices' : option}
            </option>
          ))}
        </select>
      </div>

      <div className="bus-traffic-list">
        {filteredItems.map((item) => (
          <article key={item.id} className={`bus-traffic-item is-${item.bus}`}>
            <div className="bus-traffic-item__head">
              <div className="bus-traffic-item__meta">
                <span className="eyebrow">
                  {formatShortTime(item.atIso)} · {item.bus.toUpperCase()} · {item.module}
                </span>
                <strong>{item.title}</strong>
              </div>
              <div className="bus-traffic-item__chips">
                <span className={item.direction === 'host-out' ? 'status-badge is-warn' : 'status-badge is-on'}>
                  {item.direction === 'host-out' ? (
                    <>
                      <ArrowUpRight size={12} />
                      <span>Host out</span>
                    </>
                  ) : (
                    <>
                      <ArrowDownLeft size={12} />
                      <span>Device in</span>
                    </>
                  )}
                </span>
                <span className="status-badge">
                  {item.bus === 'i2c' ? <Waves size={12} /> : <Cpu size={12} />}
                  <span>{item.device}</span>
                </span>
              </div>
            </div>

            <p className="bus-traffic-item__summary">{item.summary}</p>
            <p className="bus-traffic-item__detail">{item.detail}</p>

            {(item.addressHex !== undefined ||
              item.registerHex !== undefined ||
              item.valueHex !== undefined) ? (
              <div className="bus-traffic-item__facts">
                {item.addressHex !== undefined ? <span>ADDR {item.addressHex}</span> : null}
                {item.registerHex !== undefined ? (
                  <span>
                    REG {item.registerName !== undefined ? `${item.registerName} ` : ''}
                    {item.registerHex}
                  </span>
                ) : null}
                {item.valueHex !== undefined ? <span>DATA {item.valueHex}</span> : null}
                {item.status !== undefined ? <span>STATE {item.status}</span> : null}
              </div>
            ) : null}

            {item.decodedDetail !== undefined && item.decodedDetail.length > 0 ? (
              <div className="bus-traffic-item__decode">
                <strong>Decoded meaning</strong>
                <p>{item.decodedDetail}</p>
              </div>
            ) : null}
          </article>
        ))}

        {filteredItems.length === 0 ? (
          <div className="empty-state">
            <Waves size={16} />
            <span>No bus traffic matches the current filters.</span>
          </div>
        ) : null}
      </div>
    </section>
  )
}
