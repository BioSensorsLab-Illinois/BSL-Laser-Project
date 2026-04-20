import SwiftUI
import BSLProtocol

/// V2 — "Mission Control". One dominant circular gauge consumes most of the
/// hero card; secondary telemetry lives in a flat strip below. Mirrors
/// `bsl-laser-system/project/components/Variations.jsx::V2_MissionControl`.
///
/// Safety-authoritative state is still read from `DeviceSession` — no
/// synthesized allow/deny verdicts.
struct LayoutV2MissionControl: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t
    @Environment(\.colorScheme) private var scheme

    var onOpenWavelength: () -> Void

    @State private var localSet: Double = 0
    @State private var pending: Bool = false

    private var snap: DeviceSnapshot { session.snapshot }
    private var mode: LaserMode { snap.laserMode }
    private var maxA: Double { snap.nirMaxA }
    private var setA: Double { snap.nirSetpointA }
    private var actualA: Double {
        let raw = snap.nirActualA
        return raw < 0.05 * maxA ? 0 : raw
    }
    private var pct: Int {
        guard maxA > 0 else { return 0 }
        return Int((actualA / maxA * 100).rounded())
    }

    var body: some View {
        VStack(spacing: 12) {
            hero
            stripCard
        }
        .onAppear { localSet = setA }
        .onChange(of: setA) { _, new in if !pending { localSet = new } }
    }

    // MARK: - Hero

    private var hero: some View {
        BSLCard(pad: 18) {
            VStack(spacing: 14) {
                ZStack {
                    RadialGradient(
                        colors: [t.dark ? BSL.navyDeep.opacity(0.5) : Color(red: 0.963, green: 0.968, blue: 0.988),
                                 .clear],
                        center: .init(x: 0.5, y: 0.30),
                        startRadius: 10,
                        endRadius: 180
                    )
                    .frame(height: 260)
                    .allowsHitTesting(false)
                    .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))

                    ArcGauge(
                        value: actualA,
                        maxValue: maxA,
                        size: 240,
                        stroke: 14,
                        color: mode == .lasing ? BSL.warn : (mode == .armed ? BSL.orange : t.muted),
                        setpoint: (mode != .lasing && setA > 0) ? setA : nil,
                        setpointColor: BSL.orange,
                        breathing: mode == .armed && setA > 0,
                        startAngle: 135,
                        sweep: 270
                    ) {
                        VStack(spacing: 4) {
                            Text("NIR POWER")
                                .font(.system(size: 10, weight: .heavy))
                                .tracking(2)
                                .foregroundStyle(t.muted)
                            HStack(alignment: .firstTextBaseline, spacing: 2) {
                                Text("\(pct)")
                                    .font(.system(size: 64, weight: .thin).monospacedDigit())
                                    .tracking(-3)
                                    .foregroundStyle(t.ink)
                                    .contentTransition(.numericText())
                                    .animation(.easeInOut(duration: 0.3), value: pct)
                                Text("%")
                                    .font(.system(size: 24, weight: .regular))
                                    .foregroundStyle(t.muted)
                            }
                            HStack(spacing: 4) {
                                Text(String(format: "%.2f W", actualA * 1.0))
                                    .font(.system(size: 11).monospacedDigit())
                                Text("·")
                                Text(String(format: "%.2f A", actualA))
                                    .font(.system(size: 11).monospacedDigit())
                            }
                            .foregroundStyle(t.muted)
                        }
                    }
                }

                HStack(spacing: 10) {
                    stepperCircle(icon: "minus") {
                        commit(to: max(0, localSet - maxA * 0.05))
                    }
                    ProgressView(value: min(maxA, max(0, localSet)), total: maxA)
                        .tint(mode == .lasing ? BSL.warn : BSL.orange)
                    stepperCircle(icon: "plus") {
                        commit(to: min(maxA, localSet + maxA * 0.05))
                    }
                }
                .frame(maxWidth: .infinity)

                TriggerStrip(mode: mode, compact: true)
            }
        }
    }

    @ViewBuilder private func stepperCircle(icon: String, action: @escaping () -> Void) -> some View {
        let enabled = mode != .lasing
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(enabled ? t.ink : t.dim)
                .frame(width: 44, height: 44)
                .background(t.surface)
                .overlay(
                    Circle().strokeBorder(t.border, lineWidth: 0.5)
                )
                .clipShape(Circle())
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .opacity(enabled ? 1.0 : 0.4)
    }

    // MARK: - Secondary strip

    private var stripCard: some View {
        BSLCard(pad: 0) {
            VStack(spacing: 0) {
                stripRow(
                    glyphColor: BSL.nir,
                    glyphName: "waveform.path",
                    label: "WAVELENGTH",
                    value: String(format: "%.1f nm", snap.tec.targetLambdaNm),
                    onTap: onOpenWavelength,
                    chevron: true
                )
                separator
                stripRow(
                    glyphColor: Color.blue,
                    glyphName: "thermometer.medium",
                    label: "TEC TEMP",
                    value: String(format: "%.2f °C", snap.tec.tempC),
                    trailingText: snap.tec.tempGood ? "LOCKED" : "DRIFT",
                    trailingTone: snap.tec.tempGood ? .ok : .caution
                )
                separator
                stripRow(
                    glyphColor: BSL.caution,
                    glyphName: "lightbulb.fill",
                    label: "ILLUMINATION",
                    value: "\(snap.bench.requestedLedDutyCyclePct)%",
                    trailingText: nil,
                    bar: Double(snap.bench.requestedLedDutyCyclePct) / 100.0,
                    barColor: Color.blue
                )
                separator
                stripRow(
                    glyphColor: BSL.ok,
                    glyphName: "bolt.circle",
                    label: "POWER",
                    value: String(format: "%.0f V / %.1f W", snap.pd.sourceVoltageV, snap.pd.negotiatedPowerW),
                    trailingText: snap.pd.contractValid ? "PD LOCKED" : "NO PD",
                    trailingTone: snap.pd.contractValid ? .ok : .warn,
                    last: true
                )
            }
        }
    }

    private var separator: some View {
        Rectangle().fill(t.border).frame(height: 0.5)
    }

    @ViewBuilder
    private func stripRow(
        glyphColor: Color,
        glyphName: String,
        label: String,
        value: String,
        onTap: (() -> Void)? = nil,
        chevron: Bool = false,
        trailingText: String? = nil,
        trailingTone: BSLPill<Text>.Tone = .ok,
        bar: Double? = nil,
        barColor: Color = BSL.orange,
        last: Bool = false
    ) -> some View {
        let row = HStack(spacing: 12) {
            ZStack {
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(t.dark ? Color.white.opacity(0.05) : BSL.navy.opacity(0.05))
                Image(systemName: glyphName)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(glyphColor)
            }
            .frame(width: 28, height: 28)
            VStack(alignment: .leading, spacing: 1) {
                Text(label)
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                Text(value)
                    .font(.system(size: 16, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.ink)
            }
            Spacer(minLength: 8)
            if let bar {
                MicroBar(value: bar, color: barColor)
                    .frame(width: 60)
            }
            if let trailingText {
                BSLPill(trailingTone) { Text(trailingText) }
            }
            if chevron {
                Image(systemName: "chevron.right")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(t.dim)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .contentShape(Rectangle())

        if let onTap {
            Button(action: onTap) { row }.buttonStyle(.plain)
        } else {
            row
        }
    }

    // MARK: - Commit

    private func commit(to value: Double) {
        let clamped = min(max(value, 0), maxA)
        localSet = clamped
        session.rememberNirSetpoint(clamped)
        guard snap.bench.hostControlReadiness.nirBlockedReason == .none else { return }
        pending = true
        Task {
            defer { pending = false }
            let result = await session.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(snap.bench.requestedNirEnabled),
                    "current_a": .double(clamped),
                ]
            )
            if case .success(let resp) = result, resp.ok {
                session.rememberNirSetpoint(clamped)
            }
        }
    }
}

/// 60×4 rectangular progress bar used inside the strip rows.
private struct MicroBar: View {
    var value: Double
    var color: Color
    @Environment(\.bslTheme) private var t
    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 2).fill(t.border)
                RoundedRectangle(cornerRadius: 2).fill(color)
                    .frame(width: max(0, min(1, value)) * geo.size.width)
            }
        }
        .frame(height: 4)
    }
}
