import { invoke } from '@tauri-apps/api/core'
import { listen } from '@tauri-apps/api/event'
import type { UnlistenFn } from '@tauri-apps/api/event'

import type {
  BridgeEvent,
  FirmwareInspection,
} from '../domain/model'
import type { ConsoleBridge } from './bridge'

const EVENT_NAME = 'bsl://bridge'

export function createTauriBridge(): ConsoleBridge {
  let unlisten: UnlistenFn | null = null

  return {
    kind: 'tauri',
    async start(listener) {
      unlisten = await listen<BridgeEvent>(EVENT_NAME, (event) => {
        listener(event.payload)
      })
    },
    async stop() {
      if (unlisten !== null) {
        unlisten()
        unlisten = null
      }
    },
    listSerialPorts() {
      return invoke<Array<{ name: string; label: string }>>('serial_list_ports')
    },
    connectSerial(port) {
      return invoke('serial_connect', { port, baudRate: 115200 })
    },
    disconnectSerial() {
      return invoke('serial_disconnect')
    },
    connectWireless(url) {
      return invoke('wireless_connect', { url })
    },
    disconnectWireless() {
      return invoke('wireless_disconnect')
    },
    sendCommand(channel, envelope) {
      return invoke('transport_send_command', { channel, envelope })
    },
    inspectFirmware(path) {
      return invoke<FirmwareInspection>('inspect_firmware', { path })
    },
    flashFirmware(port, path) {
      return invoke('flash_firmware', { request: { port, path } })
    },
    exportSession(path, payload) {
      return invoke('export_session', {
        path,
        payload: JSON.stringify(payload, null, 2),
      })
    },
    writeSessionAutosave(path, payload) {
      return invoke('write_session_autosave', {
        request: {
          path,
          payload: JSON.stringify(payload, null, 2),
        },
      })
    },
    readSessionFile(path) {
      return invoke<string>('read_session_file', { path })
    },
  }
}
