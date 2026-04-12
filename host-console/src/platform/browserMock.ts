import type {
  BridgeEvent,
  CommandEnvelope,
  FirmwareInspection,
} from '../domain/model'
import { makeMockState } from '../domain/mock'
import type { ConsoleBridge } from './bridge'

function makeLineEvent(channel: string, line: string): BridgeEvent {
  return {
    kind: 'protocol-line',
    channel,
    line,
  }
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value))
}

export function createBrowserMockBridge(): ConsoleBridge {
  let listener: ((event: BridgeEvent) => void) | null = null
  let tickId: number | null = null
  const seed = makeMockState()
  let runtimeMode = seed.snapshot.operate.runtimeMode

  const emitSnapshot = () => {
    if (listener === null) return
    listener(
      makeLineEvent(
        'browser-mock',
        JSON.stringify({
          type: 'event',
          event: 'status_snapshot',
          timestamp_ms: Date.now(),
          payload: {
            snapshot: seed.snapshot,
          },
        }),
      ),
    )
  }

  const emitLog = (category: string, message: string) => {
    if (listener === null) return
    listener(
      makeLineEvent(
        'browser-mock',
        JSON.stringify({
          type: 'event',
          event: 'log',
          timestamp_ms: Date.now(),
          payload: {
            category,
            message,
          },
        }),
      ),
    )
  }

  const emitLiveTelemetry = () => {
    if (listener === null) return
    seed.snapshot.session.uptimeSeconds += 1
    seed.snapshot.imu.beamPitchDeg += Math.sin(Date.now() / 1800) * 0.05
    seed.snapshot.tof.distanceM = 0.38 + Math.sin(Date.now() / 2200) * 0.01
    listener(
      makeLineEvent(
        'browser-mock',
        JSON.stringify({
          type: 'event',
          event: 'live_telemetry',
          timestamp_ms: Date.now(),
          payload: {
            snapshot: {
              session: seed.snapshot.session,
              pd: seed.snapshot.pd,
              rails: seed.snapshot.rails,
              imu: seed.snapshot.imu,
              tof: seed.snapshot.tof,
              buttons: seed.snapshot.buttons,
              laser: seed.snapshot.laser,
              tec: seed.snapshot.tec,
              safety: seed.snapshot.safety,
              deployment: seed.snapshot.deployment,
              bench: seed.snapshot.operate,
              bringup: {
                serviceModeRequested: seed.snapshot.integrate.serviceModeRequested,
                serviceModeActive: seed.snapshot.integrate.serviceModeActive,
                interlocksDisabled: seed.snapshot.integrate.interlocksDisabled,
                profileName: seed.snapshot.integrate.profileName,
                profileRevision: seed.snapshot.integrate.profileRevision,
                persistenceDirty: seed.snapshot.integrate.persistenceDirty,
                persistenceAvailable: seed.snapshot.integrate.persistenceAvailable,
                lastSaveOk: seed.snapshot.integrate.lastSaveOk,
                modules: seed.snapshot.integrate.modules,
                power: seed.snapshot.integrate.power,
                illumination: {
                  tof: {
                    enabled: seed.snapshot.integrate.illumination.tofEnabled,
                    dutyCyclePct: seed.snapshot.integrate.illumination.tofDutyCyclePct,
                    frequencyHz: seed.snapshot.integrate.illumination.tofFrequencyHz,
                  },
                },
                tuning: seed.snapshot.integrate.tuning,
                tools: seed.snapshot.integrate.tools,
              },
              gpioInspector: seed.snapshot.gpioInspector,
              fault: seed.snapshot.fault,
              wireless: seed.snapshot.wireless,
              transport: seed.snapshot.transport,
            },
          },
        }),
      ),
    )
  }

  const acknowledge = (command: CommandEnvelope, ok: boolean, result?: unknown, error?: string) => {
    if (listener === null) return
    listener(
      makeLineEvent(
        'browser-mock',
        JSON.stringify({
          type: 'resp',
          id: command.id,
          ok,
          result,
          error,
        }),
      ),
    )
  }

  const serviceWritable = () =>
    !seed.snapshot.deployment.active && seed.snapshot.integrate.serviceModeActive

  const hostOutputAllowed = () =>
    runtimeMode === 'modulated_host' &&
    seed.snapshot.deployment.active &&
    seed.snapshot.deployment.ready &&
    !seed.snapshot.deployment.running

  const completeDeployment = () => {
    const hostOnly = seed.snapshot.pd.sourceIsHostOnly || seed.snapshot.pd.sourceVoltageV < 9
    const steps: typeof seed.snapshot.deployment.steps = seed.snapshot.deployment.steps.map((step) => ({
      ...step,
      status: 'passed',
    }))
    if (hostOnly) {
      steps[1].status = 'failed'
      for (let index = 2; index < steps.length; index += 1) {
        steps[index].status = 'pending'
      }
      seed.snapshot.deployment.failed = true
      seed.snapshot.deployment.ready = false
      seed.snapshot.deployment.failureCode = 'pd_insufficient'
      seed.snapshot.deployment.failureReason = 'USB-only Phase 1 bench detected. A source of at least 9 V is required before power-dependent deployment steps can continue.'
      seed.snapshot.deployment.lastCompletedStep = 'ownership_reclaim'
      seed.snapshot.deployment.maxLaserCurrentA = 0
      seed.snapshot.deployment.maxOpticalPowerW = 0
      seed.snapshot.session.state = 'PROGRAMMING_ONLY'
    } else {
      seed.snapshot.deployment.failed = false
      seed.snapshot.deployment.ready = true
      seed.snapshot.deployment.failureCode = 'none'
      seed.snapshot.deployment.failureReason = ''
      seed.snapshot.deployment.lastCompletedStep = 'ready_posture'
      seed.snapshot.deployment.maxLaserCurrentA = 5
      seed.snapshot.deployment.maxOpticalPowerW = 5
      seed.snapshot.session.state = 'READY_NIR'
      seed.snapshot.rails.tec.enabled = true
      seed.snapshot.rails.tec.pgood = true
      seed.snapshot.rails.ld.enabled = true
      seed.snapshot.rails.ld.pgood = true
    }
    seed.snapshot.deployment.running = false
    seed.snapshot.deployment.currentStep = 'none'
    seed.snapshot.deployment.steps = steps
  }

  return {
    kind: 'browser-mock',
    async start(eventListener) {
      listener = eventListener
      listener({
        kind: 'transport',
        channel: 'browser-mock',
        status: 'connected',
        detail: 'Browser inspection mode is active.',
      })
      emitSnapshot()
      tickId = window.setInterval(() => emitLiveTelemetry(), 900)
    },
    async stop() {
      if (tickId !== null) {
        window.clearInterval(tickId)
      }
      tickId = null
      listener = null
    },
    async listSerialPorts() {
      return seed.ports
    },
    async connectSerial(port) {
      seed.snapshot.transport.mode = 'serial'
      seed.snapshot.transport.status = 'connected'
      seed.snapshot.transport.serialPort = port
      seed.snapshot.transport.detail = `Mock serial link active on ${port}.`
      emitSnapshot()
      emitLog('transport', `Mock serial link active on ${port}.`)
    },
    async disconnectSerial() {
      seed.snapshot.transport.mode = 'browser-mock'
      seed.snapshot.transport.status = 'disconnected'
      seed.snapshot.transport.detail = 'Mock serial link closed.'
      emitSnapshot()
    },
    async connectWireless(url) {
      seed.snapshot.transport.mode = 'wireless'
      seed.snapshot.transport.status = 'connected'
      seed.snapshot.transport.wirelessUrl = url
      seed.snapshot.transport.detail = `Mock wireless link active on ${url}.`
      emitSnapshot()
      emitLog('transport', `Mock wireless link active on ${url}.`)
    },
    async disconnectWireless() {
      seed.snapshot.transport.mode = 'browser-mock'
      seed.snapshot.transport.status = 'disconnected'
      seed.snapshot.transport.detail = 'Mock wireless link closed.'
      emitSnapshot()
    },
    async sendCommand(_channel, envelope) {
      let ok = true
      let error = ''

      switch (envelope.cmd) {
        case 'get_status':
        case 'get_faults':
          break
        case 'enter_deployment_mode':
          seed.snapshot.integrate.serviceModeRequested = false
          seed.snapshot.integrate.serviceModeActive = false
          seed.snapshot.integrate.interlocksDisabled = false
          seed.snapshot.deployment.active = true
          seed.snapshot.deployment.running = false
          seed.snapshot.deployment.ready = false
          seed.snapshot.deployment.failed = false
          seed.snapshot.deployment.currentStep = 'none'
          seed.snapshot.deployment.lastCompletedStep = 'none'
          seed.snapshot.deployment.failureCode = 'none'
          seed.snapshot.deployment.failureReason = ''
          emitLog('deployment', 'Deployment mode entered and service ownership was reclaimed.')
          break
        case 'exit_deployment_mode':
          seed.snapshot.deployment.active = false
          seed.snapshot.deployment.running = false
          seed.snapshot.deployment.ready = false
          seed.snapshot.deployment.failed = false
          seed.snapshot.deployment.currentStep = 'none'
          seed.snapshot.deployment.lastCompletedStep = 'none'
          seed.snapshot.deployment.failureCode = 'none'
          seed.snapshot.deployment.failureReason = ''
          seed.snapshot.session.state = 'SAFE_IDLE'
          seed.snapshot.rails.ld.enabled = false
          seed.snapshot.rails.ld.pgood = false
          seed.snapshot.rails.tec.enabled = false
          seed.snapshot.rails.tec.pgood = false
          emitLog('deployment', 'Deployment mode exited and the controller returned to a safe idle posture.')
          break
        case 'run_deployment_sequence':
          seed.snapshot.deployment.running = true
          completeDeployment()
          emitLog('deployment', seed.snapshot.deployment.ready ? 'Deployment checklist completed and the controller reached ready posture.' : 'Deployment checklist failed on the USB-only bench.')
          break
        case 'set_deployment_target':
          if (typeof envelope.args?.temp_c === 'number') {
            seed.snapshot.deployment.targetMode = 'temp'
            seed.snapshot.deployment.targetTempC = envelope.args.temp_c
          }
          if (typeof envelope.args?.lambda_nm === 'number') {
            seed.snapshot.deployment.targetMode = 'lambda'
            seed.snapshot.deployment.targetLambdaNm = envelope.args.lambda_nm
          }
          break
        case 'set_deployment_safety':
        case 'set_runtime_safety':
          Object.entries(envelope.args ?? {}).forEach(([key, value]) => {
            if (typeof value === 'number') {
              const mapping = key
                .replaceAll('_', ' ')
                .replace(/ ([a-z])/g, (_, letter) => letter.toUpperCase())
                .replace(/^([a-z])/, (_, letter) => letter.toLowerCase()) as keyof typeof seed.snapshot.safety
              if (mapping in seed.snapshot.safety) {
                ;(seed.snapshot.safety[mapping] as number) = value
              }
            }
          })
          emitLog('service', `${envelope.cmd === 'set_runtime_safety' ? 'Runtime' : 'Deployment'} safety thresholds updated.`)
          break
        case 'configure_wireless':
          if (envelope.args?.mode === 'station') {
            const nextSsid = typeof envelope.args?.ssid === 'string' ? envelope.args.ssid.trim() : ''
            if (nextSsid.length === 0) {
              ok = false
              error = 'Station mode requires an SSID.'
              break
            }
            seed.snapshot.wireless.mode = 'station'
            seed.snapshot.wireless.stationConfigured = true
            seed.snapshot.wireless.stationConnecting = false
            seed.snapshot.wireless.stationConnected = true
            seed.snapshot.wireless.stationSsid = nextSsid
            seed.snapshot.wireless.ssid = nextSsid
            seed.snapshot.wireless.apReady = false
            seed.snapshot.wireless.stationRssiDbm = -54
            seed.snapshot.wireless.stationChannel = 11
            seed.snapshot.wireless.ipAddress = '192.168.1.77'
            seed.snapshot.wireless.wsUrl = 'ws://192.168.1.77/ws'
            seed.snapshot.wireless.lastError = ''
            emitLog('transport', `Controller joined ${nextSsid} and published ws://192.168.1.77/ws.`)
          } else {
            seed.snapshot.wireless.mode = 'softap'
            seed.snapshot.wireless.apReady = true
            seed.snapshot.wireless.stationConnected = false
            seed.snapshot.wireless.stationConnecting = false
            seed.snapshot.wireless.ipAddress = '192.168.4.1'
            seed.snapshot.wireless.wsUrl = 'ws://192.168.4.1/ws'
            seed.snapshot.wireless.lastError = ''
            emitLog('transport', 'Bench AP restored on ws://192.168.4.1/ws.')
          }
          break
        case 'scan_wireless_networks':
          seed.snapshot.wireless.scanInProgress = false
          seed.snapshot.wireless.scannedNetworks = [
            { ssid: 'Lab-2G', rssiDbm: -51, channel: 11, secure: true },
            { ssid: 'SurgicalBench', rssiDbm: -58, channel: 6, secure: true },
            { ssid: 'Guest-2G', rssiDbm: -71, channel: 1, secure: false },
          ]
          emitLog('transport', 'Controller scanned nearby 2.4 GHz Wi-Fi targets.')
          break
        case 'clear_faults':
          seed.snapshot.fault.latched = false
          seed.snapshot.fault.activeCode = 'none'
          seed.snapshot.fault.activeCount = 0
          emitLog('fault', 'Mock fault latch cleared.')
          break
        case 'reboot':
          seed.snapshot.integrate.serviceModeRequested = false
          seed.snapshot.integrate.serviceModeActive = false
          seed.snapshot.integrate.interlocksDisabled = false
          seed.snapshot.laser.alignmentEnabled = false
          seed.snapshot.laser.nirEnabled = false
          seed.snapshot.session.state = 'BOOT_INIT'
          emitLog('service', 'Controlled reboot requested; outputs were dropped safe first.')
          break
        case 'enter_service_mode':
          if (!seed.snapshot.deployment.active) {
            seed.snapshot.integrate.serviceModeRequested = true
            seed.snapshot.integrate.serviceModeActive = true
            seed.snapshot.session.state = 'SERVICE_MODE'
            emitLog('service', 'Service mode active.')
          }
          break
        case 'exit_service_mode':
          seed.snapshot.integrate.serviceModeRequested = false
          seed.snapshot.integrate.serviceModeActive = false
          seed.snapshot.integrate.interlocksDisabled = false
          seed.snapshot.session.state = 'SAFE_IDLE'
          emitLog('service', 'Service mode closed.')
          break
        case 'set_interlocks_disabled':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active and deployment inactive before interlocks can be defeated.'
            break
          }
          seed.snapshot.integrate.interlocksDisabled = Boolean(envelope.args?.enabled)
          emitLog('service', seed.snapshot.integrate.interlocksDisabled ? 'Bench override active; normal beam interlocks are defeated.' : 'Normal interlock supervision restored.')
          break
        case 'set_runtime_mode':
          if (envelope.args?.mode !== 'binary_trigger' && envelope.args?.mode !== 'modulated_host') {
            ok = false
            error = 'Unsupported runtime mode.'
            break
          }
          runtimeMode = envelope.args.mode as typeof runtimeMode
          seed.snapshot.operate.runtimeMode = runtimeMode
          seed.snapshot.operate.requestedAlignmentEnabled = false
          seed.snapshot.operate.requestedNirEnabled = false
          if (runtimeMode === 'binary_trigger') {
            seed.snapshot.operate.modulationEnabled = false
          }
          break
        case 'enable_alignment':
        case 'disable_alignment':
          ok = false
          error = 'Host alignment requests are blocked in v2 pending the physical trigger path.'
          break
        case 'set_target_temp':
          if (hostOutputAllowed() && typeof envelope.args?.temp_c === 'number') {
            seed.snapshot.operate.targetMode = 'temp'
            seed.snapshot.tec.targetTempC = envelope.args.temp_c
          }
          break
        case 'set_target_lambda':
          if (hostOutputAllowed() && typeof envelope.args?.lambda_nm === 'number') {
            seed.snapshot.operate.targetMode = 'lambda'
            seed.snapshot.tec.targetLambdaNm = envelope.args.lambda_nm
          }
          break
        case 'set_laser_power':
          if (!hostOutputAllowed()) {
            ok = false
            error = 'Host runtime output control is only available in modulated_host mode after deployment is ready.'
            break
          }
          if (typeof envelope.args?.current_a === 'number') {
            seed.snapshot.laser.commandedCurrentA = clamp(envelope.args.current_a, 0, seed.snapshot.deployment.maxLaserCurrentA || seed.snapshot.safety.maxLaserCurrentA)
          }
          break
        case 'laser_output_enable':
          if (!hostOutputAllowed()) {
            ok = false
            error = 'Host runtime output control is only available in modulated_host mode after deployment is ready.'
            break
          }
          seed.snapshot.operate.requestedNirEnabled = true
          seed.snapshot.laser.nirEnabled = true
          break
        case 'laser_output_disable':
          seed.snapshot.operate.requestedNirEnabled = false
          seed.snapshot.laser.nirEnabled = false
          break
        case 'configure_modulation':
          if (!hostOutputAllowed()) {
            ok = false
            error = 'Host runtime output control is only available in modulated_host mode after deployment is ready.'
            break
          }
          seed.snapshot.operate.modulationEnabled = Boolean(envelope.args?.enabled ?? true)
          if (typeof envelope.args?.frequency_hz === 'number') {
            seed.snapshot.operate.modulationFrequencyHz = clamp(envelope.args.frequency_hz, 0, 4000)
          }
          if (typeof envelope.args?.duty_cycle_pct === 'number') {
            seed.snapshot.operate.modulationDutyCyclePct = clamp(envelope.args.duty_cycle_pct, 0, 100)
          }
          break
        case 'set_profile_name':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before the bring-up profile can be renamed.'
            break
          }
          if (typeof envelope.args?.name === 'string' && envelope.args.name.trim().length > 0) {
            seed.snapshot.integrate.profileName = envelope.args.name.trim().slice(0, 24)
            seed.snapshot.integrate.profileRevision += 1
          }
          break
        case 'save_bringup_profile':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before saving the bring-up profile.'
            break
          }
          seed.snapshot.integrate.lastSaveOk = true
          seed.snapshot.integrate.persistenceDirty = false
          seed.snapshot.integrate.tools.lastAction = 'Mock bring-up profile saved.'
          break
        case 'set_module_state':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before module state changes.'
            break
          }
          if (typeof envelope.args?.module === 'string') {
            const moduleName = envelope.args.module === 'laser_driver' ? 'laserDriver' : envelope.args.module
            if (moduleName in seed.snapshot.integrate.modules) {
              const key = moduleName as keyof typeof seed.snapshot.integrate.modules
              seed.snapshot.integrate.modules[key] = {
                ...seed.snapshot.integrate.modules[key],
                expectedPresent: Boolean(envelope.args.expected_present),
                debugEnabled: Boolean(envelope.args.debug_enabled),
                detected: Boolean(envelope.args.expected_present),
                healthy: Boolean(envelope.args.expected_present),
              }
            }
          }
          break
        case 'set_supply_enable':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before rail staging.'
            break
          }
          if (envelope.args?.rail === 'ld') {
            const enabled = Boolean(envelope.args.enabled)
            seed.snapshot.integrate.power.ldRequested = enabled
            seed.snapshot.rails.ld.enabled = enabled
            seed.snapshot.rails.ld.pgood = enabled
          }
          if (envelope.args?.rail === 'tec') {
            const enabled = Boolean(envelope.args.enabled)
            seed.snapshot.integrate.power.tecRequested = enabled
            seed.snapshot.rails.tec.enabled = enabled
            seed.snapshot.rails.tec.pgood = enabled
          }
          break
        case 'set_gpio_override':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before GPIO overrides.'
            break
          }
          if (typeof envelope.args?.gpio === 'number' && typeof envelope.args?.mode === 'string') {
            const pin = seed.snapshot.gpioInspector.pins.find((entry) => entry.gpioNum === envelope.args?.gpio)
            if (pin !== undefined) {
              if (envelope.args.mode === 'firmware') {
                pin.overrideActive = false
                pin.overrideMode = 'firmware'
                pin.overrideLevelHigh = false
                pin.overridePullupEnabled = false
                pin.overridePulldownEnabled = false
              } else {
                pin.overrideActive = true
                pin.overrideMode = envelope.args.mode as 'input' | 'output'
                pin.overrideLevelHigh = Boolean(envelope.args.level_high)
                pin.overridePullupEnabled = Boolean(envelope.args.pullup_enabled)
                pin.overridePulldownEnabled = Boolean(envelope.args.pulldown_enabled)
              }
            }
            seed.snapshot.gpioInspector.anyOverrideActive = seed.snapshot.gpioInspector.pins.some((pin) => pin.overrideActive)
            seed.snapshot.gpioInspector.activeOverrideCount = seed.snapshot.gpioInspector.pins.filter((pin) => pin.overrideActive).length
          }
          break
        case 'clear_gpio_overrides':
          seed.snapshot.gpioInspector.pins.forEach((pin) => {
            pin.overrideActive = false
            pin.overrideMode = 'firmware'
            pin.overrideLevelHigh = false
            pin.overridePullupEnabled = false
            pin.overridePulldownEnabled = false
          })
          seed.snapshot.gpioInspector.anyOverrideActive = false
          seed.snapshot.gpioInspector.activeOverrideCount = 0
          break
        case 'i2c_scan':
          seed.snapshot.integrate.tools.lastI2cScan = '0x28 0x29 0x48 0x5A'
          break
        case 'i2c_read':
          seed.snapshot.integrate.tools.lastI2cOp = `read ${String(envelope.args?.address)} reg ${String(envelope.args?.reg)} -> 0x00`
          break
        case 'i2c_write':
          seed.snapshot.integrate.tools.lastI2cOp = `write ${String(envelope.args?.address)} reg ${String(envelope.args?.reg)} <- ${String(envelope.args?.value)}`
          break
        case 'spi_read':
          seed.snapshot.integrate.tools.lastSpiOp = `read ${String(envelope.args?.device)} reg ${String(envelope.args?.reg)} -> 0x6C`
          break
        case 'spi_write':
          seed.snapshot.integrate.tools.lastSpiOp = `write ${String(envelope.args?.device)} reg ${String(envelope.args?.reg)} <- ${String(envelope.args?.value)}`
          break
        case 'dac_debug_config':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before DAC tuning.'
            break
          }
          if (typeof envelope.args?.reference_mode === 'string') seed.snapshot.integrate.tuning.dacReferenceMode = envelope.args.reference_mode as any
          if (typeof envelope.args?.gain_2x === 'boolean') seed.snapshot.integrate.tuning.dacGain2x = envelope.args.gain_2x
          if (typeof envelope.args?.ref_div === 'boolean') seed.snapshot.integrate.tuning.dacRefDiv = envelope.args.ref_div
          if (typeof envelope.args?.sync_mode === 'string') seed.snapshot.integrate.tuning.dacSyncMode = envelope.args.sync_mode as any
          break
        case 'dac_debug_set':
          if (typeof envelope.args?.channel === 'string' && typeof envelope.args?.voltage_v === 'number') {
            if (envelope.args.channel === 'ld') seed.snapshot.integrate.tuning.dacLdChannelV = envelope.args.voltage_v
            if (envelope.args.channel === 'tec') seed.snapshot.integrate.tuning.dacTecChannelV = envelope.args.voltage_v
          }
          break
        case 'imu_debug_config':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before IMU tuning.'
            break
          }
          Object.assign(seed.snapshot.integrate.tuning, {
            imuOdrHz: typeof envelope.args?.odr_hz === 'number' ? envelope.args.odr_hz : seed.snapshot.integrate.tuning.imuOdrHz,
            imuAccelRangeG: typeof envelope.args?.accel_range_g === 'number' ? envelope.args.accel_range_g : seed.snapshot.integrate.tuning.imuAccelRangeG,
            imuGyroRangeDps: typeof envelope.args?.gyro_range_dps === 'number' ? envelope.args.gyro_range_dps : seed.snapshot.integrate.tuning.imuGyroRangeDps,
            imuGyroEnabled: typeof envelope.args?.gyro_enabled === 'boolean' ? envelope.args.gyro_enabled : seed.snapshot.integrate.tuning.imuGyroEnabled,
            imuLpf2Enabled: typeof envelope.args?.lpf2_enabled === 'boolean' ? envelope.args.lpf2_enabled : seed.snapshot.integrate.tuning.imuLpf2Enabled,
            imuTimestampEnabled: typeof envelope.args?.timestamp_enabled === 'boolean' ? envelope.args.timestamp_enabled : seed.snapshot.integrate.tuning.imuTimestampEnabled,
            imuBduEnabled: typeof envelope.args?.bdu_enabled === 'boolean' ? envelope.args.bdu_enabled : seed.snapshot.integrate.tuning.imuBduEnabled,
            imuIfIncEnabled: typeof envelope.args?.if_inc_enabled === 'boolean' ? envelope.args.if_inc_enabled : seed.snapshot.integrate.tuning.imuIfIncEnabled,
            imuI2cDisabled: typeof envelope.args?.i2c_disabled === 'boolean' ? envelope.args.i2c_disabled : seed.snapshot.integrate.tuning.imuI2cDisabled,
          })
          break
        case 'tof_debug_config':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before ToF tuning.'
            break
          }
          if (typeof envelope.args?.min_range_m === 'number') seed.snapshot.integrate.tuning.tofMinRangeM = envelope.args.min_range_m
          if (typeof envelope.args?.max_range_m === 'number') seed.snapshot.integrate.tuning.tofMaxRangeM = envelope.args.max_range_m
          if (typeof envelope.args?.stale_timeout_ms === 'number') seed.snapshot.integrate.tuning.tofStaleTimeoutMs = envelope.args.stale_timeout_ms
          break
        case 'tof_illumination_set':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before ToF illumination changes.'
            break
          }
          seed.snapshot.integrate.illumination.tofEnabled = Boolean(envelope.args?.enabled)
          if (typeof envelope.args?.duty_cycle_pct === 'number') seed.snapshot.integrate.illumination.tofDutyCyclePct = envelope.args.duty_cycle_pct
          if (typeof envelope.args?.frequency_hz === 'number') seed.snapshot.integrate.illumination.tofFrequencyHz = envelope.args.frequency_hz
          break
        case 'pd_debug_config':
        case 'pd_save_firmware_plan':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before PD tuning.'
            break
          }
          if (typeof envelope.args?.programming_only_max_w === 'number') seed.snapshot.integrate.tuning.pdProgrammingOnlyMaxW = envelope.args.programming_only_max_w
          if (typeof envelope.args?.reduced_mode_min_w === 'number') seed.snapshot.integrate.tuning.pdReducedModeMinW = envelope.args.reduced_mode_min_w
          if (typeof envelope.args?.reduced_mode_max_w === 'number') seed.snapshot.integrate.tuning.pdReducedModeMaxW = envelope.args.reduced_mode_max_w
          if (typeof envelope.args?.full_mode_min_w === 'number') seed.snapshot.integrate.tuning.pdFullModeMinW = envelope.args.full_mode_min_w
          if (typeof envelope.args?.firmware_plan_enabled === 'boolean') seed.snapshot.integrate.tuning.pdFirmwarePlanEnabled = envelope.args.firmware_plan_enabled
          seed.snapshot.integrate.tuning.pdProfiles = seed.snapshot.integrate.tuning.pdProfiles.map((profile, index) => ({
            enabled: typeof envelope.args?.[`pdo${index + 1}_enabled`] === 'boolean' ? Boolean(envelope.args?.[`pdo${index + 1}_enabled`]) : profile.enabled,
            voltageV: typeof envelope.args?.[`pdo${index + 1}_voltage_v`] === 'number' ? Number(envelope.args?.[`pdo${index + 1}_voltage_v`]) : profile.voltageV,
            currentA: typeof envelope.args?.[`pdo${index + 1}_current_a`] === 'number' ? Number(envelope.args?.[`pdo${index + 1}_current_a`]) : profile.currentA,
          }))
          break
        case 'pd_burn_nvm':
          emitLog('service', 'Mock PD NVM burn accepted. Real hardware endurance is finite.')
          break
        case 'refresh_pd_status':
          emitLog('pd', `PD refresh -> ${seed.snapshot.pd.sourceVoltageV.toFixed(1)} V, ${seed.snapshot.pd.sourceCurrentA.toFixed(2)} A, ${seed.snapshot.pd.negotiatedPowerW.toFixed(1)} W`)
          break
        case 'haptic_debug_config':
          if (!serviceWritable()) {
            ok = false
            error = 'Service mode must be active before haptic tuning.'
            break
          }
          if (typeof envelope.args?.mode === 'string') seed.snapshot.integrate.tuning.hapticMode = envelope.args.mode as any
          if (typeof envelope.args?.library === 'number') seed.snapshot.integrate.tuning.hapticLibrary = envelope.args.library
          if (typeof envelope.args?.actuator === 'string') seed.snapshot.integrate.tuning.hapticActuator = envelope.args.actuator as any
          if (typeof envelope.args?.effect_id === 'number') seed.snapshot.integrate.tuning.hapticEffectId = envelope.args.effect_id
          if (typeof envelope.args?.rtp_level === 'number') seed.snapshot.integrate.tuning.hapticRtpLevel = envelope.args.rtp_level
          break
        case 'haptic_debug_fire':
          emitLog('haptic', `Haptic effect ${seed.snapshot.integrate.tuning.hapticEffectId} fired once in the mock rig.`)
          break
        case 'set_haptic_enable':
          emitLog('haptic', Boolean(envelope.args?.enabled) ? 'Haptic output enabled.' : 'Haptic output disabled.')
          break
        case 'haptic_external_trigger_pattern':
          emitLog('haptic', `External trigger pattern fired: ${String(envelope.args?.pulse_count ?? 0)} pulse(s).`)
          break
        default:
          break
      }

      acknowledge(envelope, ok, { snapshot: seed.snapshot }, ok ? undefined : error)
      emitSnapshot()
    },
    async inspectFirmware(path) {
      return {
        ...(seed.firmware as FirmwareInspection),
        path,
      }
    },
    async flashFirmware(_port, _path) {
      if (listener !== null) {
        listener({
          kind: 'flash-progress',
          channel: 'serial',
          phase: 'done',
          percent: 100,
          detail: 'Browser mock flash rehearsal completed.',
        })
      }
    },
    async exportSession(_path, _payload) {
      return
    },
    async writeSessionAutosave(_path, _payload) {
      return
    },
    async readSessionFile(_path) {
      return JSON.stringify(seed, null, 2)
    },
  }
}
