import { ESPLoader, Transport as EspLoaderTransport } from 'esptool-js'

import { interpretControllerLine } from './controller-protocol'
import type {
  CommandEnvelope,
  DeviceTransport,
  FirmwarePackageDescriptor,
  FirmwareTransferProgress,
  TransportMessage,
} from '../types'

type SerialLikePort = {
  open(options: {
    baudRate: number
    dataBits?: number
    stopBits?: number
    parity?: 'none' | 'even' | 'odd'
    bufferSize?: number
    flowControl?: 'none' | 'hardware'
  }): Promise<void>
  close(): Promise<void>
  readable: ReadableStream<Uint8Array> | null
  writable: WritableStream<Uint8Array> | null
  setSignals?(signals: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void>
  getInfo?(): { usbVendorId?: number; usbProductId?: number }
}

type SerialLikeNavigator = Navigator & {
  serial?: {
    getPorts?(): Promise<SerialLikePort[]>
    requestPort(options?: {
      filters?: Array<{ usbVendorId?: number; usbProductId?: number }>
    }): Promise<SerialLikePort>
  }
}

type FlashSerialPort = ConstructorParameters<typeof EspLoaderTransport>[0]
const ESP_USB_JTAG_SERIAL_PID = 0x1001
type ResettableSerialTransport = {
  setRTS(state: boolean): Promise<void>
  setDTR(state: boolean): Promise<void>
}

function nowIso(): string {
  return new Date().toISOString()
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms))
}

async function runApplicationReset(transport: ResettableSerialTransport): Promise<void> {
  await transport.setDTR(false)
  await transport.setRTS(true)
  await sleep(120)
  await transport.setRTS(false)
  await sleep(220)
}

async function pulseApplicationResetPort(port: SerialLikePort): Promise<void> {
  if (typeof port.setSignals !== 'function') {
    throw new Error('This serial port does not support control-line reset signalling.')
  }

  await runApplicationReset({
    setRTS: async (state) => {
      await port.setSignals?.({ requestToSend: state })
    },
    setDTR: async (state) => {
      await port.setSignals?.({ dataTerminalReady: state })
    },
  })
}

function portInfoKey(port: SerialLikePort | null): string {
  if (port?.getInfo === undefined) {
    return ''
  }

  const info = port.getInfo()
  return `${info.usbVendorId ?? 'na'}:${info.usbProductId ?? 'na'}`
}

export class WebSerialTransport implements DeviceTransport {
  readonly kind = 'serial'

  readonly label = 'USB CDC / Web Serial'

  readonly supportsFirmwareTransfer = true

  private listeners = new Set<(message: TransportMessage) => void>()

  private port: SerialLikePort | null = null

  private active = false

  private disconnecting = false

  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null

  private readLoopTask: Promise<void> | null = null

  private eventSequence = 0

  private handshakeProbeTimers: number[] = []

  private protocolReady = false

  private nextEventId(kind: string): string {
    this.eventSequence += 1
    return `${Date.now()}-${this.eventSequence}-${kind}`
  }

  private async waitForProtocolReady(timeoutMs: number): Promise<boolean> {
    const deadline = Date.now() + timeoutMs

    while (Date.now() < deadline) {
      if (this.protocolReady) {
        return true
      }

      await sleep(120)
    }

    return this.protocolReady
  }

  private clearHandshakeProbeTimers(): void {
    for (const timerId of this.handshakeProbeTimers) {
      window.clearTimeout(timerId)
    }
    this.handshakeProbeTimers = []
  }

