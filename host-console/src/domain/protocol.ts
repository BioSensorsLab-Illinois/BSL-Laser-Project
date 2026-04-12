import {
  makeEmptySnapshot,
  severityToTone,
} from './model'
import type {
  DeepPartial,
  EventEntry,
  ProtocolOutcome,
  Severity,
  Snapshot,
} from './model'

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

function readNumber(value: unknown, fallback = 0): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback
}

function readBoolean(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback
}

function readString(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback
}

function merge<T extends object>(base: T, incoming: DeepPartial<T> | undefined): T {
  return { ...base, ...(incoming ?? {}) } as T
}

function mergeSnapshot(current: Snapshot, incoming: DeepPartial<Snapshot>): Snapshot {
  return {
    ...current,
    ...incoming,
    identity: merge(current.identity, incoming.identity),
    session: merge(current.session, incoming.session),
    transport: merge(current.transport, incoming.transport),
    wireless: merge(current.wireless, incoming.wireless),
    pd: {
      ...current.pd,
      ...(incoming.pd ?? {}),
      sinkProfiles: (incoming.pd?.sinkProfiles ?? current.pd.sinkProfiles) as Snapshot['pd']['sinkProfiles'],
    },
    rails: {
      ld: merge(current.rails.ld, incoming.rails?.ld),
      tec: merge(current.rails.tec, incoming.rails?.tec),
    },
    imu: merge(current.imu, incoming.imu),
    tof: merge(current.tof, incoming.tof),
    buttons: merge(current.buttons, incoming.buttons),
    laser: merge(current.laser, incoming.laser),
    tec: merge(current.tec, incoming.tec),
    safety: merge(current.safety, incoming.safety),
    deployment: merge(current.deployment, incoming.deployment),
    operate: merge(current.operate, incoming.operate),
    integrate: {
      ...current.integrate,
      ...(incoming.integrate ?? {}),
      power: merge(current.integrate.power, incoming.integrate?.power),
      illumination: merge(current.integrate.illumination, incoming.integrate?.illumination),
      modules: ({
        ...current.integrate.modules,
        ...(incoming.integrate?.modules ?? {}),
      } as Snapshot['integrate']['modules']),
      tuning: {
        ...current.integrate.tuning,
        ...(incoming.integrate?.tuning ?? {}),
        pdProfiles: (incoming.integrate?.tuning?.pdProfiles ?? current.integrate.tuning.pdProfiles) as Snapshot['integrate']['tuning']['pdProfiles'],
      },
      tools: merge(current.integrate.tools, incoming.integrate?.tools),
    },
    gpioInspector: {
      ...current.gpioInspector,
      ...(incoming.gpioInspector ?? {}),
      pins: (incoming.gpioInspector?.pins ?? current.gpioInspector.pins) as Snapshot['gpioInspector']['pins'],
    },
    fault: merge(current.fault, incoming.fault),
  }
}

function decodeFastTelemetry(payload: Record<string, unknown>): DeepPartial<Snapshot> | undefined {
  const version = readNumber(payload.v, -1)
  const metrics = payload.m

  if (version !== 1 || !Array.isArray(metrics) || metrics.length < 17) {
    return undefined
  }

  const values = metrics.map((entry) => readNumber(entry, Number.NaN))
  if (values.some((entry) => Number.isNaN(entry))) {
    return undefined
  }

  const [
    imuFlags,
    pitchCenti,
    rollCenti,
    yawCenti,
    pitchLimitCenti,
    tofFlags,
    distanceMm,
    laserFlags,
    measuredCurrentMa,
    driverTempCenti,
    tecFlags,
    tecTempCenti,
    tecTempAdcMv,
    tecCurrentCentiA,
    tecVoltageCentiV,
    safetyFlags,
    buttonFlags,
  ] = values

  const bit = (value: number, index: number) => ((value >> index) & 1) === 1

  return {
    imu: {
      valid: bit(imuFlags, 0),
      fresh: bit(imuFlags, 1),
      beamPitchDeg: pitchCenti / 100,
      beamRollDeg: rollCenti / 100,
      beamYawDeg: yawCenti / 100,
      beamPitchLimitDeg: pitchLimitCenti / 100,
    },
    tof: {
      valid: bit(tofFlags, 0),
      fresh: bit(tofFlags, 1),
      distanceM: distanceMm / 1000,
    },
    buttons: {
      stage1Pressed: bit(buttonFlags, 0),
      stage2Pressed: bit(buttonFlags, 1),
    },
    laser: {
      alignmentEnabled: bit(laserFlags, 0),
      nirEnabled: bit(laserFlags, 1),
      driverStandby: bit(laserFlags, 2),
      loopGood: bit(laserFlags, 3),
      telemetryValid: bit(laserFlags, 4),
      measuredCurrentA: measuredCurrentMa / 1000,
      driverTempC: driverTempCenti / 100,
    },
    tec: {
      tempGood: bit(tecFlags, 0),
      telemetryValid: bit(tecFlags, 1),
      tempC: tecTempCenti / 100,
      tempAdcVoltageV: tecTempAdcMv / 1000,
      currentA: tecCurrentCentiA / 100,
      voltageV: tecVoltageCentiV / 100,
    },
    safety: {
      allowAlignment: bit(safetyFlags, 0),
      allowNir: bit(safetyFlags, 1),
      horizonBlocked: bit(safetyFlags, 2),
      distanceBlocked: bit(safetyFlags, 3),
      lambdaDriftBlocked: bit(safetyFlags, 4),
      tecTempAdcBlocked: bit(safetyFlags, 5),
    },
  }
}

