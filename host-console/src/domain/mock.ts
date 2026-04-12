import {
  makeDefaultWorkbenchState,
  severityToTone,
} from './model'
import type {
  CommandLogEntry,
  EventEntry,
  FirmwareInspection,
  WorkbenchState,
} from './model'

function iso(offsetSeconds = 0): string {
  return new Date(Date.now() - offsetSeconds * 1000).toISOString()
}

function makeEvent(entry: Omit<EventEntry, 'tone'> & { tone?: EventEntry['tone'] }): EventEntry {
  return {
    ...entry,
    tone: entry.tone ?? severityToTone(entry.severity),
  }
}

function makeCommand(entry: Partial<CommandLogEntry> & Pick<CommandLogEntry, 'id' | 'cmd' | 'note'>): CommandLogEntry {
  return {
    id: entry.id,
    cmd: entry.cmd,
    note: entry.note,
    atIso: entry.atIso ?? new Date().toISOString(),
    status: entry.status ?? 'queued',
    detail: entry.detail ?? '',
    risk: entry.risk ?? 'read',
    module: entry.module ?? 'system',
  }
}

export function makeMockState(): WorkbenchState {
  const state = makeDefaultWorkbenchState()
  const snapshot = state.snapshot

  snapshot.identity = {
    label: 'BSL Console V2',
    firmwareVersion: '91b424d-dirty',
    hardwareRevision: 'rev-0',
    serialNumber: 'UNPROVISIONED',
    protocolVersion: 'host-v1',
    boardName: 'esp32s3',
    buildUtc: '2026-04-06T03:18:27Z',
  }
  snapshot.session = {
    uptimeSeconds: 147,
    state: 'PROGRAMMING_ONLY',
    powerTier: 'programming_only',
    bootReason: 'usb-only-phase-1',
  }
  snapshot.transport = {
    mode: 'browser-mock',
    status: 'connected',
    detail: 'Browser inspection mode mirrors the Phase 1 USB-only bench contract.',
    serialPort: '/dev/cu.usbmodem201101',
    wirelessUrl: 'ws://192.168.4.1/ws',
  }
  snapshot.wireless = {
    started: true,
    mode: 'softap',
    apReady: true,
    stationConfigured: true,
    stationConnecting: false,
    stationConnected: false,
    clientCount: 1,
    ssid: 'BSL-HTLS-Bench',
    stationSsid: 'Lab-2G',
    stationRssiDbm: -61,
    stationChannel: 6,
    scanInProgress: false,
    scannedNetworks: [
      { ssid: 'Lab-2G', rssiDbm: -51, channel: 11, secure: true },
      { ssid: 'SurgicalBench', rssiDbm: -58, channel: 6, secure: true },
      { ssid: 'Guest-2G', rssiDbm: -71, channel: 1, secure: false },
    ],
    ipAddress: '192.168.4.1',
    wsUrl: 'ws://192.168.4.1/ws',
    lastError: '',
  }
  snapshot.pd.contractValid = true
  snapshot.pd.negotiatedPowerW = 2.5
  snapshot.pd.sourceVoltageV = 5
  snapshot.pd.sourceCurrentA = 0.5
  snapshot.pd.operatingCurrentA = 0.5
  snapshot.pd.contractObjectPosition = 0
  snapshot.pd.sinkProfileCount = 3
  snapshot.pd.sinkProfiles = [
    { enabled: true, voltageV: 5, currentA: 0.5 },
    { enabled: true, voltageV: 9, currentA: 2 },
    { enabled: true, voltageV: 12, currentA: 1.5 },
  ]
  snapshot.pd.sourceIsHostOnly = true
  snapshot.deployment = {
    active: true,
    running: false,
    ready: false,
    failed: true,
    currentStep: 'none',
    lastCompletedStep: 'ownership_reclaim',
    failureCode: 'pd_insufficient',
    failureReason: 'USB-only Phase 1 bench detected. A source of at least 9 V is required before power-dependent deployment steps can continue.',
    targetMode: 'lambda',
    targetTempC: 25.01,
    targetLambdaNm: 778.56,
    maxLaserCurrentA: 5.2,
    maxOpticalPowerW: 5,
    steps: [
      { key: 'ownership_reclaim', label: 'Reclaim ownership', status: 'passed' },
      { key: 'pd_inspect', label: 'Qualify source voltage', status: 'failed' },
      { key: 'power_cap', label: 'Derive runtime cap', status: 'pending' },
      { key: 'outputs_off', label: 'Confirm safe output posture', status: 'pending' },
      { key: 'stabilize_3v3', label: 'Stabilize digital rail', status: 'pending' },
      { key: 'dac_safe_zero', label: 'Prime DAC and zero command', status: 'pending' },
      { key: 'peripherals_verify', label: 'Verify IMU, ToF, haptic', status: 'pending' },
      { key: 'rail_sequence', label: 'Sequence TEC then LD', status: 'pending' },
      { key: 'tec_settle', label: 'Hold for TEC settle', status: 'pending' },
      { key: 'ready_posture', label: 'Validate ready posture', status: 'pending' },
    ],
  }
  snapshot.operate.runtimeMode = 'modulated_host'
  snapshot.operate.runtimeModeSwitchAllowed = true
  snapshot.operate.modulationFrequencyHz = 2000
  snapshot.operate.modulationDutyCyclePct = 50
  snapshot.operate.lowStateCurrentA = 0.1
  snapshot.integrate.profileName = 'precision bench'
  snapshot.integrate.profileRevision = 8
  snapshot.integrate.persistenceAvailable = true
  snapshot.integrate.lastSaveOk = true
  snapshot.integrate.tools.lastI2cScan = '0x28 0x29 0x48 0x5A'
  snapshot.integrate.tools.lastI2cOp = 'read 0x48 reg 0x07 -> 0x00'
  snapshot.integrate.tools.lastSpiOp = 'read imu reg 0x0F -> 0x6C'
  snapshot.integrate.tools.lastAction = 'Inspection-mode dataset loaded.'
  snapshot.gpioInspector = {
    anyOverrideActive: false,
    activeOverrideCount: 0,
    pins: [
      { gpioNum: 4, modulePin: 26, outputCapable: true, inputEnabled: true, outputEnabled: true, openDrainEnabled: false, pullupEnabled: true, pulldownEnabled: false, levelHigh: true, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
      { gpioNum: 5, modulePin: 27, outputCapable: true, inputEnabled: true, outputEnabled: true, openDrainEnabled: false, pullupEnabled: true, pulldownEnabled: false, levelHigh: true, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
      { gpioNum: 6, modulePin: 28, outputCapable: true, inputEnabled: false, outputEnabled: true, openDrainEnabled: false, pullupEnabled: false, pulldownEnabled: true, levelHigh: false, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
      { gpioNum: 7, modulePin: 29, outputCapable: true, inputEnabled: false, outputEnabled: true, openDrainEnabled: false, pullupEnabled: false, pulldownEnabled: true, levelHigh: false, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
      { gpioNum: 13, modulePin: 16, outputCapable: true, inputEnabled: false, outputEnabled: true, openDrainEnabled: false, pullupEnabled: false, pulldownEnabled: true, levelHigh: false, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
      { gpioNum: 21, modulePin: 25, outputCapable: true, inputEnabled: false, outputEnabled: true, openDrainEnabled: false, pullupEnabled: false, pulldownEnabled: true, levelHigh: false, overrideActive: false, overrideMode: 'firmware', overrideLevelHigh: false, overridePullupEnabled: false, overridePulldownEnabled: false },
    ],
  }

  const firmware: FirmwareInspection = {
    path: '/Users/zz4/BSL/BSL-Laser/build/bsl_laser_controller.bin',
    fileName: 'bsl_laser_controller.bin',
    packageName: 'BSL Controller Bundle',
    version: '91b424d-dirty',
    board: 'esp32s3',
    note: 'Raw app binary with embedded BSL signature.',
    bytes: 996016,
    sha256: 'f42d9debaa425a586b6379bb7005a9639bf7baca6f3fb02a4c5a66b02584e35f',
    extension: 'bin',
    format: 'binary',
    rawBinary: true,
    flashOffsetHex: '0x10000',
    signature: {
      schemaVersion: 1,
      productName: 'BSL-HTLS Gen2',
      projectName: 'bsl_laser_controller',
      boardName: 'esp32s3',
      protocolVersion: 'host-v1',
      hardwareScope: 'main-controller',
      firmwareVersion: '91b424d-dirty',
      buildUtc: '2026-04-06T03:18:27Z',
      payloadSha256Hex: 'c9f5b95fd8d5971e7f9b8ab1b9c39f5c8b70b1bc2d7ef385ec4a180f2f5e3ad4',
      verified: true,
    },
    segments: [
      {
        name: 'app',
        fileName: 'bsl_laser_controller.bin',
        path: '/Users/zz4/BSL/BSL-Laser/build/bsl_laser_controller.bin',
        offsetHex: '0x10000',
        bytes: 996016,
        sha256: 'f42d9debaa425a586b6379bb7005a9639bf7baca6f3fb02a4c5a66b02584e35f',
        role: 'app',
      },
    ],
    transfer: {
      supported: true,
      note: 'Native Tauri flash path is available for this package.',
    },
  }

  const events: EventEntry[] = [
    makeEvent({
      id: 'evt-1',
      atIso: iso(25),
      category: 'deployment',
      title: 'USB-only deployment block',
      detail: 'The controller rejected deployment progress beyond source qualification because only 5 V USB host power is present.',
      severity: 'warn',
      source: 'firmware',
      module: 'deployment',
      summary: 'USB-only power blocked deployment.',
    }),
    makeEvent({
      id: 'evt-2',
      atIso: iso(47),
      category: 'transport',
      title: 'Serial link live',
      detail: 'Desktop transport attached to /dev/cu.usbmodem201101.',
      severity: 'ok',
      source: 'derived',
      module: 'transport',
      summary: 'Serial transport active.',
    }),
    makeEvent({
      id: 'evt-3',
      atIso: iso(65),
      category: 'wireless',
      title: 'Bench AP ready',
      detail: 'Controller bench AP is online at ws://192.168.4.1/ws and nearby 2.4 GHz networks were scanned.',
      severity: 'info',
      source: 'derived',
      module: 'transport',
      summary: 'Bench AP available.',
    }),
    makeEvent({
      id: 'evt-4',
      atIso: iso(102),
      category: 'safety',
      title: 'Runtime mode gated',
      detail: 'Host output controls remain unavailable until deployment reaches a terminal ready posture.',
      severity: 'info',
      source: 'derived',
      module: 'operate',
      summary: 'Output controls blocked.',
    }),
  ]

  state.page = 'system'
  state.snapshot = snapshot
  state.ports = [{ name: '/dev/cu.usbmodem201101', label: '/dev/cu.usbmodem201101' }]
  state.firmware = firmware
  state.flash = {
    phase: 'idle',
    percent: 0,
    detail: 'No flash in progress.',
  }
  state.commands = [
    makeCommand({
      id: 1201,
      cmd: 'enter_deployment_mode',
      note: 'Operator opened the deployment gate.',
      atIso: iso(24),
      status: 'ok',
      detail: 'Full snapshot returned after deployment gate activation.',
      risk: 'service',
      module: 'deployment',
    }),
    makeCommand({
      id: 1202,
      cmd: 'run_deployment_sequence',
      note: 'Operator ran the inline checklist.',
      atIso: iso(23),
      status: 'ok',
      detail: 'Checklist reached a terminal failed posture on the USB-only bench.',
      risk: 'service',
      module: 'deployment',
    }),
    makeCommand({
      id: 1203,
      cmd: 'scan_wireless_networks',
      note: 'Refreshed nearby controller Wi-Fi targets.',
      atIso: iso(15),
      status: 'ok',
      detail: 'Three 2.4 GHz SSIDs found.',
      risk: 'read',
      module: 'transport',
    }),
  ]
  state.events = events
  state.rawFeed = [
    '{"type":"event","event":"fast_telemetry","timestamp_ms":54198,"payload":{"snapshot":{"session":{"state":"PROGRAMMING_ONLY"},"deployment":{"failed":true},"bench":{"runtimeMode":"modulated_host"}}}}',
    '{"type":"event","event":"status_snapshot","timestamp_ms":54280,"payload":{"snapshot":{"session":{"state":"PROGRAMMING_ONLY"},"deployment":{"failed":true}}}}',
    '{"type":"resp","id":1201,"ok":true,"result":{"deployment":{"active":true}}}',
    '{"type":"resp","id":1202,"ok":true,"result":{"deployment":{"active":true,"failed":true,"failureCode":"pd_insufficient"}}}',
  ]
  state.sessionAutosave = {
    supported: true,
    enabled: true,
    targetPath: '/Users/zz4/Documents/bsl-session-latest.json',
    lastWriteAtIso: iso(8),
    lastError: '',
  }

  return state
}
