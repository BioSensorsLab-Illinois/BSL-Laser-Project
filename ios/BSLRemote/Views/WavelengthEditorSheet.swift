import SwiftUI
import BSLProtocol

/// Redesigned wavelength sheet: big λ readout, 770–790 nm scrubber, five
/// preset chips. Mirrors `bsl-laser-system/project/BSL Laser Controller.html:1656-1716`.
///
/// Apply sends `operate.set_target { mode:"lambda", lambda_nm }`. The
/// firmware clamps against the wavelength LUT and returns the clamped target
/// via telemetry. PIN gate applies before the command per the existing
/// AuthGate contract.
struct WavelengthEditorSheet: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth
    @Environment(\.dismiss) private var dismiss
    @Environment(\.colorScheme) private var scheme

    @State private var lambda: Double = 785.0
    @State private var applying: Bool = false
    @State private var banner: String? = nil
    @State private var showingPin: Bool = false

    private let presets: [Double] = [770.0, 775.0, 780.0, 785.0, 790.0]
    private let lowerBound: Double = 770.0
    private let upperBound: Double = 790.0

    private var tec: TecStatus { session.snapshot.tec }
    private var safety: SafetyStatus { session.snapshot.safety }
    private var t: BSLTheme { BSLTheme(scheme) }

    // rough coupling between λ and TEC target (display-only estimate)
    private var tecEstimate: Double { 25.0 + (lambda - 785.0) * 0.38 }

    var body: some View {
        BSLThemeProvider {
            VStack(spacing: 0) {
                Capsule().fill(t.border)
                    .frame(width: 40, height: 4)
                    .padding(.top, 8)
                ScrollView {
                    VStack(alignment: .leading, spacing: 22) {
                        header
                        heroReadout
                        scrubber
                        presetChips
                        if let banner { bannerView(banner) }
                        live
                    }
                    .padding(.horizontal, 22)
                    .padding(.top, 18)
                    .padding(.bottom, 20)
                }
                actions
                    .padding(.horizontal, 22)
                    .padding(.bottom, 22)
            }
            .background(t.surface)
            .clipShape(RoundedRectangle(cornerRadius: 24, style: .continuous))
            .onAppear { lambda = tec.targetLambdaNm > 0 ? tec.targetLambdaNm : 785.0 }
            .sheet(isPresented: $showingPin) {
                PinGate {
                    Task { await apply() }
                    showingPin = false
                }
                .environment(auth)
            }
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("SET WAVELENGTH")
                .font(.system(size: 10, weight: .heavy))
                .tracking(1.5)
                .foregroundStyle(t.muted)
            Text("λ — Tune the NIR band")
                .font(.system(size: 28, weight: .bold))
                .foregroundStyle(t.ink)
            Text("Wavelength is coupled to TEC temperature. The controller will drive the TEC to match your target λ and hold it there.")
                .font(.system(size: 12))
                .foregroundStyle(t.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var heroReadout: some View {
        VStack(spacing: 10) {
            VStack(spacing: 4) {
                Text(String(format: "%.1f", lambda))
                    .font(.system(size: 72, weight: .thin).monospacedDigit())
                    .foregroundStyle(t.ink)
                Text("nanometers")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(BSL.nir)
            }
            Text(String(format: "→ TEC target ≈ %.2f °C", tecEstimate))
                .font(.system(size: 11).monospacedDigit())
                .foregroundStyle(t.muted)
        }
        .frame(maxWidth: .infinity)
        .padding(22)
        .background(scheme == .dark ? BSL.nir.opacity(0.1) : BSL.nirSoft)
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    }

    private var scrubber: some View {
        VStack(spacing: 4) {
            Slider(value: $lambda, in: lowerBound...upperBound, step: 0.1)
                .tint(BSL.nir)
            HStack {
                Text("\(String(format: "%.1f", lowerBound))")
                Spacer()
                Text("\(String(format: "%.1f", upperBound))")
            }
            .font(.system(size: 10).monospacedDigit())
            .foregroundStyle(t.dim)
        }
    }

    private var presetChips: some View {
        HStack(spacing: 6) {
            ForEach(presets, id: \.self) { p in
                let active = abs(lambda - p) < 0.05
                Button {
                    lambda = p
                } label: {
                    Text(String(format: "%.1f", p))
                        .font(.system(size: 12, weight: .semibold).monospacedDigit())
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 10)
                        .background(active ? (scheme == .dark ? BSL.nir.opacity(0.15) : BSL.nirSoft) : t.surface)
                        .foregroundStyle(active ? BSL.nir : t.ink)
                        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
                        .overlay(
                            RoundedRectangle(cornerRadius: 12, style: .continuous)
                                .strokeBorder(active ? BSL.nir : t.border, lineWidth: 0.5)
                        )
                }
                .buttonStyle(.plain)
            }
        }
    }

    private func bannerView(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 12))
            .foregroundStyle(t.muted)
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(t.bg)
            .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private var live: some View {
        VStack(spacing: 8) {
            row("Actual", String(format: "%.2f nm", tec.actualLambdaNm))
            row("Drift",  String(format: "%+.2f nm", safety.lambdaDriftNm))
            row("Drift limit", String(format: "±%.2f nm", safety.lambdaDriftLimitNm))
            row("TEC actual",  String(format: "%.2f °C",  tec.tempC))
        }
        .padding(12)
        .background(t.bg)
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label).font(.system(size: 11)).foregroundStyle(t.muted)
            Spacer()
            Text(value).font(.system(size: 12, weight: .semibold).monospacedDigit()).foregroundStyle(t.ink)
        }
    }

    private var actions: some View {
        HStack(spacing: 8) {
            Button("Cancel") { dismiss() }
                .buttonStyle(SecondaryButtonStyle())
            Button(action: onApply) {
                HStack {
                    if applying { ProgressView().tint(.white) }
                    Text(applying ? "Applying…" : "Apply · TEC will settle")
                }
            }
            .buttonStyle(PrimaryButtonStyle())
            .disabled(applying)
        }
    }

    private func onApply() {
        if auth.isUnlocked { Task { await apply() } }
        else { showingPin = true }
    }

    private func apply() async {
        applying = true
        defer { applying = false }
        let result = await session.sendCommand(
            "operate.set_target",
            args: [
                "mode": .string("lambda"),
                "lambda_nm": .double(lambda),
            ]
        )
        switch result {
        case .success(let resp):
            if resp.ok {
                dismiss()
            } else {
                banner = "Rejected: \(resp.error ?? "unknown reason")"
            }
        case .failure(let err):
            banner = "Failed: \(err.localizedDescription)"
        }
    }
}

private struct PrimaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 14, weight: .bold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 14)
            .background(BSL.orange)
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .shadow(color: BSL.orange.opacity(0.25), radius: 8, x: 0, y: 4)
            .opacity(configuration.isPressed ? 0.85 : 1.0)
    }
}

private struct SecondaryButtonStyle: ButtonStyle {
    @Environment(\.colorScheme) var scheme
    func makeBody(configuration: Configuration) -> some View {
        let t = BSLTheme(scheme)
        configuration.label
            .font(.system(size: 14, weight: .semibold))
            .foregroundStyle(t.ink)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 14)
            .background(t.surface)
            .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .strokeBorder(t.border, lineWidth: 0.5)
            )
            .opacity(configuration.isPressed ? 0.7 : 1.0)
    }
}
