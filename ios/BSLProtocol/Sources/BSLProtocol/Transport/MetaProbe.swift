import Foundation

/// Response body of `GET /meta` — see
/// `components/laser_controller/src/laser_controller_wireless.c:789-813`.
public struct MetaResponse: Decodable, Sendable, Equatable {
    public let mode: String
    public let ssid: String
    public let stationSsid: String
    public let wsUrl: String
    public let ipAddress: String

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        mode = try c.decodeIfPresent(String.self, forKey: .mode) ?? ""
        ssid = try c.decodeIfPresent(String.self, forKey: .ssid) ?? ""
        stationSsid = try c.decodeIfPresent(String.self, forKey: .stationSsid) ?? ""
        wsUrl = try c.decodeIfPresent(String.self, forKey: .wsUrl) ?? ""
        ipAddress = try c.decodeIfPresent(String.self, forKey: .ipAddress) ?? ""
    }

    private enum CodingKeys: String, CodingKey {
        case mode, ssid, stationSsid, wsUrl, ipAddress
    }
}

/// Probes `GET http://<ip>/meta` to discover the controller's WebSocket URL.
/// Timeout is hard-capped at 1 s so a dead IP never stalls the Connect flow.
public actor MetaProbe {
    public enum ProbeError: LocalizedError, Sendable {
        case badURL
        case timeout
        case httpStatus(Int)
        case decode(String)

        public var errorDescription: String? {
            switch self {
            case .badURL: return "The controller address is not a valid URL."
            case .timeout: return "The controller did not respond within 1 s."
            case .httpStatus(let code): return "Unexpected HTTP status \(code) from /meta."
            case .decode(let reason): return "The /meta response did not decode: \(reason)"
            }
        }
    }

    private let session: URLSession

    public init(timeoutSeconds: Double = 1.0) {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = timeoutSeconds
        config.timeoutIntervalForResource = timeoutSeconds
        config.waitsForConnectivity = false
        config.allowsCellularAccess = false
        config.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        self.session = URLSession(configuration: config)
    }

    public func probe(ip: String) async throws -> MetaResponse {
        guard let url = URL(string: "http://\(ip)/meta") else {
            throw ProbeError.badURL
        }
        return try await probe(url: url)
    }

    public func probe(url: URL) async throws -> MetaResponse {
        let (data, response) = try await session.data(from: url)
        guard let http = response as? HTTPURLResponse else {
            throw ProbeError.decode("no HTTP response")
        }
        guard http.statusCode == 200 else {
            throw ProbeError.httpStatus(http.statusCode)
        }
        do {
            return try JSONDecoder().decode(MetaResponse.self, from: data)
        } catch {
            throw ProbeError.decode(String(describing: error))
        }
    }

    /// Probe the SoftAP address and, in parallel, an optional station IP.
    /// Returns the first success, preferring station when both succeed AND
    /// the station reports `mode == "station"`.
    public func discover(stationIp: String?) async -> MetaResponse? {
        async let apAttempt: MetaResponse? = try? probe(ip: "192.168.4.1")
        async let staAttempt: MetaResponse? = {
            guard let ip = stationIp else { return nil }
            return try? await probe(ip: ip)
        }()

        let ap = await apAttempt
        let sta = await staAttempt

        if let sta, sta.mode == "station" { return sta }
        if let ap { return ap }
        return sta
    }
}