function eventSeverity(category: string, title: string, detail: string): Severity {
  const combined = `${category} ${title} ${detail}`.toLowerCase()
  if (combined.includes('fault') || combined.includes('panic') || combined.includes('error')) return 'critical'
  if (combined.includes('warn') || combined.includes('deployment') || combined.includes('block') || combined.includes('hold')) return 'warn'
  if (combined.includes('connect') || combined.includes('ready') || combined.includes('saved') || combined.includes('live')) return 'ok'
  return 'info'
}

function makeEvent(base: Omit<EventEntry, 'tone'> & { tone?: EventEntry['tone'] }): EventEntry {
  return {
    ...base,
    tone: base.tone ?? severityToTone(base.severity),
  }
}

function normalizeModuleState(raw: unknown, seed: Snapshot['integrate']['modules'][keyof Snapshot['integrate']['modules']]) {
  if (!isRecord(raw)) return seed
  return {
    expectedPresent: readBoolean(raw.expectedPresent, seed.expectedPresent),
    debugEnabled: readBoolean(raw.debugEnabled, seed.debugEnabled),
    detected: readBoolean(raw.detected, seed.detected),
    healthy: readBoolean(raw.healthy, seed.healthy),
  }
}

function normalizeGpioRows(raw: unknown, seed: Snapshot['gpioInspector']['pins']): Snapshot['gpioInspector']['pins'] {
  if (!Array.isArray(raw)) return seed

  return raw.flatMap((row) => {
    if (Array.isArray(row)) {
      return [{
        gpioNum: readNumber(row[0]),
        modulePin: readNumber(row[1]),
        outputCapable: true,
        inputEnabled: false,
        outputEnabled: false,
        openDrainEnabled: false,
        pullupEnabled: false,
        pulldownEnabled: false,
        levelHigh: readNumber(row[2]) > 0,
        overrideActive: false,
        overrideMode: 'firmware' as const,
        overrideLevelHigh: false,
        overridePullupEnabled: false,
        overridePulldownEnabled: false,
      }]
    }

    if (isRecord(row)) {
      const overrideMode: Snapshot['gpioInspector']['pins'][number]['overrideMode'] =
        readString(row.overrideMode) === 'output'
          ? 'output'
          : readString(row.overrideMode) === 'input'
            ? 'input'
            : 'firmware'
      return [{
        gpioNum: readNumber(row.gpioNum),
        modulePin: readNumber(row.modulePin),
        outputCapable: readBoolean(row.outputCapable, true),
        inputEnabled: readBoolean(row.inputEnabled),
        outputEnabled: readBoolean(row.outputEnabled),
        openDrainEnabled: readBoolean(row.openDrainEnabled),
        pullupEnabled: readBoolean(row.pullupEnabled),
        pulldownEnabled: readBoolean(row.pulldownEnabled),
        levelHigh: readBoolean(row.levelHigh),
        overrideActive: readBoolean(row.overrideActive),
        overrideMode,
        overrideLevelHigh: readBoolean(row.overrideLevelHigh),
        overridePullupEnabled: readBoolean(row.overridePullupEnabled),
        overridePulldownEnabled: readBoolean(row.overridePulldownEnabled),
      }]
    }

    return []
  })
}

