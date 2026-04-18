import Foundation

/// Frozen at-trip diagnostic frame for a fault. Mirrors
/// `FaultTriggerDiag` in `host-console/src/types.ts:621-629`. Firmware
/// currently populates this only for `ld_overtemp` per
/// `docs/protocol-spec.md:848-908`. Shape is general so future faults can
/// reuse it without a schema migration.
public struct FaultTriggerDiag: Codable, Sendable, Equatable {
    public var code: String
    public var measuredC: Double
    public var measuredVoltageV: Double
    public var limitC: Double
    public var ldPgoodForMs: Int
    public var sbdnNotOffForMs: Int
    public var expr: String

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        code = try c.decodeIfPresent(String.self, forKey: .code) ?? ""
        measuredC = try c.decodeIfPresent(Double.self, forKey: .measuredC) ?? 0
        measuredVoltageV = try c.decodeIfPresent(Double.self, forKey: .measuredVoltageV) ?? 0
        limitC = try c.decodeIfPresent(Double.self, forKey: .limitC) ?? 0
        ldPgoodForMs = try c.decodeIfPresent(Int.self, forKey: .ldPgoodForMs) ?? 0
        sbdnNotOffForMs = try c.decodeIfPresent(Int.self, forKey: .sbdnNotOffForMs) ?? 0
        expr = try c.decodeIfPresent(String.self, forKey: .expr) ?? ""
    }

    private enum CodingKeys: String, CodingKey {
        case code, measuredC, measuredVoltageV, limitC, ldPgoodForMs, sbdnNotOffForMs, expr
    }
}

/// Compact fault summary. Mirrors `FaultSummary` in
/// `host-console/src/types.ts:631-649`.
public struct FaultSummary: Codable, Sendable, Equatable {
    public var latched: Bool
    public var activeCode: String
    public var activeClass: String
    public var latchedCode: String
    public var latchedClass: String
    public var activeReason: String
    public var latchedReason: String
    public var activeCount: Int
    public var tripCounter: Int
    public var lastFaultAtIso: String?
    public var triggerDiag: FaultTriggerDiag?

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        latched = try c.decodeIfPresent(Bool.self, forKey: .latched) ?? false
        activeCode = try c.decodeIfPresent(String.self, forKey: .activeCode) ?? "none"
        activeClass = try c.decodeIfPresent(String.self, forKey: .activeClass) ?? "none"
        latchedCode = try c.decodeIfPresent(String.self, forKey: .latchedCode) ?? "none"
        latchedClass = try c.decodeIfPresent(String.self, forKey: .latchedClass) ?? "none"
        activeReason = try c.decodeIfPresent(String.self, forKey: .activeReason) ?? ""
        latchedReason = try c.decodeIfPresent(String.self, forKey: .latchedReason) ?? ""
        activeCount = try c.decodeIfPresent(Int.self, forKey: .activeCount) ?? 0
        tripCounter = try c.decodeIfPresent(Int.self, forKey: .tripCounter) ?? 0
        lastFaultAtIso = try c.decodeIfPresent(String.self, forKey: .lastFaultAtIso)
        triggerDiag = try c.decodeIfPresent(FaultTriggerDiag.self, forKey: .triggerDiag)
    }

    public static let empty = FaultSummary.decode(from: "{}")

    private static func decode(from json: String) -> FaultSummary {
        try! JSONDecoder().decode(FaultSummary.self, from: Data(json.utf8))
    }

    private enum CodingKeys: String, CodingKey {
        case latched, activeCode, activeClass, latchedCode, latchedClass
        case activeReason, latchedReason, activeCount, tripCounter, lastFaultAtIso
        case triggerDiag
    }

    /// An active fault that is NOT latched is an auto-clearing interlock in
    /// the current bench image (see `docs/protocol-spec.md:627-628`).
    public var isAutoClearing: Bool {
        !latched && activeCode != "none" && !activeCode.isEmpty
    }
}
