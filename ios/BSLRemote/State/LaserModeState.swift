import Foundation
import BSLProtocol

/// Three-state model derived from firmware telemetry.
///
/// - `disarmed` — deployment not active; setpoint editable but nothing primed
/// - `armed`    — deployment active + ready-idle; waiting for external trigger
/// - `lasing`   — `laser.measuredCurrentA > safety.offCurrentThresholdA`
///
/// Firmware is the safety authority. This is a *read-only* projection.
enum LaserMode: Equatable {
    case disarmed
    case armed
    case lasing

    var label: String {
        switch self {
        case .disarmed: return "DISARMED"
        case .armed:    return "ARMED · READY"
        case .lasing:   return "LASING"
        }
    }

    var shortLabel: String {
        switch self {
        case .disarmed: return "DISARMED"
        case .armed:    return "ARMED"
        case .lasing:   return "LASING"
        }
    }
}

extension DeviceSnapshot {
    /// Derive `LaserMode` from the snapshot. Requires:
    ///   - `armed`  → `deployment.active && deployment.ready && deployment.readyIdle`
    ///   - `lasing` → `laser.measuredCurrentA > safety.offCurrentThresholdA`
    var laserMode: LaserMode {
        if laser.measuredCurrentA > safety.offCurrentThresholdA {
            return .lasing
        }
        if deployment.active && deployment.ready && deployment.readyIdle {
            return .armed
        }
        return .disarmed
    }

    /// The setpoint in Amps. Source of truth is `bench.requestedCurrentA`.
    var nirSetpointA: Double { bench.requestedCurrentA }

    /// The actual measured current in Amps.
    var nirActualA: Double { laser.measuredCurrentA }

    /// The firmware-enforced ceiling the slider must not exceed.
    var nirMaxA: Double { max(safety.maxLaserCurrentA, 0.1) }

    /// Percentage (0–100) of setpoint against the firmware ceiling.
    var nirSetpointPct: Int {
        guard nirMaxA > 0 else { return 0 }
        return Int((nirSetpointA / nirMaxA * 100).rounded())
    }

    /// Percentage (0–100) of actual against the ceiling.
    var nirActualPct: Int {
        guard nirMaxA > 0 else { return 0 }
        return Int((nirActualA / nirMaxA * 100).rounded())
    }

    /// Coarse "external trigger" proxy. No dedicated footswitch field exists in
    /// telemetry; derive from `laser.nirEnabled` (firmware accepted the command)
    /// combined with lasing. When armed+nirEnabled but not yet lasing we report
    /// the footswitch as "awaiting".
    var externalTriggerActive: Bool {
        laser.measuredCurrentA > safety.offCurrentThresholdA
    }

    /// True if a latched fault is set — rendered as Master Warning.
    var hasMasterWarning: Bool { fault.latched }

    /// True if any auto-clearing interlock is active — rendered as Master Caution.
    var hasMasterCaution: Bool {
        if fault.isAutoClearing { return true }
        if safety.horizonBlocked { return true }
        if safety.distanceBlocked { return true }
        if safety.lambdaDriftBlocked { return true }
        if safety.tecTempAdcBlocked { return true }
        if !imu.valid || !imu.fresh { return true }
        if !tof.valid || !tof.fresh { return true }
        return false
    }
}
