import type {
  BringupModuleMap,
  BringupModuleStatus,
  BringupStatus,
  DacReferenceMode,
  DacSyncMode,
  DeviceSnapshot,
  HapticActuator,
  HapticMode,
  ModuleKey,
} from '../types'
import { estimateTecVoltageFromTempC } from './tec-calibration'

const DEFAULT_TEC_BRINGUP_TEMP_C = 25
const DEFAULT_TEC_BRINGUP_VOLTAGE_V = estimateTecVoltageFromTempC(DEFAULT_TEC_BRINGUP_TEMP_C)

export const moduleKeys: ModuleKey[] = [
  'imu',
  'dac',
  'haptic',
  'tof',
  'buttons',
  'pd',
  'laserDriver',
  'tec',
]

export const moduleMeta: Record<
  ModuleKey,
  {
    label: string
    transport: string
    detail: string
    datasheetStatus: string
    validationMode: 'probe' | 'monitored'
    validationDetail: string
  }
> = {
  imu: {
    label: 'IMU',
    transport: 'SPI',
    detail: 'LSM6DSO orientation source and horizon interlock input.',
    datasheetStatus: 'SPI production path defined in datasheet and board recon.',
    validationMode: 'probe',
    validationDetail: 'Health comes from live SPI identity and runtime sample freshness.',
  },
  dac: {
    label: 'DAC',
    transport: 'I2C 0x48',
    detail: 'DAC80502 actuator shadow for LD and TEC command channels.',
    datasheetStatus: 'I2C-mode register set is present in the local datasheet.',
    validationMode: 'probe',
    validationDetail: 'Health comes from direct I2C register access and DAC status readback.',
  },
  haptic: {
    label: 'Haptic',
    transport: 'I2C 0x5A',
    detail: 'DRV2605 operator feedback path and motor test surface.',
    datasheetStatus: 'Register map and mode selection are documented locally.',
    validationMode: 'probe',
    validationDetail: 'Health comes from direct I2C register access and enable-trigger readback.',
  },
  tof: {
    label: 'ToF',
    transport: 'I2C 0x29 + GPIO7/GPIO6',
    detail: 'VL53L1X distance interlock board on the shared I2C bus, with GPIO7 interrupt in and GPIO6 LED-control out.',
    datasheetStatus: 'VL53L1X board mapping is now known. XSHUT is not exported on this board revision, so firmware must use shared-I2C probe plus optional GPIO1 interrupt assist.',
    validationMode: 'probe',
    validationDetail: 'Health comes from direct sensor traffic plus live distance freshness.',
  },
  buttons: {
    label: 'Buttons',
    transport: 'GPIO',
    detail: 'Two-stage trigger and local operator input stack.',
    datasheetStatus: 'Board-only feature. No dedicated programmable peripheral.',
    validationMode: 'monitored',
    validationDetail: 'This path is GPIO-monitored rather than bus-probed, so it should stay declared live without an identity probe.',
  },
  pd: {
    label: 'USB-PD',
    transport: 'I2C 0x28',
    detail: 'STUSB4500 sink-planning page for PDO priorities and runtime power-tier thresholds.',
    datasheetStatus: 'Autonomous sink controller. Bench writes stage a planned policy and host runtime thresholds, not hidden live bypasses.',
    validationMode: 'probe',
    validationDetail: 'Health comes from direct STUSB4500 I2C transactions and contract refreshes.',
  },
  laserDriver: {
    label: 'Laser driver',
    transport: 'Mixed',
    detail: 'ATLS6A214 path, standby gating, and current supervision.',
    datasheetStatus: 'Analog path with fast shutdown on SBDN and PCN current selection.',
    validationMode: 'monitored',
    validationDetail: 'This is an analog/electrical path. Presence is supervised through rail and signal readback, not a digital probe exchange.',
  },
  tec: {
    label: 'TEC loop',
    transport: 'Mixed',
    detail: 'TEC controller supervision and wavelength-settle path.',
    datasheetStatus: 'Analog controller; firmware supervises TMS, TEMPGD, ITEC, and VTEC.',
    validationMode: 'monitored',
    validationDetail: 'This is an analog/electrical path. Presence is supervised through temperature/current/rail readback, not a digital probe exchange.',
  },
}

