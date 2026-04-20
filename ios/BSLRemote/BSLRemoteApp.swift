import SwiftUI
import BSLProtocol

@main
struct BSLRemoteApp: App {
    @State private var session = DeviceSession()
    @State private var auth = AuthGate()
    @State private var appearance = AppearanceStore()
    @State private var layout = LayoutPreference()
    @State private var welcome = WelcomeFlowState()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(session)
                .environment(auth)
                .environment(appearance)
                .environment(layout)
                .environment(welcome)
                .preferredColorScheme(appearance.overrideScheme)
                .onChange(of: scenePhase) { _, phase in
                    switch phase {
                    case .background, .inactive:
                        auth.markBackgrounded()
                    case .active:
                        auth.handleForeground()
                    @unknown default:
                        break
                    }
                }
        }
    }
}

/// Three-screen router. Welcome → Connect → Main.
///   - Welcome is shown on every cold launch until the user taps Continue.
///     The flag lives in `WelcomeFlowState` which is a process-lifetime
///     `@Observable` — warm foreground resumes do NOT re-present Welcome.
///   - Connect is shown until DeviceSession has ever seen a `.connected`
///     state for this URL (same rule as before the redesign).
///   - Main is persistent once seen; transient drops are handled inline
///     inside MainView.
struct RootView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(WelcomeFlowState.self) private var welcome

    var body: some View {
        if !welcome.acknowledged {
            WelcomeView(onContinue: { welcome.acknowledged = true })
                .transition(.opacity)
        } else if session.hasEverConnected {
            MainView()
                .transition(.opacity)
        } else {
            ConnectView()
                .transition(.opacity)
        }
    }
}
