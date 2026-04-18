import Foundation

extension DeviceSnapshot {
    /// Overlays a `live_telemetry` patch onto a live snapshot. Only sub-blocks
    /// named in `presentKeys` are replaced; every other sub-block is preserved
    /// from `base`. Without this guard, absent blocks decode to defaults and
    /// silently clobber live state. Mirrors
    /// `host-console/src/lib/live-telemetry.ts` semantics.
    public static func overlay(
        base: DeviceSnapshot,
        patch: DeviceSnapshot,
        presentKeys: Set<String>
    ) -> DeviceSnapshot {
        var merged = base
        if presentKeys.contains("session") { merged.session = patch.session }
        if presentKeys.contains("rails") { merged.rails = patch.rails }
        if presentKeys.contains("imu") { merged.imu = patch.imu }
        if presentKeys.contains("tof") { merged.tof = patch.tof }
        if presentKeys.contains("laser") { merged.laser = patch.laser }
        if presentKeys.contains("tec") { merged.tec = patch.tec }
        if presentKeys.contains("pd") { merged.pd = patch.pd }
        if presentKeys.contains("bench") { merged.bench = patch.bench }
        if presentKeys.contains("safety") { merged.safety = patch.safety }
        if presentKeys.contains("fault") { merged.fault = patch.fault }
        if presentKeys.contains("deployment") { merged.deployment = patch.deployment }
        return merged
    }
}