export const imuOdrOptions = [12.5, 26, 52, 104, 208, 416, 833, 1660]
export const imuAccelRangeOptions = [2, 4, 8, 16]
export const imuGyroRangeOptions = [125, 250, 500, 1000, 2000]

export const dacReferenceOptions: Array<{
  value: DacReferenceMode
  label: string
  detail: string
}> = [
  {
    value: 'internal',
    label: 'Internal 2.5 V',
    detail: 'Default bring-up posture. Keep the local precision reference enabled.',
  },
  {
    value: 'external',
    label: 'External reference',
    detail: 'Only use when the board stuffing and bench wiring are proven.',
  },
]

export const dacSyncModeOptions: Array<{
  value: DacSyncMode
  label: string
  detail: string
}> = [
  {
    value: 'async',
    label: 'Asynchronous update',
    detail: 'I2C writes update the output immediately on the last acknowledge.',
  },
  {
    value: 'sync',
    label: 'Synchronous update',
    detail: 'Buffer writes wait for a later LDAC or trigger-style commit event.',
  },
]

export const hapticModeOptions: Array<{
  value: HapticMode
  label: string
  detail: string
}> = [
  {
    value: 'internal_trigger',
    label: 'Internal trigger',
    detail: 'Safest first bring-up path for firing short library effects.',
  },
  {
    value: 'external_edge',
    label: 'External edge',
    detail: 'Edge-triggered IN/TRIG mode. Avoid until the shared-net hazard is cleared.',
  },
  {
    value: 'external_level',
    label: 'External level',
    detail: 'Level-triggered IN/TRIG mode. Avoid until the shared-net hazard is cleared.',
  },
  {
    value: 'pwm_analog',
    label: 'PWM or analog',
    detail: 'Continuous input mode for direct wave shaping or analog drive.',
  },
  {
    value: 'audio_to_vibe',
    label: 'Audio to vibe',
    detail: 'Not recommended for this instrument bring-up. Included for completeness.',
  },
  {
    value: 'rtp',
    label: 'Real-time playback',
    detail: 'I2C-driven amplitude updates for controlled continuous tests.',
  },
  {
    value: 'diagnostics',
    label: 'Diagnostics',
    detail: 'Built-in diagnostic routine launched from the GO bit.',
  },
  {
    value: 'auto_cal',
    label: 'Auto calibration',
    detail: 'Bench-only calibration sequence. Do not run near beam-control wiring until isolated.',
  },
]

export const hapticActuatorOptions: Array<{
  value: HapticActuator
  label: string
  detail: string
}> = [
  {
    value: 'erm',
    label: 'ERM',
    detail: 'Default board expectation for the current motor-driver bring-up path.',
  },
  {
    value: 'lra',
    label: 'LRA',
    detail: 'Requires resonance-aware tuning and, typically, a different library choice.',
  },
]

export const hapticLibraryOptions = [
  { value: 1, label: 'ERM library A' },
  { value: 2, label: 'ERM library B' },
  { value: 3, label: 'ERM library C' },
  { value: 4, label: 'ERM library D' },
  { value: 5, label: 'ERM library E' },
  { value: 6, label: 'LRA library' },
] as const

export const pdVoltageOptions = [
  { value: 5, label: '5 V' },
  { value: 9, label: '9 V' },
  { value: 12, label: '12 V' },
  { value: 15, label: '15 V' },
  { value: 20, label: '20 V' },
] as const

export const pdCurrentOptions = [
  { value: 0.5, label: '0.50 A' },
  { value: 0.75, label: '0.75 A' },
  { value: 1.0, label: '1.00 A' },
  { value: 1.25, label: '1.25 A' },
  { value: 1.5, label: '1.50 A' },
  { value: 1.75, label: '1.75 A' },
  { value: 2.0, label: '2.00 A' },
  { value: 2.25, label: '2.25 A' },
  { value: 2.5, label: '2.50 A' },
  { value: 2.75, label: '2.75 A' },
  { value: 3.0, label: '3.00 A' },
  { value: 3.5, label: '3.50 A' },
  { value: 4.0, label: '4.00 A' },
  { value: 4.5, label: '4.50 A' },
  { value: 5.0, label: '5.00 A' },
] as const

