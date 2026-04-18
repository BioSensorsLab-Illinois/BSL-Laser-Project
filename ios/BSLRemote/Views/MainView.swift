import SwiftUI
import BSLProtocol

struct MainView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth
    @State private var showingSettings = false
    @State private var showingWavelength = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if session.snapshot.bench.usbDebugMock.active {
                        UsbMockBanner(status: session.snapshot.bench.usbDebugMock)
                    }
                    if session.isStale {
                        StaleBanner()
                    }
                    if let firmwareError = session.lastFirmwareError {
                        FirmwareErrorBanner(message: firmwareError) {
                            session.lastFirmwareError = nil
                        }
                    }

                    PowerCard()
                    LedCard()
                    WavelengthCard(onTap: { showingWavelength = true })
                    TemperatureCard()
                    PowerStatusCard()
                    MasterCautionCard()
                    MasterWarningCard()
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
            .background(Color(.systemBackground))
            .navigationTitle("BSL Controller")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    ConnectionDot(connection: session.connection)
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        showingSettings = true
                    } label: {
                        Image(systemName: auth.isUnlocked ? "lock.open" : "lock")
                    }
                    .accessibilityLabel("Settings")
                }
            }
            .sheet(isPresented: $showingSettings) {
                SettingsView()
                    .environment(session)
                    .environment(auth)
            }
            .sheet(isPresented: $showingWavelength) {
                WavelengthEditorSheet()
                    .environment(session)
                    .environment(auth)
            }
        }
    }
}

private struct ConnectionDot: View {
    let connection: DeviceSession.Connection

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(color)
                .frame(width: 8, height: 8)
            Text(label)
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }

    private var color: Color {
        switch connection {
        case .connected: return .green
        case .connecting: return .yellow
        case .disconnected, .failed: return .red
        }
    }

    private var label: String {
        switch connection {
        case .connected: return "live"
        case .connecting: return "connecting"
        case .disconnected(let reason): return reason
        case .failed(let reason): return reason
        }
    }
}

struct StaleBanner: View {
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "clock.arrow.circlepath")
            Text("Telemetry stale — reconnecting")
                .font(.footnote)
            Spacer()
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.yellow.opacity(0.15))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.yellow.opacity(0.6)))
        .foregroundStyle(.yellow)
    }
}

struct FirmwareErrorBanner: View {
    let message: String
    let onDismiss: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.red)
            VStack(alignment: .leading, spacing: 4) {
                Text("Firmware rejected")
                    .font(.footnote.weight(.semibold))
                Text(message)
                    .font(.footnote)
                    .foregroundStyle(.primary)
                    .multilineTextAlignment(.leading)
            }
            Spacer()
            Button(action: onDismiss) {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.secondary)
            }
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.red.opacity(0.12))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.red.opacity(0.6)))
    }
}
