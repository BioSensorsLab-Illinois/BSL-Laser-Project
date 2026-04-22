import Foundation

public struct RailState: Codable, Sendable, Equatable {
    public var enabled: Bool
    public var pgood: Bool

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        enabled = try c.decodeIfPresent(Bool.self, forKey: .enabled) ?? false
        pgood = try c.decodeIfPresent(Bool.self, forKey: .pgood) ?? false
    }

    public init(enabled: Bool = false, pgood: Bool = false) {
        self.enabled = enabled
        self.pgood = pgood
    }

    private enum CodingKeys: String, CodingKey { case enabled, pgood }
}

public struct RailsBlock: Codable, Sendable, Equatable {
    public var ld: RailState
    public var tec: RailState

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ld = try c.decodeIfPresent(RailState.self, forKey: .ld) ?? RailState()
        tec = try c.decodeIfPresent(RailState.self, forKey: .tec) ?? RailState()
    }

    public init(ld: RailState = RailState(), tec: RailState = RailState()) {
        self.ld = ld
        self.tec = tec
    }

    private enum CodingKeys: String, CodingKey { case ld, tec }
}

public struct ImuStatus: Codable, Sendable, Equatable {
    public var valid: Bool
    public var fresh: Bool

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        valid = try c.decodeIfPresent(Bool.self, forKey: .valid) ?? false
        fresh = try c.decodeIfPresent(Bool.self, forKey: .fresh) ?? false
    }

    public init(valid: Bool = false, fresh: Bool = false) {
        self.valid = valid
        self.fresh = fresh
    }

    private enum CodingKeys: String, CodingKey { case valid, fresh }
}

public struct TofStatus: Codable, Sendable, Equatable {
    public var valid: Bool
    public var fresh: Bool
    public var distanceM: Double

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        valid = try c.decodeIfPresent(Bool.self, forKey: .valid) ?? false
        fresh = try c.decodeIfPresent(Bool.self, forKey: .fresh) ?? false
        distanceM = try c.decodeIfPresent(Double.self, forKey: .distanceM) ?? 0
    }

    public init(valid: Bool = false, fresh: Bool = false, distanceM: Double = 0) {
        self.valid = valid
        self.fresh = fresh
        self.distanceM = distanceM
    }

    private enum CodingKeys: String, CodingKey { case valid, fresh, distanceM }
}

public struct LaserStatus: Codable, Sendable, Equatable {
    public var nirEnabled: Bool
    public var alignmentEnabled: Bool
    public var measuredCurrentA: Double
    public var commandedCurrentA: Double
    public var loopGood: Bool
    public var driverTempC: Double
    public var driverStandby: Bool

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        nirEnabled = try c.decodeIfPresent(Bool.self, forKey: .nirEnabled) ?? false
        alignmentEnabled = try c.decodeIfPresent(Bool.self, forKey: .alignmentEnabled) ?? false
        measuredCurrentA = try c.decodeIfPresent(Double.self, forKey: .measuredCurrentA) ?? 0
        commandedCurrentA = try c.decodeIfPresent(Double.self, forKey: .commandedCurrentA) ?? 0
        loopGood = try c.decodeIfPresent(Bool.self, forKey: .loopGood) ?? false
        driverTempC = try c.decodeIfPresent(Double.self, forKey: .driverTempC) ?? 0
        driverStandby = try c.decodeIfPresent(Bool.self, forKey: .driverStandby) ?? true
    }

    public init(
        nirEnabled: Bool = false,
        alignmentEnabled: Bool = false,
        measuredCurrentA: Double = 0,
        commandedCurrentA: Double = 0,
        loopGood: Bool = false,
        driverTempC: Double = 0,
        driverStandby: Bool = true
    ) {
        self.nirEnabled = nirEnabled
        self.alignmentEnabled = alignmentEnabled
        self.measuredCurrentA = measuredCurrentA
        self.commandedCurrentA = commandedCurrentA
        self.loopGood = loopGood
        self.driverTempC = driverTempC
        self.driverStandby = driverStandby
    }

    private enum CodingKeys: String, CodingKey {
        case nirEnabled, alignmentEnabled
        case measuredCurrentA, commandedCurrentA
        case loopGood, driverTempC, driverStandby
    }
}

