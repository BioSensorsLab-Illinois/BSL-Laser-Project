import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import {
  Activity,
  Cable,
  Compass,
  Cpu,
  Gamepad2,
  Layers3,
  PlugZap,
  Radio,
  Save,
  ShieldAlert,
  SlidersHorizontal,
  Thermometer,
  Waves,
  Wrench,
} from 'lucide-react'

import { HelpHint } from './HelpHint'
import { ConfirmActionDialog } from './ConfirmActionDialog'
import { ControllerBusyOverlay } from './ControllerBusyOverlay'
import { ImuPostureCard } from './ImuPostureCard'
import { ProgressMeter } from './ProgressMeter'
import {
  dacReferenceOptions,
  dacSyncModeOptions,
  hapticActuatorOptions,
  hapticLibraryOptions,
  hapticModeOptions,
  imuAccelRangeOptions,
  imuGyroRangeOptions,
  imuOdrOptions,
  makeDefaultPdProfiles,
  moduleKeys,
  moduleMeta,
  pdCurrentOptions,
  pdVoltageOptions,
} from '../lib/bringup'
import { formatNumber } from '../lib/format'
import type { UiTone } from '../lib/presentation'
import type {
  BringupModuleMap,
  BringupModuleStatus,
  DacReferenceMode,
  DacSyncMode,
  DeviceSnapshot,
  HapticActuator,
  HapticMode,
  ModuleKey,
  PdSinkProfile,
  TransportStatus,
} from '../types'

type BringupWorkbenchProps = {
  snapshot: DeviceSnapshot
  transportStatus: TransportStatus
  onIssueCommandAwaitAck: (
    cmd: string,
    risk: 'read' | 'write' | 'service' | 'firmware',
    note: string,
    args?: Record<string, number | string | boolean>,
    options?: {
      logHistory?: boolean
      timeoutMs?: number
    },
  ) => Promise<{
    ok: boolean
    note: string
  }>
}

type BringupPageKey = 'workflow' | ModuleKey

type BringupFormState = {
  profileName: string
  modules: BringupModuleMap
  dacLdVoltage: string
  dacTecVoltage: string
  dacReferenceMode: DacReferenceMode
  dacGain2x: boolean
  dacRefDiv: boolean
  dacSyncMode: DacSyncMode
  imuOdrHz: string
  imuAccelRangeG: string
  imuGyroRangeDps: string
  imuGyroEnabled: boolean
  imuLpf2Enabled: boolean
  imuTimestampEnabled: boolean
  imuBduEnabled: boolean
  imuIfIncEnabled: boolean
  imuI2cDisabled: boolean
  tofMinRangeM: string
  tofMaxRangeM: string
  tofStaleTimeoutMs: string
  pdProfiles: Array<{
    enabled: boolean
    voltageV: string
    currentA: string
  }>
  pdProgrammingOnlyMaxW: string
  pdReducedModeMinW: string
  pdReducedModeMaxW: string
  pdFullModeMinW: string
  pdFirmwarePlanEnabled: boolean
  hapticEffectId: string
  hapticMode: HapticMode
  hapticLibrary: string
  hapticActuator: HapticActuator
  hapticRtpLevel: string
  safetyHorizonThresholdDeg: string
  safetyHorizonHysteresisDeg: string
  safetyTofMinRangeM: string
  safetyTofMaxRangeM: string
  safetyTofHysteresisM: string
  safetyImuStaleMs: string
  safetyTofStaleMs: string
  safetyRailGoodTimeoutMs: string
  safetyLambdaDriftLimitNm: string
  safetyLambdaDriftHysteresisNm: string
  safetyLambdaDriftHoldMs: string
  safetyLdOvertempLimitC: string
  safetyTecTempAdcTripV: string
  safetyTecTempAdcHysteresisV: string
  safetyTecTempAdcHoldMs: string
  safetyTecMinCommandC: string
  safetyTecMaxCommandC: string
  safetyTecReadyToleranceC: string
  safetyMaxLaserCurrentA: string
}

type BringupDraft = BringupFormState

type PageDefinition = {
  id: BringupPageKey
  label: string
  detail: string
  icon: typeof Wrench
}

type ModuleScore = {
  progress: number
  tone: UiTone
  label: string
}

type BringupStoredState = {
  version: number
  activePage: BringupPageKey
  form: BringupDraft
  i2cAddress: string
  i2cRegister: string
  i2cValue: string
  spiDevice: string
  spiRegister: string
  spiValue: string
}

type BringupOperation = {
  label: string
  detail: string
  percent: number
  tone: UiTone
  requiresConfirm?: boolean
}

type ProbeRequest = {
  id: string
  cmd: string
  note: string
  args?: Record<string, number | string | boolean>
}

const HOST_DRAFT_STORAGE_KEY = 'bsl-bringup-draft-v6'
const LEGACY_HOST_DRAFT_STORAGE_KEY = 'bsl-bringup-draft-v3'
const BRINGUP_ACK_TIMEOUT_MS = 2400
const BRINGUP_PROBE_INTERVAL_MS = 1500
const BRINGUP_PD_REFRESH_TIMEOUT_MS = 2600
const BRINGUP_PD_APPLY_TIMEOUT_MS = 7000
const BRINGUP_PD_NVM_TIMEOUT_MS = 12000
const BRINGUP_SERVICE_MODE_WAIT_MS = 2200
const BRINGUP_SUCCESS_DISMISS_MS = 260

const bringupPages: PageDefinition[] = [
  {
    id: 'workflow',
    label: 'Service',
    detail: 'Start here for write session control, saved plan, and runtime safety policy.',
    icon: Wrench,
  },
  {
    id: 'imu',
    label: 'IMU',
    detail: 'LSM6DSO identity, sampling, and runtime flags.',
    icon: Compass,
  },
  {
    id: 'dac',
    label: 'DAC',
    detail: 'DAC80502 shadow voltages, reference, and update policy.',
    icon: SlidersHorizontal,
  },
  {
    id: 'haptic',
    label: 'Haptic',
    detail: 'DRV2605 mode, library, actuator, and quick effect tests.',
    icon: Gamepad2,
  },
  {
    id: 'tof',
    label: 'ToF',
    detail: 'Distance window and stale-data supervision placeholder.',
    icon: Waves,
  },
  {
    id: 'buttons',
    label: 'Buttons',
    detail: 'Two-stage trigger population and future debounce path.',
    icon: Layers3,
  },
  {
    id: 'pd',
    label: 'USB-PD',
    detail: 'STUSB4500 sink priorities, allowed power plan, and runtime threshold checks.',
    icon: PlugZap,
  },
  {
    id: 'laserDriver',
    label: 'Laser Driver',
    detail: 'ATLS6A214 supervision notes and readiness checks.',
    icon: Radio,
  },
  {
    id: 'tec',
    label: 'TEC',
    detail: 'TEC supervision readiness and temperature loop status.',
    icon: Thermometer,
  },
]

function toFirmwareModuleName(module: ModuleKey): string {
  return module === 'laserDriver' ? 'laser_driver' : module
}

