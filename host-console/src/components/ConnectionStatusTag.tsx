import { Cable, Wifi } from 'lucide-react'

import { transportLabel } from '../lib/wireless'
import type { DeviceSnapshot, TransportKind, TransportStatus } from '../types'

type ConnectionStatusTagProps = {
  snapshot: DeviceSnapshot
  transportKind: TransportKind
  transportStatus: TransportStatus
  transportDetail: string
}

function describeConnection(
  snapshot: DeviceSnapshot,
  transportKind: TransportKind,
  transportStatus: TransportStatus,
  transportDetail: string,
): {
  tone: 'connected' | 'connecting' | 'error' | 'disconnected'
  label: string
  detail: string
  wireless: boolean
} {
  if (transportStatus === 'connected') {
    if (transportKind === 'wifi') {
      if (snapshot.wireless.mode === 'station' && snapshot.wireless.stationConnected) {
        return {
          tone: 'connected',
          label: 'Connected via WiFi',
          detail: snapshot.wireless.ssid || snapshot.wireless.wsUrl,
          wireless: true,
        }
      }

      return {
        tone: 'connected',
        label: 'Connected via AP',
        detail: snapshot.wireless.wsUrl || snapshot.wireless.ssid,
        wireless: true,
      }
    }

    return {
      tone: 'connected',
      label: transportKind === 'mock' ? 'Connected to mock rig' : 'Connected via USB',
      detail: transportKind === 'mock' ? 'Logic-only controller session' : 'Web Serial session active',
      wireless: false,
    }
  }

  if (transportStatus === 'connecting') {
    return {
      tone: 'connecting',
      label:
        transportKind === 'wifi'
          ? snapshot.wireless.mode === 'station'
            ? 'Connecting via WiFi'
            : 'Connecting via AP'
          : transportKind === 'mock'
            ? 'Starting mock rig'
            : 'Connecting via USB',
      detail: transportDetail,
      wireless: transportKind === 'wifi',
    }
  }

  if (transportStatus === 'error') {
    return {
      tone: 'error',
      label: 'Connection failed',
      detail: snapshot.wireless.lastError.trim() || transportDetail || 'Check transport and retry.',
      wireless: transportKind === 'wifi',
    }
  }

  return {
    tone: 'disconnected',
    label: 'Link offline',
    detail: `${transportLabel(transportKind)} selected`,
    wireless: transportKind === 'wifi',
  }
}

export function ConnectionStatusTag({
  snapshot,
  transportKind,
  transportStatus,
  transportDetail,
}: ConnectionStatusTagProps) {
  const status = describeConnection(snapshot, transportKind, transportStatus, transportDetail)
  const Icon = status.wireless ? Wifi : Cable

  return (
    <div
      className={`connection-status-tag is-${status.tone}`}
      aria-live="polite"
      aria-atomic="true"
    >
      <div className="connection-status-tag__icon">
        <Icon size={14} />
      </div>
      <div className="connection-status-tag__copy">
        <strong>{status.label}</strong>
        <small>{status.detail}</small>
      </div>
    </div>
  )
}