public struct TecStatus: Codable, Sendable, Equatable {
    public var targetTempC: Double
    public var targetLambdaNm: Double
    public var actualLambdaNm: Double
    public var tempGood: Bool
    public var tempC: Double
    public var currentA: Double
    public var voltageV: Double
    public var settlingSecondsRemaining: Int

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        targetTempC = try c.decodeIfPresent(Double.self, forKey: .targetTempC) ?? 25
        targetLambdaNm = try c.decodeIfPresent(Double.self, forKey: .targetLambdaNm) ?? 785
        actualLambdaNm = try c.decodeIfPresent(Double.self, forKey: .actualLambdaNm) ?? 0
        tempGood = try c.decodeIfPresent(Bool.self, forKey: .tempGood) ?? false
        tempC = try c.decodeIfPresent(Double.self, forKey: .tempC) ?? 0
        currentA = try c.decodeIfPresent(Double.self, forKey: .currentA) ?? 0
        voltageV = try c.decodeIfPresent(Double.self, forKey: .voltageV) ?? 0
        settlingSecondsRemaining = try c.decodeIfPresent(Int.self, forKey: .settlingSecondsRemaining) ?? 0
    }

    public init(
        targetTempC: Double = 25,
        targetLambdaNm: Double = 785,
        actualLambdaNm: Double = 0,
        tempGood: Bool = false,
        tempC: Double = 0,
        currentA: Double = 0,
        voltageV: Double = 0,
        settlingSecondsRemaining: Int = 0
    ) {
        self.targetTempC = targetTempC
        self.targetLambdaNm = targetLambdaNm
        self.actualLambdaNm = actualLambdaNm
        self.tempGood = tempGood
        self.tempC = tempC
        self.currentA = currentA
        self.voltageV = voltageV
        self.settlingSecondsRemaining = settlingSecondsRemaining
    }

    private enum CodingKeys: String, CodingKey {
        case targetTempC, targetLambdaNm, actualLambdaNm
        case tempGood, tempC, currentA, voltageV, settlingSecondsRemaining
    }
}

public struct PdStatus: Codable, Sendable, Equatable {
    public var contractValid: Bool
    public var negotiatedPowerW: Double
    public var sourceVoltageV: Double
    public var sourceCurrentA: Double

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        contractValid = try c.decodeIfPresent(Bool.self, forKey: .contractValid) ?? false
        negotiatedPowerW = try c.decodeIfPresent(Double.self, forKey: .negotiatedPowerW) ?? 0
        sourceVoltageV = try c.decodeIfPresent(Double.self, forKey: .sourceVoltageV) ?? 0
        sourceCurrentA = try c.decodeIfPresent(Double.self, forKey: .sourceCurrentA) ?? 0
    }

    public init(
        contractValid: Bool = false,
        negotiatedPowerW: Double = 0,
        sourceVoltageV: Double = 0,
        sourceCurrentA: Double = 0
    ) {
        self.contractValid = contractValid
        self.negotiatedPowerW = negotiatedPowerW
        self.sourceVoltageV = sourceVoltageV
        self.sourceCurrentA = sourceCurrentA
    }

    private enum CodingKeys: String, CodingKey {
        case contractValid, negotiatedPowerW, sourceVoltageV, sourceCurrentA
    }
}

public struct SessionStatus: Codable, Sendable, Equatable {
    public var uptimeSeconds: Int
    public var state: SystemState
    public var powerTier: PowerTier

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        uptimeSeconds = try c.decodeIfPresent(Int.self, forKey: .uptimeSeconds) ?? 0
        state = try c.decodeIfPresent(SystemState.self, forKey: .state) ?? .unknown
        powerTier = try c.decodeIfPresent(PowerTier.self, forKey: .powerTier) ?? .unknown
    }

    public init(uptimeSeconds: Int = 0, state: SystemState = .unknown, powerTier: PowerTier = .unknown) {
        self.uptimeSeconds = uptimeSeconds
        self.state = state
        self.powerTier = powerTier
    }

    private enum CodingKeys: String, CodingKey { case uptimeSeconds, state, powerTier }
}