function normalizeSnapshotPayload(raw: Record<string, unknown>): DeepPartial<Snapshot> {
  const seed = makeEmptySnapshot()
  const bringup = isRecord(raw.bringup) ? raw.bringup : {}

  return {
    identity: isRecord(raw.identity)
      ? {
          label: readString(raw.identity.label, seed.identity.label),
          firmwareVersion: readString(raw.identity.firmwareVersion, seed.identity.firmwareVersion),
          hardwareRevision: readString(raw.identity.hardwareRevision, seed.identity.hardwareRevision),
          serialNumber: readString(raw.identity.serialNumber, seed.identity.serialNumber),
          protocolVersion: readString(raw.identity.protocolVersion, seed.identity.protocolVersion),
          boardName: readString(raw.identity.boardName, seed.identity.boardName),
          buildUtc: readString(raw.identity.buildUtc, seed.identity.buildUtc),
        }
      : undefined,
    session: isRecord(raw.session)
      ? {
          uptimeSeconds: readNumber(raw.session.uptimeSeconds, seed.session.uptimeSeconds),
          state: readString(raw.session.state, seed.session.state) as Snapshot['session']['state'],
          powerTier: readString(raw.session.powerTier, seed.session.powerTier) as Snapshot['session']['powerTier'],
          bootReason: readString(raw.session.bootReason, seed.session.bootReason),
        }
      : undefined,
    transport: isRecord(raw.transport)
      ? {
          mode: readString(raw.transport.mode, seed.transport.mode) as Snapshot['transport']['mode'],
          status: readString(raw.transport.status, seed.transport.status) as Snapshot['transport']['status'],
          detail: readString(raw.transport.detail, seed.transport.detail),
          serialPort: readString(raw.transport.serialPort, seed.transport.serialPort),
          wirelessUrl: readString(raw.transport.wirelessUrl, seed.transport.wirelessUrl),
        }
      : undefined,
    wireless: isRecord(raw.wireless)
      ? {
          started: readBoolean(raw.wireless.started, seed.wireless.started),
          mode: readString(raw.wireless.mode, seed.wireless.mode) as Snapshot['wireless']['mode'],
          apReady: readBoolean(raw.wireless.apReady),
          stationConfigured: readBoolean(raw.wireless.stationConfigured),
          stationConnecting: readBoolean(raw.wireless.stationConnecting),
          stationConnected: readBoolean(raw.wireless.stationConnected),
          clientCount: readNumber(raw.wireless.clientCount),
          ssid: readString(raw.wireless.ssid, seed.wireless.ssid),
          stationSsid: readString(raw.wireless.stationSsid),
          stationRssiDbm: readNumber(raw.wireless.stationRssiDbm),
          stationChannel: readNumber(raw.wireless.stationChannel),
          scanInProgress: readBoolean(raw.wireless.scanInProgress),
          scannedNetworks: Array.isArray(raw.wireless.scannedNetworks)
            ? raw.wireless.scannedNetworks.filter(isRecord).map((network) => ({
                ssid: readString(network.ssid),
                rssiDbm: readNumber(network.rssiDbm),
                channel: readNumber(network.channel),
                secure: readBoolean(network.secure),
              }))
            : seed.wireless.scannedNetworks,
          ipAddress: readString(raw.wireless.ipAddress),
          wsUrl: readString(raw.wireless.wsUrl),
          lastError: readString(raw.wireless.lastError),
        }
      : undefined,
    pd: isRecord(raw.pd)
      ? {
          contractValid: readBoolean(raw.pd.contractValid),
          negotiatedPowerW: readNumber(raw.pd.negotiatedPowerW),
          sourceVoltageV: readNumber(raw.pd.sourceVoltageV),
          sourceCurrentA: readNumber(raw.pd.sourceCurrentA),
          operatingCurrentA: readNumber(raw.pd.operatingCurrentA),
          contractObjectPosition: readNumber(raw.pd.contractObjectPosition, seed.pd.contractObjectPosition),
          sinkProfileCount: readNumber(raw.pd.sinkProfileCount, seed.pd.sinkProfileCount),
          sinkProfiles: Array.isArray(raw.pd.sinkProfiles)
            ? raw.pd.sinkProfiles.filter(isRecord).map((profile) => ({
                enabled: readBoolean(profile.enabled),
                voltageV: readNumber(profile.voltageV),
                currentA: readNumber(profile.currentA),
              }))
            : seed.pd.sinkProfiles,
          sourceIsHostOnly: readBoolean(raw.pd.sourceIsHostOnly),
        }
      : undefined,
    rails: isRecord(raw.rails)
      ? {
          ld: isRecord(raw.rails.ld) ? { enabled: readBoolean(raw.rails.ld.enabled), pgood: readBoolean(raw.rails.ld.pgood) } : seed.rails.ld,
          tec: isRecord(raw.rails.tec) ? { enabled: readBoolean(raw.rails.tec.enabled), pgood: readBoolean(raw.rails.tec.pgood) } : seed.rails.tec,
        }
      : undefined,
    imu: isRecord(raw.imu)
      ? {
          valid: readBoolean(raw.imu.valid),
          fresh: readBoolean(raw.imu.fresh),
          beamPitchDeg: readNumber(raw.imu.beamPitchDeg),
          beamRollDeg: readNumber(raw.imu.beamRollDeg),
          beamYawDeg: readNumber(raw.imu.beamYawDeg),
          beamYawRelative: readBoolean(raw.imu.beamYawRelative),
          beamPitchLimitDeg: readNumber(raw.imu.beamPitchLimitDeg),
        }
      : undefined,
    tof: isRecord(raw.tof)
      ? {
          valid: readBoolean(raw.tof.valid),
          fresh: readBoolean(raw.tof.fresh),
          distanceM: readNumber(raw.tof.distanceM),
          minRangeM: readNumber(raw.tof.minRangeM, seed.tof.minRangeM),
          maxRangeM: readNumber(raw.tof.maxRangeM, seed.tof.maxRangeM),
        }
      : undefined,
    buttons: isRecord(raw.buttons)
      ? {
          stage1Pressed: readBoolean(raw.buttons.stage1Pressed),
          stage2Pressed: readBoolean(raw.buttons.stage2Pressed),
        }
      : undefined,
    laser: isRecord(raw.laser)
      ? {
          alignmentEnabled: readBoolean(raw.laser.alignmentEnabled),
          nirEnabled: readBoolean(raw.laser.nirEnabled),
          driverStandby: readBoolean(raw.laser.driverStandby),
          telemetryValid: readBoolean(raw.laser.telemetryValid),
          commandVoltageV: readNumber(raw.laser.commandVoltageV),
          commandedCurrentA: readNumber(raw.laser.commandedCurrentA),
          currentMonitorVoltageV: readNumber(raw.laser.currentMonitorVoltageV),
          measuredCurrentA: readNumber(raw.laser.measuredCurrentA),
          loopGood: readBoolean(raw.laser.loopGood),
          driverTempVoltageV: readNumber(raw.laser.driverTempVoltageV),
          driverTempC: readNumber(raw.laser.driverTempC),
        }
      : undefined,
    tec: isRecord(raw.tec)
      ? {
          targetTempC: readNumber(raw.tec.targetTempC),
          targetLambdaNm: readNumber(raw.tec.targetLambdaNm),
          actualLambdaNm: readNumber(raw.tec.actualLambdaNm),
          telemetryValid: readBoolean(raw.tec.telemetryValid),
          commandVoltageV: readNumber(raw.tec.commandVoltageV),
          tempGood: readBoolean(raw.tec.tempGood),
          tempC: readNumber(raw.tec.tempC),
          tempAdcVoltageV: readNumber(raw.tec.tempAdcVoltageV),
          currentA: readNumber(raw.tec.currentA),
          voltageV: readNumber(raw.tec.voltageV),
          settlingSecondsRemaining: readNumber(raw.tec.settlingSecondsRemaining),
        }
      : undefined,
    safety: isRecord(raw.safety)
      ? {
          allowAlignment: readBoolean(raw.safety.allowAlignment),
          allowNir: readBoolean(raw.safety.allowNir),
          horizonBlocked: readBoolean(raw.safety.horizonBlocked),
          distanceBlocked: readBoolean(raw.safety.distanceBlocked),
          lambdaDriftBlocked: readBoolean(raw.safety.lambdaDriftBlocked),
          tecTempAdcBlocked: readBoolean(raw.safety.tecTempAdcBlocked),
          horizonThresholdDeg: readNumber(raw.safety.horizonThresholdDeg, seed.safety.horizonThresholdDeg),
          horizonHysteresisDeg: readNumber(raw.safety.horizonHysteresisDeg, seed.safety.horizonHysteresisDeg),
          tofMinRangeM: readNumber(raw.safety.tofMinRangeM, seed.safety.tofMinRangeM),
          tofMaxRangeM: readNumber(raw.safety.tofMaxRangeM, seed.safety.tofMaxRangeM),
          tofHysteresisM: readNumber(raw.safety.tofHysteresisM, seed.safety.tofHysteresisM),
          imuStaleMs: readNumber(raw.safety.imuStaleMs, seed.safety.imuStaleMs),
          tofStaleMs: readNumber(raw.safety.tofStaleMs, seed.safety.tofStaleMs),
          railGoodTimeoutMs: readNumber(raw.safety.railGoodTimeoutMs, seed.safety.railGoodTimeoutMs),
          lambdaDriftLimitNm: readNumber(raw.safety.lambdaDriftLimitNm, seed.safety.lambdaDriftLimitNm),
          lambdaDriftHysteresisNm: readNumber(raw.safety.lambdaDriftHysteresisNm, seed.safety.lambdaDriftHysteresisNm),
          lambdaDriftHoldMs: readNumber(raw.safety.lambdaDriftHoldMs, seed.safety.lambdaDriftHoldMs),
          ldOvertempLimitC: readNumber(raw.safety.ldOvertempLimitC, seed.safety.ldOvertempLimitC),
          tecTempAdcTripV: readNumber(raw.safety.tecTempAdcTripV, seed.safety.tecTempAdcTripV),
          tecTempAdcHysteresisV: readNumber(raw.safety.tecTempAdcHysteresisV, seed.safety.tecTempAdcHysteresisV),
          tecTempAdcHoldMs: readNumber(raw.safety.tecTempAdcHoldMs, seed.safety.tecTempAdcHoldMs),
          tecMinCommandC: readNumber(raw.safety.tecMinCommandC, seed.safety.tecMinCommandC),
          tecMaxCommandC: readNumber(raw.safety.tecMaxCommandC, seed.safety.tecMaxCommandC),
          tecReadyToleranceC: readNumber(raw.safety.tecReadyToleranceC, seed.safety.tecReadyToleranceC),
          maxLaserCurrentA: readNumber(raw.safety.maxLaserCurrentA, seed.safety.maxLaserCurrentA),
          actualLambdaNm: readNumber(raw.safety.actualLambdaNm, seed.safety.actualLambdaNm),
          targetLambdaNm: readNumber(raw.safety.targetLambdaNm, seed.safety.targetLambdaNm),
          lambdaDriftNm: readNumber(raw.safety.lambdaDriftNm, seed.safety.lambdaDriftNm),
          tempAdcVoltageV: readNumber(raw.safety.tempAdcVoltageV, seed.safety.tempAdcVoltageV),
        }
      : undefined,
    deployment: isRecord(raw.deployment)
      ? {
          active: readBoolean(raw.deployment.active),
          running: readBoolean(raw.deployment.running),
          ready: readBoolean(raw.deployment.ready),
          failed: readBoolean(raw.deployment.failed),
          currentStep: readString(raw.deployment.currentStep),
          lastCompletedStep: readString(raw.deployment.lastCompletedStep),
          failureCode: readString(raw.deployment.failureCode),
          failureReason: readString(raw.deployment.failureReason),
          targetMode: readString(raw.deployment.targetMode, seed.deployment.targetMode) as Snapshot['deployment']['targetMode'],
          targetTempC: readNumber(raw.deployment.targetTempC),
          targetLambdaNm: readNumber(raw.deployment.targetLambdaNm),
          maxLaserCurrentA: readNumber(raw.deployment.maxLaserCurrentA),
          maxOpticalPowerW: readNumber(raw.deployment.maxOpticalPowerW),
          steps: Array.isArray(raw.deployment.steps)
            ? raw.deployment.steps.filter(isRecord).map((step) => ({
                key: readString(step.key),
                label: readString(step.label),
                status: readString(step.status, 'inactive') as Snapshot['deployment']['steps'][number]['status'],
              }))
            : seed.deployment.steps,
        }
      : undefined,
    operate: isRecord(raw.bench)
      ? {
          targetMode: readString(raw.bench.targetMode, seed.operate.targetMode) as Snapshot['operate']['targetMode'],
          runtimeMode: readString(raw.bench.runtimeMode, seed.operate.runtimeMode) as Snapshot['operate']['runtimeMode'],
          runtimeModeSwitchAllowed: readBoolean(raw.bench.runtimeModeSwitchAllowed, seed.operate.runtimeModeSwitchAllowed),
          runtimeModeLockReason: readString(raw.bench.runtimeModeLockReason),
          requestedAlignmentEnabled: readBoolean(raw.bench.requestedAlignmentEnabled),
          requestedNirEnabled: readBoolean(raw.bench.requestedNirEnabled),
          modulationEnabled: readBoolean(raw.bench.modulationEnabled),
          modulationFrequencyHz: readNumber(raw.bench.modulationFrequencyHz, seed.operate.modulationFrequencyHz),
          modulationDutyCyclePct: readNumber(raw.bench.modulationDutyCyclePct, seed.operate.modulationDutyCyclePct),
          lowStateCurrentA: readNumber(raw.bench.lowStateCurrentA, seed.operate.lowStateCurrentA),
        }
      : undefined,
    integrate: isRecord(bringup)
      ? {
          serviceModeRequested: readBoolean(bringup.serviceModeRequested),
          serviceModeActive: readBoolean(bringup.serviceModeActive),
          interlocksDisabled: readBoolean(bringup.interlocksDisabled),
          profileName: readString(bringup.profileName, seed.integrate.profileName),
          profileRevision: readNumber(bringup.profileRevision, seed.integrate.profileRevision),
          persistenceDirty: readBoolean(bringup.persistenceDirty),
          persistenceAvailable: readBoolean(bringup.persistenceAvailable, seed.integrate.persistenceAvailable),
          lastSaveOk: readBoolean(bringup.lastSaveOk, seed.integrate.lastSaveOk),
          power: isRecord(bringup.power)
            ? {
                ldRequested: readBoolean(bringup.power.ldRequested),
                tecRequested: readBoolean(bringup.power.tecRequested),
              }
            : seed.integrate.power,
          illumination: isRecord(bringup.illumination) && isRecord(bringup.illumination.tof)
            ? {
                tofEnabled: readBoolean(bringup.illumination.tof.enabled),
                tofDutyCyclePct: readNumber(bringup.illumination.tof.dutyCyclePct),
                tofFrequencyHz: readNumber(bringup.illumination.tof.frequencyHz, seed.integrate.illumination.tofFrequencyHz),
              }
            : seed.integrate.illumination,
          modules: isRecord(bringup.modules)
            ? {
                imu: normalizeModuleState(bringup.modules.imu, seed.integrate.modules.imu),
                dac: normalizeModuleState(bringup.modules.dac, seed.integrate.modules.dac),
                haptic: normalizeModuleState(bringup.modules.haptic, seed.integrate.modules.haptic),
                tof: normalizeModuleState(bringup.modules.tof, seed.integrate.modules.tof),
                buttons: normalizeModuleState(bringup.modules.buttons, seed.integrate.modules.buttons),
                pd: normalizeModuleState(bringup.modules.pd, seed.integrate.modules.pd),
                laserDriver: normalizeModuleState(bringup.modules.laserDriver ?? bringup.modules.laser_driver, seed.integrate.modules.laserDriver),
                tec: normalizeModuleState(bringup.modules.tec, seed.integrate.modules.tec),
              }
            : seed.integrate.modules,
          tuning: isRecord(bringup.tuning)
            ? {
                dacLdChannelV: readNumber(bringup.tuning.dacLdChannelV, seed.integrate.tuning.dacLdChannelV),
                dacTecChannelV: readNumber(bringup.tuning.dacTecChannelV, seed.integrate.tuning.dacTecChannelV),
                dacReferenceMode: readString(bringup.tuning.dacReferenceMode, seed.integrate.tuning.dacReferenceMode) as Snapshot['integrate']['tuning']['dacReferenceMode'],
                dacGain2x: readBoolean(bringup.tuning.dacGain2x, seed.integrate.tuning.dacGain2x),
                dacRefDiv: readBoolean(bringup.tuning.dacRefDiv, seed.integrate.tuning.dacRefDiv),
                dacSyncMode: readString(bringup.tuning.dacSyncMode, seed.integrate.tuning.dacSyncMode) as Snapshot['integrate']['tuning']['dacSyncMode'],
                imuOdrHz: readNumber(bringup.tuning.imuOdrHz, seed.integrate.tuning.imuOdrHz),
                imuAccelRangeG: readNumber(bringup.tuning.imuAccelRangeG, seed.integrate.tuning.imuAccelRangeG),
                imuGyroRangeDps: readNumber(bringup.tuning.imuGyroRangeDps, seed.integrate.tuning.imuGyroRangeDps),
                imuGyroEnabled: readBoolean(bringup.tuning.imuGyroEnabled, seed.integrate.tuning.imuGyroEnabled),
                imuLpf2Enabled: readBoolean(bringup.tuning.imuLpf2Enabled, seed.integrate.tuning.imuLpf2Enabled),
                imuTimestampEnabled: readBoolean(bringup.tuning.imuTimestampEnabled, seed.integrate.tuning.imuTimestampEnabled),
                imuBduEnabled: readBoolean(bringup.tuning.imuBduEnabled, seed.integrate.tuning.imuBduEnabled),
                imuIfIncEnabled: readBoolean(bringup.tuning.imuIfIncEnabled, seed.integrate.tuning.imuIfIncEnabled),
                imuI2cDisabled: readBoolean(bringup.tuning.imuI2cDisabled, seed.integrate.tuning.imuI2cDisabled),
                tofMinRangeM: readNumber(bringup.tuning.tofMinRangeM, seed.integrate.tuning.tofMinRangeM),
                tofMaxRangeM: readNumber(bringup.tuning.tofMaxRangeM, seed.integrate.tuning.tofMaxRangeM),
                tofStaleTimeoutMs: readNumber(bringup.tuning.tofStaleTimeoutMs, seed.integrate.tuning.tofStaleTimeoutMs),
                pdProfiles: Array.isArray(bringup.tuning.pdProfiles)
                  ? bringup.tuning.pdProfiles.filter(isRecord).map((profile) => ({
                      enabled: readBoolean(profile.enabled),
                      voltageV: readNumber(profile.voltageV),
                      currentA: readNumber(profile.currentA),
                    }))
                  : seed.integrate.tuning.pdProfiles,
                pdProgrammingOnlyMaxW: readNumber(bringup.tuning.pdProgrammingOnlyMaxW, seed.integrate.tuning.pdProgrammingOnlyMaxW),
                pdReducedModeMinW: readNumber(bringup.tuning.pdReducedModeMinW, seed.integrate.tuning.pdReducedModeMinW),
                pdReducedModeMaxW: readNumber(bringup.tuning.pdReducedModeMaxW, seed.integrate.tuning.pdReducedModeMaxW),
                pdFullModeMinW: readNumber(bringup.tuning.pdFullModeMinW, seed.integrate.tuning.pdFullModeMinW),
                pdFirmwarePlanEnabled: readBoolean(bringup.tuning.pdFirmwarePlanEnabled, seed.integrate.tuning.pdFirmwarePlanEnabled),
                hapticEffectId: readNumber(bringup.tuning.hapticEffectId, seed.integrate.tuning.hapticEffectId),
                hapticMode: readString(bringup.tuning.hapticMode, seed.integrate.tuning.hapticMode) as Snapshot['integrate']['tuning']['hapticMode'],
                hapticLibrary: readNumber(bringup.tuning.hapticLibrary, seed.integrate.tuning.hapticLibrary),
                hapticActuator: readString(bringup.tuning.hapticActuator, seed.integrate.tuning.hapticActuator) as Snapshot['integrate']['tuning']['hapticActuator'],
                hapticRtpLevel: readNumber(bringup.tuning.hapticRtpLevel, seed.integrate.tuning.hapticRtpLevel),
              }
            : undefined,
          tools: isRecord(bringup.tools)
            ? {
                lastI2cScan: readString(bringup.tools.lastI2cScan),
                lastI2cOp: readString(bringup.tools.lastI2cOp),
                lastSpiOp: readString(bringup.tools.lastSpiOp),
                lastAction: readString(bringup.tools.lastAction),
              }
            : seed.integrate.tools,
        }
      : undefined,
    gpioInspector: isRecord(raw.gpioInspector)
      ? {
          anyOverrideActive: readBoolean(raw.gpioInspector.anyOverrideActive),
          activeOverrideCount: readNumber(raw.gpioInspector.activeOverrideCount),
          pins: normalizeGpioRows(raw.gpioInspector.pins, seed.gpioInspector.pins),
        }
      : undefined,
    fault: isRecord(raw.fault)
      ? {
          latched: readBoolean(raw.fault.latched),
          activeCode: readString(raw.fault.activeCode),
          activeCount: readNumber(raw.fault.activeCount),
          tripCounter: readNumber(raw.fault.tripCounter),
          lastFaultAtIso: raw.fault.lastFaultAtIso === null ? null : readString(raw.fault.lastFaultAtIso),
        }
      : undefined,
  }
}