function parseNumber(value: string, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

function formatRegisterHex(value: number, width = 4): string {
  return `0x${Math.max(0, Math.trunc(value)).toString(16).toUpperCase().padStart(width, '0')}`
}

function readbackErrorLabel(errorCode: number, errorName: string): string {
  return errorCode === 0 ? 'ESP_OK' : errorName
}

function pdProfilePowerW(profile: Pick<PdSinkProfile, 'voltageV' | 'currentA'>): number {
  return profile.voltageV * profile.currentA
}

function classifyPdTierFromPlan(
  negotiatedPowerW: number,
  sourceIsHostOnly: boolean,
  programmingOnlyMaxW: number,
  reducedModeMinW: number,
  reducedModeMaxW: number,
  fullModeMinW: number,
): DeviceSnapshot['session']['powerTier'] {
  const operationalMinW = Math.max(programmingOnlyMaxW, reducedModeMinW)

  if (sourceIsHostOnly) {
    return 'programming_only'
  }

  if (negotiatedPowerW < operationalMinW) {
    return 'insufficient'
  }

  if (negotiatedPowerW <= reducedModeMaxW) {
    return 'reduced'
  }

  if (negotiatedPowerW >= fullModeMinW) {
    return 'full'
  }

  return 'reduced'
}

function pdObjectLabel(index: number): string {
  switch (index) {
    case 2:
      return 'PDO3'
    case 1:
      return 'PDO2'
    default:
      return 'PDO1'
  }
}

function pdPriorityDetail(index: number): string {
  switch (index) {
    case 2:
      return 'First-choice high-power request'
    case 1:
      return 'Second-choice fallback'
    default:
      return 'Mandatory 5 V fallback'
  }
}

function readbackPdProfiles(snapshot: DeviceSnapshot): PdSinkProfile[] {
  const liveProfiles =
    snapshot.pd.sinkProfiles.length > 0
      ? snapshot.pd.sinkProfiles
      : makeDefaultPdProfiles()

  return Array.from({ length: 3 }, (_, index) => {
    const profile = liveProfiles[index]

    return profile ?? {
      enabled: false,
      voltageV: 0,
      currentA: 0,
    }
  })
}

function modulePlanSignature(modules: BringupModuleMap): string {
  return moduleKeys
    .map((module) => {
      const status = modules[module]
      return `${module}:${status.expectedPresent ? 1 : 0}:${status.debugEnabled ? 1 : 0}`
    })
    .join('|')
}

function makeFormState(snapshot: DeviceSnapshot): BringupFormState {
  const pdProfiles = snapshot.bringup.tuning.pdProfiles ?? makeDefaultPdProfiles()

  return {
    profileName: snapshot.bringup.profileName,
    modules: snapshot.bringup.modules,
    dacLdVoltage: String(snapshot.bringup.tuning.dacLdChannelV),
    dacTecVoltage: String(snapshot.bringup.tuning.dacTecChannelV),
    dacReferenceMode: snapshot.bringup.tuning.dacReferenceMode,
    dacGain2x: snapshot.bringup.tuning.dacGain2x,
    dacRefDiv: snapshot.bringup.tuning.dacRefDiv,
    dacSyncMode: snapshot.bringup.tuning.dacSyncMode,
    imuOdrHz: String(snapshot.bringup.tuning.imuOdrHz),
    imuAccelRangeG: String(snapshot.bringup.tuning.imuAccelRangeG),
    imuGyroRangeDps: String(snapshot.bringup.tuning.imuGyroRangeDps),
    imuGyroEnabled: snapshot.bringup.tuning.imuGyroEnabled,
    imuLpf2Enabled: snapshot.bringup.tuning.imuLpf2Enabled,
    imuTimestampEnabled: snapshot.bringup.tuning.imuTimestampEnabled,
    imuBduEnabled: snapshot.bringup.tuning.imuBduEnabled,
    imuIfIncEnabled: snapshot.bringup.tuning.imuIfIncEnabled,
    imuI2cDisabled: snapshot.bringup.tuning.imuI2cDisabled,
    tofMinRangeM: String(snapshot.bringup.tuning.tofMinRangeM),
    tofMaxRangeM: String(snapshot.bringup.tuning.tofMaxRangeM),
    tofStaleTimeoutMs: String(snapshot.bringup.tuning.tofStaleTimeoutMs),
    pdProfiles: pdProfiles.map((profile) => ({
      enabled: profile.enabled,
      voltageV: String(profile.voltageV),
      currentA: String(profile.currentA),
    })),
    pdProgrammingOnlyMaxW: String(snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30),
    pdReducedModeMinW: String(snapshot.bringup.tuning.pdReducedModeMinW ?? 30),
    pdReducedModeMaxW: String(snapshot.bringup.tuning.pdReducedModeMaxW ?? 35),
    pdFullModeMinW: String(snapshot.bringup.tuning.pdFullModeMinW ?? 35.1),
    pdFirmwarePlanEnabled: snapshot.bringup.tuning.pdFirmwarePlanEnabled ?? false,
    hapticEffectId: String(snapshot.bringup.tuning.hapticEffectId),
    hapticMode: snapshot.bringup.tuning.hapticMode,
    hapticLibrary: String(snapshot.bringup.tuning.hapticLibrary),
    hapticActuator: snapshot.bringup.tuning.hapticActuator,
    hapticRtpLevel: String(snapshot.bringup.tuning.hapticRtpLevel),
    safetyHorizonThresholdDeg: formatNumber(snapshot.safety.horizonThresholdDeg, 1),
    safetyHorizonHysteresisDeg: formatNumber(snapshot.safety.horizonHysteresisDeg, 1),
    safetyTofMinRangeM: formatNumber(snapshot.safety.tofMinRangeM, 3),
    safetyTofMaxRangeM: formatNumber(snapshot.safety.tofMaxRangeM, 3),
    safetyTofHysteresisM: formatNumber(snapshot.safety.tofHysteresisM, 3),
    safetyImuStaleMs: String(snapshot.safety.imuStaleMs),
    safetyTofStaleMs: String(snapshot.safety.tofStaleMs),
    safetyRailGoodTimeoutMs: String(snapshot.safety.railGoodTimeoutMs),
    safetyLambdaDriftLimitNm: formatNumber(snapshot.safety.lambdaDriftLimitNm, 2),
    safetyLambdaDriftHysteresisNm: formatNumber(snapshot.safety.lambdaDriftHysteresisNm, 2),
    safetyLambdaDriftHoldMs: String(snapshot.safety.lambdaDriftHoldMs),
    safetyLdOvertempLimitC: formatNumber(snapshot.safety.ldOvertempLimitC, 1),
    safetyTecTempAdcTripV: formatNumber(snapshot.safety.tecTempAdcTripV, 3),
    safetyTecTempAdcHysteresisV: formatNumber(snapshot.safety.tecTempAdcHysteresisV, 3),
    safetyTecTempAdcHoldMs: String(snapshot.safety.tecTempAdcHoldMs),
    safetyTecMinCommandC: formatNumber(snapshot.safety.tecMinCommandC, 1),
    safetyTecMaxCommandC: formatNumber(snapshot.safety.tecMaxCommandC, 1),
    safetyTecReadyToleranceC: formatNumber(snapshot.safety.tecReadyToleranceC, 2),
    safetyMaxLaserCurrentA: formatNumber(snapshot.safety.maxLaserCurrentA, 2),
  }
}

function sanitizeDacDraft(form: BringupFormState): BringupFormState {
  const dacRefDiv =
    form.dacReferenceMode === 'internal' && form.dacGain2x
      ? true
      : form.dacRefDiv

  return {
    ...form,
    dacRefDiv,
  }
}

function ensureModuleDebugWrite(
  form: BringupFormState,
  module: ModuleKey,
): BringupFormState {
  if (form.modules[module].expectedPresent || form.modules[module].debugEnabled) {
    return form
  }

  return {
    ...form,
    modules: {
      ...form.modules,
      [module]: {
        ...form.modules[module],
        debugEnabled: true,
      },
    },
  }
}

function makeStoredState(snapshot: DeviceSnapshot): BringupStoredState {
  return {
    version: 8,
    activePage: 'workflow',
    form: makeFormState(snapshot),
    i2cAddress: '0x48',
    i2cRegister: '0x00',
    i2cValue: '0x00',
    spiDevice: 'imu',
    spiRegister: '0x0F',
    spiValue: '0x00',
  }
}

function persistStoredState(state: BringupStoredState): void {
  window.localStorage.setItem(HOST_DRAFT_STORAGE_KEY, JSON.stringify(state))
}

function loadHostDraftState(snapshot: DeviceSnapshot): BringupStoredState {
  try {
    const raw = window.localStorage.getItem(HOST_DRAFT_STORAGE_KEY)

    if (raw !== null) {
      const parsed = JSON.parse(raw) as Partial<BringupStoredState>

      if (
        parsed.form !== undefined &&
        typeof parsed.activePage === 'string'
      ) {
        const defaultState = makeStoredState(snapshot)
        return {
          ...defaultState,
          ...parsed,
          form: sanitizeDacDraft({
            ...defaultState.form,
            ...(parsed.form as Partial<BringupDraft>),
          } as BringupDraft),
          activePage:
            parsed.activePage === 'workflow' ||
            parsed.activePage in moduleMeta
              ? (parsed.activePage as BringupPageKey)
              : 'workflow',
        }
      }
    }

    const legacyRaw = window.localStorage.getItem(LEGACY_HOST_DRAFT_STORAGE_KEY)
    if (legacyRaw !== null) {
      const legacyDraft = JSON.parse(legacyRaw) as BringupDraft
      return {
        ...makeStoredState(snapshot),
        form: sanitizeDacDraft(legacyDraft),
      }
    }
  } catch {
    return makeStoredState(snapshot)
  }

  return makeStoredState(snapshot)
}

function mergePlannedAndLiveModules(
  planned: BringupModuleMap,
  live: BringupModuleMap,
): BringupModuleMap {
  return {
    imu: {
      ...live.imu,
      expectedPresent: planned.imu.expectedPresent,
      debugEnabled: planned.imu.debugEnabled,
    },
    dac: {
      ...live.dac,
      expectedPresent: planned.dac.expectedPresent,
      debugEnabled: planned.dac.debugEnabled,
    },
    haptic: {
      ...live.haptic,
      expectedPresent: planned.haptic.expectedPresent,
      debugEnabled: planned.haptic.debugEnabled,
    },
    tof: {
      ...live.tof,
      expectedPresent: planned.tof.expectedPresent,
      debugEnabled: planned.tof.debugEnabled,
    },
    buttons: {
      ...live.buttons,
      expectedPresent: planned.buttons.expectedPresent,
      debugEnabled: planned.buttons.debugEnabled,
    },
    pd: {
      ...live.pd,
      expectedPresent: planned.pd.expectedPresent,
      debugEnabled: planned.pd.debugEnabled,
    },
    laserDriver: {
      ...live.laserDriver,
      expectedPresent: planned.laserDriver.expectedPresent,
      debugEnabled: planned.laserDriver.debugEnabled,
    },
    tec: {
      ...live.tec,
      expectedPresent: planned.tec.expectedPresent,
      debugEnabled: planned.tec.debugEnabled,
    },
  }
}

function pause(ms: number): Promise<void> {
  return new Promise((resolve) => {
    window.setTimeout(resolve, ms)
  })
}

function buildProbeQueue(modules: BringupModuleMap): ProbeRequest[] {
  const probes: ProbeRequest[] = [
    {
      id: 'status',
      cmd: 'get_status',
      note: 'Refresh the live bring-up snapshot.',
    },
  ]

  if (modules.imu.expectedPresent || modules.imu.debugEnabled) {
    probes.push({
      id: 'imu-whoami',
      cmd: 'spi_read',
      note: 'Background IMU identity probe.',
      args: {
        device: 'imu',
        reg: 0x0f,
      },
    })
  }

  if (modules.pd.expectedPresent || modules.pd.debugEnabled) {
    probes.push({
      id: 'pd-status',
      cmd: 'i2c_read',
      note: 'Background STUSB4500 status probe.',
      args: {
        address: 0x28,
        reg: 0x06,
      },
    })
  }

  if (modules.dac.expectedPresent || modules.dac.debugEnabled) {
    probes.push({
      id: 'dac-config',
      cmd: 'i2c_read',
      note: 'Background DAC CONFIG probe.',
      args: {
        address: 0x48,
        reg: 0x03,
      },
    })
  }

  if (modules.haptic.expectedPresent || modules.haptic.debugEnabled) {
    probes.push({
      id: 'haptic-mode',
      cmd: 'i2c_read',
      note: 'Background haptic MODE probe.',
      args: {
        address: 0x5a,
        reg: 0x01,
      },
    })
  }

  return probes
}

function moduleScore(status: BringupModuleStatus): ModuleScore {
  if (!status.expectedPresent) {
    return {
      progress: 0,
      tone: 'critical',
      label: status.detected ? 'Unexpected response' : 'Not installed',
    }
  }

  if (!status.detected) {
    return {
      progress: status.debugEnabled ? 42 : 28,
      tone: 'warning',
      label: 'Awaiting probe',
    }
  }

  if (!status.healthy) {
    return {
      progress: 68,
      tone: 'warning',
      label: 'Needs validation',
    }
  }

  if (!status.debugEnabled) {
    return {
      progress: 82,
      tone: 'warning',
      label: 'Live, tools off',
    }
  }

  return {
    progress: 100,
    tone: 'steady',
    label: 'Detected',
  }
}

function moduleSummary(
  status: BringupModuleStatus,
  connected: boolean,
): { headline: string; detail: string } {
  if (!status.expectedPresent) {
    return {
      headline: status.detected ? 'Unexpected response on undeclared hardware' : 'Not part of this bench build',
      detail: status.detected
        ? 'This module is not declared installed, but the firmware saw bus or register activity from it.'
        : 'Leave this module off until the hardware is physically populated.',
    }
  }

  if (!connected) {
    return {
      headline: 'Planned, not probed yet',
      detail: 'This is only a bench plan until the board is connected and you run a probe.',
    }
  }

  if (!status.detected) {
    return {
      headline: 'Expected, waiting for first probe',
      detail: 'The board says this module is planned, but there is no successful identity or register probe yet.',
    }
  }

  if (!status.healthy) {
    return {
      headline: 'Detected, still needs validation',
      detail: 'A response was seen, but the module is not yet marked healthy.',
    }
  }

  return {
    headline: 'Detected and responding',
    detail: 'The current snapshot shows a successful probe and a healthy module status.',
  }
}

function overallBringupPercent(modules: BringupModuleMap): number {
  const plannedModules = moduleKeys.filter((module) => modules[module].expectedPresent)

  if (plannedModules.length === 0) {
    return 0
  }

  const scores = plannedModules.map((module) => moduleScore(modules[module]).progress)
  return Math.round(
    scores.reduce((total, progress) => total + progress, 0) / scores.length,
  )
}

function FieldLabel({
  label,
  help,
}: {
  label: string
  help: string
}) {
  return (
    <span className="field-label">
      <span>{label}</span>
      <HelpHint text={help} />
    </span>
  )
}

export function BringupWorkbench({
  snapshot,
  transportStatus,
  onIssueCommandAwaitAck,
}: BringupWorkbenchProps) {
  const initialStoredStateRef = useRef<BringupStoredState | null>(null)
  if (initialStoredStateRef.current === null) {
    initialStoredStateRef.current = loadHostDraftState(snapshot)
  }

  const initialStoredState = initialStoredStateRef.current
  const [activePage, setActivePage] = useState<BringupPageKey>(initialStoredState.activePage)
  const [draftNote, setDraftNote] = useState('Bench plan auto-saves locally in this browser.')
  const [form, setForm] = useState<BringupFormState>(initialStoredState.form)
  const [i2cAddress, setI2cAddress] = useState(initialStoredState.i2cAddress)
  const [i2cRegister, setI2cRegister] = useState(initialStoredState.i2cRegister)
  const [i2cValue, setI2cValue] = useState(initialStoredState.i2cValue)
  const [spiDevice, setSpiDevice] = useState(initialStoredState.spiDevice)
  const [spiRegister, setSpiRegister] = useState(initialStoredState.spiRegister)
  const [spiValue, setSpiValue] = useState(initialStoredState.spiValue)
  const [operation, setOperation] = useState<BringupOperation | null>(null)
  const [pdNvmConfirmOpen, setPdNvmConfirmOpen] = useState(false)

  const connected = transportStatus === 'connected'
  const serviceModeRequested = connected && snapshot.bringup.serviceModeRequested
  const serviceModeActive = connected && snapshot.bringup.serviceModeActive
  const readsDisabled = !connected || operation !== null
  const writesDisabled = !connected || operation !== null
  const liveModules = connected ? snapshot.bringup.modules : form.modules
  const displayModules = useMemo(
    () => mergePlannedAndLiveModules(form.modules, liveModules),
    [form.modules, liveModules],
  )
  const livePdProfiles = snapshot.bringup.tuning.pdProfiles ?? makeDefaultPdProfiles()
  const overallProgress = overallBringupPercent(displayModules)
  const livePageModule =
    activePage !== 'workflow'
      ? displayModules[activePage]
      : null
  const livePageScore = livePageModule === null ? null : moduleScore(livePageModule)
  const operationBusyRef = useRef(false)
  const operationConfirmResolveRef = useRef<(() => void) | null>(null)
  const probeBusyRef = useRef(false)
  const moduleSyncBusyRef = useRef(false)
  const probeIndexRef = useRef(0)
  const latestSnapshotRef = useRef(snapshot)
  const desiredModulePlanSignature = useMemo(
    () => modulePlanSignature(form.modules),
    [form.modules],
  )
  const liveModulePlanSignature = useMemo(
    () => modulePlanSignature(snapshot.bringup.modules),
    [snapshot.bringup.modules],
  )

  useEffect(() => {
    latestSnapshotRef.current = snapshot
  }, [snapshot])

  function dismissOperation() {
    const resolve = operationConfirmResolveRef.current
    operationConfirmResolveRef.current = null
    setOperation(null)
    resolve?.()
  }

  function waitForOperationConfirm() {
    return new Promise<void>((resolve) => {
      operationConfirmResolveRef.current = resolve
    })
  }

  async function holdOperationError(label: string, detail: string) {
    setDraftNote(detail)
    setOperation({
      label,
      detail,
      percent: 100,
      tone: 'critical',
      requiresConfirm: true,
    })
    await waitForOperationConfirm()
  }

  function patchForm<K extends keyof BringupFormState>(
    key: K,
    value: BringupFormState[K],
  ) {
    setForm((current) => ({
      ...current,
      [key]: value,
    }))
  }

  function patchModule(module: ModuleKey, patch: Partial<BringupModuleStatus>) {
    setForm((current) => ({
      ...current,
      modules: {
        ...current.modules,
        [module]: {
          ...current.modules[module],
          ...patch,
        },
      },
    }))
  }

  function patchPdProfile(
    index: number,
    patch: Partial<BringupFormState['pdProfiles'][number]>,
  ) {
    setForm((current) => ({
      ...current,
      pdProfiles: current.pdProfiles.map((profile, profileIndex) =>
        profileIndex === index ? { ...profile, ...patch } : profile,
      ),
    }))
  }

  const pollLiveStatus = useCallback(
    async (logHistory = false) =>
      onIssueCommandAwaitAck(
        'get_status',
        'read',
        'Refresh the live bring-up snapshot.',
        undefined,
        {
          logHistory,
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ),
    [onIssueCommandAwaitAck],
  )

  useEffect(() => {
    persistStoredState({
      version: 8,
      activePage,
      form,
      i2cAddress,
      i2cRegister,
      i2cValue,
      spiDevice,
      spiRegister,
      spiValue,
    })
  }, [
    activePage,
    form,
    i2cAddress,
    i2cRegister,
    i2cValue,
    spiDevice,
    spiRegister,
    spiValue,
  ])

  useEffect(() => {
    if (!connected || desiredModulePlanSignature === liveModulePlanSignature) {
      return
    }

    const timerId = window.setTimeout(() => {
      if (operationBusyRef.current || probeBusyRef.current || moduleSyncBusyRef.current) {
        return
      }

      moduleSyncBusyRef.current = true
      probeBusyRef.current = true

      void (async () => {
        let syncedCount = 0

        try {
          const liveModulesSnapshot = latestSnapshotRef.current.bringup.modules

          for (const module of moduleKeys) {
            const planned = form.modules[module]
            const live = liveModulesSnapshot[module]

            if (
              planned.expectedPresent === live.expectedPresent &&
              planned.debugEnabled === live.debugEnabled
            ) {
              continue
            }

            const result = await onIssueCommandAwaitAck(
              'set_module_state',
              'write',
              `Auto-sync bring-up plan for ${moduleMeta[module].label}.`,
              {
                module: toFirmwareModuleName(module),
                expected_present: planned.expectedPresent,
                debug_enabled: planned.debugEnabled,
              },
              {
                logHistory: false,
                timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
              },
            )

            if (!result.ok) {
              setDraftNote(
                `${moduleMeta[module].label} plan did not auto-sync: ${result.note}`,
              )
              return
            }

            syncedCount += 1
          }

          if (syncedCount > 0) {
            void pollLiveStatus(false)
            setDraftNote(
              syncedCount === 1
                ? 'One module plan auto-synced to the controller.'
                : `${syncedCount} module plans auto-synced to the controller.`,
            )
          }
        } finally {
          probeBusyRef.current = false
          moduleSyncBusyRef.current = false
        }
      })()
    }, 280)

    return () => {
      window.clearTimeout(timerId)
    }
  }, [
    connected,
    desiredModulePlanSignature,
    form.modules,
    liveModulePlanSignature,
    onIssueCommandAwaitAck,
    pollLiveStatus,
  ])

  function syncFormFromSnapshot() {
    setForm(makeFormState(snapshot))
    setDraftNote('Live device bring-up values loaded into the local bench plan.')
  }

  function syncSafetyPolicyFromSnapshot() {
    const liveForm = makeFormState(snapshot)
    setForm((current) => ({
      ...current,
      safetyHorizonThresholdDeg: liveForm.safetyHorizonThresholdDeg,
      safetyHorizonHysteresisDeg: liveForm.safetyHorizonHysteresisDeg,
      safetyTofMinRangeM: liveForm.safetyTofMinRangeM,
      safetyTofMaxRangeM: liveForm.safetyTofMaxRangeM,
      safetyTofHysteresisM: liveForm.safetyTofHysteresisM,
      safetyImuStaleMs: liveForm.safetyImuStaleMs,
      safetyTofStaleMs: liveForm.safetyTofStaleMs,
      safetyRailGoodTimeoutMs: liveForm.safetyRailGoodTimeoutMs,
      safetyLambdaDriftLimitNm: liveForm.safetyLambdaDriftLimitNm,
      safetyLambdaDriftHysteresisNm: liveForm.safetyLambdaDriftHysteresisNm,
      safetyLambdaDriftHoldMs: liveForm.safetyLambdaDriftHoldMs,
      safetyLdOvertempLimitC: liveForm.safetyLdOvertempLimitC,
      safetyTecTempAdcTripV: liveForm.safetyTecTempAdcTripV,
      safetyTecTempAdcHysteresisV: liveForm.safetyTecTempAdcHysteresisV,
      safetyTecTempAdcHoldMs: liveForm.safetyTecTempAdcHoldMs,
      safetyTecMinCommandC: liveForm.safetyTecMinCommandC,
      safetyTecMaxCommandC: liveForm.safetyTecMaxCommandC,
      safetyTecReadyToleranceC: liveForm.safetyTecReadyToleranceC,
      safetyMaxLaserCurrentA: liveForm.safetyMaxLaserCurrentA,
    }))
    setDraftNote('Live runtime safety policy loaded into the local bench plan.')
  }

  function saveHostDraft() {
    persistStoredState({
      version: 8,
      activePage,
      form,
      i2cAddress,
      i2cRegister,
      i2cValue,
      spiDevice,
      spiRegister,
      spiValue,
    })
    setDraftNote(`Bench plan saved locally at ${new Date().toLocaleTimeString()}.`)
  }

  function restoreHostDraft() {
    const stored = loadHostDraftState(snapshot)
    setActivePage(stored.activePage)
    setForm(stored.form)
    setI2cAddress(stored.i2cAddress)
    setI2cRegister(stored.i2cRegister)
    setI2cValue(stored.i2cValue)
    setSpiDevice(stored.spiDevice)
    setSpiRegister(stored.spiRegister)
    setSpiValue(stored.spiValue)
    setDraftNote('Auto-saved bench plan restored into the bring-up workspace.')
  }

  async function refreshLiveStatus(logHistory = false, showBusy = false) {
    if (!showBusy) {
      return pollLiveStatus(logHistory)
    }

    const ok = await runCommandSequence(
      'Refresh live plan',
      'Live bring-up status refreshed from the controller.',
      [
        {
          detail: 'Refreshing the live bring-up profile and module snapshot...',
          cmd: 'get_status',
          risk: 'read',
          note: 'Refresh the live bring-up snapshot.',
          logHistory,
        },
      ],
      {
        refreshAfter: false,
      },
    )

    return {
      ok,
      note: ok
        ? 'Live bring-up status refreshed from the controller.'
        : 'Live bring-up refresh did not complete.',
    }
  }

  async function pollPdStatus(logHistory = true) {
    return onIssueCommandAwaitAck(
      'refresh_pd_status',
      'read',
      'Force the controller to refresh the live STUSB4500 contract snapshot now.',
      undefined,
      {
        logHistory,
        timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
      },
    )
  }

  async function refreshPdStatus(logHistory = true, showBusy = false) {
    if (!showBusy) {
      return pollPdStatus(logHistory)
    }

    const ok = await runCommandSequence(
      'Refresh PD status',
      'USB-PD contract and runtime PDO readback refreshed from the controller.',
      [
        {
          detail: 'Refreshing live STUSB4500 contract and PDO readback...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Force the controller to refresh the live STUSB4500 contract snapshot now.',
          logHistory,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )

    return {
      ok,
      note: ok
        ? 'USB-PD contract and runtime PDO readback refreshed from the controller.'
        : 'USB-PD refresh did not complete.',
    }
  }

  async function ensureServiceMode(label: string): Promise<boolean> {
    if (!connected) {
      setDraftNote('Connect the board before starting a write session.')
      return false
    }

    if (serviceModeActive) {
      return true
    }

    setOperation({
      label,
      detail: serviceModeRequested
        ? 'Write session requested. Refreshing live status...'
        : 'Requesting service mode for controller writes...',
      percent: 22,
      tone: 'warning',
    })

    if (!serviceModeRequested) {
      const enterResult = await onIssueCommandAwaitAck(
        'enter_service_mode',
        'service',
        'Request bring-up service mode with outputs held safe.',
        undefined,
        {
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      )

      if (!enterResult.ok) {
        await holdOperationError(label, enterResult.note)
        return false
      }
    }

    await pause(120)
    const waitStartedAt = window.performance.now()

    for (;;) {
      const refreshResult = await pollLiveStatus(false)
      if (!refreshResult.ok) {
        await holdOperationError(label, refreshResult.note)
        return false
      }

      if (latestSnapshotRef.current.bringup.serviceModeActive) {
        return true
      }

      if (window.performance.now() - waitStartedAt >= BRINGUP_SERVICE_MODE_WAIT_MS) {
        await holdOperationError(
          label,
          'Service mode did not become active before the write timeout expired.',
        )
        return false
      }

      setOperation({
        label,
        detail: 'Waiting for the controller to enter write-safe service mode...',
        percent: 46,
        tone: 'warning',
      })
      await pause(140)
    }
  }

  async function runCommandSequence(
    label: string,
    successNote: string,
    steps: Array<{
      detail: string
      cmd: string
      risk: 'read' | 'write' | 'service' | 'firmware'
      note: string
      args?: Record<string, number | string | boolean>
      requireService?: boolean
      logHistory?: boolean
      timeoutMs?: number
    }>,
    options?: {
      refreshAfter?: boolean
    },
  ): Promise<boolean> {
    if (!connected) {
      setDraftNote('Board is offline. The local bench plan is still saved in this browser.')
      return false
    }

    if (operationBusyRef.current) {
      setDraftNote('A bring-up action is already running. Wait for it to finish.')
      return false
    }

    operationBusyRef.current = true
    const refreshAfter = options?.refreshAfter ?? true
    const needsService = steps.some((step) => step.requireService)
    const totalSteps = steps.length + (needsService ? 1 : 0) + (refreshAfter ? 1 : 0)
    let completedSteps = 0
    let clearOnExit = true

    try {
      for (let attempts = 0; probeBusyRef.current && attempts < 12; attempts += 1) {
        await pause(60)
      }

      if (needsService) {
        const ready = await ensureServiceMode(label)
        if (!ready) {
          clearOnExit = false
          return false
        }
        completedSteps += 1
      }

      for (const step of steps) {
        setOperation({
          label,
          detail: step.detail,
          percent: Math.max(
            18,
            Math.round((completedSteps / totalSteps) * 100),
          ),
          tone: 'warning',
        })

        let result = await onIssueCommandAwaitAck(
          step.cmd,
          step.risk,
          step.note,
          step.args,
          {
            logHistory: step.logHistory ?? true,
            timeoutMs: step.timeoutMs ?? BRINGUP_ACK_TIMEOUT_MS,
          },
        )

        if (
          !result.ok &&
          step.requireService &&
          result.note.toLowerCase().includes('service mode')
        ) {
          const recovered = await ensureServiceMode(label)
          if (recovered) {
            result = await onIssueCommandAwaitAck(
              step.cmd,
              step.risk,
              step.note,
              step.args,
              {
                logHistory: step.logHistory ?? true,
                timeoutMs: step.timeoutMs ?? BRINGUP_ACK_TIMEOUT_MS,
              },
            )
          }
        }

        if (!result.ok) {
          await holdOperationError(label, result.note)
          clearOnExit = false
          return false
        }

        completedSteps += 1
      }

      if (refreshAfter) {
        setOperation({
          label,
          detail: 'Refreshing live status and readback...',
          percent: Math.max(
            72,
            Math.round((completedSteps / totalSteps) * 100),
          ),
          tone: 'warning',
        })
        const refreshResult = await pollLiveStatus(false)
        if (!refreshResult.ok) {
          await holdOperationError(label, refreshResult.note)
          clearOnExit = false
          return false
        }
        completedSteps += 1
      }

      setDraftNote(successNote)
      setOperation({
        label,
        detail: 'Complete.',
        percent: 100,
        tone: 'steady',
      })
      await pause(BRINGUP_SUCCESS_DISMISS_MS)
      return true
    } finally {
      operationBusyRef.current = false
      if (clearOnExit) {
        setOperation(null)
      }
    }
  }

  async function saveModulePlan(module: ModuleKey) {
    const moduleStatus = form.modules[module]

    saveHostDraft()

    if (!connected) {
      setDraftNote(
        `${moduleMeta[module].label} plan saved locally. Connect the board when you want to mirror it.`,
      )
      return
    }

    await runCommandSequence(
      `Save ${moduleMeta[module].label} plan`,
      `${moduleMeta[module].label} plan saved to the controller profile.`,
      [
        {
          detail: `Saving ${moduleMeta[module].label} plan...`,
          cmd: 'set_module_state',
          risk: 'write',
          note: `Save bring-up plan for ${moduleMeta[module].label}.`,
          args: {
            module: toFirmwareModuleName(module),
            expected_present: moduleStatus.expectedPresent,
            debug_enabled: moduleStatus.debugEnabled,
          },
        },
      ],
    )
  }

  async function saveProfileName() {
    saveHostDraft()

    if (!connected) {
      setDraftNote('Profile name saved locally. Connect the board when you want to mirror it.')
      return
    }

    await runCommandSequence(
      'Save profile name',
      'Bring-up profile name saved to the controller.',
      [
        {
          detail: 'Saving profile name...',
          cmd: 'set_profile_name',
          risk: 'write',
          note: 'Rename the active bring-up profile.',
          args: {
            name: form.profileName,
          },
        },
      ],
    )
  }

  async function requestServiceMode() {
    if (!connected) {
      setDraftNote('Connect the board before starting a write session.')
      return
    }

    if (operationBusyRef.current) {
      setDraftNote('A bring-up action is already running. Wait for it to finish.')
      return
    }

    operationBusyRef.current = true
    let clearOnExit = true

    try {
      const ready = await ensureServiceMode('Start write session')
      if (!ready) {
        clearOnExit = false
        return
      }

      setDraftNote('Service mode requested. Hardware-write tools are now armed.')
      setOperation({
        label: 'Start write session',
        detail: 'Write session ready.',
        percent: 100,
        tone: 'steady',
      })
      await pause(BRINGUP_SUCCESS_DISMISS_MS)
    } finally {
      operationBusyRef.current = false
      if (clearOnExit) {
        setOperation(null)
      }
    }
  }

  async function applySafetyPolicy() {
    await runCommandSequence(
      'Apply runtime safety policy',
      'Runtime safety thresholds applied to the controller.',
      [
        {
          detail: 'Applying runtime safety thresholds and hold windows...',
          cmd: 'set_runtime_safety',
          risk: 'service',
          note: 'Update runtime safety thresholds, hysteresis, and timeout policy from the bring-up service page.',
          requireService: true,
          args: {
            horizon_threshold_deg: parseNumber(
              form.safetyHorizonThresholdDeg,
              snapshot.safety.horizonThresholdDeg,
            ),
            horizon_hysteresis_deg: parseNumber(
              form.safetyHorizonHysteresisDeg,
              snapshot.safety.horizonHysteresisDeg,
            ),
            tof_min_range_m: parseNumber(
              form.safetyTofMinRangeM,
              snapshot.safety.tofMinRangeM,
            ),
            tof_max_range_m: parseNumber(
              form.safetyTofMaxRangeM,
              snapshot.safety.tofMaxRangeM,
            ),
            tof_hysteresis_m: parseNumber(
              form.safetyTofHysteresisM,
              snapshot.safety.tofHysteresisM,
            ),
            imu_stale_ms: Math.round(
              parseNumber(form.safetyImuStaleMs, snapshot.safety.imuStaleMs),
            ),
            tof_stale_ms: Math.round(
              parseNumber(form.safetyTofStaleMs, snapshot.safety.tofStaleMs),
            ),
            rail_good_timeout_ms: Math.round(
              parseNumber(
                form.safetyRailGoodTimeoutMs,
                snapshot.safety.railGoodTimeoutMs,
              ),
            ),
            lambda_drift_limit_nm: parseNumber(
              form.safetyLambdaDriftLimitNm,
              snapshot.safety.lambdaDriftLimitNm,
            ),
            lambda_drift_hysteresis_nm: parseNumber(
              form.safetyLambdaDriftHysteresisNm,
              snapshot.safety.lambdaDriftHysteresisNm,
            ),
            lambda_drift_hold_ms: Math.round(
              parseNumber(
                form.safetyLambdaDriftHoldMs,
                snapshot.safety.lambdaDriftHoldMs,
              ),
            ),
            ld_overtemp_limit_c: parseNumber(
              form.safetyLdOvertempLimitC,
              snapshot.safety.ldOvertempLimitC,
            ),
            tec_temp_adc_trip_v: parseNumber(
              form.safetyTecTempAdcTripV,
              snapshot.safety.tecTempAdcTripV,
            ),
            tec_temp_adc_hysteresis_v: parseNumber(
              form.safetyTecTempAdcHysteresisV,
              snapshot.safety.tecTempAdcHysteresisV,
            ),
            tec_temp_adc_hold_ms: Math.round(
              parseNumber(
                form.safetyTecTempAdcHoldMs,
                snapshot.safety.tecTempAdcHoldMs,
              ),
            ),
            tec_min_command_c: parseNumber(
              form.safetyTecMinCommandC,
              snapshot.safety.tecMinCommandC,
            ),
            tec_max_command_c: parseNumber(
              form.safetyTecMaxCommandC,
              snapshot.safety.tecMaxCommandC,
            ),
            tec_ready_tolerance_c: parseNumber(
              form.safetyTecReadyToleranceC,
              snapshot.safety.tecReadyToleranceC,
            ),
            max_laser_current_a: parseNumber(
              form.safetyMaxLaserCurrentA,
              snapshot.safety.maxLaserCurrentA,
            ),
          },
        },
      ],
    )
  }

  function moduleStateStep(module: ModuleKey, detail: string) {
    return {
      detail,
      cmd: 'set_module_state',
      risk: 'write' as const,
      note: `Sync the ${moduleMeta[module].label} module plan before applying module-specific settings.`,
      args: {
        module: toFirmwareModuleName(module),
        expected_present: form.modules[module].expectedPresent,
        debug_enabled: form.modules[module].debugEnabled,
      },
    }
  }

  async function applyDacProfile() {
    const preparedForm = ensureModuleDebugWrite(sanitizeDacDraft(form), 'dac')

    if (preparedForm !== form) {
      setForm(preparedForm)
      if (!form.modules.dac.expectedPresent && !form.modules.dac.debugEnabled) {
        setDraftNote('DAC debug tooling was auto-enabled for this write so the controller can accept DAC commands.')
      }
    }

    await runCommandSequence(
      'Apply DAC profile',
      'DAC profile applied to the controller bring-up state.',
      [
        {
          detail: 'Syncing DAC module plan...',
          cmd: 'set_module_state',
          risk: 'write',
          note: 'Sync the DAC module plan before applying DAC settings.',
          args: {
            module: toFirmwareModuleName('dac'),
            expected_present: preparedForm.modules.dac.expectedPresent,
            debug_enabled: preparedForm.modules.dac.debugEnabled,
          },
        },
        {
          detail: 'Applying DAC reference and update policy...',
          cmd: 'dac_debug_config',
          risk: 'service',
          note: 'Stage DAC reference, gain, divider, and update behavior for bring-up.',
          requireService: true,
          args: {
            reference_mode: preparedForm.dacReferenceMode,
            gain_2x: preparedForm.dacGain2x,
            ref_div: preparedForm.dacRefDiv,
            sync_mode: preparedForm.dacSyncMode,
          },
        },
        {
          detail: 'Staging DAC LD channel shadow voltage...',
          cmd: 'dac_debug_set',
          risk: 'service',
          note: 'Stage the DAC LD channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'ld',
            voltage_v: parseNumber(
              preparedForm.dacLdVoltage,
              snapshot.bringup.tuning.dacLdChannelV,
            ),
          },
        },
        {
          detail: 'Staging DAC TEC channel shadow voltage...',
          cmd: 'dac_debug_set',
          risk: 'service',
          note: 'Stage the DAC TEC channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'tec',
            voltage_v: parseNumber(
              preparedForm.dacTecVoltage,
              snapshot.bringup.tuning.dacTecChannelV,
            ),
          },
        },
        {
          detail: 'Checking DAC status register for reference alarm...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 STATUS to confirm REF-ALARM is clear and the analog outputs are allowed to drive.',
          args: {
            address: 0x48,
            reg: 0x07,
          },
        },
        {
          detail: 'Reading back DAC LD output register...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 channel A data register to verify the staged LD output code.',
          args: {
            address: 0x48,
            reg: 0x08,
          },
        },
        {
          detail: 'Reading back DAC TEC output register...',
          cmd: 'i2c_read',
          risk: 'read',
          note: 'Read back DAC80502 channel B data register to verify the staged TEC output code.',
          args: {
            address: 0x48,
            reg: 0x09,
          },
        },
      ],
    )
  }

  async function applyImuProfile() {
    await runCommandSequence(
      'Apply IMU tuning',
      'IMU tuning applied to the controller bring-up state.',
      [
        moduleStateStep('imu', 'Syncing IMU module plan...'),
        {
          detail: 'Applying IMU runtime settings...',
          cmd: 'imu_debug_config',
          risk: 'service',
          note: 'Stage the IMU bring-up configuration.',
          requireService: true,
          args: {
            odr_hz: parseNumber(form.imuOdrHz, snapshot.bringup.tuning.imuOdrHz),
            accel_range_g: parseNumber(
              form.imuAccelRangeG,
              snapshot.bringup.tuning.imuAccelRangeG,
            ),
            gyro_range_dps: parseNumber(
              form.imuGyroRangeDps,
              snapshot.bringup.tuning.imuGyroRangeDps,
            ),
            gyro_enabled: form.imuGyroEnabled,
            lpf2_enabled: form.imuLpf2Enabled,
            timestamp_enabled: form.imuTimestampEnabled,
            bdu_enabled: form.imuBduEnabled,
            if_inc_enabled: form.imuIfIncEnabled,
            i2c_disabled: form.imuI2cDisabled,
          },
        },
      ],
    )
  }

  async function applyTofProfile() {
    await runCommandSequence(
      'Apply ToF limits',
      'ToF limits staged in the controller bring-up state.',
      [
        moduleStateStep('tof', 'Syncing ToF module plan...'),
        {
          detail: 'Applying ToF safety window...',
          cmd: 'tof_debug_config',
          risk: 'service',
          note: 'Stage ToF debug thresholds and freshness timeout.',
          requireService: true,
          args: {
            min_range_m: parseNumber(
              form.tofMinRangeM,
              snapshot.bringup.tuning.tofMinRangeM,
            ),
            max_range_m: parseNumber(
              form.tofMaxRangeM,
              snapshot.bringup.tuning.tofMaxRangeM,
            ),
            stale_timeout_ms: parseNumber(
              form.tofStaleTimeoutMs,
              snapshot.bringup.tuning.tofStaleTimeoutMs,
            ),
          },
        },
      ],
    )
  }

  async function applyPdProfile() {
    await runCommandSequence(
      'Apply PD runtime PDOs',
      'USB-PD runtime PDOs applied, renegotiation requested, and live readback refreshed.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: 'Uploading STUSB4500 runtime PDO plan...',
          cmd: 'pd_debug_config',
          risk: 'service',
          note: 'Apply STUSB4500 runtime PDO settings and update firmware power thresholds.',
          requireService: true,
          logHistory: true,
          timeoutMs: BRINGUP_PD_APPLY_TIMEOUT_MS,
          args: {
            programming_only_max_w: parseNumber(
              form.pdProgrammingOnlyMaxW,
              snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
            ),
            reduced_mode_min_w: parseNumber(
              form.pdReducedModeMinW,
              snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
            ),
            reduced_mode_max_w: parseNumber(
              form.pdReducedModeMaxW,
              snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
            ),
            full_mode_min_w: parseNumber(
              form.pdFullModeMinW,
              snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
            ),
            pdo1_enabled: form.pdProfiles[0]?.enabled ?? false,
            pdo1_voltage_v: parseNumber(
              form.pdProfiles[0]?.voltageV ?? '0',
              livePdProfiles[0]?.voltageV ?? 0,
            ),
            pdo1_current_a: parseNumber(
              form.pdProfiles[0]?.currentA ?? '0',
              livePdProfiles[0]?.currentA ?? 0,
            ),
            pdo2_enabled: form.pdProfiles[1]?.enabled ?? false,
            pdo2_voltage_v: parseNumber(
              form.pdProfiles[1]?.voltageV ?? '0',
              livePdProfiles[1]?.voltageV ?? 0,
            ),
            pdo2_current_a: parseNumber(
              form.pdProfiles[1]?.currentA ?? '0',
              livePdProfiles[1]?.currentA ?? 0,
            ),
            pdo3_enabled: form.pdProfiles[2]?.enabled ?? false,
            pdo3_voltage_v: parseNumber(
              form.pdProfiles[2]?.voltageV ?? '0',
              livePdProfiles[2]?.voltageV ?? 0,
            ),
            pdo3_current_a: parseNumber(
              form.pdProfiles[2]?.currentA ?? '0',
              livePdProfiles[2]?.currentA ?? 0,
            ),
          },
        },
        {
          detail: 'Refreshing live STUSB contract and PDO readback...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Force the controller to refresh STUSB4500 live status and PDO readback.',
          logHistory: false,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function burnPdProfileToNvm() {
    setPdNvmConfirmOpen(false)
    await runCommandSequence(
      'Burn PD PDOs to NVM',
      'STUSB4500 NVM burn command issued.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: 'Writing the current PDO plan into STUSB4500 nonvolatile memory...',
          cmd: 'pd_burn_nvm',
          risk: 'service',
          note: 'Burn the current STUSB4500 PDO configuration into NVM so it becomes the startup default after reset.',
          requireService: true,
          timeoutMs: BRINGUP_PD_NVM_TIMEOUT_MS,
        },
        {
          detail: 'Refreshing live STUSB contract and PDO readback after NVM action...',
          cmd: 'refresh_pd_status',
          risk: 'read',
          note: 'Refresh STUSB4500 contract and PDO readback after the NVM action.',
          logHistory: false,
          timeoutMs: BRINGUP_PD_REFRESH_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function savePdProfileToFirmware() {
    await runCommandSequence(
      form.pdFirmwarePlanEnabled
        ? 'Save firmware PDO plan'
        : 'Save firmware PDO plan disabled',
      form.pdFirmwarePlanEnabled
        ? 'Firmware-owned PDO plan saved and will be reconciled into STUSB runtime state whenever the controller sees a mismatch online.'
        : 'Firmware-owned PDO plan saved with auto-reconcile disabled.',
      [
        moduleStateStep('pd', 'Syncing USB-PD module plan...'),
        {
          detail: form.pdFirmwarePlanEnabled
            ? 'Validating runtime PDO plan before saving it to controller firmware...'
            : 'Saving PDO thresholds and disabling firmware auto-reconcile...',
          cmd: 'pd_save_firmware_plan',
          risk: 'service',
          note: 'Save the current PDO plan into controller firmware so the ESP32 can compare it against STUSB runtime PDOs and re-apply it only when needed.',
          requireService: true,
          logHistory: true,
          timeoutMs: BRINGUP_PD_APPLY_TIMEOUT_MS,
          args: {
            firmware_plan_enabled: form.pdFirmwarePlanEnabled,
            programming_only_max_w: parseNumber(
              form.pdProgrammingOnlyMaxW,
              snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
            ),
            reduced_mode_min_w: parseNumber(
              form.pdReducedModeMinW,
              snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
            ),
            reduced_mode_max_w: parseNumber(
              form.pdReducedModeMaxW,
              snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
            ),
            full_mode_min_w: parseNumber(
              form.pdFullModeMinW,
              snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
            ),
            pdo1_enabled: form.pdProfiles[0]?.enabled ?? false,
            pdo1_voltage_v: parseNumber(
              form.pdProfiles[0]?.voltageV ?? '0',
              livePdProfiles[0]?.voltageV ?? 0,
            ),
            pdo1_current_a: parseNumber(
              form.pdProfiles[0]?.currentA ?? '0',
              livePdProfiles[0]?.currentA ?? 0,
            ),
            pdo2_enabled: form.pdProfiles[1]?.enabled ?? false,
            pdo2_voltage_v: parseNumber(
              form.pdProfiles[1]?.voltageV ?? '0',
              livePdProfiles[1]?.voltageV ?? 0,
            ),
            pdo2_current_a: parseNumber(
              form.pdProfiles[1]?.currentA ?? '0',
              livePdProfiles[1]?.currentA ?? 0,
            ),
            pdo3_enabled: form.pdProfiles[2]?.enabled ?? false,
            pdo3_voltage_v: parseNumber(
              form.pdProfiles[2]?.voltageV ?? '0',
              livePdProfiles[2]?.voltageV ?? 0,
            ),
            pdo3_current_a: parseNumber(
              form.pdProfiles[2]?.currentA ?? '0',
              livePdProfiles[2]?.currentA ?? 0,
            ),
          },
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  async function applyHapticProfile() {
    await runCommandSequence(
      'Apply haptic profile',
      'Haptic profile applied to the controller bring-up state.',
      [
        moduleStateStep('haptic', 'Syncing haptic module plan...'),
        {
          detail: 'Applying DRV2605 profile...',
          cmd: 'haptic_debug_config',
          risk: 'service',
          note: 'Stage DRV2605 mode, library, actuator, RTP, and effect settings.',
          requireService: true,
          args: {
            effect_id: parseNumber(
              form.hapticEffectId,
              snapshot.bringup.tuning.hapticEffectId,
            ),
            mode: form.hapticMode,
            library: parseNumber(
              form.hapticLibrary,
              snapshot.bringup.tuning.hapticLibrary,
            ),
            actuator: form.hapticActuator,
            rtp_level: parseNumber(
              form.hapticRtpLevel,
              snapshot.bringup.tuning.hapticRtpLevel,
            ),
          },
        },
      ],
    )
  }

  async function pushDraftToDevice() {
    await runCommandSequence(
      'Push full bring-up plan',
      'Full bring-up plan pushed to the controller profile.',
      [
        {
          detail: 'Saving bring-up profile name...',
          cmd: 'set_profile_name',
          risk: 'write',
          note: 'Update the active bring-up profile name.',
          args: {
            name: form.profileName,
          },
        },
        ...moduleKeys.map((module) => ({
          detail: `Saving ${moduleMeta[module].label} plan...`,
          cmd: 'set_module_state',
          risk: 'write' as const,
          note: `Update bring-up plan for ${moduleMeta[module].label}.`,
          args: {
            module: toFirmwareModuleName(module),
            expected_present: form.modules[module].expectedPresent,
            debug_enabled: form.modules[module].debugEnabled,
          },
        })),
        {
          detail: 'Applying DAC profile...',
          cmd: 'dac_debug_config',
          risk: 'service' as const,
          note: 'Stage DAC reference, gain, divider, and update behavior for bring-up.',
          requireService: true,
          args: {
            reference_mode: form.dacReferenceMode,
            gain_2x: form.dacGain2x,
            ref_div: form.dacRefDiv,
            sync_mode: form.dacSyncMode,
          },
        },
        {
          detail: 'Applying DAC LD shadow...',
          cmd: 'dac_debug_set',
          risk: 'service' as const,
          note: 'Stage the DAC LD channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'ld',
            voltage_v: parseNumber(
              form.dacLdVoltage,
              snapshot.bringup.tuning.dacLdChannelV,
            ),
          },
        },
        {
          detail: 'Applying DAC TEC shadow...',
          cmd: 'dac_debug_set',
          risk: 'service' as const,
          note: 'Stage the DAC TEC channel shadow voltage.',
          requireService: true,
          args: {
            channel: 'tec',
            voltage_v: parseNumber(
              form.dacTecVoltage,
              snapshot.bringup.tuning.dacTecChannelV,
            ),
          },
        },
        {
          detail: 'Applying IMU tuning...',
          cmd: 'imu_debug_config',
          risk: 'service' as const,
          note: 'Stage the IMU bring-up configuration.',
          requireService: true,
          args: {
            odr_hz: parseNumber(form.imuOdrHz, snapshot.bringup.tuning.imuOdrHz),
            accel_range_g: parseNumber(
              form.imuAccelRangeG,
              snapshot.bringup.tuning.imuAccelRangeG,
            ),
            gyro_range_dps: parseNumber(
              form.imuGyroRangeDps,
              snapshot.bringup.tuning.imuGyroRangeDps,
            ),
            gyro_enabled: form.imuGyroEnabled,
            lpf2_enabled: form.imuLpf2Enabled,
            timestamp_enabled: form.imuTimestampEnabled,
            bdu_enabled: form.imuBduEnabled,
            if_inc_enabled: form.imuIfIncEnabled,
            i2c_disabled: form.imuI2cDisabled,
          },
        },
        {
          detail: 'Applying ToF limits...',
          cmd: 'tof_debug_config',
          risk: 'service' as const,
          note: 'Stage ToF debug thresholds and freshness timeout.',
          requireService: true,
          args: {
            min_range_m: parseNumber(
              form.tofMinRangeM,
              snapshot.bringup.tuning.tofMinRangeM,
            ),
            max_range_m: parseNumber(
              form.tofMaxRangeM,
              snapshot.bringup.tuning.tofMaxRangeM,
            ),
            stale_timeout_ms: parseNumber(
              form.tofStaleTimeoutMs,
              snapshot.bringup.tuning.tofStaleTimeoutMs,
            ),
          },
        },
        {
          detail: 'Applying PD runtime PDOs...',
          cmd: 'pd_debug_config',
          risk: 'service' as const,
          note: 'Apply STUSB4500 runtime PDO settings and update firmware power thresholds.',
          requireService: true,
          args: {
            programming_only_max_w: parseNumber(
              form.pdProgrammingOnlyMaxW,
              snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
            ),
            reduced_mode_min_w: parseNumber(
              form.pdReducedModeMinW,
              snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
            ),
            reduced_mode_max_w: parseNumber(
              form.pdReducedModeMaxW,
              snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
            ),
            full_mode_min_w: parseNumber(
              form.pdFullModeMinW,
              snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
            ),
            pdo1_enabled: form.pdProfiles[0]?.enabled ?? false,
            pdo1_voltage_v: parseNumber(
              form.pdProfiles[0]?.voltageV ?? '0',
              livePdProfiles[0]?.voltageV ?? 0,
            ),
            pdo1_current_a: parseNumber(
              form.pdProfiles[0]?.currentA ?? '0',
              livePdProfiles[0]?.currentA ?? 0,
            ),
            pdo2_enabled: form.pdProfiles[1]?.enabled ?? false,
            pdo2_voltage_v: parseNumber(
              form.pdProfiles[1]?.voltageV ?? '0',
              livePdProfiles[1]?.voltageV ?? 0,
            ),
            pdo2_current_a: parseNumber(
              form.pdProfiles[1]?.currentA ?? '0',
              livePdProfiles[1]?.currentA ?? 0,
            ),
            pdo3_enabled: form.pdProfiles[2]?.enabled ?? false,
            pdo3_voltage_v: parseNumber(
              form.pdProfiles[2]?.voltageV ?? '0',
              livePdProfiles[2]?.voltageV ?? 0,
            ),
            pdo3_current_a: parseNumber(
              form.pdProfiles[2]?.currentA ?? '0',
              livePdProfiles[2]?.currentA ?? 0,
            ),
          },
        },
        {
          detail: 'Applying haptic profile...',
          cmd: 'haptic_debug_config',
          risk: 'service' as const,
          note: 'Stage DRV2605 mode, library, actuator, RTP, and effect settings.',
          requireService: true,
          args: {
            effect_id: parseNumber(
              form.hapticEffectId,
              snapshot.bringup.tuning.hapticEffectId,
            ),
            mode: form.hapticMode,
            library: parseNumber(
              form.hapticLibrary,
              snapshot.bringup.tuning.hapticLibrary,
            ),
            actuator: form.hapticActuator,
            rtp_level: parseNumber(
              form.hapticRtpLevel,
              snapshot.bringup.tuning.hapticRtpLevel,
            ),
          },
        },
      ],
    )
  }

  function moduleQuickReadI2c(
    label: string,
    address: number,
    reg: number,
    note: string,
  ) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'i2c_read',
          risk: 'read',
          note,
          args: { address, reg },
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function moduleQuickReadSpi(
    label: string,
    device: string,
    reg: number,
    note: string,
  ) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'spi_read',
          risk: 'read',
          note,
          args: { device, reg },
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  function moduleQuickScanI2c(label: string, note: string) {
    return runCommandSequence(
      label,
      note,
      [
        {
          detail: note,
          cmd: 'i2c_scan',
          risk: 'read',
          note,
          timeoutMs: BRINGUP_ACK_TIMEOUT_MS,
        },
      ],
      {
        refreshAfter: false,
      },
    )
  }

  useEffect(() => {
    if (!connected) {
      return
    }

    const probeQueue = buildProbeQueue(form.modules)
    if (probeQueue.length === 0) {
      return
    }

    const timerId = window.setInterval(() => {
      if (operationBusyRef.current || probeBusyRef.current) {
        return
      }

      const probe = probeQueue[probeIndexRef.current % probeQueue.length]
      probeIndexRef.current += 1
      probeBusyRef.current = true

      void onIssueCommandAwaitAck(
        probe.cmd,
        'read',
        probe.note,
        probe.args,
        {
          logHistory: false,
          timeoutMs: 1400,
        },
      ).finally(() => {
        probeBusyRef.current = false
      })
    }, BRINGUP_PROBE_INTERVAL_MS)

    return () => {
      window.clearInterval(timerId)
    }
  }, [connected, form.modules, onIssueCommandAwaitAck])

  function renderModuleSettings(module: ModuleKey) {
    const liveStatus = displayModules[module]
    const plannedStatus = form.modules[module]
    const score = moduleScore(liveStatus)
    const summary = moduleSummary(liveStatus, connected)
    const meta = moduleMeta[module]

    return (
      <article className="bringup-module-frame">
        <div className="bringup-module-frame__head">
          <div>
            <p className="eyebrow">{meta.transport}</p>
            <h2>{meta.label}</h2>
          </div>
          <div className="bringup-module-frame__status">
            {connected ? (
              <>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  planned {plannedStatus.expectedPresent ? 'yes' : 'no'}
                </span>
                {plannedStatus.expectedPresent ? (
                  <span className={liveStatus.detected ? 'status-badge is-on' : 'status-badge is-warn'}>
                    probe {liveStatus.detected ? 'seen' : 'not yet'}
                  </span>
                ) : null}
                {liveStatus.detected ? (
                  <span className={liveStatus.healthy ? 'status-badge is-on' : 'status-badge is-warn'}>
                    health {liveStatus.healthy ? 'ok' : 'check'}
                  </span>
                ) : null}
              </>
            ) : (
              <>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  offline plan
                </span>
                <span className={plannedStatus.expectedPresent ? 'status-badge is-on' : 'status-badge'}>
                  installed {plannedStatus.expectedPresent ? 'planned' : 'no'}
                </span>
              </>
            )}
            <span
              className={
                plannedStatus.expectedPresent && plannedStatus.debugEnabled
                  ? 'status-badge is-on'
                  : 'status-badge'
              }
            >
              tools {plannedStatus.expectedPresent && plannedStatus.debugEnabled ? 'enabled' : 'off'}
            </span>
          </div>
        </div>

        <p className="panel-note">{meta.detail}</p>

        <div className="bringup-module-frame__summary">
          <div>
            <strong>{summary.headline}</strong>
            <p className="inline-help">{summary.detail}</p>
          </div>
          <div className="bringup-module-frame__meter">
            <span>{score.progress}%</span>
            <ProgressMeter value={score.progress} tone={score.tone} compact />
          </div>
        </div>

        <div className="bringup-toggle-row">
          <label className="arming-toggle is-compact">
            <input
              type="checkbox"
              checked={plannedStatus.expectedPresent}
              title="Declare whether this module is physically populated on the current bench build."
              onChange={(event) =>
                patchModule(module, {
                  expectedPresent: event.target.checked,
                })
              }
            />
            <span>Expected populated on this bench build.</span>
          </label>

          <label className="arming-toggle is-compact">
            <input
              type="checkbox"
              checked={plannedStatus.debugEnabled}
              title="Allow service-only tools and staged config for this module."
              onChange={(event) =>
                patchModule(module, {
                  debugEnabled: event.target.checked,
                })
              }
            />
            <span>Enable service-mode debug tooling for this module.</span>
          </label>

          <button
            type="button"
            className="action-button is-inline"
            disabled={operation !== null}
            title={`Save the planned install state and debug-tool flag for ${meta.label}. This does not require service mode.`}
            onClick={() => {
              void saveModulePlan(module)
            }}
          >
            Save module plan
          </button>
        </div>
      </article>
    )
  }

  function renderWorkflowPage() {
    return (
      <div className="bringup-page-grid">
        <article className="panel-cutout bringup-hero">
          <div className="cutout-head">
            <Wrench size={16} />
            <strong>Bring-up start guide</strong>
          </div>
          <p className="panel-note">
            Keep this simple: pick only the hardware that is physically installed,
            read identity registers first, and only use service mode when you want
            to write settings back to the controller. Read-only probes do not need
            service mode.
          </p>

          <div className="bringup-hero__meter">
            <div>
              <span className="eyebrow">Selected modules</span>
              <strong>
                {overallProgress}% readiness for the modules you have marked as installed
              </strong>
            </div>
            <ProgressMeter
              value={overallProgress}
              tone={overallProgress >= 80 ? 'steady' : overallProgress >= 50 ? 'warning' : 'critical'}
            />
          </div>

          <div className="bringup-step-list">
            <div className="bringup-step-list__item">
              <strong>1. Connect the board</strong>
              <p>Use Web Serial for the real unit or Mock rig if you just want to learn the flow.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>2. Choose one module page</strong>
              <p>IMU, DAC, PD, haptic, ToF, trigger, TEC, and laser driver can all be tackled independently.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>3. Mark only installed hardware</strong>
              <p>If a board or IC is not populated yet, leave it unplanned so the UI does not pretend it should respond.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>4. Probe first, tune second</strong>
              <p>Run WHO_AM_I or simple register reads first. Only after a successful probe should you apply tuning.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>5. Use service mode only for writes</strong>
              <p>Read-only probes and bench planning work without service mode. Use a write session only for hardware writes and staged tuning.</p>
            </div>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Radio size={16} />
            <strong>Connection and service mode</strong>
          </div>

          <div className="status-badges">
            <span className={connected ? 'status-badge is-on' : 'status-badge'}>
              Link {connected ? 'live' : 'offline'}
            </span>
            <span
              className={
                serviceModeActive
                  ? 'status-badge is-on'
                  : serviceModeRequested
                    ? 'status-badge is-warn'
                    : 'status-badge'
              }
            >
              Service {serviceModeActive ? 'active' : serviceModeRequested ? 'requesting' : 'off'}
            </span>
            <span className={serviceModeActive ? 'status-badge is-on' : 'status-badge is-warn'}>
              Writes {serviceModeActive ? 'armed' : 'read only'}
            </span>
            <span className="status-badge is-on">Local plan auto-saved</span>
          </div>

          <p className="panel-note">
            If you only want live status or register reads, stay in read-only mode. Service mode is just the guarded write session for staged hardware config and register writes.
          </p>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={!connected || serviceModeActive || operation !== null}
              title="Request service mode with beam permission held off."
              onClick={() => {
                void requestServiceMode()
              }}
            >
              Enter service mode
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={!connected || !serviceModeActive || operation !== null}
              title="Return the controller to normal safe supervision."
              onClick={() =>
                void runCommandSequence(
                  'Exit write session',
                  'Service mode exited.',
                  [
                    {
                      detail: 'Leaving service mode...',
                      cmd: 'exit_service_mode',
                      risk: 'service',
                      note: 'Return the controller to normal safe supervision.',
                    },
                  ],
                )
              }
            >
              Exit service mode
            </button>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <ShieldAlert size={16} />
            <strong>Runtime safety policy</strong>
          </div>

          <p className="panel-note">
            Keep operational control in the Control page. Use this service page for threshold, hysteresis, and timeout policy that changes how the firmware supervises the bench.
          </p>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={operation !== null}
              title="Overwrite the staged safety policy with the latest live controller snapshot."
              onClick={syncSafetyPolicyFromSnapshot}
            >
              Sync live policy
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the staged runtime safety policy. A write session will be requested automatically if needed."
              onClick={() => {
                void applySafetyPolicy()
              }}
            >
              Apply safety policy
            </button>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Horizon threshold (deg)"
                help="Beam pitch angle above which the horizon interlock trips."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyHorizonThresholdDeg}
                title="Beam pitch angle above which the horizon interlock trips."
                onChange={(event) => patchForm('safetyHorizonThresholdDeg', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Horizon hysteresis (deg)"
                help="Clear margin below the trip threshold for the horizon interlock."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyHorizonHysteresisDeg}
                title="Clear margin below the trip threshold for the horizon interlock."
                onChange={(event) => patchForm('safetyHorizonHysteresisDeg', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF min range (m)"
                help="Minimum permitted distance before the range interlock trips."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTofMinRangeM}
                title="Minimum permitted distance before the range interlock trips."
                onChange={(event) => patchForm('safetyTofMinRangeM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF max range (m)"
                help="Maximum permitted distance before the range interlock trips."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTofMaxRangeM}
                title="Maximum permitted distance before the range interlock trips."
                onChange={(event) => patchForm('safetyTofMaxRangeM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF hysteresis (m)"
                help="Clear margin used after a ToF trip."
              />
              <input
                type="number"
                step="0.005"
                value={form.safetyTofHysteresisM}
                title="Clear margin used after a ToF trip."
                onChange={(event) => patchForm('safetyTofHysteresisM', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="IMU stale timeout (ms)"
                help="Maximum age of IMU data before it is treated unsafe."
              />
              <input
                type="number"
                step="1"
                value={form.safetyImuStaleMs}
                title="Maximum age of IMU data before it is treated unsafe."
                onChange={(event) => patchForm('safetyImuStaleMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="ToF stale timeout (ms)"
                help="Maximum age of ToF data before it is treated unsafe."
              />
              <input
                type="number"
                step="1"
                value={form.safetyTofStaleMs}
                title="Maximum age of ToF data before it is treated unsafe."
                onChange={(event) => patchForm('safetyTofStaleMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Rail-good timeout (ms)"
                help="How long a commanded rail can miss PGOOD before the controller faults."
              />
              <input
                type="number"
                step="1"
                value={form.safetyRailGoodTimeoutMs}
                title="How long a commanded rail can miss PGOOD before the controller faults."
                onChange={(event) => patchForm('safetyRailGoodTimeoutMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda drift limit (nm)"
                help="Maximum absolute wavelength drift before NIR is blocked."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyLambdaDriftLimitNm}
                title="Maximum absolute wavelength drift before NIR is blocked."
                onChange={(event) => patchForm('safetyLambdaDriftLimitNm', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda hysteresis (nm)"
                help="Recovery margin required before a lambda drift trip clears."
              />
              <input
                type="number"
                step="0.05"
                value={form.safetyLambdaDriftHysteresisNm}
                title="Recovery margin required before a lambda drift trip clears."
                onChange={(event) => patchForm('safetyLambdaDriftHysteresisNm', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Lambda hold (ms)"
                help="Persistence window before lambda drift changes the interlock state."
              />
              <input
                type="number"
                step="1"
                value={form.safetyLambdaDriftHoldMs}
                title="Persistence window before lambda drift changes the interlock state."
                onChange={(event) => patchForm('safetyLambdaDriftHoldMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="LD overtemp (°C)"
                help="Laser driver temperature limit before a major fault is raised."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyLdOvertempLimitC}
                title="Laser driver temperature limit before a major fault is raised."
                onChange={(event) => patchForm('safetyLdOvertempLimitC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC trip (V)"
                help="Trip voltage on the TEC temperature ADC monitor."
              />
              <input
                type="number"
                step="0.001"
                value={form.safetyTecTempAdcTripV}
                title="Trip voltage on the TEC temperature ADC monitor."
                onChange={(event) => patchForm('safetyTecTempAdcTripV', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC hysteresis (V)"
                help="Recovery margin below the ADC trip threshold."
              />
              <input
                type="number"
                step="0.001"
                value={form.safetyTecTempAdcHysteresisV}
                title="Recovery margin below the ADC trip threshold."
                onChange={(event) => patchForm('safetyTecTempAdcHysteresisV', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC temp ADC hold (ms)"
                help="Persistence window before the TEC ADC thermal trip changes state."
              />
              <input
                type="number"
                step="1"
                value={form.safetyTecTempAdcHoldMs}
                title="Persistence window before the TEC ADC thermal trip changes state."
                onChange={(event) => patchForm('safetyTecTempAdcHoldMs', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC min command (°C)"
                help="Lower software clamp for TEC target commands."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyTecMinCommandC}
                title="Lower software clamp for TEC target commands."
                onChange={(event) => patchForm('safetyTecMinCommandC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC max command (°C)"
                help="Upper software clamp for TEC target commands."
              />
              <input
                type="number"
                step="0.1"
                value={form.safetyTecMaxCommandC}
                title="Upper software clamp for TEC target commands."
                onChange={(event) => patchForm('safetyTecMaxCommandC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="TEC ready tolerance (°C)"
                help="Tolerance around the TEC target used for ready and settled status."
              />
              <input
                type="number"
                step="0.01"
                value={form.safetyTecReadyToleranceC}
                title="Tolerance around the TEC target used for ready and settled status."
                onChange={(event) => patchForm('safetyTecReadyToleranceC', event.target.value)}
              />
            </label>
            <label className="field">
              <FieldLabel
                label="Max laser current (A)"
                help="Maximum host-stageable laser current."
              />
              <input
                type="number"
                step="0.05"
                value={form.safetyMaxLaserCurrentA}
                title="Maximum host-stageable laser current."
                onChange={(event) => patchForm('safetyMaxLaserCurrentA', event.target.value)}
              />
            </label>
          </div>

          <div className="status-badges">
            <span className={snapshot.safety.horizonBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Compass size={14} />
              Horizon {snapshot.safety.horizonBlocked ? 'blocked' : 'clear'}
            </span>
            <span className={snapshot.safety.distanceBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Activity size={14} />
              Range {snapshot.safety.distanceBlocked ? 'blocked' : 'clear'}
            </span>
            <span className={snapshot.safety.lambdaDriftBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Radio size={14} />
              Lambda {snapshot.safety.lambdaDriftBlocked ? 'drifting' : 'stable'}
            </span>
            <span className={snapshot.safety.tecTempAdcBlocked ? 'status-badge is-warn' : 'status-badge is-on'}>
              <Thermometer size={14} />
              Temp ADC {snapshot.safety.tecTempAdcBlocked ? 'high' : 'clear'}
            </span>
          </div>
        </article>

        <article className="panel-cutout bringup-module-overview">
          <div className="cutout-head">
            <Cpu size={16} />
            <strong>Choose a module</strong>
          </div>
          <p className="panel-note">
            Start with the subsystem you actually have wired. A module is only marked detected after a successful probe.
          </p>
          <div className="bringup-roster">
            {moduleKeys.map((module) => {
              const meta = moduleMeta[module]
              const score = moduleScore(displayModules[module])
              const summary = moduleSummary(displayModules[module], connected)

              return (
                <button
                  key={module}
                  type="button"
                  className="bringup-roster__item"
                  title={`Open the ${meta.label} bring-up page.`}
                  onClick={() => setActivePage(module)}
                >
                  <div>
                    <strong>{meta.label}</strong>
                    <p>{summary.detail}</p>
                  </div>
                  <div className="bringup-roster__meter">
                    <span>{score.label}</span>
                    <ProgressMeter value={score.progress} tone={score.tone} compact />
                  </div>
                </button>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Save size={16} />
            <strong>Optional plan tools</strong>
          </div>

          <p className="panel-note">
            The browser keeps this plan automatically. Use the controls below if you want to restore a previous local draft or mirror the full plan into the controller.
          </p>

          <div className="field">
            <div className="field__head">
              <FieldLabel
                label="Profile name"
                help="Short bench profile name mirrored into the service snapshot and host draft."
              />
              <strong>rev {snapshot.bringup.profileRevision}</strong>
            </div>
            <div className="field__pair">
              <input
                type="text"
                maxLength={24}
                value={form.profileName}
                title="Rename the active bring-up profile."
                onChange={(event) => patchForm('profileName', event.target.value)}
              />
              <button
                type="button"
                className="action-button is-inline"
                disabled={operation !== null}
                title="Save the profile name to the controller. This does not require service mode."
                onClick={() =>
                  void saveProfileName()
                }
              >
                Save name
              </button>
            </div>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              title="Save the full bring-up form into browser-local storage."
              onClick={saveHostDraft}
            >
              Save local draft
            </button>
            <button
              type="button"
              className="action-button is-inline"
              title="Restore the last browser-local bring-up draft."
              onClick={restoreHostDraft}
            >
              Load local draft
            </button>
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={!connected || operation !== null}
              title="Push the entire staged bring-up draft to the controller. A write session will be requested automatically only when needed."
              onClick={() => {
                void pushDraftToDevice()
              }}
            >
              Push full plan
            </button>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={!connected || operation !== null}
              title="Ask the current bench firmware to persist the active bring-up profile. Service mode is not required."
              onClick={() =>
                void runCommandSequence(
                  'Save bring-up profile',
                  'Bring-up profile save requested from the controller.',
                  [
                    {
                      detail: 'Requesting device-side profile save...',
                      cmd: 'save_bringup_profile',
                      risk: 'write',
                      note: 'Request device-side persistence for the active bring-up profile.',
                    },
                  ],
                )
              }
            >
              Attempt device save
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled || operation !== null}
              title="Refresh the live bring-up profile from the controller."
              onClick={() =>
                void refreshLiveStatus(true, true)
              }
            >
              Refresh live plan
            </button>
            <button
              type="button"
              className="action-button is-inline"
              title="Overwrite the form with the latest live snapshot."
              onClick={syncFormFromSnapshot}
            >
              Load live into form
            </button>
          </div>

          <div className="note-strip">
            <span>{draftNote}</span>
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Cable size={16} />
            <strong>Bus tools moved</strong>
          </div>
          <p className="panel-note">
            Bus Lab, decoded I2C and SPI traffic, and device identity workspace now
            live in the top-level Tools page so module bring-up stays focused on
            installation, probing, and tuning.
          </p>
          <div className="status-badges">
            <span className="status-badge is-on">Use Tools for bus scan and register reads</span>
            <span className="status-badge is-warn">Writes still need service mode</span>
          </div>
        </article>
      </div>
    )
  }

  function renderImuPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('imu')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Compass size={16} />
            <strong>Runtime sampling</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Output data rate"
                help="Datasheet CTRL1_XL and CTRL2_G operating data-rate selection. Start around 104 to 208 Hz for bench bring-up."
              />
              <select
                value={form.imuOdrHz}
                title="Select the IMU output data rate to stage into service mode."
                onChange={(event) => patchForm('imuOdrHz', event.target.value)}
              >
                {imuOdrOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} Hz
                  </option>
                ))}
              </select>
            </label>

            <label className="field">
              <FieldLabel
                label="Accel full scale"
                help="CTRL1_XL full-scale range for the accelerometer path."
              />
              <select
                value={form.imuAccelRangeG}
                title="Select the accelerometer full-scale range."
                onChange={(event) => patchForm('imuAccelRangeG', event.target.value)}
              >
                {imuAccelRangeOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} g
                  </option>
                ))}
              </select>
            </label>

            <label className="field">
              <FieldLabel
                label="Gyro full scale"
                help="CTRL2_G full-scale range for the gyroscope path."
              />
              <select
                value={form.imuGyroRangeDps}
                title="Select the gyroscope full-scale range."
                onChange={(event) => patchForm('imuGyroRangeDps', event.target.value)}
              >
                {imuGyroRangeOptions.map((option) => (
                  <option key={option} value={option}>
                    {option} dps
                  </option>
                ))}
              </select>
            </label>
          </div>

          <div className="bringup-toggle-grid">
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuGyroEnabled}
                title="Keep the gyroscope enabled during bring-up."
                onChange={(event) => patchForm('imuGyroEnabled', event.target.checked)}
              />
              <span>Gyroscope enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuLpf2Enabled}
                title="Route accelerometer output through LPF2 when staged."
                onChange={(event) => patchForm('imuLpf2Enabled', event.target.checked)}
              />
              <span>LPF2 enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuTimestampEnabled}
                title="Enable the timestamp counter for freshness work."
                onChange={(event) => patchForm('imuTimestampEnabled', event.target.checked)}
              />
              <span>Timestamp counter enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuBduEnabled}
                title="Block-data-update keeps multi-byte samples coherent across reads."
                onChange={(event) => patchForm('imuBduEnabled', event.target.checked)}
              />
              <span>BDU enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuIfIncEnabled}
                title="IF_INC enables multi-byte auto-increment during SPI register access."
                onChange={(event) => patchForm('imuIfIncEnabled', event.target.checked)}
              />
              <span>Address auto-increment enabled</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.imuI2cDisabled}
                title="Disable the I2C block in CTRL4_C after SPI verification."
                onChange={(event) => patchForm('imuI2cDisabled', event.target.checked)}
              />
              <span>I2C block disabled</span>
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the full IMU runtime configuration. A write session will be requested automatically if needed."
              onClick={() => {
                void applyImuProfile()
              }}
            >
              Apply IMU tuning
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read WHO_AM_I over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU WHO_AM_I',
                  'imu',
                  0x0f,
                  'Read the LSM6DSO WHO_AM_I register over SPI.',
                )
              }}
            >
              Read WHO_AM_I
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read CTRL3_C over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU CTRL3_C',
                  'imu',
                  0x12,
                  'Read the LSM6DSO CTRL3_C register over SPI.',
                )
              }}
            >
              Read CTRL3_C
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read CTRL4_C over SPI."
              onClick={() => {
                void moduleQuickReadSpi(
                  'Read IMU CTRL4_C',
                  'imu',
                  0x13,
                  'Read the LSM6DSO CTRL4_C register over SPI.',
                )
              }}
            >
              Read CTRL4_C
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.imu.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Configured in silicon</span>
              <strong>{snapshot.peripherals.imu.configured ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>WHO_AM_I</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.whoAmI, 2)}</strong>
            </div>
            <div>
              <span>STATUS_REG</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.statusReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL1_XL</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl1XlReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL2_G</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl2GReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL3_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl3CReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL4_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl4CReg, 2)}</strong>
            </div>
            <div>
              <span>CTRL10_C</span>
              <strong>{formatRegisterHex(snapshot.peripherals.imu.ctrl10CReg, 2)}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.imu.lastErrorCode,
                  snapshot.peripherals.imu.lastError,
                )}
              </strong>
            </div>
          </div>

          <p className="inline-help">
            This block is actual IMU peripheral readback from the LSM6DSO over SPI.
            It is not the staged host tuning form.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastSpiOp}</p>
        </article>

        <ImuPostureCard snapshot={snapshot} />
      </div>
    )
  }

  function renderDacPage() {
    const dacModeNeedsRefDiv =
      form.dacReferenceMode === 'internal' && form.dacGain2x && !form.dacRefDiv

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('dac')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Reference and output policy</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Reference source"
                help="DAC80502 supports internal or external reference selection. Default to internal during first bring-up."
              />
              <select
                value={form.dacReferenceMode}
                title="Choose the DAC reference source."
                onChange={(event) =>
                  patchForm('dacReferenceMode', event.target.value as DacReferenceMode)
                }
              >
                {dacReferenceOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{dacReferenceOptions.find((option) => option.value === form.dacReferenceMode)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Update mode"
                help="Asynchronous updates act immediately on I2C write. Synchronous mode waits for a later trigger or LDAC-style commit."
              />
              <select
                value={form.dacSyncMode}
                title="Choose whether DAC outputs update asynchronously or synchronously."
                onChange={(event) =>
                  patchForm('dacSyncMode', event.target.value as DacSyncMode)
                }
              >
                {dacSyncModeOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{dacSyncModeOptions.find((option) => option.value === form.dacSyncMode)?.detail}</small>
            </label>
          </div>

          <div className="bringup-toggle-grid">
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.dacGain2x}
                title="Use the DAC output buffer gain x2 option."
                onChange={(event) => patchForm('dacGain2x', event.target.checked)}
              />
              <span>Buffer gain x2</span>
            </label>
            <label className="arming-toggle is-compact">
              <input
                type="checkbox"
                checked={form.dacRefDiv}
                title="Enable the reference divide-by-two path for span scaling."
                onChange={(event) => patchForm('dacRefDiv', event.target.checked)}
              />
              <span>Reference divider enabled</span>
            </label>
          </div>

          <p className="inline-help">
            Board-valid DAC output span on this PCB is `0.0-2.5 V` for both channels.
            `DAC_OUTA` drives laser `LISH`; `DAC_OUTB` drives TEC `TMS`.
          </p>
          {dacModeNeedsRefDiv ? (
            <p className="inline-help">
              Internal reference with gain `x2` needs the reference divider enabled on this
              `3.3 V` board. If `REF-DIV` is off, the DAC trips `REF-ALARM` and both outputs
              collapse to `0 V`.
            </p>
          ) : null}

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="LD shadow voltage"
                help="Service-only shadow voltage for the laser-current DAC channel. Board range is 0.0 V to 2.5 V."
              />
              <input
                value={form.dacLdVoltage}
                title="Stage the laser-current DAC shadow voltage in volts."
                onChange={(event) => patchForm('dacLdVoltage', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="TEC shadow voltage"
                help="Service-only shadow voltage for the TEC target DAC channel. Board range is 0.0 V to 2.5 V."
              />
              <input
                value={form.dacTecVoltage}
                title="Stage the TEC-target DAC shadow voltage in volts."
                onChange={(event) => patchForm('dacTecVoltage', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled || dacModeNeedsRefDiv}
              title={
                dacModeNeedsRefDiv
                  ? 'Enable the reference divider before applying an internal-reference x2 gain DAC profile on this 3.3 V board.'
                  : 'Push the full DAC bring-up profile and both channel shadow voltages.'
              }
              onClick={() => {
                void applyDacProfile()
              }}
            >
              Apply DAC profile
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC SYNC register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC SYNC',
                  0x48,
                  0x02,
                  'Read the DAC80502 SYNC register.',
                )
              }}
            >
              Read SYNC
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC CONFIG register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC CONFIG',
                  0x48,
                  0x03,
                  'Read the DAC80502 CONFIG register.',
                )
              }}
            >
              Read CONFIG
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC STATUS register to check REF-ALARM."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC STATUS',
                  0x48,
                  0x07,
                  'Read the DAC80502 STATUS register. REF-ALARM=1 means the reference headroom is invalid and all outputs are forced to 0 V.',
                )
              }}
            >
              Read STATUS
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC GAIN register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC GAIN',
                  0x48,
                  0x04,
                  'Read the DAC80502 GAIN register.',
                )
              }}
            >
              Read GAIN
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC channel A data register that drives DAC_OUTA / LISH."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC LD DATA',
                  0x48,
                  0x08,
                  'Read the DAC80502 channel A data register that drives DAC_OUTA toward the laser current command input.',
                )
              }}
            >
              Read LD DATA
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DAC channel B data register that drives DAC_OUTB / TMS."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DAC TEC DATA',
                  0x48,
                  0x09,
                  'Read the DAC80502 channel B data register that drives DAC_OUTB toward the TEC target input.',
                )
              }}
            >
              Read TEC DATA
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.dac.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Configured in silicon</span>
              <strong>{snapshot.peripherals.dac.configured ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>REF_ALARM</span>
              <strong>{snapshot.peripherals.dac.refAlarm ? 'Asserted' : 'Clear'}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.dac.lastErrorCode,
                  snapshot.peripherals.dac.lastError,
                )}
              </strong>
            </div>
            <div>
              <span>SYNC</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.syncReg)}</strong>
            </div>
            <div>
              <span>CONFIG</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.configReg)}</strong>
            </div>
            <div>
              <span>GAIN</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.gainReg)}</strong>
            </div>
            <div>
              <span>STATUS</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.statusReg)}</strong>
            </div>
            <div>
              <span>DATA A / LD</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.dataAReg)}</strong>
            </div>
            <div>
              <span>DATA B / TEC</span>
              <strong>{formatRegisterHex(snapshot.peripherals.dac.dataBReg)}</strong>
            </div>
          </div>

          <p className="inline-help">
            These DAC fields are actual readback from DAC80502 registers. They are the
            controller&apos;s live peripheral truth, separate from the staged form above.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Cpu size={16} />
            <strong>Safe-start note</strong>
          </div>
          <p className="inline-help">
            This page writes the real DAC80502 output registers. `DAC_OUTA` feeds the
            laser driver `LISH` command input and `DAC_OUTB` feeds the TEC controller
            `TMS` input. Keep both actuator rails disabled while proving those analog
            command voltages so the downstream laser and TEC power stages stay off.
          </p>
          <div className="bringup-fact-grid">
            <div>
              <span>Requested LD output</span>
              <strong>{formatNumber(snapshot.bringup.tuning.dacLdChannelV, 3)} V</strong>
            </div>
            <div>
              <span>Requested TEC output</span>
              <strong>{formatNumber(snapshot.bringup.tuning.dacTecChannelV, 3)} V</strong>
            </div>
          </div>
        </article>
      </div>
    )
  }

  function renderHapticPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('haptic')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Gamepad2 size={16} />
            <strong>Mode and actuator</strong>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Operating mode"
                help="DRV2605 MODE[2:0] selection. Start with internal trigger or RTP during bring-up."
              />
              <select
                value={form.hapticMode}
                title="Choose the DRV2605 operating mode to stage."
                onChange={(event) =>
                  patchForm('hapticMode', event.target.value as HapticMode)
                }
              >
                {hapticModeOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{hapticModeOptions.find((option) => option.value === form.hapticMode)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Actuator type"
                help="Select ERM or LRA so the feedback control and library assumptions stay aligned."
              />
              <select
                value={form.hapticActuator}
                title="Select the haptic actuator type."
                onChange={(event) =>
                  patchForm(
                    'hapticActuator',
                    event.target.value as HapticActuator,
                  )
                }
              >
                {hapticActuatorOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
              <small>{hapticActuatorOptions.find((option) => option.value === form.hapticActuator)?.detail}</small>
            </label>

            <label className="field">
              <FieldLabel
                label="Library"
                help="ROM library choice. Libraries 1 to 5 are ERM families, 6 is the LRA library."
              />
              <select
                value={form.hapticLibrary}
                title="Choose the DRV2605 ROM library."
                onChange={(event) => patchForm('hapticLibrary', event.target.value)}
              >
                {hapticLibraryOptions.map((option) => (
                  <option key={option.value} value={option.value}>
                    {option.label}
                  </option>
                ))}
              </select>
            </label>
          </div>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Effect ID"
                help="Waveform sequence register value. Use 1 to 127 for library effects."
              />
              <input
                value={form.hapticEffectId}
                title="Stage the primary DRV2605 effect ID."
                onChange={(event) => patchForm('hapticEffectId', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="RTP level"
                help="Real-time playback amplitude register value from 0 to 255."
              />
              <input
                value={form.hapticRtpLevel}
                title="Stage the DRV2605 RTP amplitude byte."
                onChange={(event) => patchForm('hapticRtpLevel', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Apply the full DRV2605 configuration. A write session will be requested automatically if needed."
              onClick={() => {
                void applyHapticProfile()
              }}
            >
              Apply haptic profile
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={writesDisabled}
              title="Fire the staged haptic test effect."
              onClick={() =>
                void runCommandSequence(
                  'Fire haptic test',
                  'Haptic test command issued.',
                  [
                    {
                      detail: 'Firing haptic test...',
                      cmd: 'haptic_debug_fire',
                      risk: 'service',
                      note: 'Fire the selected haptic test effect.',
                      requireService: true,
                    },
                  ],
                )
              }
            >
              Fire haptic test
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DRV2605 mode register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DRV2605 MODE',
                  0x5a,
                  0x01,
                  'Read the DRV2605 MODE register.',
                )
              }}
            >
              Read MODE
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read the DRV2605 GO register."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read DRV2605 GO',
                  0x5a,
                  0x0c,
                  'Read the DRV2605 GO register.',
                )
              }}
            >
              Read GO
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Peripheral reachable</span>
              <strong>{snapshot.peripherals.haptic.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>MODE</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.modeReg, 2)}</strong>
            </div>
            <div>
              <span>LIBRARY</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.libraryReg, 2)}</strong>
            </div>
            <div>
              <span>GO</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.goReg, 2)}</strong>
            </div>
            <div>
              <span>FEEDBACK</span>
              <strong>{formatRegisterHex(snapshot.peripherals.haptic.feedbackReg, 2)}</strong>
            </div>
            <div>
              <span>Last peripheral error</span>
              <strong>
                {readbackErrorLabel(
                  snapshot.peripherals.haptic.lastErrorCode,
                  snapshot.peripherals.haptic.lastError,
                )}
              </strong>
            </div>
          </div>

          <p className="inline-help">
            These fields are actual DRV2605 register readback from the peripheral, not
            just staged host settings.
          </p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Waves size={16} />
            <strong>Hazard note</strong>
          </div>
          <p className="inline-help">
            The recovered netlist suggests the haptic trigger path may share IO37
            with the visible laser enable path. Stay on short, explicit, service-only
            tests until that coupling is disproven on hardware.
          </p>
        </article>
      </div>
    )
  }

  function renderTofPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('tof')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Waves size={16} />
            <strong>Safety window placeholder</strong>
          </div>
          <p className="panel-note">
            The repository still does not include the ToF datasheet or the missing
            Sensor and LED board files. This page only stages generic safety limits
            and freshness expectations. It does not imply a real device model.
          </p>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Minimum range"
                help="Lower bound for the allowed distance window in meters."
              />
              <input
                value={form.tofMinRangeM}
                title="Stage the minimum allowed ToF range in meters."
                onChange={(event) => patchForm('tofMinRangeM', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Maximum range"
                help="Upper bound for the allowed distance window in meters."
              />
              <input
                value={form.tofMaxRangeM}
                title="Stage the maximum allowed ToF range in meters."
                onChange={(event) => patchForm('tofMaxRangeM', event.target.value)}
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Stale timeout"
                help="Freshness budget in milliseconds. Unknown or stale distance should be treated unsafe."
              />
              <input
                value={form.tofStaleTimeoutMs}
                title="Stage the ToF stale-data timeout in milliseconds."
                onChange={(event) => patchForm('tofStaleTimeoutMs', event.target.value)}
              />
            </label>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Stage the generic ToF range window and stale-data timeout."
              onClick={() => {
                void applyTofProfile()
              }}
            >
              Apply ToF limits
            </button>
          </div>

          <div className="bringup-fact-grid">
            <div>
              <span>Live distance</span>
              <strong>{formatNumber(snapshot.tof.distanceM, 2)} m</strong>
            </div>
            <div>
              <span>Validity</span>
              <strong>{snapshot.tof.valid && snapshot.tof.fresh ? 'Fresh' : 'Unsafe'}</strong>
            </div>
          </div>
        </article>
      </div>
    )
  }

  function renderPdPage() {
    const displayOrder = [2, 1, 0] as const
    const plannedProfiles = form.pdProfiles.map((profile) => ({
      enabled: profile.enabled,
      voltageV: parseNumber(profile.voltageV, 0),
      currentA: parseNumber(profile.currentA, 0),
    }))
    const liveProfiles = readbackPdProfiles(snapshot)
    const plannedTier = classifyPdTierFromPlan(
      snapshot.pd.negotiatedPowerW,
      snapshot.pd.sourceIsHostOnly,
      parseNumber(
        form.pdProgrammingOnlyMaxW,
        snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMinW,
        snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMaxW,
        snapshot.bringup.tuning.pdReducedModeMaxW ?? 35,
      ),
      parseNumber(
        form.pdFullModeMinW,
        snapshot.bringup.tuning.pdFullModeMinW ?? 35.1,
      ),
    )
    const matchedProfileIndex =
      snapshot.pd.contractObjectPosition > 0
        ? snapshot.pd.contractObjectPosition - 1
        : -1
    const highestEnabledProfilePowerW = plannedProfiles.reduce((highest, profile) => {
      if (!profile.enabled) {
        return highest
      }

      return Math.max(highest, pdProfilePowerW(profile))
    }, 0)
    const minimumOperationalW = Math.max(
      parseNumber(
        form.pdProgrammingOnlyMaxW,
        snapshot.bringup.tuning.pdProgrammingOnlyMaxW ?? 30,
      ),
      parseNumber(
        form.pdReducedModeMinW,
        snapshot.bringup.tuning.pdReducedModeMinW ?? 30,
      ),
    )

    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('pd')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <PlugZap size={16} />
            <strong>Live contract</strong>
          </div>
          <p className="panel-note">
            This block is the live contract the firmware is reading from the STUSB4500
            and the current RDO. It is the closest thing to ground truth on the board.
          </p>
          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Force the controller to re-read the STUSB4500 live contract and PDO runtime registers now."
              onClick={() => {
                void refreshPdStatus(true, true)
              }}
            >
              Refresh PD now
            </button>
          </div>
          <div className="bringup-fact-grid">
            <div>
              <span>Negotiated power</span>
              <strong>{formatNumber(snapshot.pd.negotiatedPowerW, 1)} W</strong>
            </div>
            <div>
              <span>Source voltage</span>
              <strong>{formatNumber(snapshot.pd.sourceVoltageV, 1)} V</strong>
            </div>
            <div>
              <span>Source current</span>
              <strong>{formatNumber(snapshot.pd.sourceCurrentA, 2)} A</strong>
            </div>
            <div>
              <span>Operating current</span>
              <strong>{formatNumber(snapshot.pd.operatingCurrentA, 2)} A</strong>
            </div>
            <div>
              <span>Live runtime tier</span>
              <strong>{snapshot.session.powerTier}</strong>
            </div>
            <div>
              <span>Selected PDO</span>
              <strong>
                {matchedProfileIndex >= 0
                  ? `${pdObjectLabel(matchedProfileIndex)} active`
                  : snapshot.pd.sourceIsHostOnly
                    ? 'Host-only 5 V'
                    : 'No explicit PDO'}
              </strong>
            </div>
            <div>
              <span>Chip PDO count</span>
              <strong>{snapshot.pd.sinkProfileCount}</strong>
            </div>
            <div>
              <span>Source class</span>
              <strong>{snapshot.pd.sourceIsHostOnly ? 'Computer host' : 'PD supply'}</strong>
            </div>
          </div>
          <div className="bringup-fact-grid bringup-fact-grid--compact">
            <div>
              <span>Chip reachable</span>
              <strong>{snapshot.peripherals.pd.reachable ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>CC_STATUS</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.ccStatusReg, 2)}</strong>
            </div>
            <div>
              <span>DPM_PDO_NUMB</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.pdoCountReg, 2)}</strong>
            </div>
            <div>
              <span>RDO_STATUS_0..3</span>
              <strong>{formatRegisterHex(snapshot.peripherals.pd.rdoStatusRaw, 8)}</strong>
            </div>
          </div>
          <p className="inline-help">
            These raw fields are actual STUSB4500 register readback. They are not the
            staged firmware PDO plan.
          </p>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <Wrench size={16} />
            <strong>Actual STUSB runtime PDOs</strong>
          </div>
          <p className="panel-note">
            These are the live sink PDO values read back from the STUSB4500 runtime
            registers. Priority is highest to lowest: PDO3, then PDO2, then PDO1. PDO1
            is the mandatory 5 V fallback.
          </p>

          <div className="bringup-profile-grid">
            {displayOrder.map((index) => {
              const profile = liveProfiles[index]
              const liveMatch = matchedProfileIndex === index

              return (
                <article key={`pd-live-profile-${index}`} className="bringup-profile-card">
                  <div className="bringup-profile-card__head">
                    <div>
                      <p className="eyebrow">{pdPriorityDetail(index)}</p>
                      <h3>{pdObjectLabel(index)}</h3>
                    </div>
                    <span
                      className={
                        liveMatch
                          ? 'status-badge is-on'
                          : profile.enabled
                            ? 'status-badge is-warn'
                            : 'status-badge'
                      }
                    >
                      {liveMatch ? 'active contract' : profile.enabled ? 'available' : 'disabled'}
                    </span>
                  </div>

                  <div className="field-grid">
                    <div className="field">
                      <FieldLabel
                        label="Voltage"
                        help="Read back from the STUSB4500 runtime PDO registers."
                      />
                      <div className="field-readback">{formatNumber(profile.voltageV, 2)} V</div>
                    </div>

                    <div className="field">
                      <FieldLabel
                        label="Current"
                        help="Read back from the STUSB4500 runtime PDO registers."
                      />
                      <div className="field-readback">{formatNumber(profile.currentA, 2)} A</div>
                    </div>
                  </div>

                  <div className="bringup-fact-grid bringup-fact-grid--compact">
                    <div>
                      <span>Configured power</span>
                      <strong>
                        {formatNumber(pdProfilePowerW(profile), 1)} W
                      </strong>
                    </div>
                    <div>
                      <span>Role</span>
                      <strong>{pdPriorityDetail(index)}</strong>
                    </div>
                  </div>
                </article>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Requested runtime PDOs</strong>
          </div>
          <p className="panel-note">
            Edit the runtime PDO plan you want the firmware to write into the STUSB4500.
            PDO1 stays at 5 V as the fallback object. PDO3 is the highest-priority
            request, PDO2 is the middle request, and PDO1 is the last fallback.
          </p>
          <p className="inline-help">
            Applying PDOs can trigger immediate USB-PD renegotiation. If this eval board
            is powering the controller on the same bench, the USB link may drop briefly
            while the source and sink settle on the new contract.
          </p>
          <p className="inline-help">
            Runtime PDO apply is for iteration. NVM burn is a separate manufacturing-style
            action and should only be used after the runtime plan has been validated.
          </p>
          <div className="bringup-toggle-row">
            <label className="arming-toggle">
              <input
                type="checkbox"
                checked={form.pdFirmwarePlanEnabled}
                title="When enabled, the controller stores this PDO plan in its own NVS and compares it against the live STUSB runtime PDO table whenever the ESP32 is online."
                onChange={(event) =>
                  patchForm('pdFirmwarePlanEnabled', event.target.checked)
                }
              />
              <span>Firmware owns this PDO plan and auto-loads it on mismatch</span>
            </label>
            <span className={form.pdFirmwarePlanEnabled ? 'status-badge is-on' : 'status-badge'}>
              {form.pdFirmwarePlanEnabled ? 'firmware auto-load on' : 'firmware auto-load off'}
            </span>
          </div>
          <p className="inline-help">
            Saving to firmware is different from STUSB NVM burn. The ESP32 stores the
            validated plan in its own NVS, compares it against live STUSB readback on
            startup, and only pushes runtime PDOs when the chip does not already match.
          </p>

          <div className="bringup-profile-grid">
            {displayOrder.map((index) => {
              const profile = form.pdProfiles[index]
              const liveMatch = matchedProfileIndex === index
              const requestedPowerW = pdProfilePowerW({
                voltageV: index === 0 ? 5 : parseNumber(profile.voltageV, 0),
                currentA: parseNumber(profile.currentA, 0),
              })

              return (
                <article key={`pd-plan-profile-${index}`} className="bringup-profile-card">
                  <div className="bringup-profile-card__head">
                    <div>
                      <p className="eyebrow">{pdPriorityDetail(index)}</p>
                      <h3>{pdObjectLabel(index)}</h3>
                    </div>
                    <span
                      className={
                        liveMatch
                          ? 'status-badge is-on'
                          : (index === 0 ? true : profile.enabled)
                            ? 'status-badge is-warn'
                            : 'status-badge'
                      }
                    >
                      {liveMatch
                        ? 'matches contract'
                        : index === 0
                          ? 'fixed fallback'
                          : profile.enabled
                            ? 'ready to apply'
                            : 'disabled'}
                    </span>
                  </div>

                  <div className="bringup-toggle-row">
                    <label className="arming-toggle is-compact">
                      <input
                        type="checkbox"
                        checked={index === 0 ? true : profile.enabled}
                        disabled={index === 0}
                        title={
                          index === 0
                            ? 'PDO1 is kept enabled as the mandatory 5 V fallback.'
                            : `Enable or disable ${pdObjectLabel(index)}.`
                        }
                        onChange={(event) =>
                          patchPdProfile(index, { enabled: event.target.checked })
                        }
                      />
                      <span>{index === 0 ? '5 V fallback always enabled' : 'Allow this request'}</span>
                    </label>
                  </div>

                  <div className="field-grid">
                    <label className="field">
                      <FieldLabel
                        label="Voltage"
                        help={
                          index === 0
                            ? 'PDO1 is fixed to 5 V by STUSB4500 hardware.'
                            : 'Choose a standard fixed-voltage PDO target. This editor intentionally limits you to the common fixed-voltage set.'
                        }
                      />
                      <select
                        value={index === 0 ? '5' : profile.voltageV}
                        disabled={index === 0}
                        title={
                          index === 0
                            ? 'PDO1 voltage is fixed at 5 V.'
                            : `Choose the requested voltage for ${pdObjectLabel(index)}.`
                        }
                        onChange={(event) =>
                          patchPdProfile(index, { voltageV: event.target.value })
                        }
                      >
                        {pdVoltageOptions.map((option) => (
                          <option key={`pd-voltage-${option.value}`} value={String(option.value)}>
                            {option.label}
                          </option>
                        ))}
                      </select>
                    </label>

                    <label className="field">
                      <FieldLabel
                        label="Current"
                        help="Choose a curated fixed-current request instead of free-form entry so the saved firmware plan stays sane and auditable."
                      />
                      <select
                        value={profile.currentA}
                        title={`Choose the requested current for ${pdObjectLabel(index)}.`}
                        onChange={(event) =>
                          patchPdProfile(index, { currentA: event.target.value })
                        }
                      >
                        {pdCurrentOptions.map((option) => (
                          <option key={`pd-current-${option.value}`} value={String(option.value)}>
                            {option.label}
                          </option>
                        ))}
                      </select>
                    </label>
                  </div>

                  <div className="bringup-fact-grid bringup-fact-grid--compact">
                    <div>
                      <span>Requested power</span>
                      <strong>{formatNumber(requestedPowerW, 1)} W</strong>
                    </div>
                    <div>
                      <span>Actual readback</span>
                      <strong>
                        {formatNumber(liveProfiles[index].voltageV, 1)} V / {formatNumber(liveProfiles[index].currentA, 2)} A
                      </strong>
                    </div>
                  </div>
                </article>
              )
            })}
          </div>
        </article>

        <article className="panel-cutout">
          <div className="cutout-head">
            <SlidersHorizontal size={16} />
            <strong>Firmware power thresholds</strong>
          </div>
          <p className="panel-note">
            These thresholds classify the current source as programming-only,
            insufficient, reduced, or full. They do not force a source to offer a
            higher-power contract if the source cannot satisfy it.
          </p>

          <div className="field-grid">
            <label className="field">
              <FieldLabel
                label="Programming-only ceiling"
                help="Below this power, the bench stays non-operational unless it is just a plain 5 V host attachment."
              />
              <input
                value={form.pdProgrammingOnlyMaxW}
                title="Set the maximum power still considered too weak for normal operation."
                onChange={(event) =>
                  patchForm('pdProgrammingOnlyMaxW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Reduced-mode minimum"
                help="Minimum negotiated power required before reduced mode is allowed."
              />
              <input
                value={form.pdReducedModeMinW}
                title="Set the minimum power required for reduced mode."
                onChange={(event) =>
                  patchForm('pdReducedModeMinW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Reduced-mode ceiling"
                help="Upper edge of reduced mode. Anything above this can qualify for full mode if the full threshold is met."
              />
              <input
                value={form.pdReducedModeMaxW}
                title="Set the upper bound for reduced mode."
                onChange={(event) =>
                  patchForm('pdReducedModeMaxW', event.target.value)
                }
              />
            </label>

            <label className="field">
              <FieldLabel
                label="Full-mode minimum"
                help="Minimum negotiated power required before the controller may classify the source as full power."
              />
              <input
                value={form.pdFullModeMinW}
                title="Set the minimum power required for full mode."
                onChange={(event) =>
                  patchForm('pdFullModeMinW', event.target.value)
                }
              />
            </label>
          </div>

          <div className="bringup-verification-grid">
            <div>
              <span>Highest enabled request</span>
              <strong>{formatNumber(highestEnabledProfilePowerW, 1)} W</strong>
            </div>
            <div>
              <span>Minimum operational power</span>
              <strong>{formatNumber(minimumOperationalW, 1)} W</strong>
            </div>
            <div>
              <span>Draft tier with current source</span>
              <strong>{plannedTier}</strong>
            </div>
            <div>
              <span>Current live selection</span>
              <strong>
                {matchedProfileIndex >= 0 ? pdObjectLabel(matchedProfileIndex) : '5 V / none'}
              </strong>
            </div>
          </div>

          <div className="button-row">
            <button
              type="button"
              className="action-button is-inline is-accent"
              disabled={writesDisabled}
              title="Write the requested runtime PDOs into the STUSB4500 and update firmware power thresholds."
              onClick={() => {
                void applyPdProfile()
              }}
            >
              Apply runtime PDOs
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={writesDisabled}
              title="Verify the requested PDOs by applying them, refreshing live STUSB readback, then saving the validated plan into controller firmware."
              onClick={() => {
                void savePdProfileToFirmware()
              }}
            >
              Save PDO plan to firmware
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Force the controller to re-read the STUSB4500 live contract and PDO runtime registers now."
              onClick={() =>
                void refreshPdStatus(true, true)
              }
            >
              Refresh PD now
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Run an I2C discovery scan against declared bring-up targets."
              onClick={() =>
                void moduleQuickScanI2c(
                  'Scan I2C bus',
                  'Run an I2C discovery scan against declared bring-up targets.',
                )
              }
            >
              Scan I2C
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read STUSB4500 status register 0x06."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read STUSB4500 0x06',
                  0x28,
                  0x06,
                  'Read the STUSB4500 status byte at 0x06.',
                )
              }}
            >
              Read 0x06
            </button>
            <button
              type="button"
              className="action-button is-inline"
              disabled={readsDisabled}
              title="Read STUSB4500 status register 0x07."
              onClick={() => {
                void moduleQuickReadI2c(
                  'Read STUSB4500 0x07',
                  0x28,
                  0x07,
                  'Read the STUSB4500 status byte at 0x07.',
                )
              }}
            >
              Read 0x07
            </button>
            <button
              type="button"
              className="action-button is-inline is-danger"
              disabled={writesDisabled}
              title="Burn the currently validated PDO plan into STUSB4500 NVM. Use only for final provisioning because NVM endurance is finite."
              onClick={() => setPdNvmConfirmOpen(true)}
            >
              Burn PDOs to NVM
            </button>
          </div>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cScan}</p>
          <p className="inline-help">{snapshot.bringup.tools.lastI2cOp}</p>
        </article>
      </div>
    )
  }

  function renderButtonsPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('buttons')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Layers3 size={16} />
            <strong>Two-stage trigger note</strong>
          </div>
          <p className="panel-note">
            The current bench plan keeps the trigger absent. This page exists so the
            absence is explicit and auditable instead of surfacing as a fault.
          </p>
          <div className="bringup-step-list">
            <div className="bringup-step-list__item">
              <strong>Stage 1 future job</strong>
              <p>Green alignment laser request gated by the same hard interlocks.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>Stage 2 future job</strong>
              <p>NIR request with immediate beam-off on release or illegal transition.</p>
            </div>
            <div className="bringup-step-list__item">
              <strong>Current status</strong>
              <p>No GPIO mapping is treated as authoritative until the missing sensor board files are reviewed.</p>
            </div>
          </div>
        </article>
      </div>
    )
  }

  function renderLaserPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('laserDriver')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Radio size={16} />
            <strong>Supervision snapshot</strong>
          </div>
          <div className="bringup-fact-grid">
            <div>
              <span>Driver standby</span>
              <strong>{snapshot.laser.driverStandby ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Loop good</span>
              <strong>{snapshot.laser.loopGood ? 'Yes' : 'No'}</strong>
            </div>
            <div>
              <span>Measured current</span>
              <strong>{formatNumber(snapshot.laser.measuredCurrentA, 3)} A</strong>
            </div>
            <div>
              <span>Driver temp</span>
              <strong>{formatNumber(snapshot.laser.driverTempC, 1)} C</strong>
            </div>
          </div>
          <p className="inline-help">
            This page is intentionally read-only in bring-up. Use DAC and Control
            pages to stage current requests, but keep the real laser hardware absent
            until IMU, PD, and rail supervision are proven.
          </p>
        </article>
      </div>
    )
  }

  function renderTecPage() {
    return (
      <div className="bringup-page-grid">
        {renderModuleSettings('tec')}

        <article className="panel-cutout">
          <div className="cutout-head">
            <Thermometer size={16} />
            <strong>TEC readiness</strong>
          </div>
          <div className="bringup-fact-grid">
            <div>
              <span>Target temp</span>
              <strong>{formatNumber(snapshot.tec.targetTempC, 1)} C</strong>
            </div>
            <div>
              <span>Target lambda</span>
              <strong>{formatNumber(snapshot.tec.targetLambdaNm, 2)} nm</strong>
            </div>
            <div>
              <span>Current</span>
              <strong>{formatNumber(snapshot.tec.currentA, 2)} A</strong>
            </div>
            <div>
              <span>Voltage</span>
              <strong>{formatNumber(snapshot.tec.voltageV, 2)} V</strong>
            </div>
          </div>
          <p className="inline-help">
            TEC14 is an analog controller. Bring-up should prove DAC shadowing,
            TEMPGD behavior, and telemetry plausibility before the real TEC loop is
            considered populated.
          </p>
        </article>
      </div>
    )
  }

  function renderActivePage() {
    switch (activePage) {
      case 'workflow':
        return renderWorkflowPage()
      case 'imu':
        return renderImuPage()
      case 'dac':
        return renderDacPage()
      case 'haptic':
        return renderHapticPage()
      case 'tof':
        return renderTofPage()
      case 'pd':
        return renderPdPage()
      case 'buttons':
        return renderButtonsPage()
      case 'laserDriver':
        return renderLaserPage()
      case 'tec':
        return renderTecPage()
      default:
        return renderWorkflowPage()
    }
  }

  return (
    <section
      className={operation !== null ? 'panel-section bringup-workspace is-busy' : 'panel-section bringup-workspace'}
      aria-busy={operation !== null}
    >
      <div className="panel-section__head">
        <div>
          <p className="eyebrow">Bring-up mode</p>
          <h2>Module bring-up</h2>
        </div>
        <p className="panel-note">
          Pick one subsystem, verify that it is really present, then stage settings only after a successful probe.
        </p>
      </div>

      <div className="bringup-shell">
        <aside className="bringup-sidebar">
          <div className="bringup-sidebar__summary">
            <div>
              <p className="eyebrow">Current bench plan</p>
              <strong>{form.profileName}</strong>
            </div>
            <div className="status-badges">
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                {connected ? 'Board linked' : 'Offline'}
              </span>
              <span
                className={
                  serviceModeActive
                    ? 'status-badge is-on'
                    : serviceModeRequested
                      ? 'status-badge is-warn'
                      : 'status-badge'
                }
              >
                {serviceModeActive
                  ? 'Write session active'
                  : serviceModeRequested
                    ? 'Write session requested'
                    : 'Read-only link'}
              </span>
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                {connected ? 'Probe loop running' : 'Probe loop paused'}
              </span>
            </div>
            <ProgressMeter
              value={overallProgress}
              tone={overallProgress >= 80 ? 'steady' : overallProgress >= 50 ? 'warning' : 'critical'}
            />
            {operation !== null ? (
              <div className="bringup-operation">
                <div className="bringup-operation__head">
                  <strong>{operation.label}</strong>
                  <span>{operation.percent}%</span>
                </div>
                <ProgressMeter value={operation.percent} tone={operation.tone} compact />
                <small>{operation.detail}</small>
              </div>
            ) : (
              <small>{snapshot.bringup.tools.lastAction}</small>
            )}
          </div>

          <nav className="bringup-nav" aria-label="Bring-up pages">
            {bringupPages.map((page) => {
              const Icon = page.icon
              const isActive = page.id === activePage
              const score =
                page.id === 'workflow'
                  ? {
                      progress: overallProgress,
                      tone: (
                        overallProgress >= 80
                          ? 'steady'
                          : overallProgress >= 50
                            ? 'warning'
                            : 'critical'
                      ) as UiTone,
                      label: 'Service and policy',
                    }
                  : moduleScore(displayModules[page.id])

              return (
                <button
                  key={page.id}
                  type="button"
                  className={isActive ? 'bringup-nav__item is-active' : 'bringup-nav__item'}
                  title={page.detail}
                  onClick={() => setActivePage(page.id)}
                >
                  <div className="bringup-nav__icon">
                    <Icon size={16} />
                  </div>
                  <div className="bringup-nav__copy">
                    <strong>{page.label}</strong>
                    <small>{score.label}</small>
                  </div>
                  <div className="bringup-nav__meter">
                    <ProgressMeter value={score.progress} tone={score.tone} compact />
                  </div>
                </button>
              )
            })}
          </nav>
        </aside>

        <div className="bringup-content">
          <div className="bringup-content__header">
            <div>
              <p className="eyebrow">Current page</p>
              <h3>
                {bringupPages.find((page) => page.id === activePage)?.label}
              </h3>
            </div>
            <div className="bringup-content__status">
              <span className={connected ? 'status-badge is-on' : 'status-badge'}>
                Link {connected ? 'live' : 'offline'}
              </span>
              <span
                className={
                  serviceModeActive
                    ? 'status-badge is-on'
                    : serviceModeRequested
                      ? 'status-badge is-warn'
                      : 'status-badge'
                }
              >
                {serviceModeActive ? 'Service active' : serviceModeRequested ? 'Service requested' : 'Service off'}
              </span>
              {livePageScore !== null ? (
                <span className={livePageScore.tone === 'critical' ? 'status-badge is-warn' : 'status-badge is-on'}>
                  {livePageScore.label}
                </span>
              ) : null}
            </div>
          </div>

          {renderActivePage()}
        </div>
      </div>

      <div className="command-footer">
        <div className="inline-token">
          <Cpu size={14} />
          <span>Live action: {snapshot.bringup.tools.lastAction}</span>
        </div>
        <div className="inline-token">
          <span>Profile: {form.profileName}</span>
        </div>
      </div>

      {operation !== null ? (
        <ControllerBusyOverlay
          label={operation.label}
          detail={operation.detail}
          percent={operation.percent}
          tone={operation.tone}
          footer={
            operation.requiresConfirm
              ? 'Controller action failed. Review the message and confirm before controls unlock.'
              : 'Controller busy. Bring-up controls are locked until this action finishes.'
          }
          confirmLabel={operation.requiresConfirm ? 'Confirm and close' : undefined}
          onConfirm={operation.requiresConfirm ? dismissOperation : undefined}
        />
      ) : null}
      {pdNvmConfirmOpen ? (
        <ConfirmActionDialog
          title="Burn STUSB4500 PDOs to NVM"
          detail="This action is for final provisioning only. The STUSB4500 uses these NVM values as startup defaults after reset or power-up."
          confirmLabel="Burn to NVM"
          tone="critical"
          bullets={[
            'NVM endurance is finite. Do not use NVM burn for iterative tuning.',
            'Validate the runtime PDO plan first with Apply runtime PDOs and live readback.',
            'A successful burn changes the controller startup defaults after STUSB reset or power cycle.',
          ]}
          onCancel={() => setPdNvmConfirmOpen(false)}
          onConfirm={() => {
            void burnPdProfileToNvm()
          }}
        />
      ) : null}
    </section>
  )
}
