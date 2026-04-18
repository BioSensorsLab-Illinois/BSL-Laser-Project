import Foundation

#if os(iOS)
import NetworkExtension
#endif

/// Proposes a one-tap join to the controller's SoftAP using
/// `NEHotspotConfiguration`. Shipped credentials match
/// `components/laser_controller/src/laser_controller_wireless.c:25-26`.
///
/// Requires the `com.apple.developer.networking.HotspotConfiguration`
/// entitlement on iOS. On macOS (used for command-line SPM tests), every
/// method short-circuits with `.notAvailable` — callers fall back to the
/// manual-URL entry in `ConnectView`.
public struct HotspotJoin: Sendable {
    public struct Credentials: Sendable, Equatable {
        public let ssid: String
        public let password: String
        public static let shipped = Credentials(ssid: "BSL-HTLS-Bench", password: "bslbench2026")
    }

    public enum JoinError: LocalizedError, Sendable {
        case notAvailable
        case denied
        case system(String)

        public var errorDescription: String? {
            switch self {
            case .notAvailable:
                return "Hotspot configuration is not available on this device or build."
            case .denied:
                return "The user declined to join the controller network."
            case .system(let message):
                return message
            }
        }
    }

    public init() {}

    /// Returns the current Wi-Fi SSID if iOS exposes it (requires the
    /// HotspotConfiguration entitlement granted and the user having joined
    /// at least once in the past). Returns nil otherwise.
    public func currentSsid() async -> String? {
        #if os(iOS) && !targetEnvironment(simulator)
        if #available(iOS 14.0, *) {
            return await withCheckedContinuation { cont in
                NEHotspotNetwork.fetchCurrent { network in
                    cont.resume(returning: network?.ssid)
                }
            }
        }
        return nil
        #else
        return nil
        #endif
    }

    /// Offers the join sheet. Resolves when the user either accepts (iOS
    /// returns nil or `.alreadyAssociated`) or denies (iOS returns
    /// `.userDenied`).
    public func joinShippedNetwork() async throws {
        #if os(iOS) && !targetEnvironment(simulator)
        let creds = Credentials.shipped
        let config = NEHotspotConfiguration(ssid: creds.ssid, passphrase: creds.password, isWEP: false)
        config.joinOnce = false
        config.lifeTimeInDays = 365
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            NEHotspotConfigurationManager.shared.apply(config) { error in
                if let error = error as NSError? {
                    if error.domain == NEHotspotConfigurationErrorDomain,
                       error.code == NEHotspotConfigurationError.alreadyAssociated.rawValue {
                        cont.resume()
                        return
                    }
                    if error.domain == NEHotspotConfigurationErrorDomain,
                       error.code == NEHotspotConfigurationError.userDenied.rawValue {
                        cont.resume(throwing: JoinError.denied)
                        return
                    }
                    cont.resume(throwing: JoinError.system(error.localizedDescription))
                    return
                }
                cont.resume()
            }
        }
        #else
        throw JoinError.notAvailable
        #endif
    }
}
