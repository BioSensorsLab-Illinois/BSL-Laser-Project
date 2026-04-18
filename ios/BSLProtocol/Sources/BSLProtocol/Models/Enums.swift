import Foundation

/// Controller operating state. Mirrors `SystemState` in
/// `host-console/src/types.ts:1-14`. Unknown values from a newer firmware
/// land in `.unknown` so decoding never fails.
public enum SystemState: String, Codable, Sendable {
    case bootInit = "BOOT_INIT"
    case programmingOnly = "PROGRAMMING_ONLY"
    case safeIdle = "SAFE_IDLE"
    case powerNegotiation = "POWER_NEGOTIATION"
    case limitedPowerIdle = "LIMITED_POWER_IDLE"
    case tecWarmup = "TEC_WARMUP"
    case tecSettling = "TEC_SETTLING"
    case readyAlignment = "READY_ALIGNMENT"
    case readyNir = "READY_NIR"
    case alignmentActive = "ALIGNMENT_ACTIVE"
    case nirActive = "NIR_ACTIVE"
    case faultLatched = "FAULT_LATCHED"
    case serviceMode = "SERVICE_MODE"
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = SystemState(rawValue: raw) ?? .unknown
    }
}

public enum PowerTier: String, Codable, Sendable {
    case unknown
    case programmingOnly = "programming_only"
    case insufficient
    case reduced
    case full
    case other = "__other__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = PowerTier(rawValue: raw) ?? .other
    }
}

/// Firmware-computed reason the NIR command path is blocked. Mirrors the
/// 10-case enum in `types.ts:175-187`. Keep in lockstep — the app surfaces
/// these tokens to the operator via a reason chip.
public enum NirBlockedReason: String, Codable, Sendable {
    case none
    case notConnected = "not-connected"
    case faultLatched = "fault-latched"
    case deploymentOff = "deployment-off"
    case checklistRunning = "checklist-running"
    case checklistNotReady = "checklist-not-ready"
    case readyNotIdle = "ready-not-idle"
    case notModulatedHost = "not-modulated-host"
    case powerNotFull = "power-not-full"
    case railNotGood = "rail-not-good"
    case tecNotSettled = "tec-not-settled"
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = NirBlockedReason(rawValue: raw) ?? .unknown
    }

    /// Plain-English explanation shown when the operator taps the reason
    /// chip on the blocked power card.
    public var operatorText: String {
        switch self {
        case .none: return "Ready to command."
        case .notConnected: return "Controller is not connected."
        case .faultLatched: return "A fault is latched. Clear faults before commanding NIR."
        case .deploymentOff: return "Deployment mode is not active. Enter deployment from the desktop console."
        case .checklistRunning: return "Deployment checklist is running. Wait for it to finish."
        case .checklistNotReady: return "Deployment checklist has not reached ready state."
        case .readyNotIdle: return "Deployment is in ready-qualified; return to ready-idle before commanding."
        case .notModulatedHost: return "Operate mode is not modulated-host; host cannot command NIR in binary-trigger mode."
        case .powerNotFull: return "PD power tier is not full. Negotiate a full contract."
        case .railNotGood: return "LD or TEC rail PGOOD has not asserted."
        case .tecNotSettled: return "TEC has not settled within tolerance."
        case .unknown: return "Blocked by the firmware (unknown reason token)."
        }
    }
}

public enum LedBlockedReason: String, Codable, Sendable {
    case none
    case notConnected = "not-connected"
    case deploymentOff = "deployment-off"
    case checklistRunning = "checklist-running"
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = LedBlockedReason(rawValue: raw) ?? .unknown
    }

    public var operatorText: String {
        switch self {
        case .none: return "Ready to command."
        case .notConnected: return "Controller is not connected."
        case .deploymentOff: return "Deployment mode is not active."
        case .checklistRunning: return "Deployment checklist is running."
        case .unknown: return "Blocked by the firmware (unknown reason token)."
        }
    }
}

public enum SbdnState: String, Codable, Sendable {
    case off
    case on
    case standby
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = SbdnState(rawValue: raw) ?? .unknown
    }
}

public enum AppliedLedOwner: String, Codable, Sendable {
    case none
    case integrateService = "integrate_service"
    case operateRuntime = "operate_runtime"
    case buttonTrigger = "button_trigger"
    case deployment
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = AppliedLedOwner(rawValue: raw) ?? .unknown
    }
}

public enum DeploymentPhase: String, Codable, Sendable {
    case inactive
    case entry
    case checklist
    case readyIdle = "ready_idle"
    case failed
    case unknown = "__unknown__"

    public init(from decoder: Decoder) throws {
        let raw = try decoder.singleValueContainer().decode(String.self)
        self = DeploymentPhase(rawValue: raw) ?? .unknown
    }
}
