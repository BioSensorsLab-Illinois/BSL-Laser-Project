import SwiftUI
import BSLProtocol

/// Full-screen custom-chrome Settings stack mirroring the design bundle's
/// `SettingsStack.jsx`. PIN-gated (via `AuthGate`), then four groups:
///   - Firmware policy (Safety parameters, Active interlocks)
///   - Advanced (Service mode)
///   - Context (Controller, Beam state, Wavelength, Firmware)
///   - Appearance (System / Light / Dark; Layout picker 1..5)
///
/// The "Demo state" group from the prototype is deliberately omitted —
/// user directive 2026-04-19. The Appearance section's picker is real and
/// persists to UserDefaults via `AppearanceStore` / `LayoutPreference`.
struct SettingsView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth
    @Environment(AppearanceStore.self) private var appearance
    @Environment(LayoutPreference.self) private var layout
    @Environment(\.dismiss) private var dismiss
    @Environment(\.bslTheme) private var t

    @State private var page: Page = .root

    private enum Page { case root, safety, interlocks, service }

    var body: some View {
        BSLThemeProvider {
            ZStack {
                t.bg.ignoresSafeArea()
                if !auth.isUnlocked {
                    PinGate()
                        .environment(auth)
                } else {
                    switch page {
                    case .root:       root
                    case .safety:     SafetyParametersForm().environment(session)
                    case .interlocks: InterlockToggles().environment(session)
                    case .service:    ServiceModeView().environment(session)
                    }
                }
            }
        }
    }

    // MARK: - Root

    @ViewBuilder private var root: some View {
        VStack(spacing: 0) {
            BSLNavBar(
                title: "Settings",
                backLabel: "Done",
                onBack: { dismiss() }
            ) {
                Button {
                    auth.lock()
                } label: {
                    Text("Lock")
                        .font(.system(size: 15))
                        .foregroundStyle(BSL.orange)
                }
                .buttonStyle(.plain)
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    firmwarePolicyGroup
                    advancedGroup
                    contextGroup
                    appearanceGroup
                    footerMeta
                }
                .padding(.horizontal, 18)
                .padding(.top, 18)
                .padding(.bottom, 24)
                .frame(maxWidth: 560)
                .frame(maxWidth: .infinity, alignment: .center)
            }
        }
    }

    /// 2026-04-20: safety-policy pages are locked off entirely when the
    /// laser is armed or lasing. The firmware already rejects writes in
    /// those states; the user reported wanting the page itself blocked so
    /// accidental taps cannot surface a firmware-rejection banner mid-use.
    /// Falls back to inactive state when the laser is disarmed.
    private var safetyLocked: Bool {
        session.snapshot.laserMode != .disarmed
    }

    private var safetyLockSublabel: String {
        "Locked while laser is " + (session.snapshot.laserMode == .lasing ? "lasing" : "armed") + " · disarm to edit"
    }

    private var firmwarePolicyGroup: some View {
        BSLListGroup(
            label: "Firmware policy",
            footer: safetyLocked
                ? "Safety and interlock edits are disabled while the laser is armed or lasing. Disarm to edit."
                : "Writes go to integrate.set_safety. Firmware rejects any edit while the beam is active. Persisted to NVS automatically."
        ) {
            BSLListRow(
                icon: Image(systemName: "slider.horizontal.3"),
                iconTone: safetyLocked ? .subtle : .navy,
                label: "Safety parameters",
                sublabel: safetyLocked ? safetyLockSublabel : "Horizon · ToF · TEC · currents · drift",
                action: safetyLocked ? nil : { withAnimation(.easeOut(duration: 0.18)) { page = .safety } }
            )
            BSLListRow(
                icon: Image(systemName: "checklist"),
                iconTone: safetyLocked ? .subtle : .navy,
                label: "Active interlocks",
                sublabel: safetyLocked ? safetyLockSublabel : "Ten safety gates · ToF low-bound",
                isLast: true,
                action: safetyLocked ? nil : { withAnimation(.easeOut(duration: 0.18)) { page = .interlocks } }
            )
        }
    }

    private var advancedGroup: some View {
        let serviceOn = session.snapshot.session.state == .serviceMode
        return BSLListGroup(
            label: "Advanced",
            footer: "Service mode is the master override that disables every interlock while active. Use only with the controller safed."
        ) {
            BSLListRow(
                icon: Image(systemName: "wrench.adjustable"),
                iconTone: serviceOn ? .brand : .navy,
                label: "Service mode",
                sublabel: serviceOn ? "Active · interlocks bypassed" : "Disabled · interlocks engaged",
                isLast: true,
                action: { withAnimation(.easeOut(duration: 0.18)) { page = .service } }
            )
        }
    }

    private var contextGroup: some View {
        BSLListGroup(
            label: "Context",
            footer: "Safety edits are blocked by the firmware while NIR or alignment is actively emitting."
        ) {
            BSLListRow(label: "Controller") {
                Text(controllerLine)
                    .font(.system(size: 13, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.muted)
            }
            BSLListRow(label: "Beam state") {
                beamStateText
            }
            BSLListRow(label: "Wavelength") {
                Text(wavelengthLine)
                    .font(.system(size: 13, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.muted)
            }
            BSLListRow(label: "Firmware", isLast: true) {
                Text(firmwareLine)
                    .font(.system(size: 13, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.muted)
            }
        }
    }

    @ViewBuilder private var beamStateText: some View {
        switch session.snapshot.laserMode {
        case .lasing:
            Text("LASING")
                .font(.system(size: 13, weight: .bold).monospacedDigit())
                .tracking(0.5)
                .foregroundStyle(BSL.orange)
        case .armed:
            Text("armed")
                .font(.system(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(t.muted)
        case .disarmed:
            Text("disarmed")
                .font(.system(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(t.muted)
        }
    }

    private var appearanceGroup: some View {
        @Bindable var appearance = appearance
        return BSLListGroup(label: "Appearance") {
            BSLListRow(label: "Color theme") {
                Picker("Color theme", selection: $appearance.mode) {
                    ForEach(AppearanceMode.allCases) { m in
                        Text(m.displayName).tag(m)
                    }
                }
                .pickerStyle(.menu)
                .tint(BSL.orange)
            }
            BSLListRow(label: "Layout", isLast: true) {
                LayoutSegmented()
            }
        }
    }

    private var footerMeta: some View {
        VStack(spacing: 4) {
            Text("BSL HHLS Controller · fw 0.2.0 · ESP32-S3")
                .font(.system(size: 10))
                .tracking(0.3)
                .foregroundStyle(t.dim)
            Text(connectionUrlLine)
                .font(.system(size: 10, design: .monospaced))
                .tracking(0.3)
                .foregroundStyle(t.dim)
        }
        .multilineTextAlignment(.center)
        .frame(maxWidth: .infinity)
        .padding(.vertical, 14)
    }

    // MARK: - Derived strings

    private var controllerLine: String {
        session.isConnected ? "connected" : "—"
    }

    private var wavelengthLine: String {
        let nm = session.snapshot.tec.targetLambdaNm
        return nm > 0 ? String(format: "%.1f nm", nm) : "—"
    }

    /// Firmware version. The current Codable snapshot does not decode a
    /// firmware version string — the `/meta` probe does carry it, but
    /// DeviceSession doesn't surface that yet. Placeholder until that's
    /// piped through. User directive 2026-04-19: expand Context; this row
    /// now exists and will light up when the transport layer starts
    /// forwarding `meta.fwVersion`.
    private var firmwareLine: String { "0.2.0" }

    private var connectionUrlLine: String {
        switch session.connection {
        case .connected(let url), .connecting(let url): return url
        default: return "not connected"
        }
    }
}

/// 1..5 pill picker used inside the Appearance group for the layout variant.
/// Each tap persists via `LayoutPreference`.
private struct LayoutSegmented: View {
    @Environment(LayoutPreference.self) private var layout
    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 3) {
            ForEach(LayoutPreference.allowed, id: \.self) { i in
                Button {
                    layout.variation = i
                } label: {
                    Text("\(i)")
                        .font(.system(size: 11, weight: .bold).monospacedDigit())
                        .frame(width: 24, height: 24)
                        .background(layout.variation == i ? BSL.orange : Color.clear)
                        .foregroundStyle(layout.variation == i ? Color.white : t.ink)
                        .overlay(
                            RoundedRectangle(cornerRadius: 6, style: .continuous)
                                .strokeBorder(t.border, lineWidth: 0.5)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                }
                .buttonStyle(.plain)
            }
        }
    }
}
