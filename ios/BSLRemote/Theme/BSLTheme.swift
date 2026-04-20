import SwiftUI

/// Theme context — adapts to the current `colorScheme` and exposes semantic
/// color accessors used by the redesigned components.
struct BSLTheme {
    let dark: Bool

    init(_ scheme: ColorScheme) { self.dark = scheme == .dark }

    var bg: Color            { dark ? BSL.Dark.bg : BSL.Light.bg }
    var surface: Color       { dark ? BSL.Dark.surface : BSL.Light.surface }
    var surface2: Color      { dark ? BSL.Dark.surface2 : BSL.Light.surface2 }
    var border: Color        { dark ? BSL.Dark.border : BSL.Light.border }
    var borderStrong: Color  { dark ? BSL.Dark.borderStrong : BSL.Light.borderStrong }
    var ink: Color           { dark ? BSL.Dark.ink : BSL.Light.ink }
    var muted: Color         { dark ? BSL.Dark.muted : BSL.Light.muted }
    var dim: Color           { dark ? BSL.Dark.dim : BSL.Light.dim }
    var trackFill: Color     { dark ? Color.white.opacity(0.08) : Color(red: 0.934, green: 0.945, blue: 0.964) }

    /// Subtle row-fill for inset grouped lists (a touch lighter in dark than
    /// `surface`). Matches `SettingsStack.jsx:18` — `rgba(255,255,255,0.03)`
    /// in dark, pure white in light.
    var rowFill: Color { dark ? Color.white.opacity(0.03) : Color.white }
}

/// Convenience to pull the current `BSLTheme` from the environment without
/// passing `colorScheme` manually at every call site.
struct BSLThemeKey: EnvironmentKey {
    static let defaultValue = BSLTheme(.light)
}
extension EnvironmentValues {
    var bslTheme: BSLTheme {
        get { self[BSLThemeKey.self] }
        set { self[BSLThemeKey.self] = newValue }
    }
}

struct BSLThemeProvider<Content: View>: View {
    @Environment(\.colorScheme) private var scheme
    @ViewBuilder var content: () -> Content
    var body: some View {
        content().environment(\.bslTheme, BSLTheme(scheme))
    }
}
