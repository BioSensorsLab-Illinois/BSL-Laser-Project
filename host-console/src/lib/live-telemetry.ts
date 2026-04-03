import type {
  DeviceSnapshot,
  DeploymentStep,
  RealtimeTelemetry,
  RealtimeTelemetryPatch,
} from '../types'

type Listener = () => void

export interface RealtimeTelemetryStore {
  getSnapshot(): RealtimeTelemetry
  subscribe(listener: Listener): () => void
}

export interface RealtimeTelemetryController extends RealtimeTelemetryStore {
  publish(telemetry: RealtimeTelemetryPatch): void
  reset(snapshot: DeviceSnapshot): void
  syncFromSnapshot(snapshot: DeviceSnapshot): void
}

function normalizeDeploymentSteps(
  steps: Array<Partial<DeploymentStep> | undefined> | DeploymentStep[],
  fallback: DeploymentStep[],
): DeploymentStep[] {
  return steps.map((step, index) => ({
    key: step?.key ?? fallback[index]?.key ?? `step_${index + 1}`,
    label: step?.label ?? fallback[index]?.label ?? `Step ${index + 1}`,
    status: step?.status ?? fallback[index]?.status ?? 'inactive',
  }))
}

export function makeRealtimeTelemetryFromSnapshot(
  snapshot: DeviceSnapshot,
): RealtimeTelemetry {
  return {
    session: {
      uptimeSeconds: snapshot.session.uptimeSeconds,
      state: snapshot.session.state,
      powerTier: snapshot.session.powerTier,
    },
    pd: {
      contractValid: snapshot.pd.contractValid,
      negotiatedPowerW: snapshot.pd.negotiatedPowerW,
      sourceVoltageV: snapshot.pd.sourceVoltageV,
      sourceCurrentA: snapshot.pd.sourceCurrentA,
      operatingCurrentA: snapshot.pd.operatingCurrentA,
      sourceIsHostOnly: snapshot.pd.sourceIsHostOnly,
    },
    rails: {
      ld: { ...snapshot.rails.ld },
      tec: { ...snapshot.rails.tec },
    },
    imu: { ...snapshot.imu },
    tof: { ...snapshot.tof },
    laser: { ...snapshot.laser },
    tec: { ...snapshot.tec },
    safety: {
      allowAlignment: snapshot.safety.allowAlignment,
      allowNir: snapshot.safety.allowNir,
      horizonBlocked: snapshot.safety.horizonBlocked,
      distanceBlocked: snapshot.safety.distanceBlocked,
      lambdaDriftBlocked: snapshot.safety.lambdaDriftBlocked,
      tecTempAdcBlocked: snapshot.safety.tecTempAdcBlocked,
      actualLambdaNm: snapshot.safety.actualLambdaNm,
      targetLambdaNm: snapshot.safety.targetLambdaNm,
      lambdaDriftNm: snapshot.safety.lambdaDriftNm,
      tempAdcVoltageV: snapshot.safety.tempAdcVoltageV,
    },
    buttons: { ...snapshot.buttons },
    bringup: {
      serviceModeRequested: snapshot.bringup.serviceModeRequested,
      serviceModeActive: snapshot.bringup.serviceModeActive,
      interlocksDisabled: snapshot.bringup.interlocksDisabled,
      illumination: {
        tof: { ...snapshot.bringup.illumination.tof },
      },
    },
    deployment: {
      ...snapshot.deployment,
      steps: snapshot.deployment.steps.map((step) => ({ ...step })),
    },
    fault: {
      latched: snapshot.fault.latched,
      activeCode: snapshot.fault.activeCode,
      activeCount: snapshot.fault.activeCount,
      tripCounter: snapshot.fault.tripCounter,
    },
  }
}

