import SwiftUI
import BSLProtocol

/// First-screen flow: detect current Wi-Fi, offer one-tap join to
/// `BSL-HTLS-Bench`, probe `/meta`, open the WebSocket.
struct ConnectView: View {
    @Environment(DeviceSession.self) private var session

    @State private var currentSsid: String? = nil
    @State private var probingStatus: String = "Ready."
    @State private var probing: Bool = false
    @State private var manualURL: String = ""

    private let join = HotspotJoin()
    private let probe = MetaProbe(timeoutSeconds: 1.5)

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    hero
                    connectionStatus
                    actionButtons
                    manualSection
                }
                .padding(20)
            }
            .navigationTitle("Connect")
            .navigationBarTitleDisplayMode(.large)
            .task { await refreshSsid() }
        }
    }

    private var hero: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("BSL laser controller")
                .font(.title3.weight(.semibold))
            Text("Join `BSL-HTLS-Bench` or the controller's station network, then open the WebSocket.")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
    }

    private var connectionStatus: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: "wifi")
                Text(currentSsid.map { "Joined: \($0)" } ?? "Current SSID unknown")
                    .font(.footnote)
                Spacer()
                Button("Refresh") {
                    Task { await refreshSsid() }
                }
                .font(.footnote)
            }
            .padding(10)
            .background(Color(.secondarySystemBackground))
            .clipShape(RoundedRectangle(cornerRadius: 8))

            if probing {
                HStack(spacing: 8) {
                    ProgressView()
                    Text(probingStatus).font(.footnote)
                }
            } else {
                Text(probingStatus)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            switch session.connection {
            case .connecting(let url):
                Text("Opening \(url)…").font(.footnote).foregroundStyle(.blue)
            case .failed(let r):
                Text("Failed: \(r)").font(.footnote).foregroundStyle(.red)
            case .disconnected(let r):
                Text(r).font(.footnote).foregroundStyle(.secondary)
            case .connected:
                EmptyView()
            }
        }
    }

    private var actionButtons: some View {
        VStack(spacing: 10) {
            Button {
                Task { await joinThenProbe() }
            } label: {
                Label("Join BSL-HTLS-Bench and connect", systemImage: "antenna.radiowaves.left.and.right")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)

            Button {
                Task { await probeAndConnect() }
            } label: {
                Label("Already joined — probe now", systemImage: "dot.radiowaves.up.forward")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
        }
    }

    private var manualSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Manual")
                .font(.subheadline.weight(.semibold))
            Text("If the controller is on a custom IP or you are testing against the mock server on localhost, enter a WebSocket URL directly.")
                .font(.footnote)
                .foregroundStyle(.secondary)
            TextField("ws://host:port/ws", text: $manualURL)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled(true)
                .keyboardType(.URL)
                .padding(10)
                .background(Color(.secondarySystemBackground))
                .clipShape(RoundedRectangle(cornerRadius: 8))
            Button("Connect to URL") {
                connectManual()
            }
            .disabled(manualURL.isEmpty)
        }
    }

    // MARK: - Actions

    private func refreshSsid() async {
        currentSsid = await join.currentSsid()
    }

    private func joinThenProbe() async {
        probing = true
        probingStatus = "Requesting join…"
        defer { probing = false }
        do {
            try await join.joinShippedNetwork()
            currentSsid = await join.currentSsid()
            await probeAndConnect()
        } catch HotspotJoin.JoinError.notAvailable {
            probingStatus = "Hotspot join unavailable on this device. Join manually in iOS Settings or use the manual URL below."
        } catch HotspotJoin.JoinError.denied {
            probingStatus = "Join request declined."
        } catch {
            probingStatus = "Join failed: \(error.localizedDescription)"
        }
    }

    private func probeAndConnect() async {
        probing = true
        probingStatus = "Probing /meta on 192.168.4.1…"
        defer { probing = false }
        let stored = UserDefaults.standard.string(forKey: DeviceSession.lastWsUrlDefaultsKey)
        let stationIp = stored.flatMap { URL(string: $0)?.host }
        if let meta = await probe.discover(stationIp: stationIp) {
            probingStatus = "Resolved \(meta.mode) at \(meta.ipAddress)."
            if let url = URL(string: meta.wsUrl) {
                session.connect(url: url)
            } else {
                probingStatus = "Controller reported an invalid wsUrl: \(meta.wsUrl)"
            }
        } else {
            probingStatus = "No controller responded on 192.168.4.1 or the last-known station IP. Check Wi-Fi."
        }
    }

    private func connectManual() {
        guard let url = URL(string: manualURL) else {
            probingStatus = "That is not a valid URL."
            return
        }
        session.connect(url: url)
    }
}