public struct DeploymentSnapshot: Codable, Sendable, Equatable {
    public var active: Bool
    public var running: Bool
    public var ready: Bool
    public var readyIdle: Bool
    public var failed: Bool
    public var phase: DeploymentPhase
    public var maxLaserCurrentA: Double
    public var targetLambdaNm: Double
    public var targetTempC: Double
    /// Step identifiers emitted by firmware (`laser_controller_comms.c`
    /// 2334-2351): `currentStepKey` / `lastCompletedStepKey` carry a
    /// stable token like `RAIL_SEQUENCE` the UI can pattern-match.
    /// `currentStepIndex` is 0-based (NONE=0) into the fixed sequence
    /// defined in `laser_controller_deployment.h`.
    public var currentStepKey: String
    public var currentStepIndex: Int
    public var lastCompletedStepKey: String
    public var sequenceId: Int
    public var primaryFailureCode: String
    public var primaryFailureReason: String

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        active = try c.decodeIfPresent(Bool.self, forKey: .active) ?? false
        running = try c.decodeIfPresent(Bool.self, forKey: .running) ?? false
        ready = try c.decodeIfPresent(Bool.self, forKey: .ready) ?? false
        readyIdle = try c.decodeIfPresent(Bool.self, forKey: .readyIdle) ?? false
        failed = try c.decodeIfPresent(Bool.self, forKey: .failed) ?? false
        phase = try c.decodeIfPresent(DeploymentPhase.self, forKey: .phase) ?? .inactive
        maxLaserCurrentA = try c.decodeIfPresent(Double.self, forKey: .maxLaserCurrentA) ?? 5.0
        targetLambdaNm = try c.decodeIfPresent(Double.self, forKey: .targetLambdaNm) ?? 785
        targetTempC = try c.decodeIfPresent(Double.self, forKey: .targetTempC) ?? 25
        currentStepKey = try c.decodeIfPresent(String.self, forKey: .currentStepKey) ?? "NONE"
        currentStepIndex = try c.decodeIfPresent(Int.self, forKey: .currentStepIndex) ?? 0
        lastCompletedStepKey = try c.decodeIfPresent(String.self, forKey: .lastCompletedStepKey) ?? "NONE"
        sequenceId = try c.decodeIfPresent(Int.self, forKey: .sequenceId) ?? 0
        primaryFailureCode = try c.decodeIfPresent(String.self, forKey: .primaryFailureCode) ?? "none"
        primaryFailureReason = try c.decodeIfPresent(String.self, forKey: .primaryFailureReason) ?? ""
    }

    public init(
        active: Bool = false,
        running: Bool = false,
        ready: Bool = false,
        readyIdle: Bool = false,
        failed: Bool = false,
        phase: DeploymentPhase = .inactive,
        maxLaserCurrentA: Double = 5.0,
        targetLambdaNm: Double = 785,
        targetTempC: Double = 25,
        currentStepKey: String = "NONE",
        currentStepIndex: Int = 0,
        lastCompletedStepKey: String = "NONE",
        sequenceId: Int = 0,
        primaryFailureCode: String = "none",
        primaryFailureReason: String = ""
    ) {
        self.active = active
        self.running = running
        self.ready = ready
        self.readyIdle = readyIdle
        self.failed = failed
        self.phase = phase
        self.maxLaserCurrentA = maxLaserCurrentA
        self.targetLambdaNm = targetLambdaNm
        self.targetTempC = targetTempC
        self.currentStepKey = currentStepKey
        self.currentStepIndex = currentStepIndex
        self.lastCompletedStepKey = lastCompletedStepKey
        self.sequenceId = sequenceId
        self.primaryFailureCode = primaryFailureCode
        self.primaryFailureReason = primaryFailureReason
    }

    private enum CodingKeys: String, CodingKey {
        case active, running, ready, readyIdle, failed, phase
        case maxLaserCurrentA, targetLambdaNm, targetTempC
        case currentStepKey, currentStepIndex, lastCompletedStepKey
        case sequenceId, primaryFailureCode, primaryFailureReason
    }
}

/// Human-readable labels for the firmware deployment steps. Keys mirror
/// `laser_controller_deployment_step_name` token output in
/// `laser_controller_deployment.c`. Displayed in the iOS DeployBar so the
/// operator sees concrete progress (e.g. "Verifying peripherals",
/// "Settling TEC") instead of a generic "running checklist" spinner.
public enum DeploymentStepLabel {
    public static let total = 11