function makeModuleStatus(
  expectedPresent: boolean,
  debugEnabled: boolean,
): BringupModuleStatus {
  return {
    expectedPresent,
    debugEnabled,
    detected: false,
    healthy: false,
  }
}

export function makeDefaultPdProfiles() {
  return [
    { enabled: true, voltageV: 5, currentA: 3 },
    { enabled: true, voltageV: 15, currentA: 2 },
    { enabled: true, voltageV: 20, currentA: 2.25 },
  ]
}

export function makeDefaultBringupModules(): BringupModuleMap {
  return {
    imu: makeModuleStatus(false, false),
    dac: makeModuleStatus(false, false),
    haptic: makeModuleStatus(false, false),
    tof: makeModuleStatus(false, false),
    buttons: makeModuleStatus(false, false),
    pd: makeModuleStatus(false, false),
    laserDriver: makeModuleStatus(false, false),
    tec: makeModuleStatus(false, false),
  }
}

export function observeBringupModuleStatus(
  module: ModuleKey,
  snapshot: DeviceSnapshot,
): Pick<BringupModuleStatus, 'detected' | 'healthy'> {
  switch (module) {
    case 'imu': {
      const peripheral = snapshot.peripherals.imu
      const detected =
        peripheral.reachable ||
        peripheral.configured ||
        peripheral.whoAmI === 0x6c ||
        snapshot.imu.valid ||
        snapshot.imu.fresh
      const healthy =
        snapshot.imu.valid ||
        snapshot.imu.fresh ||
        (peripheral.reachable && peripheral.configured)
      return { detected, healthy }
    }
    case 'dac': {
      const peripheral = snapshot.peripherals.dac
      const registerReadbackSeen =
        peripheral.syncReg !== 0 ||
        peripheral.configReg !== 0 ||
        peripheral.gainReg !== 0 ||
        peripheral.statusReg !== 0 ||
        peripheral.dataAReg !== 0 ||
        peripheral.dataBReg !== 0
      const detected =
        peripheral.reachable || peripheral.configured || registerReadbackSeen
      const healthy =
        peripheral.reachable && (peripheral.configured || registerReadbackSeen)
      return { detected, healthy }
    }
    case 'haptic': {
      const peripheral = snapshot.peripherals.haptic
      const registerReadbackSeen =
        peripheral.modeReg !== 0 ||
        peripheral.libraryReg !== 0 ||
        peripheral.feedbackReg !== 0 ||
        peripheral.goReg !== 0
      const detected =
        peripheral.reachable ||
        peripheral.enablePinHigh ||
        peripheral.triggerPinHigh ||
        registerReadbackSeen
      return { detected, healthy: peripheral.reachable }
    }
    case 'tof': {
      const peripheral = snapshot.peripherals.tof
      const detected =
        peripheral.reachable ||
        peripheral.configured ||
        peripheral.sensorId !== 0 ||
        peripheral.dataReady ||
        snapshot.tof.valid ||
        snapshot.tof.fresh
      const healthy =
        snapshot.tof.valid ||
        snapshot.tof.fresh ||
        (peripheral.reachable &&
          (peripheral.configured || peripheral.sensorId !== 0))
      return { detected, healthy }
    }
    case 'pd': {
      const peripheral = snapshot.peripherals.pd
      const detected =
        peripheral.reachable ||
        peripheral.attached ||
        snapshot.pd.contractValid ||
        snapshot.pd.sourceVoltageV > 0 ||
        snapshot.pd.sourceCurrentA > 0
      const healthy = peripheral.reachable || snapshot.pd.contractValid
      return { detected, healthy }
    }
    case 'buttons': {
      const detected =
        snapshot.buttons.stage1Pressed ||
        snapshot.buttons.stage2Pressed ||
        snapshot.buttons.stage1Edge ||
        snapshot.buttons.stage2Edge
      return { detected, healthy: detected }
    }
    case 'laserDriver': {
      const detected =
        snapshot.rails.ld.enabled ||
        snapshot.rails.ld.pgood ||
        snapshot.laser.measuredCurrentA > 0 ||
        snapshot.laser.driverTempC !== 0
      const healthy = snapshot.rails.ld.pgood || snapshot.laser.loopGood
      return { detected, healthy }
    }
    case 'tec': {
      const detected =
        snapshot.rails.tec.enabled ||
        snapshot.rails.tec.pgood ||
        snapshot.tec.tempC !== 0 ||
        snapshot.tec.currentA !== 0 ||
        snapshot.tec.voltageV !== 0
      const healthy = snapshot.rails.tec.pgood || snapshot.tec.tempGood
      return { detected, healthy }
    }
  }
}

