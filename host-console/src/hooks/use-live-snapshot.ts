import { useMemo, useSyncExternalStore } from 'react'

import {
  mergeRealtimeTelemetryIntoSnapshot,
  type RealtimeTelemetryStore,
} from '../lib/live-telemetry'
import type { DeviceSnapshot } from '../types'

export function useLiveSnapshot(
  snapshot: DeviceSnapshot,
  telemetryStore: RealtimeTelemetryStore,
): DeviceSnapshot {
  const telemetry = useSyncExternalStore(
    telemetryStore.subscribe,
    telemetryStore.getSnapshot,
    telemetryStore.getSnapshot,
  )

  return useMemo(
    () => mergeRealtimeTelemetryIntoSnapshot(snapshot, telemetry),
    [snapshot, telemetry],
  )
}
