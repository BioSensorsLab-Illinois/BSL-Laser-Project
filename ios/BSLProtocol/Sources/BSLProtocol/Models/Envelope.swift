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

/// Classified inbound frame. The transport hands these to the `DeviceSession`.
public enum InboundFrame: Sendable {
    case statusSnapshot(DeviceSnapshot)
    /// `presentKeys` lists the top-level blocks that were actually in the
    /// live_telemetry payload. The session layer uses this to overlay only
    /// those sub-blocks and leave the rest of the snapshot untouched.
    /// Without this set, absent blocks decode to defaults and would silently
    /// clobber live state.
    case liveTelemetry(snapshot: DeviceSnapshot, presentKeys: Set<String>)
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
                    return .liveTelemetry(snapshot: snapshot, presentKeys: Set(payload.keys))
                }
                // If the envelope embedded the payload at the top level
                // (some firmware paths do this for the initial snapshot),
                // fall back to decoding the whole object.
                if let snapshot = try? JSONDecoder().decode(DeviceSnapshot.self, from: data) {
                    if event == "status_snapshot" {
                        return .statusSnapshot(snapshot)
                    }
                    return .liveTelemetry(snapshot: snapshot, presentKeys: Set(json.keys))
                }
                return .decodeError("could not decode \(event ?? "?") payload")
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
