import SwiftUI
import BSLProtocol

/// Redesigned V1 "Clinical Dashboard" layout. Mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:730-901`.
///
/// Three fixed regions:
///   - StatusHeader (BioSensors Lab · HHLS · state + pills + gear)
///   - FaultBanner (only when `hasMasterWarning` / `hasMasterCaution`)
///   - Scrollable middle: NirHeroCard · LedStripCard · Wavelength+TEC grid · PowerRail
///   - Sticky DeployBar (HOLD TO ARM / STOP · ARMED)
///
/// Deep-dive safety cards live behind the banners (tap to present).
struct MainView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth

    @State private var showingSettings: Bool = false
    @State private var showingWavelength: Bool = false
    @State private var showingWarning: Bool = false
    @State private var showingCaution: Bool = false
    @State private var sessionTicker: Int = 0

    #if DEBUG
    @State private var tweaks = DebugTweaks()
    @State private var tweaksPresented: Bool = false
    #endif

    private var snap: DeviceSnapshot { session.snapshot }

    var body: some View {
        BSLThemeProvider {
            content
                .onReceive(Timer.publish(every: 1, on: .main, in: .common).autoconnect()) { _ in
                    sessionTicker = snap.session.uptimeSeconds
                }
        }
    }

    @ViewBuilder private var content: some View {
        ZStack(alignment: .bottom) {
            VStack(spacing: 0) {
                StatusHeader(
                    connected: session.isConnected,
                    mode: snap.laserMode,
                    hasFault: snap.hasMasterWarning,
                    hasCaution: snap.hasMasterCaution,
                    sessionSeconds: sessionTicker,
                    onOpenSettings: { showingSettings = true }
                )

                banners

                ScrollView {
                    VStack(spacing: 12) {
                        if snap.bench.usbDebugMock.active {
                            UsbMockBanner(status: snap.bench.usbDebugMock)
                        }
                        if !session.isConnected {
                            InlineNote(text: reconnectNote, tone: .caution)
                        } else if session.isStale {
                            InlineNote(text: "Telemetry stale — reconnecting…", tone: .caution)
                        }
                        if let firmwareError = session.lastFirmwareError {
                            InlineNote(text: "Firmware rejected: \(firmwareError)", tone: .warn) {
                                session.lastFirmwareError = nil
                            }
                        }

                        NirHeroCard()
                        LedStripCard()
                        HStack(spacing: 10) {
                            WavelengthMiniCard(onTap: { showingWavelength = true })
                            TemperatureMiniCard()
                        }
                        PowerRailCard()
                        Color.clear.frame(height: 110) // breathing room above sticky bar
                    }
                    .padding(.horizontal, 14)
                    .padding(.top, 2)
                }
                .scrollBounceBehavior(.basedOnSize)
            }

            DeployBar(
                mode: snap.laserMode,
                connected: session.isConnected,
                hasWarning: snap.hasMasterWarning
            )

            #if DEBUG
            if tweaksPresented {
                TweaksPanel(tweaks: tweaks)
                    .padding(.bottom, 120).padding(.trailing, 14)
                    .frame(maxWidth: .infinity, alignment: .trailing)
            }
            #endif
        }
        #if DEBUG
        .overlay(alignment: .topTrailing) {
            Button {
                withAnimation(.easeOut(duration: 0.15)) { tweaksPresented.toggle() }
            } label: {
                Image(systemName: "slider.horizontal.3")
                    .font(.system(size: 11, weight: .semibold))
                    .padding(6)
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Debug tweaks")
            .offset(x: -8, y: 60)
        }
        #endif
        .sheet(isPresented: $showingSettings) {
            SettingsView()
                .environment(session)
                .environment(auth)
        }
        .sheet(isPresented: $showingWavelength) {
            WavelengthEditorSheet()
                .environment(session)
                .environment(auth)
                .presentationDetents([.large])
        }
        .sheet(isPresented: $showingCaution) {
            MasterCautionSheet()
                .environment(session)
                .presentationDetents([.medium, .large])
        }
        .sheet(isPresented: $showingWarning) {
            MasterWarningSheet()
                .environment(session)
                .environment(auth)
                .presentationDetents([.medium, .large])
        }
    }

    @ViewBuilder private var banners: some View {
        if snap.hasMasterWarning {
            FaultBanner(
                kind: .warning,
                title: "MASTER WARNING",
                detail: warningDetail,
                onTap: { showingWarning = true }
            )
            .padding(.horizontal, 14).padding(.bottom, 6)
        } else if snap.hasMasterCaution {
            FaultBanner(
                kind: .caution,
                title: "MASTER CAUTION",
                detail: cautionDetail,
                onTap: { showingCaution = true }
            )
            .padding(.horizontal, 14).padding(.bottom, 6)
        }
    }

    private var warningDetail: String {
        let code = snap.fault.latchedCode
        if !code.isEmpty, code != "none" {
            return "\(code) — service required"
        }
        return "Latched fault · service required"
    }

    private var reconnectNote: String {
        switch session.connection {
        case .connecting: return "Reconnecting to the controller…"
        case .disconnected(let reason): return "Link lost — reconnecting (\(reason))"
        case .failed(let reason): return "Link failed — retrying (\(reason))"
        case .connected: return "Reconnecting…"
        }
    }

    private var cautionDetail: String {
        if snap.safety.tecTempAdcBlocked { return "TEC ADC trip · auto-recoverable" }
        if snap.safety.distanceBlocked { return "ToF distance · out of range" }
        if snap.safety.horizonBlocked { return "Horizon · out of cone" }
        if snap.safety.lambdaDriftBlocked { return "Lambda drift · over limit" }
        if !snap.imu.valid || !snap.imu.fresh { return "IMU · invalid or stale" }
        if !snap.tof.valid || !snap.tof.fresh { return "ToF · invalid or stale" }
        return "Auto-recoverable interlock active"
    }
}

/// Sheet wrapper that re-uses the existing MasterCautionCard for full detail.
private struct MasterCautionSheet: View {
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            ScrollView { MasterCautionCard().padding(16) }
                .navigationTitle("Master Caution")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button("Done") { dismiss() }
                    }
                }
        }
    }
}

/// Sheet wrapper for the existing MasterWarningCard (includes Clear Faults
/// action with PIN gate).
private struct MasterWarningSheet: View {
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            ScrollView { MasterWarningCard().padding(16) }
                .navigationTitle("Master Warning")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button("Done") { dismiss() }
                    }
                }
        }
    }
}

/// Compact inline note. Keeps stale/firmware-error banners readable in the
/// new scroll region.
private struct InlineNote: View {
    enum Tone { case caution, warn }
    let text: String
    let tone: Tone
    var onDismiss: (() -> Void)? = nil

    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: tone == .warn ? "exclamationmark.triangle.fill" : "clock.arrow.circlepath")
                .foregroundStyle(tone == .warn ? BSL.warn : BSL.caution)
            Text(text)
                .font(.system(size: 12))
                .foregroundStyle(t.ink)
                .lineLimit(2)
            Spacer()
            if let onDismiss {
                Button {
                    onDismiss()
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundStyle(t.dim)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(10)
        .background((tone == .warn ? BSL.warnSoft : BSL.cautionSoft).opacity(t.dark ? 0.25 : 1.0))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

// `UsbMockBanner` lives in Views/UsbMockBanner.swift — rendered verbatim.

