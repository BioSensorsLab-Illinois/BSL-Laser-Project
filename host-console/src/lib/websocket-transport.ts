import { interpretControllerLine } from './controller-protocol'
import type {
  CommandEnvelope,
  DeviceTransport,
  TransportMessage,
} from '../types'

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms))
}

export class WebSocketTransport implements DeviceTransport {
  readonly kind = 'wifi'

  readonly label = 'Wireless / WebSocket'

  readonly supportsFirmwareTransfer = false

  private listeners = new Set<(message: TransportMessage) => void>()

  private socket: WebSocket | null = null

  private active = false

  private disconnecting = false

  private eventSequence = 0

  private handshakeProbeTimers: number[] = []

  private readonly url: string

  constructor(url: string) {
    this.url = url
  }

  private nextEventId(kind: string): string {
    this.eventSequence += 1
    return `${Date.now()}-${this.eventSequence}-${kind}`
  }

  private clearHandshakeProbeTimers(): void {
    for (const timerId of this.handshakeProbeTimers) {
      window.clearTimeout(timerId)
    }
    this.handshakeProbeTimers = []
  }

  private scheduleHandshakeProbes(): void {
    this.clearHandshakeProbeTimers()

    const probes: Array<{ delayMs: number; cmd: 'get_status' | 'ping' }> = [
      { delayMs: 60, cmd: 'get_status' },
      { delayMs: 180, cmd: 'ping' },
      { delayMs: 420, cmd: 'ping' },
      { delayMs: 900, cmd: 'ping' },
      { delayMs: 1500, cmd: 'ping' },
      { delayMs: 2400, cmd: 'ping' },
    ]

    for (const probe of probes) {
      const timerId = window.setTimeout(() => {
        if (!this.active) {
          return
        }

        void this.sendCommand({
          id: 0,
          type: 'cmd',
          cmd: probe.cmd,
        }).catch(() => undefined)
      }, probe.delayMs)

      this.handshakeProbeTimers.push(timerId)
    }
  }

  private emit(message: TransportMessage): void {
    for (const listener of this.listeners) {
      listener(message)
    }
  }

  private handleLine(line: string): void {
    interpretControllerLine(line, {
      makeEventId: (kind) => this.nextEventId(kind),
      emit: (message) => this.emit(message),
      onProtocolReady: () => undefined,
    })
  }

  subscribe(listener: (message: TransportMessage) => void): () => void {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  async connect(): Promise<void> {
    if (this.active && this.socket !== null) {
      this.emit({
        kind: 'transport',
        status: 'connected',
        detail: 'Wireless socket already open.',
      })
      return
    }

    if (this.active || this.socket !== null) {
      await this.disconnect().catch(() => undefined)
    }

    this.emit({
      kind: 'transport',
      status: 'connecting',
      detail: `Opening wireless controller link at ${this.url}…`,
    })

    await new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url)
      this.socket = socket

      socket.addEventListener(
        'open',
        () => {
          this.active = true
          this.disconnecting = false
          this.emit({
            kind: 'transport',
            status: 'connected',
            detail: 'Wireless socket open. Waiting for controller firmware handshake…',
          })
          this.scheduleHandshakeProbes()
          resolve()
        },
        { once: true },
      )

      socket.addEventListener(
        'error',
        () => {
          reject(new Error(`Unable to open wireless controller socket at ${this.url}.`))
        },
        { once: true },
      )

      socket.addEventListener('message', (event) => {
        if (typeof event.data !== 'string') {
          return
        }

        for (const line of event.data.split('\n')) {
          const trimmed = line.trim()
          if (trimmed.length > 0) {
            this.handleLine(trimmed)
          }
        }
      })

      socket.addEventListener('close', () => {
        this.clearHandshakeProbeTimers()
        this.active = false
        this.socket = null

        if (!this.disconnecting) {
          this.emit({
            kind: 'transport',
            status: 'disconnected',
            detail:
              'Wireless controller link dropped. If the ESP32 restarted or the laptop left the bench AP, reconnect after the link recovers.',
          })
        }
      })
    }).catch((error) => {
      this.socket = null
      this.active = false
      this.disconnecting = false
      this.clearHandshakeProbeTimers()
      this.emit({
        kind: 'transport',
        status: 'error',
        detail: error instanceof Error ? error.message : 'Wireless controller link failed to open.',
      })
      throw error
    })
  }

  async disconnect(): Promise<void> {
    this.disconnecting = true
    this.active = false
    this.clearHandshakeProbeTimers()

    if (this.socket !== null) {
      const socket = this.socket
      this.socket = null
      socket.close()
      await sleep(80)
    }

    this.disconnecting = false
    this.emit({
      kind: 'transport',
      status: 'disconnected',
      detail: 'Wireless controller link closed.',
    })
  }

  async sendCommand(command: CommandEnvelope): Promise<void> {
    if (!this.active || this.socket === null || this.socket.readyState !== WebSocket.OPEN) {
      throw new Error('Wireless transport is not connected.')
    }

    this.socket.send(JSON.stringify(command))
  }
}
