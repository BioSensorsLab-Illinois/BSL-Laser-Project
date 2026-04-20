import SwiftUI
import BSLProtocol

/// V4 — "Unified Card". A single tall card holding the NIR hero at the top,
/// the wavelength ↔ TEC coupled row in the middle, a LED strip next, and a
/// PD/link footer. Mirrors
/// `bsl-laser-system/project/components/Variations.jsx::V4_Unified`.
struct LayoutV4UnifiedCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t
    @Environment(\.colorScheme) private var scheme

    var onOpenWavelength: () -> Void

    @State private var nirLocal: Double = 0
    @State private var nirDragging: Bool = false
    @State private var ledLocal: Double = 0
    @State private var ledDragging: Bool = false

    private var snap: DeviceSnapshot { session.snapshot }
    private var mode: LaserMode { snap.laserMode }
    private var maxA: Double { snap.nirMaxA }
    private var setA: Double { snap.nirSetpointA }
    private var actualA: Double {
        let raw = snap.nirActualA
        return raw < 0.05 * maxA ? 0 : raw
    }
    private var pct: Int { maxA > 0 ? Int((actualA / maxA * 100).rounded()) : 0 }

    var body: some View {
        BSLCard(pad: 0) {
            VStack(spacing: 0) {
                nirSection
                wavelengthCoupledRow
                ledSection
                footer
            }
        }
        .onAppear {
            nirLocal = setA
            ledLocal = Double(snap.bench.requestedLedDutyCyclePct)
        }
        .onChange(of: setA) { _, v in if !nirDragging { nirLocal = v } }
        .onChange(of: snap.bench.requestedLedDutyCyclePct) { _, v in
            if !ledDragging { ledLocal = Double(v) }
        }
    }

    // MARK: - NIR

    private var nirSection: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                HStack(spacing: 8) {
                    Circle()
                        .fill(mode == .lasing ? BSL.warn : (mode == .armed ? BSL.orange : t.dim))
                        .frame(width: 8, height: 8)
                        .shadow(color: mode == .lasing ? BSL.warn.opacity(0.6) : (mode == .armed ? BSL.orange.opacity(0.6) : .clear), radius: 6)
                    Text("NIR LASER")
                        .font(.system(size: 11, weight: .heavy))
                        .tracking(1)
                        .foregroundStyle(t.ink)
                }
                Spacer()
                Text(String(format: "%.1f nm · CLASS IV", snap.tec.targetLambdaNm))
                    .font(.system(size: 10).monospacedDigit())
                    .tracking(0.8)
                    .foregroundStyle(t.muted)
            }

            HStack(alignment: .center, spacing: 14) {
                ArcGauge(
                    value: actualA,
                    maxValue: maxA,
                    size: 128,
                    stroke: 8,
                    color: mode == .lasing ? BSL.warn : (mode == .armed ? BSL.orange : t.muted),
                    setpoint: (mode != .lasing && setA > 0) ? setA : nil,
                    setpointColor: BSL.orange,
                    breathing: mode == .armed && setA > 0,
                    startAngle: 135,
                    sweep: 270
                ) {
                    VStack(spacing: 2) {
                        Text("\(pct)")
                            .font(.system(size: 36, weight: .light).monospacedDigit())
                            .tracking(-1.5)
                            .foregroundStyle(t.ink)
                            .contentTransition(.numericText())
                            .animation(.easeInOut(duration: 0.25), value: pct)
                        Text("%")
                            .font(.system(size: 11))
                            .foregroundStyle(t.muted)
                    }
                }
                VStack(spacing: 8) {
                    valueRow("Optical", String(format: "%.2f W", actualA * 1.0))
                    valueRow("Drive", String(format: "%.2f A", actualA))
                    valueRow("Duty", "CW")
                    Rectangle().fill(t.border).frame(height: 1).padding(.vertical, 2)
                    Slider(value: $nirLocal,
                           in: 0...maxA,
                           onEditingChanged: { editing in
                               nirDragging = editing
                               if !editing { commit() }
                           })
                    .tint(BSL.orange)
                    .disabled(mode == .lasing)
                    .opacity(mode == .lasing ? 0.4 : 1.0)
                }
            }
        }
        .padding(18)
    }

    @ViewBuilder private func valueRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
                .font(.system(size: 11))
                .foregroundStyle(t.muted)
            Spacer()
            Text(value)
                .font(.system(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(t.ink)
                .contentTransition(.numericText())
                .animation(.easeInOut(duration: 0.25), value: value)
        }
    }

    // MARK: - Wavelength ↔ TEC coupled row

    private var wavelengthCoupledRow: some View {
        Button(action: onOpenWavelength) {
            HStack(spacing: 12) {
                Image(systemName: "waveform.path")
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundStyle(BSL.nir)
                VStack(alignment: .leading, spacing: 2) {
                    Text("WAVELENGTH ↔ TEC")
                        .font(.system(size: 10, weight: .heavy))
                        .tracking(1)
                        .foregroundStyle(t.muted)
                    HStack(alignment: .firstTextBaseline, spacing: 10) {
                        (Text(String(format: "%.1f", snap.tec.targetLambdaNm))
                         + Text(" nm").font(.system(size: 12).weight(.regular)).foregroundColor(t.muted))
                            .font(.system(size: 22, weight: .semibold).monospacedDigit())
                            .tracking(-0.5)
                            .foregroundStyle(t.ink)
                        Text("↔")
                            .font(.system(size: 11))
                            .foregroundStyle(t.muted)
                        Text(String(format: "%.2f °C", snap.tec.tempC))
                            .font(.system(size: 16).monospacedDigit())
                            .foregroundStyle(t.muted)
                    }
                }
                Spacer(minLength: 8)
                BSLPill(snap.tec.tempGood ? .ok : .caution) {
                    Text(snap.tec.tempGood ? "LOCKED" : "DRIFT")
                }
            }
            .padding(.horizontal, 18)
            .padding(.vertical, 14)
            .background(BSL.nir.opacity(scheme == .dark ? 0.06 : 0.05))
            .overlay(
                Rectangle().fill(t.border).frame(height: 0.5), alignment: .top
            )
            .overlay(
                Rectangle().fill(t.border).frame(height: 0.5), alignment: .bottom
            )
        }
        .buttonStyle(.plain)
    }

    // MARK: - LED

    private var ledSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: "lightbulb.fill")
                        .font(.system(size: 14))
                        .foregroundStyle(ledLocal > 0 ? Color.blue : t.muted)
                    Text("VISIBLE LED")
                        .font(.system(size: 11, weight: .heavy))
                        .tracking(1)
                        .foregroundStyle(t.ink)
                }
                Spacer()
                Text("\(Int(ledLocal.rounded()))%")
                    .font(.system(size: 14, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.ink)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.25), value: Int(ledLocal.rounded()))
            }
            Slider(value: $ledLocal,
                   in: 0...Double(max(snap.safety.maxTofLedDutyCyclePct, 1)),
                   step: 1,
                   onEditingChanged: { editing in
                       ledDragging = editing
                       if !editing {
                           let duty = Int(ledLocal.rounded())
                           session.rememberLed(dutyPct: duty, enabled: duty > 0)
                           guard snap.bench.hostControlReadiness.ledBlockedReason == .none else { return }
                           Task {
                               let r = await session.sendCommand(
                                   "operate.set_led",
                                   args: [
                                       "enable": .bool(duty > 0),
                                       "duty_cycle_pct": .int(duty),
                                   ]
                               )
                               if case .success(let resp) = r, resp.ok {
                                   session.rememberLed(dutyPct: duty, enabled: duty > 0)
                               }
                           }
                       }
                   })
                .tint(Color.blue)
        }
        .padding(18)
    }

    // MARK: - Footer

    private var footer: some View {
        HStack {
            footerChip(
                systemImage: "bolt.circle",
                label: String(format: "PD %.0fW", snap.pd.negotiatedPowerW),
                color: snap.pd.contractValid ? BSL.ok : t.dim
            )
            footerChip(
                systemImage: "wifi",
                label: session.isConnected ? "LINKED" : "OFFLINE",
                color: session.isConnected ? BSL.ok : t.dim
            )
            footerChip(
                systemImage: "checkmark.seal.fill",
                label: "INTERLOCKS OK",
                color: BSL.ok
            )
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 10)
        .background(t.dark ? Color.white.opacity(0.02) : BSL.Light.surface2)
        .overlay(Rectangle().fill(t.border).frame(height: 0.5), alignment: .top)
    }

    private func footerChip(systemImage: String, label: String, color: Color) -> some View {
        HStack(spacing: 6) {
            Image(systemName: systemImage)
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(color)
            Text(label)
                .font(.system(size: 10, weight: .bold))
                .tracking(0.5)
                .foregroundStyle(t.muted)
        }
        .frame(maxWidth: .infinity)
    }

    private func commit() {
        let clamped = min(max(nirLocal, 0), maxA)
        session.rememberNirSetpoint(clamped)
        guard snap.bench.hostControlReadiness.nirBlockedReason == .none else { return }
        Task {
            let res = await session.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(snap.bench.requestedNirEnabled),
                    "current_a": .double(clamped),
                ]
            )
            if case .success(let r) = res, r.ok {
                session.rememberNirSetpoint(clamped)
            }
        }
    }
}
