import Foundation

/// Firmware-computed block/pass reasons for host-driven actions plus the
/// SBDN state. Mirrors `HostControlReadiness` in
/// `host-console/src/types.ts:194-199`.
public struct HostControlReadiness: Codable, Sendable, Equatable {
    public var nirBlockedReason: NirBlockedReason
    public var ledBlockedReason: LedBlockedReason
    public var sbdnState: SbdnState

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        nirBlockedReason = try c.decodeIfPresent(NirBlockedReason.self, forKey: .nirBlockedReason) ?? .notConnected
        ledBlockedReason = try c.decodeIfPresent(LedBlockedReason.self, forKey: .ledBlockedReason) ?? .notConnected
        sbdnState = try c.decodeIfPresent(SbdnState.self, forKey: .sbdnState) ?? .off
    }

    public init(
        nirBlockedReason: NirBlockedReason = .notConnected,
        ledBlockedReason: LedBlockedReason = .notConnected,
        sbdnState: SbdnState = .off
    ) {
        self.nirBlockedReason = nirBlockedReason
        self.ledBlockedReason = ledBlockedReason
        self.sbdnState = sbdnState
    }

    private enum CodingKeys: String, CodingKey {
        case nirBlockedReason, ledBlockedReason, sbdnState
    }
}

/// USB-Debug Mock status. When `active == true`, the iOS app MUST render a
/// loud banner and treat every rail/telemetry value as synthesized. Mirrors
/// `UsbDebugMockStatus` in `host-console/src/types.ts:206-224` and the
/// safety-visibility rule in `.agent/AGENT.md`.
public struct UsbDebugMockStatus: Codable, Sendable, Equatable {
    public var active: Bool
    public var pdConflictLatched: Bool
    public var lastDisableReason: String

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        active = try c.decodeIfPresent(Bool.self, forKey: .active) ?? false
        pdConflictLatched = try c.decodeIfPresent(Bool.self, forKey: .pdConflictLatched) ?? false
        lastDisableReason = try c.decodeIfPresent(String.self, forKey: .lastDisableReason) ?? ""
    }

    public init(active: Bool = false, pdConflictLatched: Bool = false, lastDisableReason: String = "") {
        self.active = active
        self.pdConflictLatched = pdConflictLatched
        self.lastDisableReason = lastDisableReason
    }

    private enum CodingKeys: String, CodingKey {
        case active, pdConflictLatched, lastDisableReason
    }
}

/// Host-staged bench control state. Only the fields the iOS app reads are
/// decoded. Mirrors `BenchControlStatus` in
/// `host-console/src/types.ts:226-248`.
public struct BenchControlStatus: Codable, Sendable, Equatable {
    public var requestedNirEnabled: Bool
    public var requestedCurrentA: Double
    public var requestedLedEnabled: Bool
    public var requestedLedDutyCyclePct: Int
    public var appliedLedOwner: AppliedLedOwner
    public var appliedLedPinHigh: Bool
    public var illuminationEnabled: Bool
    public var illuminationDutyCyclePct: Int
    public var hostControlReadiness: HostControlReadiness
    public var usbDebugMock: UsbDebugMockStatus

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        requestedNirEnabled = try c.decodeIfPresent(Bool.self, forKey: .requestedNirEnabled) ?? false
        requestedCurrentA = try c.decodeIfPresent(Double.self, forKey: .requestedCurrentA) ?? 0
        requestedLedEnabled = try c.decodeIfPresent(Bool.self, forKey: .requestedLedEnabled) ?? false
        requestedLedDutyCyclePct = try c.decodeIfPresent(Int.self, forKey: .requestedLedDutyCyclePct) ?? 0
        appliedLedOwner = try c.decodeIfPresent(AppliedLedOwner.self, forKey: .appliedLedOwner) ?? .none
        appliedLedPinHigh = try c.decodeIfPresent(Bool.self, forKey: .appliedLedPinHigh) ?? false
        illuminationEnabled = try c.decodeIfPresent(Bool.self, forKey: .illuminationEnabled) ?? false
        illuminationDutyCyclePct = try c.decodeIfPresent(Int.self, forKey: .illuminationDutyCyclePct) ?? 0
        hostControlReadiness = try c.decodeIfPresent(HostControlReadiness.self, forKey: .hostControlReadiness) ?? HostControlReadiness()
        usbDebugMock = try c.decodeIfPresent(UsbDebugMockStatus.self, forKey: .usbDebugMock) ?? UsbDebugMockStatus()
    }

    public static let empty: BenchControlStatus = .decodeEmpty()

    private static func decodeEmpty() -> BenchControlStatus {
        try! JSONDecoder().decode(BenchControlStatus.self, from: Data("{}".utf8))
    }

    private enum CodingKeys: String, CodingKey {
        case requestedNirEnabled, requestedCurrentA
        case requestedLedEnabled, requestedLedDutyCyclePct
        case appliedLedOwner, appliedLedPinHigh
        case illuminationEnabled, illuminationDutyCyclePct
        case hostControlReadiness, usbDebugMock
    }
}
