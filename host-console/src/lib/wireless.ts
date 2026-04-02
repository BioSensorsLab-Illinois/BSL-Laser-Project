import type { TransportKind, WirelessScanNetwork, WirelessStatus } from '../types'

export const controllerBenchAp = {
  ssid: 'BSL-HTLS-Bench',
  password: 'bslbench2026',
  wsUrl: 'ws://192.168.4.1/ws',
} as const

export function wirelessModeLabel(status: WirelessStatus): string {
  return status.mode === 'station' ? 'Existing Wi-Fi' : 'Bench AP'
}

export function preferredWirelessUrl(status: WirelessStatus): string {
  if (status.wsUrl.trim().length > 0) {
    return status.wsUrl
  }

  return controllerBenchAp.wsUrl
}

export function wirelessConnectionStateLabel(status: WirelessStatus): string {
  if (status.mode === 'softap') {
    return status.apReady ? 'Bench AP online' : 'Bench AP idle'
  }

  if (status.stationConnected) {
    return 'Joined network'
  }

  if (status.stationConnecting) {
    return 'Associating'
  }

  if (status.stationConfigured) {
    return 'Saved, waiting'
  }

  return 'Station unset'
}

export function wirelessSignalPercent(rssiDbm: number): number {
  if (!Number.isFinite(rssiDbm) || rssiDbm === 0) {
    return 0
  }

  if (rssiDbm >= -50) {
    return 100
  }

  if (rssiDbm <= -90) {
    return 0
  }

  return Math.round(((rssiDbm + 90) / 40) * 100)
}

export function wirelessSignalLabel(status: WirelessStatus): string {
  if (status.mode === 'softap') {
    return `Channel 6 · ${status.clientCount} client${status.clientCount === 1 ? '' : 's'}`
  }

  if (!status.stationConnected || status.stationRssiDbm === 0) {
    return status.stationChannel > 0
      ? `Channel ${status.stationChannel}`
      : 'Signal unavailable'
  }

  const percent = wirelessSignalPercent(status.stationRssiDbm)
  const quality =
    percent >= 80 ? 'Excellent'
      : percent >= 60 ? 'Good'
        : percent >= 35 ? 'Fair'
          : 'Weak'

  return `${quality} · ${status.stationRssiDbm} dBm`
}

export function sortWirelessScanNetworks(
  networks: WirelessScanNetwork[],
): WirelessScanNetwork[] {
  return [...networks]
    .filter((network) => network.ssid.trim().length > 0)
    .sort((left, right) => right.rssiDbm - left.rssiDbm)
}

export function transportLabel(kind: TransportKind): string {
  if (kind === 'mock') {
    return 'Mock rig'
  }

  if (kind === 'wifi') {
    return 'Wireless'
  }

  return 'Web Serial'
}
