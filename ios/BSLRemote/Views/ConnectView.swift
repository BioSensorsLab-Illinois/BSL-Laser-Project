import SwiftUI
import BSLProtocol

/// Connect flow redesigned against the design bundle
/// (`bsl-laser-system/project/components/Screens.jsx::ConnectScreen`). Large
/// iOS title, hero copy, SSID chip with refresh, animated status line
/// (spinner / check / warn), two primary actions, a disclosure-style Manual
/// URL section, and an endpoints hint at the foot.
struct ConnectView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t
    @Environment(\.colorScheme) private var scheme

    @State private var currentSsid: String? = nil
    @State private var statusText: String = "Ready."
    @State private var phase: Phase = .idle
    @State private var manualURL: String = "ws://192.168.4.1/ws"
    @State private var manualOpen: Bool = false

    private let join = HotspotJoin()
    private let probe = MetaProbe(timeoutSeconds: 1.5)

    /// The four transport states we surface. Colors and icons branch off of
    /// this so the status strip reads at a glance even under motion.
    enum Phase: Equatable {
        case idle
        case joining
        case probing
        case connecting
        case connected
        case failed
    }

    var body: some View {
        BSLThemeProvider {
            ZStack(alignment: .top) {
                t.bg.ignoresSafeArea()
                GeometryReader { geo in
                    ScrollView {
                        VStack(alignment: .leading, spacing: 0) {
                            header
                            content
                        }
                        .frame(maxWidth: 560)
                        .frame(maxWidth: .infinity, alignment: .center)
                        .padding(.horizontal, max(14, geo.size.width * 0.02))
                    }
                }
            }
            .task { await refreshSsid() }
            .onChange(of: session.connection) { _, new in
                if case .connected = new { phase = .connected }
                if case .failed(let r) = new {
                    phase = .failed
                    statusText = "Link failed — \(r)"
                }
            }
        }
    }

    // MARK: - Slices

    private var header: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Connect")
                .font(.system(size: 32, weight: .bold))
                .tracking(-0.5)
                .foregroundStyle(t.ink)
        }
        .padding(.horizontal, 20)
        .padding(.top, 6)
        .padding(.bottom, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var content: some View {
        VStack(alignment: .leading, spacing: 0) {
            hero
                .padding(.horizontal, 18)
                .padding(.bottom, 18)

            ssidChip
                .padding(.horizontal, 18)
                .padding(.bottom, 12)

            statusStrip
                .padding(.horizontal, 18)
                .padding(.bottom, 20)

            VStack(spacing: 10) {
                BSLPrimaryButton(
                    title: "Join BSL-HTLS-Bench & connect",
                    icon: Image(systemName: "wifi"),
                    disabled: busy
                ) {
                    Task { await joinThenProbe() }
                }
                BSLSecondaryButton(
                    title: "Already joined — probe now",
                    icon: Image(systemName: "dot.radiowaves.up.forward"),
                    disabled: busy
                ) {
                    Task { await probeAndConnect() }
                }
            }
            .padding(.horizontal, 18)
            .padding(.bottom, 26)

            manualSection
                .padding(.horizontal, 18)
                .padding(.bottom, 20)

            endpointsHint
                .frame(maxWidth: .infinity)
                .padding(.horizontal, 18)
                .padding(.bottom, 32)
        }
    }

    private var hero: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("BSL laser controller")
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(t.ink)
            (Text("Join ") +
             Text("BSL-HTLS-Bench")
                .font(.system(size: 11.5, design: .monospaced))
                .foregroundStyle(t.ink) +
             Text(" or the controller's station network, then open the WebSocket."))
                .font(.system(size: 13))
                .foregroundStyle(t.muted)
                .lineSpacing(3)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var ssidChip: some View {
        HStack(spacing: 10) {
            Image(systemName: "wifi")
                .font(.system(size: 14, weight: .semibold))
                .foregroundStyle(t.ink.opacity(0.7))
            if let ssid = currentSsid {
                (Text("Joined: ") + Text(ssid).bold())
                    .font(.system(size: 13))
                    .foregroundStyle(t.ink)
                    .lineLimit(1)
            } else {
                Text("Current SSID unknown")
                    .font(.system(size: 13))
                    .foregroundStyle(t.muted)
                    .lineLimit(1)
            }
            Spacer(minLength: 8)
            Button {
                Task { await refreshSsid() }
            } label: {
                Text("Refresh")
                    .font(.system(size: 13, weight: .medium))
                    .foregroundStyle(BSL.orange)
                    .padding(.vertical, 2)
            }
            .buttonStyle(.plain)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .background(t.rowFill)
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private var statusStrip: some View {
        HStack(spacing: 10) {
            statusGlyph
            Text(statusText)
                .font(.system(size: 12.5).monospacedDigit())
                .foregroundStyle(statusColor)
                .lineLimit(2)
                .lineSpacing(2)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .frame(minHeight: 44)
        .background(t.surface2.opacity(scheme == .dark ? 0.4 : 1.0))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    @ViewBuilder private var statusGlyph: some View {
        switch phase {
        case .joining, .probing, .connecting:
            SpinnerRing()
                .frame(width: 14, height: 14)
        case .connected:
            ZStack {
                Circle().fill(BSL.ok)
                Image(systemName: "checkmark")
                    .font(.system(size: 8, weight: .heavy))
                    .foregroundStyle(.white)
            }
            .frame(width: 14, height: 14)
        case .failed:
            ZStack {
                Circle().fill(BSL.warn)
                Text("!")
                    .font(.system(size: 10, weight: .heavy))
                    .foregroundStyle(.white)
            }
            .frame(width: 14, height: 14)
        case .idle:
            Circle()
                .strokeBorder(t.dim, lineWidth: 1.5)
                .frame(width: 14, height: 14)
        }
    }

    private var statusColor: Color {
        switch phase {
        case .failed:     return BSL.warn
        case .connected:  return BSL.ok
        case .joining, .probing, .connecting: return BSL.orange
        case .idle:       return t.muted
        }
    }

    @ViewBuilder private var manualSection: some View {
        VStack(alignment: .leading, spacing: 0) {
            Button {
                withAnimation(.easeOut(duration: 0.15)) { manualOpen.toggle() }
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: "chevron.right")
                        .font(.system(size: 10, weight: .semibold))
                        .rotationEffect(.degrees(manualOpen ? 90 : 0))
                    Text("Manual")
                        .font(.system(size: 13, weight: .semibold))
                }
                .foregroundStyle(t.ink)
                .padding(.leading, 4)
                .padding(.bottom, 10)
            }
            .buttonStyle(.plain)

            if manualOpen {
                VStack(alignment: .leading, spacing: 10) {
                    Text("If the controller is on a custom IP or you're testing against the mock server on localhost, enter a WebSocket URL directly.")
                        .font(.system(size: 11.5))
                        .foregroundStyle(t.muted)
                        .lineSpacing(2)
                        .fixedSize(horizontal: false, vertical: true)
                    TextField("ws://host:port/ws", text: $manualURL)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled(true)
                        .keyboardType(.URL)
                        .font(.system(size: 13, design: .monospaced))
                        .padding(.horizontal, 12)
                        .padding(.vertical, 11)
                        .background(t.rowFill)
                        .overlay(
                            RoundedRectangle(cornerRadius: 10, style: .continuous)
                                .strokeBorder(t.border, lineWidth: 0.5)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                    BSLTertiaryButton(title: "Connect to URL", disabled: manualURL.isEmpty || busy) {
                        connectManual()
                    }
                }
                .padding(.leading, 4)
                .transition(.opacity.combined(with: .move(edge: .top)))
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var endpointsHint: some View {
        VStack(spacing: 4) {
            Text("Expected endpoints")
                .font(.system(size: 10.5))
                .tracking(0.3)
                .foregroundStyle(t.dim)
            Text("ws://192.168.4.1/ws  ·  http://192.168.4.1/meta")
                .font(.system(size: 10.5, design: .monospaced))
                .tracking(0.3)
                .foregroundStyle(t.dim)
        }
        .multilineTextAlignment(.center)
    }

    // MARK: - Actions (preserved from the old implementation)

    private var busy: Bool {
        switch phase {
        case .joining, .probing, .connecting: return true
        default: return false
        }
    }

    private func refreshSsid() async {
        phase = .idle
        statusText = "Checking current SSID…"
        currentSsid = await join.currentSsid()
        statusText = currentSsid.map { _ in "Ready." } ?? "Ready."
    }

    private func joinThenProbe() async {
        phase = .joining
        statusText = "Requesting join to BSL-HTLS-Bench…"
        do {
            try await join.joinShippedNetwork()
            currentSsid = await join.currentSsid()
            await probeAndConnect()
        } catch HotspotJoin.JoinError.notAvailable {
            phase = .failed
            statusText = "Hotspot join unavailable on this device. Join manually in iOS Settings or use the manual URL below."
        } catch HotspotJoin.JoinError.denied {
            phase = .failed
            statusText = "Join request declined."
        } catch {
            phase = .failed
            statusText = "Join failed: \(error.localizedDescription)"
        }
    }

    private func probeAndConnect() async {
        phase = .probing
        statusText = "Probing /meta on 192.168.4.1…"
        let stored = UserDefaults.standard.string(forKey: DeviceSession.lastWsUrlDefaultsKey)
        let stationIp = stored.flatMap { URL(string: $0)?.host }
        if let meta = await probe.discover(stationIp: stationIp) {
            statusText = "Resolved \(meta.mode) at \(meta.ipAddress)."
            if let url = URL(string: meta.wsUrl) {
                phase = .connecting
                session.connect(url: url)
            } else {
                phase = .failed
                statusText = "Controller reported an invalid wsUrl: \(meta.wsUrl)"
            }
        } else {
            phase = .failed
            statusText = "No controller responded on 192.168.4.1 or the last-known station IP. Check Wi-Fi."
        }
    }

    private func connectManual() {
        guard let url = URL(string: manualURL) else {
            phase = .failed
            statusText = "That is not a valid URL."
            return
        }
        phase = .connecting
        statusText = "Opening \(manualURL)…"
        session.connect(url: url)
    }
}

/// Stroked ring rotating clockwise. Used as the connect-flow spinner.
/// Self-contained so `ConnectView` stays focused on layout.
private struct SpinnerRing: View {
    @State private var angle: Double = 0

    @Environment(\.bslTheme) private var t

    var body: some View {
        Circle()
            .trim(from: 0, to: 0.75)
            .stroke(BSL.orange, style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
            .background(
                Circle().strokeBorder(t.border, lineWidth: 1.6)
            )
            .rotationEffect(.degrees(angle))
            .onAppear {
                withAnimation(.linear(duration: 0.8).repeatForever(autoreverses: false)) {
                    angle = 360
                }
            }
    }
}
