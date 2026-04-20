import Foundation
import CryptoKit
import Observation

/// Client-side PIN gate for the settings screen. The PIN is compile-time
/// shared — stored in this file as a SHA-256 hash plus a per-build salt, so
/// the plaintext never appears in the IPA. Any disassembler can still
/// brute-force a short PIN against the hash, so this is a "not casual
/// observers" gate, not a security boundary. The firmware remains the
/// authoritative gate for destructive safety edits
/// (`components/laser_controller/src/laser_controller_comms.c:5754`).
///
/// To rotate the PIN, regenerate `expectedHashHex` + `salt` at build time.
/// The current shipping PIN is the four-digit `2012` (user directive
/// 2026-04-19) — change before distribution.
@MainActor
@Observable
final class AuthGate {

    /// When false, the settings screen shows `PinGate`.
    private(set) var isUnlocked: Bool = false

    /// Timestamp of the most recent background-inactive transition. When
    /// more than `backgroundLockSeconds` old, the next foreground re-locks.
    private var backgroundedAt: Date?

    private let backgroundLockSeconds: TimeInterval = 60

    /// Per-build salt. Replace in custom builds.
    private let salt = "BSL-iOS-build-2026-04-18"

    /// SHA-256 hex of the shipping PIN ("2012") concatenated with the salt.
    /// Compute: `printf '%s' "2012BSL-iOS-build-2026-04-18" | shasum -a 256`
    private let expectedHashHex =
        "6a46ec00262157256d1812d022bca65871ff01a247507685f86966029a680336"

    func tryUnlock(candidate: String) -> Bool {
        let combined = candidate + salt
        let digest = SHA256.hash(data: Data(combined.utf8))
        let hex = digest.map { String(format: "%02x", $0) }.joined()
        if hex == expectedHashHex {
            isUnlocked = true
            return true
        }
        return false
    }

    func lock() {
        isUnlocked = false
        backgroundedAt = nil
    }

    /// Called when the app moves to background. Starts the 60 s auto-lock
    /// countdown.
    func markBackgrounded() {
        backgroundedAt = Date()
    }

    /// Called when the app returns to foreground. Locks if enough time has
    /// elapsed in the background.
    func handleForeground() {
        if let since = backgroundedAt,
           Date().timeIntervalSince(since) >= backgroundLockSeconds {
            lock()
        }
        backgroundedAt = nil
    }
}
