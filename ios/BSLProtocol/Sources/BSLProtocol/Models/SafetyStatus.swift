import Foundation

/// Per-interlock enable mask + the ToF low-bound-only flag. Mirrors
/// `SafetyStatus.interlocks` in `host-console/src/types.ts:812-833` and the
/// firmware struct `laser_controller_interlock_enable_t` in
/// `components/laser_controller/include/laser_controller_config.h:31-53`.
///
/// Every flag defaults to `true` on the device. Setting a flag to `false`
/// disables that specific interlock while leaving every other check active.
/// The master `bringup.interlocksDisabled` (service-mode override) sits ABOVE
/// these and short-circuits all of them.
public struct InterlockEnableMask: Codable, Sendable, Equatable {
    public var horizonEnabled: Bool
    public var distanceEnabled: Bool
    public var lambdaDriftEnabled: Bool
    public var tecTempAdcEnabled: Bool
    public var imuInvalidEnabled: Bool
    public var imuStaleEnabled: Bool
    public var tofInvalidEnabled: Bool
    public var tofStaleEnabled: Bool
    public var ldOvertempEnabled: Bool
    public var ldLoopBadEnabled: Bool
    public var tofLowBoundOnly: Bool

    public init(
        horizonEnabled: Bool = true,
        distanceEnabled: Bool = true,
        lambdaDriftEnabled: Bool = true,
        tecTempAdcEnabled: Bool = true,
        imuInvalidEnabled: Bool = true,
        imuStaleEnabled: Bool = true,
        tofInvalidEnabled: Bool = true,
        tofStaleEnabled: Bool = true,
        ldOvertempEnabled: Bool = true,
        ldLoopBadEnabled: Bool = true,
        tofLowBoundOnly: Bool = false
    ) {
        self.horizonEnabled = horizonEnabled
        self.distanceEnabled = distanceEnabled
        self.lambdaDriftEnabled = lambdaDriftEnabled
        self.tecTempAdcEnabled = tecTempAdcEnabled
        self.imuInvalidEnabled = imuInvalidEnabled
        self.imuStaleEnabled = imuStaleEnabled
        self.tofInvalidEnabled = tofInvalidEnabled
        self.tofStaleEnabled = tofStaleEnabled
        self.ldOvertempEnabled = ldOvertempEnabled
        self.ldLoopBadEnabled = ldLoopBadEnabled
        self.tofLowBoundOnly = tofLowBoundOnly
    }
}

/// Full safety-thresholds block. Only the fields the iOS app reads or writes
/// are modeled; unknown keys from a newer firmware are tolerated because we
/// decode-if-present everywhere. Mirrors `SafetyStatus` in
/// `host-console/src/types.ts:758-833`.
public struct SafetyStatus: Codable, Sendable, Equatable {
    public var allowAlignment: Bool
    public var allowNir: Bool
    public var horizonBlocked: Bool
    public var distanceBlocked: Bool
    public var lambdaDriftBlocked: Bool
    public var tecTempAdcBlocked: Bool

    // Tunable thresholds (edited in Settings → Safety)
    public var horizonThresholdDeg: Double
    public var horizonHysteresisDeg: Double
    public var tofMinRangeM: Double
    public var tofMaxRangeM: Double
    public var tofHysteresisM: Double
    public var imuStaleMs: Int
    public var tofStaleMs: Int
    public var railGoodTimeoutMs: Int
    public var lambdaDriftLimitNm: Double
    public var lambdaDriftHysteresisNm: Double
    public var lambdaDriftHoldMs: Int
    public var ldOvertempLimitC: Double
    public var tecTempAdcTripV: Double
    public var tecTempAdcHysteresisV: Double
    public var tecTempAdcHoldMs: Int
    public var tecMinCommandC: Double
    public var tecMaxCommandC: Double
    public var tecReadyToleranceC: Double
    public var maxLaserCurrentA: Double
    public var offCurrentThresholdA: Double
    public var maxTofLedDutyCyclePct: Int
    public var lioVoltageOffsetV: Double

    // Live readouts (display-only)
    public var actualLambdaNm: Double
    public var targetLambdaNm: Double
    public var lambdaDriftNm: Double
    public var tempAdcVoltageV: Double