export function mergeObservedBringupModules(
  modules: BringupModuleMap,
  snapshot: DeviceSnapshot,
  connected: boolean,
): BringupModuleMap {
  if (!connected) {
    return modules
  }

  const mergeModule = (
    module: ModuleKey,
    status: BringupModuleStatus,
  ): BringupModuleStatus => {
    const observed = observeBringupModuleStatus(module, snapshot)
    return {
      ...status,
      detected: status.detected || observed.detected,
      healthy: status.healthy || observed.healthy,
    }
  }

  return {
    imu: mergeModule('imu', modules.imu),
    dac: mergeModule('dac', modules.dac),
    haptic: mergeModule('haptic', modules.haptic),
    tof: mergeModule('tof', modules.tof),
    buttons: mergeModule('buttons', modules.buttons),
    pd: mergeModule('pd', modules.pd),
    laserDriver: mergeModule('laserDriver', modules.laserDriver),
    tec: mergeModule('tec', modules.tec),
  }
}

export function makeDefaultBringupStatus(): BringupStatus {
  return {
    serviceModeRequested: false,
    serviceModeActive: false,
    interlocksDisabled: false,
    persistenceDirty: false,
    persistenceAvailable: false,
    lastSaveOk: false,
    profileRevision: 1,
    profileName: 'manual-bringup',
    power: {
      ldRequested: false,
      tecRequested: false,
    },
    illumination: {
      tof: {
        enabled: false,
        dutyCyclePct: 0,
        frequencyHz: 5000,
      },
    },
    modules: makeDefaultBringupModules(),
    tuning: {
      dacLdChannelV: 0,
      dacTecChannelV: DEFAULT_TEC_BRINGUP_VOLTAGE_V,
      dacReferenceMode: 'internal',
      dacGain2x: true,
      dacRefDiv: true,
      dacSyncMode: 'async',
      imuOdrHz: 208,
      imuAccelRangeG: 4,
      imuGyroRangeDps: 500,
      imuGyroEnabled: true,
      imuLpf2Enabled: false,
      imuTimestampEnabled: true,
      imuBduEnabled: true,
      imuIfIncEnabled: true,
      imuI2cDisabled: true,
      tofMinRangeM: 0.2,
      tofMaxRangeM: 1,
      tofStaleTimeoutMs: 150,
      pdProfiles: makeDefaultPdProfiles(),
      pdProgrammingOnlyMaxW: 30,
      pdReducedModeMinW: 30,
      pdReducedModeMaxW: 35,
      pdFullModeMinW: 35.1,
      pdFirmwarePlanEnabled: false,
      hapticEffectId: 47,
      hapticMode: 'internal_trigger',
      hapticLibrary: 1,
      hapticActuator: 'erm',
      hapticRtpLevel: 96,
    },
    tools: {
      lastI2cScan: 'No scan run yet.',
      lastI2cOp: 'No I2C transaction yet.',
      lastSpiOp: 'No SPI transaction yet.',
      lastAction: 'Manual bring-up defaults loaded. Pick one module page and mark only the hardware that is actually installed.',
    },
  }
}
