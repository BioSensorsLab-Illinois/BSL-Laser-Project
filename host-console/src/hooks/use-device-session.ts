import { useCallback, useEffect, useEffectEvent, useMemo, useRef, useState } from 'react'

import { makeDefaultBringupStatus } from '../lib/bringup'
import { makeDefaultBenchControlStatus } from '../lib/bench-model'
import { annotateSessionEvent } from '../lib/event-decode'
import { MockTransport } from '../lib/mock-transport'
import { WebSerialTransport } from '../lib/web-serial-transport'
import type {
  CommandEnvelope,
  CommandHistoryEntry,
  CommandRisk,
  DeviceSnapshot,
  DeviceTransport,
  FirmwarePackageDescriptor,
  FirmwareTransferProgress,
  SessionEvent,
  TransportKind,
  TransportMessage,
  TransportStatus,
} from '../types'

const HOST_TRANSPORT_KIND_STORAGE_KEY = 'bsl-host-transport-kind'
const HOST_SERIAL_RECONNECT_STORAGE_KEY = 'bsl-host-serial-reconnect'
const COMMAND_ACK_TIMEOUT_MS = 2600

function makeEventId(suffix: string): string {
  return `${Date.now()}-${Math.random().toString(16).slice(2, 8)}-${suffix}`
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

  if (cmd.includes('imu') || cmd === 'spi_read' || cmd === 'spi_write') {
    return 'imu'
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

function deriveSnapshotEvents(
  previous: DeviceSnapshot,
  next: DeviceSnapshot,
): SessionEvent[] {
  const events: SessionEvent[] = []

  if (previous.bringup.serviceModeActive !== next.bringup.serviceModeActive) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('service-mode'),
        atIso: new Date().toISOString(),
        severity: next.bringup.serviceModeActive ? 'ok' : 'info',
        category: 'service',
        title: next.bringup.serviceModeActive ? 'Service mode active' : 'Service mode closed',
        detail: next.bringup.serviceModeActive
          ? 'Guarded maintenance writes are now permitted by the controller.'
          : 'Controller returned to normal safe supervision.',
        module: 'service',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastI2cScan !== next.bringup.tools.lastI2cScan) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('i2c-scan'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'I2C scan updated',
        detail: next.bringup.tools.lastI2cScan,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastI2cOp !== next.bringup.tools.lastI2cOp) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('i2c-op'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'I2C transaction',
        detail: next.bringup.tools.lastI2cOp,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (previous.bringup.tools.lastSpiOp !== next.bringup.tools.lastSpiOp) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('spi-op'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'bus',
        title: 'SPI transaction',
        detail: next.bringup.tools.lastSpiOp,
        module: 'bus',
        source: 'derived',
      }),
    )
  }

  if (
    previous.bringup.tools.lastAction !== next.bringup.tools.lastAction &&
    next.bringup.tools.lastAction.trim().length > 0
  ) {
    events.push(
      annotateSessionEvent({
        id: makeEventId('service-action'),
        atIso: new Date().toISOString(),
        severity: 'info',
        category: 'service',
        title: 'Bring-up action',
        detail: next.bringup.tools.lastAction,
        module: 'service',
        source: 'derived',
      }),
    )
  }

  return events
}

type CommandAckResult = {
  ok: boolean
  note: string
}

type PendingCommandAck = {
  resolve: (result: CommandAckResult) => void
  timeoutId: number
}

function readStoredTransportKind(): TransportKind {
  if (typeof window === 'undefined') {
    return 'mock'
  }

  const stored = window.localStorage.getItem(HOST_TRANSPORT_KIND_STORAGE_KEY)
  return stored === 'serial' ? 'serial' : 'mock'
}

function readStoredSerialReconnect(): boolean {
  if (typeof window === 'undefined') {
    return false
  }

  return window.localStorage.getItem(HOST_SERIAL_RECONNECT_STORAGE_KEY) === 'true'
}

function writeStoredTransportKind(kind: TransportKind): void {
  if (typeof window === 'undefined') {
    return
  }

  window.localStorage.setItem(HOST_TRANSPORT_KIND_STORAGE_KEY, kind)
}

