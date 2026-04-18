import SwiftUI
import BSLProtocol

struct SettingsView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if auth.isUnlocked {
                    unlockedBody
                } else {
                    PinGate()
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
                if auth.isUnlocked {
                    ToolbarItem(placement: .topBarLeading) {
                        Button("Lock") { auth.lock() }
                    }
                }
            }
        }
    }

    private var unlockedBody: some View {
        List {
            Section {
                NavigationLink {
                    SafetyParametersForm().environment(session)
                } label: {
                    Label("Safety parameters", systemImage: "slider.horizontal.3")
                }
                NavigationLink {
                    InterlockToggles().environment(session)
                } label: {
                    Label("Active interlocks", systemImage: "checklist")
                }
            } header: {
                Text("Firmware policy")
            } footer: {
                Text("Writes go to `integrate.set_safety`. Firmware rejects any edit while the beam is active. Persisted to NVS automatically.")
            }

            Section {
                NavigationLink {
                    ServiceModeSection().environment(session)
                } label: {
                    Label("Service mode", systemImage: "wrench.and.screwdriver")
                }
            } header: {
                Text("Advanced")
            } footer: {
                Text("Service mode is the master override that disables every interlock while active. Use only with the controller safed.")
            }

            Section {
                HStack {
                    Text("Controller")
                    Spacer()
                    Text(session.snapshot.session.state == .unknown ? "—" : String(describing: session.snapshot.session.state))
                        .foregroundStyle(.secondary)
                }
                HStack {
                    Text("NIR enabled")
                    Spacer()
                    Text(session.snapshot.laser.nirEnabled ? "yes" : "no")
                        .foregroundStyle(.secondary)
                }
            } header: {
                Text("Context")
            } footer: {
                Text("Safety edits are blocked by the firmware while NIR or alignment is actively emitting.")
            }
        }
    }
}

private struct ServiceModeSection: View {
    @Environment(DeviceSession.self) private var session
    @State private var confirmEnter: Bool = false
    @State private var confirmExit: Bool = false

    var body: some View {
        List {
            Section {
                Button {
                    confirmEnter = true
                } label: {
                    Label("Enter service mode", systemImage: "wrench")
                }
                Button {
                    confirmExit = true
                } label: {
                    Label("Exit service mode", systemImage: "arrow.uturn.left")
                }
            } footer: {
                Text("`enter_service_mode` relaxes the interlock master gate. The firmware keeps beam-enable GPIOs safe while in service mode but accepts bench-only commands. Exit returns to the normal operator path.")
            }
        }
        .navigationTitle("Service mode")
        .navigationBarTitleDisplayMode(.inline)
        .alert("Enter service mode?", isPresented: $confirmEnter) {
            Button("Cancel", role: .cancel) {}
            Button("Enter", role: .destructive) {
                Task {
                    _ = await session.sendCommand("enter_service_mode")
                }
            }
        } message: {
            Text("Service mode is the master interlock override. Only enter with the laser safed and with intent to perform bench work.")
        }
        .alert("Exit service mode?", isPresented: $confirmExit) {
            Button("Cancel", role: .cancel) {}
            Button("Exit", role: .destructive) {
                Task {
                    _ = await session.sendCommand("exit_service_mode")
                }
            }
        } message: {
            Text("Exits service mode and restores the normal operator gating.")
        }
    }
}
