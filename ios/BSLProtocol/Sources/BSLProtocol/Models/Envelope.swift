import Foundation

/// Command payload. Values must be primitive JSON — numbers, strings, bools.
/// Mirrors the protocol contract in `docs/protocol-spec.md:44-60`.
public enum CommandArg: Encodable, Sendable, Equatable {
    case bool(Bool)
    case int(Int)
    case double(Double)
    case string(String)

    public func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .bool(let v): try c.encode(v)
        case .int(let v): try c.encode(v)
        case .double(let v): try c.encode(v)
        case .string(let v): try c.encode(v)
        }
    }
}

public struct CommandEnvelope: Encodable, Sendable {
    public let id: Int
    public let type: String
    public let cmd: String
    public let args: [String: CommandArg]?

    public init(id: Int, cmd: String, args: [String: CommandArg]? = nil) {
        self.id = id
        self.type = "cmd"
        self.cmd = cmd
        self.args = args
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(id, forKey: .id)
        try c.encode(type, forKey: .type)
        try c.encode(cmd, forKey: .cmd)
        if let args { try c.encode(args, forKey: .args) }
    }

    private enum CodingKeys: String, CodingKey { case id, type, cmd, args }
}

/// Firmware response to a command. `ok` indicates acceptance; `error` carries
/// a human string the UI surfaces VERBATIM (never translated, never softened).
public struct ResponseEnvelope: Decodable, Sendable, Equatable {
    public let id: Int
    public let ok: Bool
    public let error: String?

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(Int.self, forKey: .id) ?? 0
        ok = try c.decodeIfPresent(Bool.self, forKey: .ok) ?? false
        error = try c.decodeIfPresent(String.self, forKey: .error)
    }

    private enum CodingKeys: String, CodingKey { case id, ok, error }
}

/// Compact high-cadence numeric telemetry (`event:"fast_telemetry"`,
/// `{"v":1,"m":[…17 values…]}`). Emitted by firmware every 180-250 ms so
/// the iOS UI can refresh IMU / ToF / laser / TEC values at 4-5 Hz even
/// when the full `live_telemetry` JSON block only lands at 1 Hz.
///
/// Mirrors the host-console decoder at
/// `host-console/src/lib/controller-protocol.ts::decodeFastTelemetryPayload`.
/// Field layout matches the firmware writer at
/// `components/laser_controller/src/laser_controller_comms.c::write_fast_telemetry_json`.
public struct FastTelemetryPatch: Sendable, Equatable {
    // IMU
    public var imuValid: Bool
    public var imuFresh: Bool
    public var beamPitchDeg: Double
    public var beamRollDeg: Double
    public var beamYawDeg: Double
    // ToF
    public var tofValid: Bool
    public var tofFresh: Bool
    public var tofDistanceM: Double
    // Laser
    public var alignmentEnabled: Bool
    public var nirEnabled: Bool
    public var driverStandby: Bool
    public var loopGood: Bool
    public var ldTelemetryValid: Bool
    public var measuredCurrentA: Double
    public var driverTempC: Double
    // TEC
    public var tecTempGood: Bool
    public var tecTelemetryValid: Bool
    public var tecTempC: Double
    public var tecTempAdcVoltageV: Double
    public var tecCurrentA: Double
    public var tecVoltageV: Double
    // Safety decision
    public var allowAlignment: Bool
    public var allowNir: Bool
    public var horizonBlocked: Bool
    public var distanceBlocked: Bool
    public var lambdaDriftBlocked: Bool
    public var tecTempAdcBlocked: Bool
    // Buttons (4-bit press state only; edges and isrFireCount come from the
    // richer live_telemetry frame)
    public var stage1Pressed: Bool
    public var stage2Pressed: Bool
    public var side1Pressed: Bool
    public var side2Pressed: Bool

    /// Parse a `{"v":1,"m":[…]}` payload. Returns `nil` on shape mismatch so
    /// the transport can fall through to `.other` cleanly.
    public static func parse(payload: [String: Any]) -> FastTelemetryPatch? {
        guard let version = payload["v"] as? Int, version == 1,
              let metrics = payload["m"] as? [Any],
              metrics.count >= 17 else {
            return nil
        }
        // Numbers in JSON may decode as Int or Double — coerce via NSNumber.
        func num(_ i: Int) -> Double? {
            guard i < metrics.count else { return nil }
            if let v = metrics[i] as? Double { return v }
            if let v = metrics[i] as? Int { return Double(v) }
            if let v = metrics[i] as? NSNumber { return v.doubleValue }
            return nil
        }
        guard
            let imuFlags = num(0).map(Int.init),
            let pitchCenti = num(1),
            let rollCenti = num(2),
            let yawCenti = num(3),
            let _ = num(4), // horizon threshold — not applied to snapshot
            let tofFlags = num(5).map(Int.init),
            let distanceMm = num(6),
            let laserFlags = num(7).map(Int.init),
            let currentMa = num(8),
            let driverTempCenti = num(9),
            let tecFlags = num(10).map(Int.init),
            let tecTempCenti = num(11),
            let tecAdcMv = num(12),
            let tecCurrentCentiA = num(13),
            let tecVoltageCentiV = num(14),
            let safetyFlags = num(15).map(Int.init),
            let buttonFlags = num(16).map(Int.init)
        else {
            return nil
        }

        func bit(_ flags: Int, _ b: Int) -> Bool { ((flags >> b) & 1) == 1 }

        return FastTelemetryPatch(
            imuValid: bit(imuFlags, 0),
            imuFresh: bit(imuFlags, 1),
            beamPitchDeg: pitchCenti / 100.0,
            beamRollDeg: rollCenti / 100.0,
            beamYawDeg: yawCenti / 100.0,
            tofValid: bit(tofFlags, 0),
            tofFresh: bit(tofFlags, 1),
            tofDistanceM: distanceMm / 1000.0,
            alignmentEnabled: bit(laserFlags, 0),
            nirEnabled: bit(laserFlags, 1),
            driverStandby: bit(laserFlags, 2),
            loopGood: bit(laserFlags, 3),
            ldTelemetryValid: bit(laserFlags, 4),
            measuredCurrentA: currentMa / 1000.0,
            driverTempC: driverTempCenti / 100.0,
            tecTempGood: bit(tecFlags, 0),
            tecTelemetryValid: bit(tecFlags, 1),
            tecTempC: tecTempCenti / 100.0,
            tecTempAdcVoltageV: tecAdcMv / 1000.0,
            tecCurrentA: tecCurrentCentiA / 100.0,
            tecVoltageV: tecVoltageCentiV / 100.0,
            allowAlignment: bit(safetyFlags, 0),
            allowNir: bit(safetyFlags, 1),
            horizonBlocked: bit(safetyFlags, 2),
            distanceBlocked: bit(safetyFlags, 3),
            lambdaDriftBlocked: bit(safetyFlags, 4),
            tecTempAdcBlocked: bit(safetyFlags, 5),
            stage1Pressed: bit(buttonFlags, 0),
            stage2Pressed: bit(buttonFlags, 1),
            side1Pressed: bit(buttonFlags, 2),
            side2Pressed: bit(buttonFlags, 3)
        )
    }
}

