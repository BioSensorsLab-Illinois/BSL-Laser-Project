import type {
  BridgeEvent,
  CommandEnvelope,
  FirmwareInspection,
  WorkbenchState,
} from '../domain/model'
import { createBrowserMockBridge } from './browserMock'
import { createTauriBridge } from './tauriBridge'

export interface ConsoleBridge {
  kind: 'browser-mock' | 'tauri'
  start(listener: (event: BridgeEvent) => void): Promise<void>
  stop(): Promise<void>
  listSerialPorts(): Promise<Array<{ name: string; label: string }>>
  connectSerial(port: string): Promise<void>
  disconnectSerial(): Promise<void>
  connectWireless(url: string): Promise<void>
  disconnectWireless(): Promise<void>
  sendCommand(channel: 'serial' | 'wireless', envelope: CommandEnvelope): Promise<void>
  inspectFirmware(path: string): Promise<FirmwareInspection>
  flashFirmware(port: string, path: string): Promise<void>
  exportSession(path: string, payload: WorkbenchState): Promise<void>
  writeSessionAutosave(path: string, payload: WorkbenchState): Promise<void>
  readSessionFile(path: string): Promise<string>
}

declare global {
  interface Window {
    __TAURI_INTERNALS__?: unknown
  }
}

export function createBridge(): ConsoleBridge {
  if (typeof window !== 'undefined' && window.__TAURI_INTERNALS__ !== undefined) {
    return createTauriBridge()
  }

  return createBrowserMockBridge()
}
