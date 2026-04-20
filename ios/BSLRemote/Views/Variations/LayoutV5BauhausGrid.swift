import SwiftUI
import BSLProtocol

/// V5 — "Bauhaus Grid". Asymmetric 2-column grid of bold colour tiles.
/// Dominant orange NIR tile (1.3fr) spans two rows on the left; navy
/// wavelength + light TEC tiles stack on the right; an LED tile and a PD
/// tile share the bottom row. Mirrors
/// `bsl-laser-system/project/components/Variations.jsx::V5_Bauhaus`.
struct LayoutV5BauhausGrid: View {
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
        VStack(spacing: 8) {
            topRow
            bottomRow
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

    // MARK: - Top row — NIR dominant (~1.3fr) + wavelength/TEC stacked (~1fr)

    private var topRow: some View {
        HStack(alignment: .top, spacing: 8) {
            nirTile
                .frame(maxWidth: .infinity)
                .layoutPriority(1.3)
            VStack(spacing: 8) {
                wavelengthTile
                tecTile
            }
            .frame(maxWidth: .infinity)
            .layoutPriority(1.0)
        }
    }

    private var nirTile: some View {
        let dominantOn = mode == .armed || mode == .lasing
        return VStack(alignment: .leading, spacing: 8) {
            VStack(alignment: .leading, spacing: 3) {
                Text("NIR LASER")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.5)
                    .foregroundStyle(dominantOn ? Color.white.opacity(0.8) : t.muted)
                Text(String(format: "%.1f nm · CL IV", snap.tec.targetLambdaNm))
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(dominantOn ? Color.white.opacity(0.6) : t.muted)
            }
            Spacer()
            VStack(alignment: .leading, spacing: 4) {
                Text(String(format: "%02d", pct))
                    .font(.system(size: 88, weight: .thin).monospacedDigit())
                    .tracking(-5)
                    .foregroundStyle(dominantOn ? .white : t.ink)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.25), value: pct)
                Text(String(format: "percent · %.2f W optical", actualA * 1.0))
                    .font(.system(size: 13, weight: .regular).monospacedDigit())
                    .foregroundStyle(dominantOn ? Color.white.opacity(0.80) : t.muted)
            }
            Slider(value: $nirLocal,
                   in: 0...maxA,
                   onEditingChanged: { editing in
                       nirDragging = editing
                       if !editing { commitNir() }
                   })
                .tint(dominantOn ? .white : BSL.orange)
                .disabled(mode == .lasing)
                .opacity(mode == .lasing ? 0.3 : 1.0)
        }
        .padding(18)
        .frame(minHeight: 260, alignment: .topLeading)
        .background(dominantOn ? BSL.orange : (t.dark ? BSL.Dark.surface2 : Color(red: 0.949, green: 0.953, blue: 0.961)))
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
    }

    private var wavelengthTile: some View {
        Button(action: onOpenWavelength) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("λ")
                        .font(.system(size: 14, weight: .heavy, design: .serif))
                        .foregroundStyle(.white.opacity(0.75))
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(.white.opacity(0.6))
                }
                Spacer()
                VStack(alignment: .leading, spacing: 2) {
                    Text(String(format: "%.1f", snap.tec.targetLambdaNm))
                        .font(.system(size: 34, weight: .light).monospacedDigit())
                        .tracking(-1.5)
                        .foregroundStyle(.white)
                    Text("nanometers")
                        .font(.system(size: 11))
                        .foregroundStyle(.white.opacity(0.7))
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity, minHeight: 126, alignment: .topLeading)
            .background(t.dark ? BSL.navyDeep : BSL.navy)
            .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
        }
        .buttonStyle(.plain)
    }

    private var tecTile: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("TEC")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.5)
                    .foregroundStyle(t.muted)
                Spacer()
                Circle().fill(snap.tec.tempGood ? BSL.ok : BSL.caution).frame(width: 6, height: 6)
            }
            Spacer()
            VStack(alignment: .leading, spacing: 2) {
                HStack(alignment: .firstTextBaseline, spacing: 2) {
                    Text(String(format: "%.2f", snap.tec.tempC))
                        .font(.system(size: 30, weight: .light).monospacedDigit())
                        .tracking(-1)
                        .foregroundStyle(t.ink)
                    Text("°C")
                        .font(.system(size: 14))
                        .foregroundStyle(t.muted)
                }
                Text(String(format: "locked · %.2f target", snap.tec.targetTempC))
                    .font(.system(size: 10))
                    .foregroundStyle(t.muted)
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, minHeight: 126, alignment: .topLeading)
        .background(t.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    // MARK: - Bottom row — LED + PD

    private var bottomRow: some View {
        HStack(spacing: 8) {
            ledTile.frame(maxWidth: .infinity)
            pdTile.frame(maxWidth: .infinity)
        }
    }

    private var ledTile: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("LED")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.5)
                    .foregroundStyle(t.muted)
                Spacer()
                Text("\(Int(ledLocal.rounded()))%")
                    .font(.system(size: 18, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.ink)
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
        .padding(14)
        .background(t.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    private var pdTile: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("POWER")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.5)
                    .foregroundStyle(t.muted)
                Spacer()
                BSLPill(snap.pd.contractValid ? .ok : .warn) {
                    Text(snap.pd.contractValid ? "PD" : "NO PD")
                }
            }
            Text(String(format: "%.0fV · %.1fW", snap.pd.sourceVoltageV, snap.pd.negotiatedPowerW))
                .font(.system(size: 18, weight: .semibold).monospacedDigit())
                .foregroundStyle(t.ink)
            Text(String(format: "%.1f W in use", BenchEstimate.derive(from: snap).totalEstimatedInputPowerW))
                .font(.system(size: 10))
                .foregroundStyle(t.muted)
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(t.surface)
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .strokeBorder(t.border, lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 20, style: .continuous))
    }

    private func commitNir() {
        let clamped = min(max(nirLocal, 0), maxA)
        session.rememberNirSetpoint(clamped)
        guard snap.bench.hostControlReadiness.nirBlockedReason == .none else { return }
        Task {
            let r = await session.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(snap.bench.requestedNirEnabled),
                    "current_a": .double(clamped),
                ]
            )
            if case .success(let resp) = r, resp.ok {
                session.rememberNirSetpoint(clamped)
            }
        }
    }
}