/// Classified inbound frame. The transport hands these to the `DeviceSession`.
public enum InboundFrame: Sendable {
    case statusSnapshot(DeviceSnapshot)
    /// `presentKeys` lists the top-level blocks that were actually in the
    /// live_telemetry payload. The session layer uses this to overlay only
    /// those sub-blocks and leave the rest of the snapshot untouched.
    /// Without this set, absent blocks decode to defaults and would silently
    /// clobber live state. `rawPayload` carries the unmodified JSON so the
    /// merger can do per-field overlays inside each sub-block (critical for
    /// blocks like `tec` where an absent field would otherwise default to 0
    /// and clobber the previous value).
    case liveTelemetry(snapshot: DeviceSnapshot, presentKeys: Set<String>, rawPayload: Data)
    /// Compact high-cadence patch. Applied as a sparse overlay so the UI
    /// refreshes key numeric readouts at 4-5 Hz without waiting for the
    /// next 1 s `live_telemetry` or 5 s `status_snapshot` frame.
    case fastTelemetry(patch: FastTelemetryPatch)
    case commandResponse(ResponseEnvelope)
    case other(event: String?)
    case decodeError(String)
}

public enum FrameParser {
    /// Parse one newline-terminated JSON line. Never throws — decode errors
    /// become `.decodeError` so a single malformed frame cannot stall the
    /// transport loop.
    public static func parse(line: String) -> InboundFrame {
        guard let data = line.data(using: .utf8) else {
            return .decodeError("non-UTF-8 line dropped")
        }
        guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return .decodeError("line is not a JSON object")
        }

        let type = json["type"] as? String

        if type == "resp" {
            if let resp = try? JSONDecoder().decode(ResponseEnvelope.self, from: data) {
                return .commandResponse(resp)
            }
            return .decodeError("resp frame did not decode")
        }

        if type == "event" {
            let event = json["event"] as? String
            // status_snapshot + live_telemetry both carry a `payload` that
            // is effectively a partial DeviceSnapshot. We decode the same
            // shape for both and let the session layer distinguish.
            if event == "status_snapshot" || event == "live_telemetry" {
                if let payload = json["payload"] as? [String: Any],
                   let payloadData = try? JSONSerialization.data(withJSONObject: payload),
                   let snapshot = try? JSONDecoder().decode(DeviceSnapshot.self, from: payloadData) {
                    if event == "status_snapshot" {
                        return .statusSnapshot(snapshot)
                    }
                    return .liveTelemetry(snapshot: snapshot, presentKeys: Set(payload.keys), rawPayload: payloadData)
                }
                // If the envelope embedded the payload at the top level
                // (some firmware paths do this for the initial snapshot),
                // fall back to decoding the whole object.
                if let snapshot = try? JSONDecoder().decode(DeviceSnapshot.self, from: data) {
                    if event == "status_snapshot" {
                        return .statusSnapshot(snapshot)
                    }
                    return .liveTelemetry(snapshot: snapshot, presentKeys: Set(json.keys), rawPayload: data)
                }
                return .decodeError("could not decode \(event ?? "?") payload")
            }
            // 2026-04-20: fast_telemetry fires at 4-5 Hz with a compact
            // numeric patch. Parse it into a sparse overlay and hand it to
            // the session — key readouts (laser current, TEC temp, beam
            // angles, ToF distance) now refresh at the firmware cadence.
            if event == "fast_telemetry" {
                if let payload = json["payload"] as? [String: Any],
                   let patch = FastTelemetryPatch.parse(payload: payload) {
                    return .fastTelemetry(patch: patch)
                }
                return .other(event: event)
            }
            return .other(event: event)
        }

        // Some firmware variants emit a bare status snapshot without an
        // envelope (`status.get` response). Try DeviceSnapshot last.
        if let snapshot = try? JSONDecoder().decode(DeviceSnapshot.self, from: data) {
            return .statusSnapshot(snapshot)
        }

        return .other(event: type)
    }
}