export function applyProtocolLine(_current: Snapshot, line: string): ProtocolOutcome {
  let parsed: unknown

  try {
    parsed = JSON.parse(line)
  } catch {
    return {
      event: makeEvent({
        id: `raw-${Date.now()}`,
        atIso: new Date().toISOString(),
        category: 'console',
        title: 'Console line',
        detail: line,
        severity: eventSeverity('console', 'Console line', line),
        source: 'firmware',
        module: 'console',
        summary: line,
      }),
    }
  }

  if (!isRecord(parsed)) {
    return {}
  }

  if (parsed.type === 'resp') {
    const result = isRecord(parsed.result) ? parsed.result : {}
    const snapshot = normalizeSnapshotPayload(isRecord(result.snapshot) ? result.snapshot : result)
    return {
      snapshot,
      commandAck: {
        id: readNumber(parsed.id),
        ok: readBoolean(parsed.ok, true),
        note: readBoolean(parsed.ok, true) ? 'Controller acknowledged command.' : readString(parsed.error, 'Command rejected.'),
      },
    }
  }

  if (parsed.type === 'event') {
    if (parsed.event === 'fast_telemetry' && isRecord(parsed.payload)) {
      const snapshot = decodeFastTelemetry(parsed.payload)
      return snapshot ? { snapshot } : {}
    }

    if ((parsed.event === 'live_telemetry' || parsed.event === 'status_snapshot') && isRecord(parsed.payload)) {
      const payload = isRecord(parsed.payload.snapshot) ? parsed.payload.snapshot : parsed.payload
      return {
        snapshot: normalizeSnapshotPayload(payload),
      }
    }

    if (parsed.event === 'log' && isRecord(parsed.payload)) {
      const category = readString(parsed.payload.category, 'log')
      const detail = readString(parsed.payload.message, readString(parsed.detail))
      const severity = eventSeverity(category, 'Firmware log', detail)
      return {
        event: makeEvent({
          id: `evt-${Date.now()}`,
          atIso: new Date().toISOString(),
          category,
          title: category === 'fault' ? 'Firmware fault' : 'Firmware log',
          detail,
          severity,
          source: 'firmware',
          module: category === 'fault' ? 'fault' : category,
          summary: detail,
        }),
      }
    }

    const category = readString(parsed.event, 'event')
    const detail = readString(parsed.detail, JSON.stringify(parsed.payload ?? {}))
    const severity = eventSeverity(category, category, detail)
    return {
      event: makeEvent({
        id: `evt-${Date.now()}`,
        atIso: new Date().toISOString(),
        category,
        title: category.replaceAll('_', ' '),
        detail,
        severity,
        source: 'firmware',
        module: category.includes('wireless') ? 'transport' : category,
        summary: detail,
      }),
    }
  }

  return {}
}

export function mergeProtocolSnapshot(current: Snapshot, patch: DeepPartial<Snapshot>): Snapshot {
  return mergeSnapshot(current, patch)
}
