import SwiftUI

/// Operator-selectable appearance preference. `system` follows `UITraitCollection`
/// (the OS-level Light/Dark toggle), `light` and `dark` force the palette
/// regardless of system. Persisted in `UserDefaults` so the choice survives
/// relaunch. Mirrors the design's three-position picker in
/// `bsl-laser-system/project/components/SettingsStack.jsx` under the
/// "Appearance" group, extended from binary dark/OR to a three-way.
enum AppearanceMode: String, CaseIterable, Identifiable, Sendable {
    case system
    case light
    case dark

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .system: return "Auto"
        case .light:  return "Light"
        case .dark:   return "OR / Dark"
        }
    }

    /// SwiftUI `ColorScheme?` override; `nil` means "follow the system".
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light:  return .light
        case .dark:   return .dark
        }
    }
}

/// Persisted appearance state. Exposed via `@Environment(AppearanceStore.self)`
/// and bound to a `ColorScheme?` override on the root view.
@MainActor
@Observable
final class AppearanceStore {

    static let defaultsKey = "BSLRemote.appearance"

    var mode: AppearanceMode {
        didSet {
            UserDefaults.standard.set(mode.rawValue, forKey: Self.defaultsKey)
        }
    }

    init() {
        let stored = UserDefaults.standard.string(forKey: Self.defaultsKey)
            .flatMap(AppearanceMode.init(rawValue:))
        self.mode = stored ?? .system
    }

    var overrideScheme: ColorScheme? { mode.colorScheme }
}

/// The operator-chosen main-page layout variation (1..5). Mirrors the
/// prototype's Appearance → Layout picker. Persisted in `UserDefaults`.
@MainActor
@Observable
final class LayoutPreference {

    static let defaultsKey = "BSLRemote.layoutVariation"
    static let allowed: ClosedRange<Int> = 1...5

    var variation: Int {
        didSet {
            let clamped = min(max(variation, Self.allowed.lowerBound), Self.allowed.upperBound)
            if clamped != variation {
                variation = clamped
                return
            }
            UserDefaults.standard.set(clamped, forKey: Self.defaultsKey)
        }
    }

    init() {
        let stored = UserDefaults.standard.integer(forKey: Self.defaultsKey)
        self.variation = Self.allowed.contains(stored) ? stored : 1
    }
}

/// One-shot Welcome-flow tracker. The user confirmed (2026-04-19) that the
/// Welcome screen MUST be shown on every cold launch — future revisions will
/// host a safety / operating-instructions animation there, and consent
/// needs to be re-acknowledged after a restart. We keep it in an observable
/// that only lives for the process lifetime so that a warm foreground-resume
/// does NOT re-present Welcome (that would interrupt an active case).
@MainActor
@Observable
final class WelcomeFlowState {
    var acknowledged: Bool = false
}