    public var interlocks: InterlockEnableMask

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        allowAlignment = try c.decodeIfPresent(Bool.self, forKey: .allowAlignment) ?? true
        allowNir = try c.decodeIfPresent(Bool.self, forKey: .allowNir) ?? false
        horizonBlocked = try c.decodeIfPresent(Bool.self, forKey: .horizonBlocked) ?? false
        distanceBlocked = try c.decodeIfPresent(Bool.self, forKey: .distanceBlocked) ?? false
        lambdaDriftBlocked = try c.decodeIfPresent(Bool.self, forKey: .lambdaDriftBlocked) ?? false
        tecTempAdcBlocked = try c.decodeIfPresent(Bool.self, forKey: .tecTempAdcBlocked) ?? false

        horizonThresholdDeg = try c.decodeIfPresent(Double.self, forKey: .horizonThresholdDeg) ?? 0
        horizonHysteresisDeg = try c.decodeIfPresent(Double.self, forKey: .horizonHysteresisDeg) ?? 3
        tofMinRangeM = try c.decodeIfPresent(Double.self, forKey: .tofMinRangeM) ?? 0.2
        tofMaxRangeM = try c.decodeIfPresent(Double.self, forKey: .tofMaxRangeM) ?? 1.0
        tofHysteresisM = try c.decodeIfPresent(Double.self, forKey: .tofHysteresisM) ?? 0.02
        imuStaleMs = try c.decodeIfPresent(Int.self, forKey: .imuStaleMs) ?? 50
        tofStaleMs = try c.decodeIfPresent(Int.self, forKey: .tofStaleMs) ?? 100
        railGoodTimeoutMs = try c.decodeIfPresent(Int.self, forKey: .railGoodTimeoutMs) ?? 250
        lambdaDriftLimitNm = try c.decodeIfPresent(Double.self, forKey: .lambdaDriftLimitNm) ?? 5.0
        lambdaDriftHysteresisNm = try c.decodeIfPresent(Double.self, forKey: .lambdaDriftHysteresisNm) ?? 0.5
        lambdaDriftHoldMs = try c.decodeIfPresent(Int.self, forKey: .lambdaDriftHoldMs) ?? 2000
        ldOvertempLimitC = try c.decodeIfPresent(Double.self, forKey: .ldOvertempLimitC) ?? 55.0
        tecTempAdcTripV = try c.decodeIfPresent(Double.self, forKey: .tecTempAdcTripV) ?? 2.45
        tecTempAdcHysteresisV = try c.decodeIfPresent(Double.self, forKey: .tecTempAdcHysteresisV) ?? 0.05
        tecTempAdcHoldMs = try c.decodeIfPresent(Int.self, forKey: .tecTempAdcHoldMs) ?? 2000
        tecMinCommandC = try c.decodeIfPresent(Double.self, forKey: .tecMinCommandC) ?? 15.0
        tecMaxCommandC = try c.decodeIfPresent(Double.self, forKey: .tecMaxCommandC) ?? 35.0
        tecReadyToleranceC = try c.decodeIfPresent(Double.self, forKey: .tecReadyToleranceC) ?? 0.25
        maxLaserCurrentA = try c.decodeIfPresent(Double.self, forKey: .maxLaserCurrentA) ?? 5.0
        offCurrentThresholdA = try c.decodeIfPresent(Double.self, forKey: .offCurrentThresholdA) ?? 0.2
        maxTofLedDutyCyclePct = try c.decodeIfPresent(Int.self, forKey: .maxTofLedDutyCyclePct) ?? 50
        lioVoltageOffsetV = try c.decodeIfPresent(Double.self, forKey: .lioVoltageOffsetV) ?? 0.07

        actualLambdaNm = try c.decodeIfPresent(Double.self, forKey: .actualLambdaNm) ?? 0
        targetLambdaNm = try c.decodeIfPresent(Double.self, forKey: .targetLambdaNm) ?? 0
        lambdaDriftNm = try c.decodeIfPresent(Double.self, forKey: .lambdaDriftNm) ?? 0
        tempAdcVoltageV = try c.decodeIfPresent(Double.self, forKey: .tempAdcVoltageV) ?? 0