  private scheduleHandshakeProbes(port: SerialLikePort): void {
    this.clearHandshakeProbeTimers()

    const probes: Array<{ delayMs: number; cmd: 'get_status' | 'ping' }> = [
      { delayMs: 150, cmd: 'get_status' },
      { delayMs: 450, cmd: 'ping' },
      { delayMs: 900, cmd: 'ping' },
      { delayMs: 1600, cmd: 'ping' },
      { delayMs: 2600, cmd: 'ping' },
      { delayMs: 3800, cmd: 'ping' },
    ]

    for (const probe of probes) {
      const timerId = window.setTimeout(() => {
        if (!this.active || this.port !== port) {
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

  private async openPort(
    port: SerialLikePort,
    connectingDetail: string,
  ): Promise<'connected' | 'retry'> {
    this.emit({
      kind: 'transport',
      status: 'connecting',
      detail: connectingDetail,
    })

    try {
      await port.open({
        baudRate: 115200,
        dataBits: 8,
        stopBits: 1,
        parity: 'none',
        flowControl: 'none',
      })
      this.port = port
    } catch (error) {
      this.port = null
      this.emit({
        kind: 'transport',
        status: 'disconnected',
        detail:
          error instanceof Error
            ? error.message
            : 'Unable to open the serial port yet.',
      })
      return 'retry'
    }

    this.active = true
    this.disconnecting = false
    this.protocolReady = false
    this.emit({
      kind: 'transport',
      status: 'connected',
      detail: 'Serial port opened at 115200 baud.',
    })
    this.readLoopTask = this.readLoop()
    this.scheduleHandshakeProbes(port)

    return 'connected'
  }

  private async acquirePort(serialNavigator: SerialLikeNavigator): Promise<SerialLikePort> {
    const grantedPorts =
      typeof serialNavigator.serial?.getPorts === 'function'
        ? await serialNavigator.serial.getPorts()
        : []

    if (grantedPorts.length > 0) {
      const existingKey = portInfoKey(this.port)
      const matchedPort =
        existingKey.length > 0
          ? grantedPorts.find((port) => portInfoKey(port) === existingKey) ?? grantedPorts[0]
          : grantedPorts[0]

      this.port = matchedPort
      return matchedPort
    }

    if (this.port !== null) {
      return this.port
    }

    this.port = await serialNavigator.serial!.requestPort()
    return this.port
  }

  subscribe(listener: (message: TransportMessage) => void): () => void {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  async connect(): Promise<void> {
    if (this.active && this.port !== null) {
      this.emit({
        kind: 'transport',
        status: 'connected',
        detail: 'Serial port already open.',
      })
      return
    }

    const serialNavigator = navigator as SerialLikeNavigator

    if (serialNavigator.serial === undefined) {
      this.emit({
        kind: 'transport',
        status: 'error',
        detail: 'Web Serial is unavailable. Use a Chromium browser or switch to mock mode.',
      })
      return
    }

    try {
      const port = await this.acquirePort(serialNavigator)
      await this.openPort(
        port,
        this.port === null
          ? 'Opening a browser serial port…'
          : 'Reopening the last serial port…',
      )
    } catch (error) {
      this.emit({
        kind: 'transport',
        status: 'error',
        detail:
          error instanceof Error
            ? error.message
            : 'Unable to open the serial port.',
      })
      return
    }
  }

  async reconnectSilently(): Promise<'connected' | 'retry' | 'no_grant'> {
    if (this.active && this.port !== null) {
      this.emit({
        kind: 'transport',
        status: 'connected',
        detail: 'Serial port already open.',
      })
      return 'connected'
    }

    const serialNavigator = navigator as SerialLikeNavigator

    if (serialNavigator.serial === undefined) {
      this.emit({
        kind: 'transport',
        status: 'error',
        detail: 'Web Serial is unavailable. Use a Chromium browser or switch to mock mode.',
      })
      return 'retry'
    }

    const grantedPorts =
      typeof serialNavigator.serial.getPorts === 'function'
        ? await serialNavigator.serial.getPorts()
        : []

    const existingKey = portInfoKey(this.port)
    const candidatePort =
      grantedPorts.length > 0
        ? existingKey.length > 0
          ? grantedPorts.find((port) => portInfoKey(port) === existingKey) ?? grantedPorts[0]
          : grantedPorts[0]
        : this.port

    if (candidatePort === null) {
      this.emit({
        kind: 'transport',
        status: 'disconnected',
        detail: 'No previously granted board port. Click Connect board once to approve Web Serial.',
      })
      return 'no_grant'
    }

    return this.openPort(candidatePort, 'Reconnecting to the last board port…')
  }

  async disconnect(): Promise<void> {
    if (!this.active && this.port === null) {
      this.clearHandshakeProbeTimers()
      this.protocolReady = false
      this.emit({
        kind: 'transport',
        status: 'disconnected',
        detail: 'Serial port closed.',
      })
      return
    }

    this.disconnecting = true
    this.active = false
    this.clearHandshakeProbeTimers()
    this.protocolReady = false

    if (this.reader !== null) {
      await this.reader.cancel().catch(() => undefined)
    }

    if (this.readLoopTask !== null) {
      await this.readLoopTask.catch(() => undefined)
    }

    if (this.port !== null) {
      await this.port.close().catch(() => undefined)
    }

    this.port = null
    this.reader = null
    this.readLoopTask = null
    this.disconnecting = false
    this.clearHandshakeProbeTimers()
    this.protocolReady = false

    this.emit({
      kind: 'transport',
      status: 'disconnected',
      detail: 'Serial port closed.',
    })
  }

  async sendCommand(command: CommandEnvelope): Promise<void> {
    if (!this.active) {
      throw new Error('Serial transport is not connected.')
    }

    if (this.port?.writable === null || this.port?.writable === undefined) {
      throw new Error('Serial transport is not writable.')
    }

    const writer = this.port.writable.getWriter()
    const encoder = new TextEncoder()

    try {
      const payload = JSON.stringify(command)
      await writer.write(encoder.encode(`${payload}\n`))
    } finally {
      writer.releaseLock()
    }
  }

  async beginFirmwareTransfer(
    pkg: FirmwarePackageDescriptor,
    onProgress: (progress: FirmwareTransferProgress) => void,
  ): Promise<void> {
    if (!pkg.webFlash.supported || pkg.webFlash.images.length === 0) {
      throw new Error(pkg.webFlash.note)
    }

    const serialNavigator = navigator as SerialLikeNavigator
    if (serialNavigator.serial === undefined) {
      throw new Error('Web Serial is unavailable in this browser.')
    }

    const missingData = pkg.webFlash.images.some((image) => image.data === undefined)
    if (missingData) {
      throw new Error('This package does not contain local binary payloads for browser flashing.')
    }

    const port = await this.acquirePort(serialNavigator)
    const portKey = portInfoKey(port)

    onProgress({
      phase: 'prepare',
      percent: 4,
      detail: 'Releasing the live serial monitor so the ESP bootloader can take over.',
    })
    await this.disconnect()
    await sleep(220)

    const terminal = {
      clean() {
        // no-op for host GUI progress
      },
      writeLine: (data: string) => {
        const line = data.trim()
        if (line.length === 0) {
          return
        }

        onProgress({
          phase: 'bootloader',
          percent: 10,
          detail: line,
        })
      },
      write: (data: string) => {
        const line = data.trim()
        if (line.length === 0) {
          return
        }

        onProgress({
          phase: 'bootloader',
          percent: 10,
          detail: line,
        })
      },
    }

    const flashTransport = new EspLoaderTransport(port as unknown as FlashSerialPort, false)
    const usingUsbJtagSerial = port.getInfo?.().usbProductId === ESP_USB_JTAG_SERIAL_PID
    const loader = new ESPLoader({
      transport: flashTransport,
      baudrate: 115200,
      terminal,
      debugLogging: false,
    })

    let flashSessionClosed = false

    try {
      const chipName = await loader.main()
      onProgress({
        phase: 'connect',
        percent: 14,
        detail: `Connected to ${chipName}.`,
      })

      const imageCount = pkg.webFlash.images.length
      await loader.writeFlash({
        fileArray: pkg.webFlash.images.map((image) => ({
          data: image.data!,
          address: image.address,
        })),
        flashMode: 'keep',
        flashFreq: 'keep',
        flashSize: 'keep',
        eraseAll: pkg.webFlash.eraseAll,
        compress: true,
        reportProgress: (fileIndex, written, total) => {
          const fileShare = 72 / imageCount
          const fileBase = 18 + fileShare * fileIndex
          const percent = total > 0 ? written / total : 0
          onProgress({
            phase: 'write',
            percent: Math.min(95, Math.round(fileBase + percent * fileShare)),
            detail: `Writing ${pkg.webFlash.images[fileIndex]?.name ?? `segment ${fileIndex + 1}`} to ${pkg.webFlash.images[fileIndex] ? `0x${pkg.webFlash.images[fileIndex]!.address.toString(16)}` : 'flash'} (${Math.round(percent * 100)}%).`,
          })
        },
      })

      onProgress({
        phase: 'reset',
        percent: 97,
        detail: usingUsbJtagSerial
          ? 'Flash write finished. Rebooting the ESP32-S3 into application mode.'
          : 'Flash write finished. Resetting the ESP32-S3.',
      })
      if (usingUsbJtagSerial) {
        await runApplicationReset(flashTransport as unknown as ResettableSerialTransport)
      } else {
        await loader.after('hard_reset')
      }
      await sleep(300)

      await flashTransport.disconnect().catch(() => undefined)
      flashSessionClosed = true
      this.port = null
      this.active = false
      this.disconnecting = false
      this.protocolReady = false

      onProgress({
        phase: 'verify',
        percent: 98,
        detail: 'Flash write finished. Waiting for the controller firmware to resume the host protocol.',
      })

      const verified = await this.reconnectAfterFlash(serialNavigator, portKey, onProgress)
      if (!verified) {
        throw new Error(
          'Flash write completed, but the controller never resumed the host protocol afterward. The board may still be in ROM bootloader or the post-flash reset path did not return to the application cleanly.',
        )
      }

      onProgress({
        phase: 'complete',
        percent: 100,
        detail: 'Browser flash complete and controller protocol verified.',
      })

      this.emit({
        kind: 'event',
        event: {
          id: this.nextEventId('firmware'),
          atIso: nowIso(),
          severity: 'ok',
          category: 'firmware',
          title: 'Browser flash complete',
          detail: `${pkg.packageName} ${pkg.version} was written successfully over Web Serial and resumed the host protocol.`,
          module: 'firmware',
          source: 'host',
          operation: 'transfer',
          summary: 'Browser flash complete',
        },
      })
    } catch (error) {
      onProgress({
        phase: 'error',
        percent: 100,
        detail: error instanceof Error ? error.message : 'Browser flash failed.',
      })
      throw error
    } finally {
      this.clearHandshakeProbeTimers()
      if (!flashSessionClosed) {
        await flashTransport.disconnect().catch(() => undefined)
      }
      if (!this.active) {
        this.port = null
        this.active = false
        this.disconnecting = false
        this.protocolReady = false
        this.emit({
          kind: 'transport',
          status: 'disconnected',
          detail:
            'Flash session finished. The board will re-enumerate after reset; reconnect when the USB port returns.',
        })
      }
    }
  }

  private async reconnectAfterFlash(
    serialNavigator: SerialLikeNavigator,
    portKey: string,
    onProgress: (progress: FirmwareTransferProgress) => void,
  ): Promise<boolean> {
    const deadline = Date.now() + 16000
    let attempt = 0

    while (Date.now() < deadline) {
      attempt += 1
      const grantedPorts =
        typeof serialNavigator.serial?.getPorts === 'function'
          ? await serialNavigator.serial.getPorts()
          : []

      const candidatePort =
        grantedPorts.find((candidate) => portInfoKey(candidate) === portKey) ??
        grantedPorts[0] ??
        null

      if (candidatePort === null) {
        onProgress({
          phase: 'verify',
          percent: 98,
          detail: `Waiting for the board USB port to reappear after reset (attempt ${attempt}).`,
        })
        await sleep(350)
        continue
      }

      const openResult = await this.openPort(
        candidatePort,
        `Reconnecting to the controller after browser flash (attempt ${attempt})…`,
      )
      if (openResult !== 'connected') {
        await sleep(400)
        continue
      }

      onProgress({
        phase: 'verify',
        percent: 99,
        detail: `Board port reopened. Verifying controller protocol (attempt ${attempt})…`,
      })

      const ready = await this.waitForProtocolReady(5000)
      if (ready) {
        return true
      }

      if (candidatePort.getInfo?.().usbProductId === ESP_USB_JTAG_SERIAL_PID) {
        onProgress({
          phase: 'verify',
          percent: 99,
          detail:
            'Board port reopened, but the host protocol is still silent. Trying one extra application reboot pulse before failing this reconnect attempt.',
        })

        await pulseApplicationResetPort(candidatePort).catch(() => undefined)
        this.clearHandshakeProbeTimers()
        this.scheduleHandshakeProbes(candidatePort)

        const readyAfterExtraReset = await this.waitForProtocolReady(5000)
        if (readyAfterExtraReset) {
          return true
        }
      }

      await this.disconnect().catch(() => undefined)
      onProgress({
        phase: 'verify',
        percent: 98,
        detail: `Controller protocol did not resume yet. Retrying reconnect (${attempt}).`,
      })
      await sleep(400)
    }

    return false
  }

  private async readLoop(): Promise<void> {
    if (this.port?.readable === null || this.port?.readable === undefined) {
      this.readLoopTask = null
      return
    }

    const reader = this.port.readable.getReader()
    this.reader = reader
    const decoder = new TextDecoder()
    let buffer = ''

    try {
      while (this.active) {
        const { value, done } = await reader.read()

        if (done) {
          break
        }

        if (value === undefined) {
          continue
        }

        buffer += decoder.decode(value, { stream: true })

        for (;;) {
          const newlineIndex = buffer.indexOf('\n')

          if (newlineIndex === -1) {
            break
          }

          const line = buffer.slice(0, newlineIndex).trim()
          buffer = buffer.slice(newlineIndex + 1)

          if (line.length > 0) {
            this.handleLine(line)
          }
        }
      }
    } catch (error) {
      if (!this.disconnecting) {
        this.emit({
          kind: 'transport',
          status: 'error',
          detail:
            error instanceof Error
              ? error.message
              : 'Serial read loop failed.',
        })
      }
    } finally {
      this.clearHandshakeProbeTimers()
      reader.releaseLock()
      this.reader = null
      this.readLoopTask = null
      if (this.active) {
        this.active = false
        this.emit({
          kind: 'transport',
          status: 'disconnected',
          detail:
            'Serial link dropped. If you pressed RST or entered download mode, this is expected. Click Connect board after the USB port comes back.',
        })
      }
    }
  }

  private handleLine(line: string): void {
    interpretControllerLine(line, {
      makeEventId: (kind) => this.nextEventId(kind),
      emit: (message) => this.emit(message),
      onProtocolReady: () => {
        this.protocolReady = true
      },
    })
  }

  private emit(message: TransportMessage): void {
    for (const listener of this.listeners) {
      listener(message)
    }
  }
}