export function mergeRealtimeTelemetryIntoSnapshot(
  snapshot: DeviceSnapshot,
  telemetry: RealtimeTelemetry,
): DeviceSnapshot {
  return {
    ...snapshot,
    session: {
      ...snapshot.session,
      uptimeSeconds: telemetry.session.uptimeSeconds,
      state: telemetry.session.state,
      powerTier: telemetry.session.powerTier,
    },
    pd: {
      ...snapshot.pd,
      contractValid: telemetry.pd.contractValid,
      negotiatedPowerW: telemetry.pd.negotiatedPowerW,
      sourceVoltageV: telemetry.pd.sourceVoltageV,
      sourceCurrentA: telemetry.pd.sourceCurrentA,
      operatingCurrentA: telemetry.pd.operatingCurrentA,
      sourceIsHostOnly: telemetry.pd.sourceIsHostOnly,
    },
    rails: {
      ld: { ...snapshot.rails.ld, ...telemetry.rails.ld },
      tec: { ...snapshot.rails.tec, ...telemetry.rails.tec },
    },
    imu: { ...snapshot.imu, ...telemetry.imu },
    tof: { ...snapshot.tof, ...telemetry.tof },
    laser: { ...snapshot.laser, ...telemetry.laser },
    tec: { ...snapshot.tec, ...telemetry.tec },
    safety: {
      ...snapshot.safety,
      ...telemetry.safety,
    },
    buttons: { ...snapshot.buttons, ...telemetry.buttons },
    bringup: {
      ...snapshot.bringup,
      serviceModeRequested: telemetry.bringup.serviceModeRequested,
      serviceModeActive: telemetry.bringup.serviceModeActive,
      interlocksDisabled: telemetry.bringup.interlocksDisabled,
      illumination: {
        ...snapshot.bringup.illumination,
        tof: {
          ...snapshot.bringup.illumination.tof,
          ...telemetry.bringup.illumination.tof,
        },
      },
    },
    deployment: {
      ...snapshot.deployment,
      ...telemetry.deployment,
      steps:
        telemetry.deployment?.steps !== undefined
          ? normalizeDeploymentSteps(telemetry.deployment.steps, snapshot.deployment.steps)
          : snapshot.deployment.steps,
    },
    fault: {
      ...snapshot.fault,
      ...telemetry.fault,
    },
  }
}

function mergeRealtimeTelemetry(
  current: RealtimeTelemetry,
  patch: RealtimeTelemetryPatch,
): RealtimeTelemetry {
  return {
    session: {
      ...current.session,
      ...(patch.session ?? {}),
    },
    pd: {
      ...current.pd,
      ...(patch.pd ?? {}),
    },
    rails: {
      ld: {
        ...current.rails.ld,
        ...(patch.rails?.ld ?? {}),
      },
      tec: {
        ...current.rails.tec,
        ...(patch.rails?.tec ?? {}),
      },
    },
    imu: {
      ...current.imu,
      ...(patch.imu ?? {}),
    },
    tof: {
      ...current.tof,
      ...(patch.tof ?? {}),
    },
    laser: {
      ...current.laser,
      ...(patch.laser ?? {}),
    },
    tec: {
      ...current.tec,
      ...(patch.tec ?? {}),
    },
    safety: {
      ...current.safety,
      ...(patch.safety ?? {}),
    },
    buttons: {
      ...current.buttons,
      ...(patch.buttons ?? {}),
    },
    bringup: {
      ...current.bringup,
      ...(patch.bringup ?? {}),
      illumination: {
        ...current.bringup.illumination,
        ...(patch.bringup?.illumination ?? {}),
        tof: {
          ...current.bringup.illumination.tof,
          ...(patch.bringup?.illumination?.tof ?? {}),
        },
      },
    },
    deployment: {
      ...current.deployment,
      ...(patch.deployment ?? {}),
      steps:
        patch.deployment?.steps !== undefined
          ? normalizeDeploymentSteps(patch.deployment.steps, current.deployment.steps)
          : current.deployment.steps,
    },
    fault: {
      ...current.fault,
      ...(patch.fault ?? {}),
    },
  }
}

function patchDiffers(current: unknown, patch: unknown): boolean {
  if (patch === undefined) {
    return false
  }

  if (patch === null || typeof patch !== 'object' || Array.isArray(patch)) {
    return !Object.is(current, patch)
  }

  if (current === null || typeof current !== 'object' || Array.isArray(current)) {
    return true
  }

  for (const [key, value] of Object.entries(patch as Record<string, unknown>)) {
    if (patchDiffers((current as Record<string, unknown>)[key], value)) {
      return true
    }
  }

  return false
}

export function createRealtimeTelemetryStore(
  initialSnapshot: DeviceSnapshot,
): RealtimeTelemetryController {
  let current = makeRealtimeTelemetryFromSnapshot(initialSnapshot)
  const listeners = new Set<Listener>()

  const notify = () => {
    for (const listener of listeners) {
      listener()
    }
  }

  return {
    getSnapshot() {
      return current
    },
    subscribe(listener: Listener) {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
    publish(telemetry: RealtimeTelemetryPatch) {
      if (!patchDiffers(current, telemetry)) {
        return
      }
      current = mergeRealtimeTelemetry(current, telemetry)
      notify()
    },
    reset(snapshot: DeviceSnapshot) {
      current = makeRealtimeTelemetryFromSnapshot(snapshot)
      notify()
    },
    syncFromSnapshot(snapshot: DeviceSnapshot) {
      const next = makeRealtimeTelemetryFromSnapshot(snapshot)
      if (!patchDiffers(current, next)) {
        return
      }
      current = next
      notify()
    },
  }
}