    public static func index(for key: String) -> Int {
        switch key.uppercased() {
        case "NONE": return 0
        case "OWNERSHIP_RECLAIM": return 1
        case "PD_INSPECT": return 2
        case "POWER_CAP": return 3
        case "OUTPUTS_OFF": return 4
        case "STABILIZE_3V3": return 5
        case "DAC_SAFE_ZERO": return 6
        case "PERIPHERALS_VERIFY": return 7
        case "RAIL_SEQUENCE": return 8
        case "TEC_SETTLE": return 9
        case "LP_GOOD_CHECK": return 10
        case "READY_POSTURE": return 11
        default: return 0
        }
    }

    public static func label(for key: String) -> String {
        switch key.uppercased() {
        case "NONE": return "Idle"
        case "OWNERSHIP_RECLAIM": return "Reclaiming bus ownership"
        case "PD_INSPECT": return "Inspecting USB-PD contract"
        case "POWER_CAP": return "Applying power cap"
        case "OUTPUTS_OFF": return "Forcing outputs safe"
        case "STABILIZE_3V3": return "Stabilizing 3.3 V rail"
        case "DAC_SAFE_ZERO": return "Zeroing DAC channels"
        case "PERIPHERALS_VERIFY": return "Verifying peripherals"
        case "RAIL_SEQUENCE": return "Sequencing TEC → LD rails"
        case "TEC_SETTLE": return "Settling TEC to target"
        case "LP_GOOD_CHECK": return "Locking LD driver loop"
        case "READY_POSTURE": return "Ready-posture qualification"
        default: return key.capitalized.replacingOccurrences(of: "_", with: " ")
        }
    }
}

/// Top-level device snapshot. Only the fields the iOS app reads are modeled;
/// everything else in the firmware payload is ignored on decode. Mirrors
/// the relevant subset of `DeviceSnapshot` in
/// `host-console/src/types.ts:835-874` plus the `live_telemetry` payload in
/// `docs/protocol-spec.md:449-499`.
public struct DeviceSnapshot: Codable, Sendable, Equatable {
    public var session: SessionStatus
    public var rails: RailsBlock
    public var imu: ImuStatus
    public var tof: TofStatus
    public var laser: LaserStatus
    public var tec: TecStatus
    public var pd: PdStatus
    public var bench: BenchControlStatus
    public var safety: SafetyStatus
    public var fault: FaultSummary
    public var deployment: DeploymentSnapshot

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        session = try c.decodeIfPresent(SessionStatus.self, forKey: .session) ?? SessionStatus()
        rails = try c.decodeIfPresent(RailsBlock.self, forKey: .rails) ?? RailsBlock()
        imu = try c.decodeIfPresent(ImuStatus.self, forKey: .imu) ?? ImuStatus()
        tof = try c.decodeIfPresent(TofStatus.self, forKey: .tof) ?? TofStatus()
        laser = try c.decodeIfPresent(LaserStatus.self, forKey: .laser) ?? LaserStatus()
        tec = try c.decodeIfPresent(TecStatus.self, forKey: .tec) ?? TecStatus()
        pd = try c.decodeIfPresent(PdStatus.self, forKey: .pd) ?? PdStatus()
        bench = try c.decodeIfPresent(BenchControlStatus.self, forKey: .bench) ?? BenchControlStatus.empty
        safety = try c.decodeIfPresent(SafetyStatus.self, forKey: .safety) ?? SafetyStatus.unknown
        fault = try c.decodeIfPresent(FaultSummary.self, forKey: .fault) ?? FaultSummary.empty
        deployment = try c.decodeIfPresent(DeploymentSnapshot.self, forKey: .deployment) ?? DeploymentSnapshot()
    }

    public init() {
        self.session = SessionStatus()
        self.rails = RailsBlock()
        self.imu = ImuStatus()
        self.tof = TofStatus()
        self.laser = LaserStatus()
        self.tec = TecStatus()
        self.pd = PdStatus()
        self.bench = BenchControlStatus.empty
        self.safety = SafetyStatus.unknown
        self.fault = FaultSummary.empty
        self.deployment = DeploymentSnapshot()
    }

    private enum CodingKeys: String, CodingKey {
        case session, rails, imu, tof, laser, tec, pd, bench, safety, fault, deployment
    }
}
