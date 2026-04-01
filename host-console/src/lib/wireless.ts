import type { TransportKind } from '../types'

export const controllerBenchAp = {
  ssid: 'BSL-HTLS-Bench',
  password: 'bslbench2026',
  wsUrl: 'ws://192.168.4.1/ws',
} as const

export function transportLabel(kind: TransportKind): string {
  if (kind === 'mock') {
    return 'Mock rig'
  }

  if (kind === 'wifi') {
    return 'Wireless'
  }

  return 'Web Serial'
}