function writeStoredSerialReconnect(enabled: boolean): void {
  if (typeof window === 'undefined') {
    return
  }

  window.localStorage.setItem(
    HOST_SERIAL_RECONNECT_STORAGE_KEY,
    enabled ? 'true' : 'false',
  )
}

function makeSeedSnapshot(): DeviceSnapshot {
  const now = new Date().toISOString()
  return {
    identity: {
      label: 'BSL-HTLS Gen2',
      firmwareVersion: 'unavailable',
      hardwareRevision: 'rev-?',
      serialNumber: 'pending',
      protocolVersion: 'host-v1',
    },
    session: {
      uptimeSeconds: 0,
      state: 'BOOT_INIT',
      powerTier: 'unknown',
      bootReason: 'unknown',
      connectedAtIso: now,
    },
    pd: {
      contractValid: false,
      negotiatedPowerW: 0,
      sourceVoltageV: 0,
      sourceCurrentA: 0,
      operatingCurrentA: 0,
      contractObjectPosition: 0,
      sinkProfileCount: 0,
      sinkProfiles: [],
      sourceIsHostOnly: false,
    },
    rails: {
      ld: { enabled: false, pgood: false },
      tec: { enabled: false, pgood: false },
    },
    imu: {
      valid: false,
      fresh: false,
      beamPitchDeg: 0,
      beamRollDeg: 0,
      beamYawDeg: 0,
      beamYawRelative: true,
      beamPitchLimitDeg: 0,
    },
    tof: {
      valid: false,
      fresh: false,
      distanceM: 0,
      minRangeM: 0.2,
      maxRangeM: 1,
    },
    laser: {
      alignmentEnabled: false,
      nirEnabled: false,
      driverStandby: true,
      measuredCurrentA: 0,
      commandedCurrentA: 0,
      loopGood: false,
      driverTempC: 0,
    },
    tec: {
      targetTempC: 0,
      targetLambdaNm: 785,
      actualLambdaNm: 785,
      tempGood: false,
      tempC: 0,
      tempAdcVoltageV: 0,
      currentA: 0,
      voltageV: 0,
      settlingSecondsRemaining: 0,
    },
    peripherals: {
      dac: {
        reachable: false,
        configured: false,
        refAlarm: false,
        syncReg: 0,
        configReg: 0,
        gainReg: 0,
        statusReg: 0,
        dataAReg: 0,
        dataBReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      pd: {
        reachable: false,
        attached: false,
        ccStatusReg: 0,
        pdoCountReg: 0,
        rdoStatusRaw: 0,
      },
      imu: {
        reachable: false,
        configured: false,
        whoAmI: 0,
        statusReg: 0,
        ctrl1XlReg: 0,
        ctrl2GReg: 0,
        ctrl3CReg: 0,
        ctrl4CReg: 0,
        ctrl10CReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      haptic: {
        reachable: false,
        modeReg: 0,
        libraryReg: 0,
        goReg: 0,
        feedbackReg: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
      tof: {
        reachable: false,
        configured: false,
        interruptLineHigh: false,
        ledCtrlAsserted: false,
        dataReady: false,
        bootState: 0,
        rangeStatus: 0,
        sensorId: 0,
        distanceMm: 0,
        lastErrorCode: 0,
        lastError: 'ESP_OK',
      },
    },
    bench: makeDefaultBenchControlStatus(),
    safety: {
      allowAlignment: false,
      allowNir: false,
      horizonBlocked: false,
      distanceBlocked: false,
      lambdaDriftBlocked: false,
      tecTempAdcBlocked: false,
      horizonThresholdDeg: 0,
      horizonHysteresisDeg: 3,
      tofMinRangeM: 0.2,
      tofMaxRangeM: 1,
      tofHysteresisM: 0.02,
      imuStaleMs: 50,
      tofStaleMs: 100,
      railGoodTimeoutMs: 250,
      lambdaDriftLimitNm: 5,
      lambdaDriftHysteresisNm: 0.5,
      lambdaDriftHoldMs: 2000,
      ldOvertempLimitC: 55,
      tecTempAdcTripV: 2.45,
      tecTempAdcHysteresisV: 0.05,
      tecTempAdcHoldMs: 2000,
      tecMinCommandC: 15,
      tecMaxCommandC: 35,
      tecReadyToleranceC: 0.25,
      maxLaserCurrentA: 5,
      actualLambdaNm: 785,
      targetLambdaNm: 785,
      lambdaDriftNm: 0,
      tempAdcVoltageV: 0,
    },
    bringup: makeDefaultBringupStatus(),
    fault: {
      latched: false,
      activeCode: 'none',
      activeCount: 0,
      tripCounter: 0,
      lastFaultAtIso: null,
    },
    counters: {
      commsTimeouts: 0,
      watchdogTrips: 0,
      brownouts: 0,
    },
  }
}

function mergeSnapshot(
  current: DeviceSnapshot,
  incoming: DeviceSnapshot,
): DeviceSnapshot {
  return {
    ...current,
    ...incoming,
    identity: { ...current.identity, ...incoming.identity },
    session: { ...current.session, ...incoming.session },
    pd: { ...current.pd, ...incoming.pd },
    rails: {
      ld: { ...current.rails.ld, ...incoming.rails.ld },
      tec: { ...current.rails.tec, ...incoming.rails.tec },
    },
    imu: { ...current.imu, ...incoming.imu },
    tof: { ...current.tof, ...incoming.tof },
    laser: { ...current.laser, ...incoming.laser },
    tec: { ...current.tec, ...incoming.tec },
    peripherals: {
      dac: { ...current.peripherals.dac, ...incoming.peripherals.dac },
      pd: { ...current.peripherals.pd, ...incoming.peripherals.pd },
      imu: { ...current.peripherals.imu, ...incoming.peripherals.imu },
      haptic: { ...current.peripherals.haptic, ...incoming.peripherals.haptic },
      tof: { ...current.peripherals.tof, ...incoming.peripherals.tof },
    },
    bench: { ...current.bench, ...incoming.bench },
    safety: { ...current.safety, ...incoming.safety },
    bringup: {
      ...current.bringup,
      ...incoming.bringup,
      modules: {
        ...current.bringup.modules,
        ...incoming.bringup.modules,
      },
      tuning: {
        ...current.bringup.tuning,
        ...incoming.bringup.tuning,
      },
      tools: {
        ...current.bringup.tools,
        ...incoming.bringup.tools,
      },
    },
    fault: { ...current.fault, ...incoming.fault },
    counters: { ...current.counters, ...incoming.counters },
  }
}

export function useDeviceSession() {
  const [transportKind, setTransportKindState] = useState<TransportKind>(() =>
    readStoredTransportKind(),
  )
  const [transportStatus, setTransportStatus] = useState<TransportStatus>('disconnected')
  const [transportDetail, setTransportDetail] = useState('No active device link.')
  const [snapshot, setSnapshot] = useState<DeviceSnapshot>(makeSeedSnapshot)
  const [events, setEvents] = useState<SessionEvent[]>([])
  const [commands, setCommands] = useState<CommandHistoryEntry[]>([])
  const [firmwareProgress, setFirmwareProgress] = useState<FirmwareTransferProgress | null>(null)
  const [serialReconnectCycle, setSerialReconnectCycle] = useState(0)
  const [serialReconnectEnabled, setSerialReconnectEnabled] = useState<boolean>(() =>
    readStoredSerialReconnect() || readStoredTransportKind() === 'serial',
  )

  const commandCounterRef = useRef(1)
  const transportRef = useRef<DeviceTransport | null>(null)
  const snapshotRef = useRef<DeviceSnapshot>(makeSeedSnapshot())
  const serialReconnectAttemptRef = useRef(false)
  const serialManualDisconnectRef = useRef(false)
  const protocolReadyRef = useRef(false)
  const flashRecoveryUntilRef = useRef(0)
  const pendingAcksRef = useRef(new Map<number, PendingCommandAck>())

  const transport = useMemo<DeviceTransport>(() => {
    if (transportKind === 'serial') {
      return new WebSerialTransport()
    }

    return new MockTransport()
  }, [transportKind])

  const appendEvent = useCallback((event: SessionEvent) => {
    setEvents((current) => [annotateSessionEvent(event), ...current].slice(0, 250))
  }, [])

  const clearPendingAcks = useCallback((message: string) => {
    for (const [commandId, pending] of pendingAcksRef.current) {
      window.clearTimeout(pending.timeoutId)
      pending.resolve({
        ok: false,
        note: message,
      })
      pendingAcksRef.current.delete(commandId)
    }
  }, [])

  const resetLiveSnapshot = useCallback(() => {
    const nextSnapshot = makeSeedSnapshot()
    snapshotRef.current = nextSnapshot
    setSnapshot(nextSnapshot)
  }, [])

  const markTransportUnhealthy = useCallback(
    (reason: string, module = 'transport') => {
      setTransportStatus('error')
      setTransportDetail(reason)
      clearPendingAcks(reason)
      resetLiveSnapshot()
      appendEvent({
        id: makeEventId('transport-health'),
        atIso: new Date().toISOString(),
        severity: 'critical',
        category: 'transport',
        title: 'Controller link unhealthy',
        detail: reason,
        module,
        source: 'host',
      })
      void transportRef.current?.disconnect().catch(() => undefined)
    },
    [appendEvent, clearPendingAcks, resetLiveSnapshot],
  )

  const handleMessage = useEffectEvent((message: TransportMessage) => {
    if (message.kind === 'transport') {
      if (
        transportKind === 'serial' &&
        serialManualDisconnectRef.current &&
        (message.status === 'connected' || message.status === 'connecting')
      ) {
        void transportRef.current?.disconnect().catch(() => undefined)
        return
      }

      if (message.status === 'connected' && transportKind === 'serial') {
        protocolReadyRef.current = false
        setTransportStatus('connecting')
        setTransportDetail('Serial port opened. Waiting for controller firmware handshake…')
        return
      }

      protocolReadyRef.current = false
      setTransportStatus(message.status)
      setTransportDetail(message.detail ?? '')

      if (message.status === 'disconnected' || message.status === 'error') {
        clearPendingAcks(message.detail ?? 'Transport became unavailable.')
        resetLiveSnapshot()
      }
      return
    }

    if (message.kind === 'snapshot') {
      protocolReadyRef.current = true
      flashRecoveryUntilRef.current = 0
      if (transportKind === 'serial' && transportStatus !== 'connected') {
        setTransportStatus('connected')
        setTransportDetail('Controller protocol active.')
      }
      const merged = mergeSnapshot(snapshotRef.current, message.snapshot)
      const derivedEvents = deriveSnapshotEvents(snapshotRef.current, merged)
      snapshotRef.current = merged
      setSnapshot(merged)

      if (derivedEvents.length > 0) {
        setEvents((existing) => [...derivedEvents.reverse(), ...existing].slice(0, 250))
      }
      return
    }

    if (message.kind === 'event') {
      appendEvent(message.event)
      return
    }

    if (message.kind === 'commandAck') {
      protocolReadyRef.current = true
      flashRecoveryUntilRef.current = 0
      if (transportKind === 'serial' && transportStatus !== 'connected') {
        setTransportStatus('connected')
        setTransportDetail('Controller protocol active.')
      }
      const pending = pendingAcksRef.current.get(message.commandId)

      if (pending !== undefined) {
        window.clearTimeout(pending.timeoutId)
        pending.resolve({
          ok: message.ok,
          note: message.note,
        })
        pendingAcksRef.current.delete(message.commandId)
      }

      setCommands((current) =>
        current.map((entry) =>
          entry.id === message.commandId
            ? {
                ...entry,
                status: message.ok ? 'ack' : 'error',
                note: message.note,
              }
            : entry,
        ),
      )
      return
    }
  })

  useEffect(() => {
    transportRef.current = transport
    const unsubscribe = transport.subscribe((message) => {
      handleMessage(message)
    })

    return () => {
      clearPendingAcks('Transport changed before the command completed.')
      unsubscribe()
      void transport.disconnect()
    }
  }, [clearPendingAcks, transport])

  const waitForCommandAck = useCallback((commandId: number, timeoutMs: number) => {
    return new Promise<CommandAckResult>((resolve) => {
      const timeoutId = window.setTimeout(() => {
        pendingAcksRef.current.delete(commandId)
        resolve({
          ok: false,
          note: 'Controller acknowledgement timed out.',
        })
      }, timeoutMs)

      pendingAcksRef.current.set(commandId, {
        resolve,
        timeoutId,
      })
    })
  }, [])

  const connect = useCallback(async () => {
    if (transportStatus === 'connected' || transportStatus === 'connecting') {
      return
    }

    if (transportKind === 'serial') {
      serialManualDisconnectRef.current = false
      setSerialReconnectEnabled(true)
      setSerialReconnectCycle(0)
      writeStoredSerialReconnect(true)
    }

    setTransportDetail('Connecting…')
    await transportRef.current?.connect()
  }, [transportKind, transportStatus])

  useEffect(() => {
    if (transportKind !== 'mock') {
      return
    }

    if (transportStatus !== 'disconnected') {
      return
    }

    const timerId = window.setTimeout(() => {
      void connect()
    }, 0)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [connect, transportKind, transportStatus])

  useEffect(() => {
    writeStoredTransportKind(transportKind)
  }, [transportKind])

  useEffect(() => {
    writeStoredSerialReconnect(serialReconnectEnabled)
  }, [serialReconnectEnabled])

  useEffect(() => {
    if (transportKind !== 'serial') {
      return
    }

    if (transportStatus !== 'connecting') {
      return
    }

    const timerId = window.setTimeout(() => {
      if (!protocolReadyRef.current) {
        const recoveringFromFlash = flashRecoveryUntilRef.current > Date.now()
        markTransportUnhealthy(
          recoveringFromFlash
            ? 'Serial port reopened after browser flash, but the controller firmware still has not resumed the host protocol. The board may still be rebooting, sitting in bootloader mode, or have failed to leave the flasher cleanly.'
            : 'Serial port opened, but the controller firmware did not answer the host protocol. The board may still be rebooting, sitting in bootloader mode, or running incompatible firmware.',
        )
      }
    }, flashRecoveryUntilRef.current > Date.now() ? 7500 : 2200)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [markTransportUnhealthy, transportKind, transportStatus])

  useEffect(() => {
    if (transportKind !== 'serial' || !serialReconnectEnabled || serialManualDisconnectRef.current) {
      return
    }

    if (firmwareProgress !== null && firmwareProgress.percent < 100) {
      return
    }

    if (transportStatus === 'connected' || transportStatus === 'connecting') {
      return
    }

    if (serialReconnectAttemptRef.current) {
      return
    }

    const reconnect = transportRef.current?.reconnectSilently

    if (reconnect === undefined) {
      return
    }

    const timerId = window.setTimeout(() => {
      if (serialManualDisconnectRef.current) {
        return
      }

      serialReconnectAttemptRef.current = true
      void reconnect.call(transportRef.current)
        .then((result) => {
          if (serialManualDisconnectRef.current) {
            return
          }

          if (result !== 'connected' && serialReconnectEnabled) {
            setSerialReconnectCycle((current) => current + 1)
          }
        })
        .finally(() => {
          serialReconnectAttemptRef.current = false
        })
    }, 1400)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [firmwareProgress, serialReconnectCycle, serialReconnectEnabled, transportKind, transportStatus])

  const disconnect = useCallback(async () => {
    if (transportKind === 'serial') {
      serialManualDisconnectRef.current = true
      setSerialReconnectEnabled(false)
      setSerialReconnectCycle(0)
      writeStoredSerialReconnect(false)
    }

    if (transportStatus === 'disconnected') {
      setFirmwareProgress(null)
      return
    }

    setFirmwareProgress(null)
    await transportRef.current?.disconnect()
  }, [transportKind, transportStatus])

  const setTransportKind = useCallback(
    async (nextKind: TransportKind) => {
      if (nextKind === transportKind) {
        return
      }

      const currentTransport = transportRef.current

      setFirmwareProgress(null)

      if (currentTransport !== null) {
        try {
          await currentTransport.disconnect()
        } catch (error) {
        appendEvent({
          id: makeEventId('transport-switch'),
          atIso: new Date().toISOString(),
          severity: 'warn',
          category: 'transport',
            title: 'Transport disconnect warning',
            detail:
              error instanceof Error
                ? error.message
                : 'Previous transport did not disconnect cleanly during handoff.',
          })
        }
      }

      setTransportStatus('disconnected')
      setTransportDetail(
        nextKind === 'serial'
          ? 'Mock rig disconnected. Web Serial selected.'
          : 'Web Serial disconnected. Mock rig selected.',
      )
      const nextSnapshot = makeSeedSnapshot()
      snapshotRef.current = nextSnapshot
      setSnapshot(nextSnapshot)
      serialManualDisconnectRef.current = false
      setSerialReconnectEnabled(nextKind === 'serial')
      setSerialReconnectCycle(0)
      setTransportKindState(nextKind)
    },
    [appendEvent, transportKind],
  )

  const issueCommand = useCallback(
    async (
      cmd: string,
      risk: CommandRisk,
      note: string,
      args?: Record<string, number | string | boolean>,
    ) => {
      const command: CommandEnvelope = {
        id: commandCounterRef.current++,
        type: 'cmd',
        cmd,
        args,
      }

      const historyEntry: CommandHistoryEntry = {
        id: command.id,
        cmd,
        risk,
        args,
        issuedAtIso: new Date().toISOString(),
        status: 'queued',
        note,
      }

      setCommands((current) => [
        historyEntry,
        ...current,
      ].slice(0, 40))

      try {
        await transportRef.current?.sendCommand(command)
        setCommands((current) =>
          current.map((entry) =>
            entry.id === command.id
              ? {
                  ...entry,
                  status: 'sent',
                }
              : entry,
          ),
        )
      } catch (error) {
        const message =
          error instanceof Error ? error.message : 'Command transport failed.'
        setCommands((current) =>
          current.map((entry) =>
            entry.id === command.id
              ? {
                  ...entry,
                  status: 'error',
                  note: message,
                }
              : entry,
          ),
        )
        appendEvent({
          id: makeEventId('command-error'),
          atIso: new Date().toISOString(),
          severity: 'critical',
          category: 'transport',
          title: 'Command delivery failed',
          detail: message,
          module: moduleFromCommand(cmd),
          source: 'host',
        })
      }
    },
    [appendEvent],
  )

  const issueCommandAwaitAck = useCallback(
    async (
      cmd: string,
      risk: CommandRisk,
      note: string,
      args?: Record<string, number | string | boolean>,
      options?: {
        logHistory?: boolean
        timeoutMs?: number
      },
    ): Promise<CommandAckResult> => {
      const logHistory = options?.logHistory ?? true
      const timeoutMs = options?.timeoutMs ?? COMMAND_ACK_TIMEOUT_MS
      const command: CommandEnvelope = {
        id: commandCounterRef.current++,
        type: 'cmd',
        cmd,
        args,
      }

      if (logHistory) {
        const historyEntry: CommandHistoryEntry = {
          id: command.id,
          cmd,
          risk,
          args,
          issuedAtIso: new Date().toISOString(),
          status: 'queued',
          note,
        }

        setCommands((current) => [historyEntry, ...current].slice(0, 40))
      }

      const ackPromise = waitForCommandAck(command.id, timeoutMs)

      try {
        await transportRef.current?.sendCommand(command)

        if (logHistory) {
          setCommands((current) =>
            current.map((entry) =>
              entry.id === command.id
                ? {
                    ...entry,
                    status: 'sent',
                  }
                : entry,
            ),
          )
        }

        const ack = await ackPromise

        if (!ack.ok) {
          if (logHistory) {
            setCommands((current) =>
              current.map((entry) =>
                entry.id === command.id
                  ? {
                      ...entry,
                      status: 'error',
                      note: ack.note,
                    }
                  : entry,
              ),
            )
          }

          appendEvent({
            id: makeEventId('command-timeout'),
            atIso: new Date().toISOString(),
            severity: ack.note.toLowerCase().includes('timed out') ? 'critical' : 'warn',
            category: 'transport',
            title: ack.note.toLowerCase().includes('timed out')
              ? 'Command timed out'
              : 'Command rejected',
            detail: `${cmd}: ${ack.note}`,
            module: moduleFromCommand(cmd),
            source: 'host',
          })

          if (
            transportKind === 'serial' &&
            ack.note.toLowerCase().includes('timed out')
          ) {
            markTransportUnhealthy(
              `The controller stopped answering protocol commands after "${cmd}". Reconnect after the firmware finishes booting or reattach the board.`,
              moduleFromCommand(cmd),
            )
          }
        }

        return ack
      } catch (error) {
        const message =
          error instanceof Error ? error.message : 'Command transport failed.'
        const pending = pendingAcksRef.current.get(command.id)

        if (pending !== undefined) {
          window.clearTimeout(pending.timeoutId)
          pending.resolve({
            ok: false,
            note: message,
          })
          pendingAcksRef.current.delete(command.id)
        }

        if (logHistory) {
          setCommands((current) =>
            current.map((entry) =>
              entry.id === command.id
                ? {
                    ...entry,
                    status: 'error',
                    note: message,
                  }
                : entry,
            ),
          )
        }

        appendEvent({
          id: makeEventId('command-error'),
          atIso: new Date().toISOString(),
          severity: 'critical',
          category: 'transport',
          title: 'Command delivery failed',
          detail: message,
          module: moduleFromCommand(cmd),
          source: 'host',
        })

        return {
          ok: false,
          note: message,
        }
      }
    },
    [appendEvent, markTransportUnhealthy, transportKind, waitForCommandAck],
  )

  const beginFirmwareTransfer = useCallback(
    async (pkg: FirmwarePackageDescriptor) => {
      const currentTransport = transportRef.current

      if (currentTransport?.beginFirmwareTransfer === undefined) {
        appendEvent({
          id: makeEventId('firmware-unsupported'),
          atIso: new Date().toISOString(),
          severity: 'warn',
          category: 'firmware',
          title: 'Direct transfer unavailable',
          detail:
            'This transport can stage packages and run preflight, but it does not implement a flashing pipeline yet.',
          module: 'firmware',
          source: 'host',
          operation: 'transfer',
        })
        return
      }

      setFirmwareProgress({
        phase: 'queue',
        percent: 0,
        detail: 'Waiting for transport handoff…',
      })
      flashRecoveryUntilRef.current = Date.now() + 15000

      try {
        await currentTransport.beginFirmwareTransfer(pkg, (progress) => {
          setFirmwareProgress(progress)
        })
      } catch (error) {
        const message =
          error instanceof Error ? error.message : 'Browser flash failed.'
        appendEvent({
          id: makeEventId('firmware-error'),
          atIso: new Date().toISOString(),
          severity: 'critical',
          category: 'firmware',
          title: 'Browser flash failed',
          detail: message,
          module: 'firmware',
          source: 'host',
          operation: 'transfer',
        })
        setFirmwareProgress({
          phase: 'error',
          percent: 100,
          detail: message,
        })
      }
    },
    [appendEvent],
  )

  const exportSession = useCallback(() => {
    const payload = {
      exportedAtIso: new Date().toISOString(),
      transportKind,
      snapshot,
      events,
      commands,
      firmwareProgress,
    }

    const blob = new Blob([JSON.stringify(payload, null, 2)], {
      type: 'application/json',
    })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')

    anchor.href = url
    anchor.download = `bsl-session-${new Date().toISOString().replaceAll(':', '-')}.json`
    anchor.click()
    URL.revokeObjectURL(url)
  }, [commands, events, firmwareProgress, snapshot, transportKind])

  return {
    transportKind,
    setTransportKind,
    transportStatus,
    transportDetail,
    snapshot,
    events,
    commands,
    firmwareProgress,
    supportsFirmwareTransfer: transport.supportsFirmwareTransfer,
    connect,
    disconnect,
    issueCommand,
    issueCommandAwaitAck,
    beginFirmwareTransfer,
    exportSession,
  }
}