        interlocks = try c.decodeIfPresent(InterlockEnableMask.self, forKey: .interlocks) ?? InterlockEnableMask()
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(allowAlignment, forKey: .allowAlignment)
        try c.encode(allowNir, forKey: .allowNir)
        try c.encode(horizonBlocked, forKey: .horizonBlocked)
        try c.encode(distanceBlocked, forKey: .distanceBlocked)
        try c.encode(lambdaDriftBlocked, forKey: .lambdaDriftBlocked)
        try c.encode(tecTempAdcBlocked, forKey: .tecTempAdcBlocked)
        try c.encode(horizonThresholdDeg, forKey: .horizonThresholdDeg)
        try c.encode(horizonHysteresisDeg, forKey: .horizonHysteresisDeg)
        try c.encode(tofMinRangeM, forKey: .tofMinRangeM)
        try c.encode(tofMaxRangeM, forKey: .tofMaxRangeM)
        try c.encode(tofHysteresisM, forKey: .tofHysteresisM)
        try c.encode(imuStaleMs, forKey: .imuStaleMs)
        try c.encode(tofStaleMs, forKey: .tofStaleMs)
        try c.encode(railGoodTimeoutMs, forKey: .railGoodTimeoutMs)
        try c.encode(lambdaDriftLimitNm, forKey: .lambdaDriftLimitNm)
        try c.encode(lambdaDriftHysteresisNm, forKey: .lambdaDriftHysteresisNm)
        try c.encode(lambdaDriftHoldMs, forKey: .lambdaDriftHoldMs)
        try c.encode(ldOvertempLimitC, forKey: .ldOvertempLimitC)
        try c.encode(tecTempAdcTripV, forKey: .tecTempAdcTripV)
        try c.encode(tecTempAdcHysteresisV, forKey: .tecTempAdcHysteresisV)
        try c.encode(tecTempAdcHoldMs, forKey: .tecTempAdcHoldMs)
        try c.encode(tecMinCommandC, forKey: .tecMinCommandC)
        try c.encode(tecMaxCommandC, forKey: .tecMaxCommandC)
        try c.encode(tecReadyToleranceC, forKey: .tecReadyToleranceC)
        try c.encode(maxLaserCurrentA, forKey: .maxLaserCurrentA)
        try c.encode(offCurrentThresholdA, forKey: .offCurrentThresholdA)
        try c.encode(maxTofLedDutyCyclePct, forKey: .maxTofLedDutyCyclePct)
        try c.encode(lioVoltageOffsetV, forKey: .lioVoltageOffsetV)
        try c.encode(actualLambdaNm, forKey: .actualLambdaNm)
        try c.encode(targetLambdaNm, forKey: .targetLambdaNm)
        try c.encode(lambdaDriftNm, forKey: .lambdaDriftNm)
        try c.encode(tempAdcVoltageV, forKey: .tempAdcVoltageV)
        try c.encode(interlocks, forKey: .interlocks)
    }

    /// Default-initialized instance used when a telemetry frame has not been
    /// received yet. The UI checks `DeviceSession.connected` / `.lastFrameAt`
    /// before rendering any value as "live".
    public static var unknown: SafetyStatus {
        var c = SafetyStatus.emptyContainer()
        c.interlocks = InterlockEnableMask()
        return c
    }

    private static func emptyContainer() -> SafetyStatus {
        // Decode an empty JSON object to exercise all decodeIfPresent
        // defaults. Keeps a single source of truth for defaults.
        let data = Data("{}".utf8)
        return try! JSONDecoder().decode(SafetyStatus.self, from: data)
    }

    private enum CodingKeys: String, CodingKey {
        case allowAlignment, allowNir
        case horizonBlocked, distanceBlocked, lambdaDriftBlocked, tecTempAdcBlocked
        case horizonThresholdDeg, horizonHysteresisDeg
        case tofMinRangeM, tofMaxRangeM, tofHysteresisM
        case imuStaleMs, tofStaleMs, railGoodTimeoutMs
        case lambdaDriftLimitNm, lambdaDriftHysteresisNm, lambdaDriftHoldMs
        case ldOvertempLimitC
        case tecTempAdcTripV, tecTempAdcHysteresisV, tecTempAdcHoldMs
        case tecMinCommandC, tecMaxCommandC, tecReadyToleranceC
        case maxLaserCurrentA, offCurrentThresholdA
        case maxTofLedDutyCyclePct, lioVoltageOffsetV
        case actualLambdaNm, targetLambdaNm, lambdaDriftNm, tempAdcVoltageV
        case interlocks
    }
}
