import SwiftUI
import BSLProtocol

@main
struct BSLRemoteApp: App {
    @State private var session = DeviceSession()
    @State private var auth = AuthGate()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(session)
                .environment(auth)
                .preferredColorScheme(.dark)
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

struct RootView: View {
    @Environment(DeviceSession.self) private var session

    var body: some View {
        if case .connected = session.connection {
            MainView()
        } else {
            ConnectView()
        }
    }
}
