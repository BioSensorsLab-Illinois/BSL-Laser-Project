import SwiftUI
import BSLProtocol

/// V3 — "Instrument Grid". Big NIR number dominates, presets snap to 0/25/
/// 50/75/100 % of the firmware ceiling, compact wavelength + TEC cells, and
/// a small LED strip. Mirrors
/// `bsl-laser-system/project/components/Variations.jsx::V3_Instrument`.
struct LayoutV3InstrumentGrid: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    var onOpenWavelength: () -> Void

    @State private var pending = false
    @State private var ledLocal: Double = 0
    @State private var ledDragging = false

    private var snap: DeviceSnapshot { session.snapshot }
    private var mode: LaserMode { snap.laserMode }
    private var maxA: Double { snap.nirMaxA }
    private var setA: Double { snap.nirSetpointA }
    private var actualA: Double {
        let raw = snap.nirActualA
        return raw < 0.05 * maxA ? 0 : raw
    }
    private var pct: Int {
        maxA > 0 ? Int((actualA / maxA * 100).rounded()) : 0
    }
    private var setPct: Int {
        maxA > 0 ? Int((setA / maxA * 100).rounded()) : 0
    }

    var body: some View {
        VStack(spacing: 10) {
            nirCard
            linkCard
        }
        .onAppear { ledLocal = Double(snap.bench.requestedLedDutyCyclePct) }
        .onChange(of: snap.bench.requestedLedDutyCyclePct) { _, v in
            if !ledDragging { ledLocal = Double(v) }
        }
    }

    private var nirCard: some View {
        BSLCard(pad: 0) {
            VStack(spacing: 0) {
                nirTop
                Rectangle().fill(t.border).frame(height: 0.5)
                secondaryCells
                Rectangle().fill(t.border).frame(height: 0.5)
                ledRow
            }
        }
    }

    private var nirTop: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("NIR · 785 nm BAND")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.5)
                    .foregroundStyle(t.muted)
                Spacer()
                BSLPill(mode == .lasing ? .warn : (mode == .armed ? .brand : .neutral)) {
                    Text(mode == .lasing ? "LASING" : (mode == .armed ? "ENABLED" : "DISABLED"))
                }
            }

            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Text(String(format: "%02d", pct))
                    .font(.system(size: 72, weight: .thin).monospacedDigit())
                    .tracking(-4)
                    .foregroundStyle(mode == .lasing ? BSL.warn : (mode == .armed ? BSL.orange : t.ink))
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.25), value: pct)
                Text("%")
                    .font(.system(size: 24, weight: .light))
                    .foregroundStyle(t.muted)
                Spacer()
                VStack(alignment: .trailing, spacing: 2) {
                    Text("OPTICAL")
                        .font(.system(size: 10, weight: .heavy))
                        .tracking(0.8)
                        .foregroundStyle(t.muted)
                    Text(String(format: "%.2f W", actualA * 1.0))
                        .font(.system(size: 18, weight: .semibold).monospacedDigit())
                        .foregroundStyle(t.ink)
                }
            }

            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3).fill(t.border)
                    RoundedRectangle(cornerRadius: 3)
                        .fill(LinearGradient(
                            colors: [BSL.orange, BSL.orangeDeep],
                            startPoint: .leading, endPoint: .trailing
                        ))
                        .frame(width: geo.size.width * min(1.0, Double(pct) / 100.0))
                        .animation(.easeInOut(duration: 0.25), value: pct)
                }
            }
            .frame(height: 6)
            HStack {
                ForEach([0, 25, 50, 75, 100], id: \.self) { n in
                    Text("\(n)")
                        .font(.system(size: 9, weight: .semibold).monospacedDigit())
                        .foregroundStyle(t.dim)
                    if n != 100 { Spacer() }
                }
            }

            HStack(spacing: 6) {
                ForEach([0, 25, 50, 75, 100], id: \.self) { n in
                    presetChip(percent: n)
                }
            }
        }
        .padding(18)
    }

    @ViewBuilder private func presetChip(percent: Int) -> some View {
        let isActive = setPct == percent
        let enabled = mode != .lasing
        Button {
            let amps = (Double(percent) / 100.0) * maxA
            commit(to: amps)
        } label: {
            Text(percent == 0 ? "OFF" : "\(percent)%")
                .font(.system(size: 11, weight: .bold).monospacedDigit())
                .foregroundStyle(isActive ? BSL.orange : t.muted)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
                .background(isActive ? BSL.orangeSoft.opacity(t.dark ? 0.18 : 1.0) : t.surface)
                .overlay(
                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                        .strokeBorder(isActive ? BSL.orange : t.border, lineWidth: 0.5)
                )
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .opacity(enabled ? 1.0 : 0.4)
    }

    private var secondaryCells: some View {
        HStack(spacing: 0) {
            cell(
                label: "λ WAVELENGTH",
                value: String(format: "%.1f", snap.tec.targetLambdaNm),
                unit: "nm",
                onTap: onOpenWavelength,
                hasChevron: true
            )
            Rectangle().fill(t.border).frame(width: 0.5)
            cell(
                label: "TEC TEMP",
                value: String(format: "%.2f", snap.tec.tempC),
                unit: "°C",
                sub: String(format: "Target %.2f", snap.tec.targetTempC)
            )
        }
    }

    @ViewBuilder
    private func cell(label: String, value: String, unit: String, sub: String? = nil, onTap: (() -> Void)? = nil, hasChevron: Bool = false) -> some View {
        let block = VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.system(size: 10, weight: .heavy))
                .tracking(1)
                .foregroundStyle(t.muted)
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(.system(size: 22, weight: .semibold).monospacedDigit())
                    .tracking(-0.5)
                    .foregroundStyle(t.ink)
                Text(unit)
                    .font(.system(size: 12))
                    .foregroundStyle(t.muted)
                if hasChevron {
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(t.dim)
                }
            }
            if let sub {
                Text(sub).font(.system(size: 10)).foregroundStyle(t.muted)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())

        if let onTap {
            Button(action: onTap) { block }.buttonStyle(.plain)
        } else {
            block
        }
    }

    private var ledRow: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("VISIBLE LED")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                Spacer()
                Text("\(Int(ledLocal.rounded()))%")
                    .font(.system(size: 14, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.ink)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.25), value: Int(ledLocal.rounded()))
            }
            Slider(
                value: $ledLocal,
                in: 0...Double(max(snap.safety.maxTofLedDutyCyclePct, 1)),
                step: 1,
                onEditingChanged: { editing in
                    ledDragging = editing
                    if !editing {
                        let duty = Int(ledLocal.rounded())
                        session.rememberLed(dutyPct: duty, enabled: duty > 0)
                        guard snap.bench.hostControlReadiness.ledBlockedReason == .none else { return }
                        Task {
                            let res = await session.sendCommand(
                                "operate.set_led",
                                args: [
                                    "enable": .bool(duty > 0),
                                    "duty_cycle_pct": .int(duty),
                                ]
                            )
                            if case .success(let r) = res, r.ok {
                                session.rememberLed(dutyPct: duty, enabled: duty > 0)
                            }
                        }
                    }
                }
            )
            .tint(Color.blue)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
    }

    // MARK: - Link card

    private var linkCard: some View {
        BSLCard(pad: 14) {
            HStack(spacing: 14) {
                kv("LINK", session.isConnected ? "WS/WiFi" : "NONE", ok: session.isConnected)
                kv("PD", String(format: "%.0fV · %.2fA", snap.pd.sourceVoltageV, snap.pd.sourceCurrentA))
                kv("UPTIME", uptime)
            }
        }
    }

    @ViewBuilder private func kv(_ label: String, _ value: String, ok: Bool = false) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(label)
                .font(.system(size: 9, weight: .heavy))
                .tracking(1)
                .foregroundStyle(t.muted)
            Text(value)
                .font(.system(size: 12, weight: .semibold).monospacedDigit())
                .foregroundStyle(ok ? BSL.ok : t.ink)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var uptime: String {
        let s = snap.session.uptimeSeconds
        let h = s / 3600
        let m = (s % 3600) / 60
        let sec = s % 60
        return String(format: "%02d:%02d:%02d", h, m, sec)
    }

    private func commit(to amps: Double) {
        let clamped = min(max(amps, 0), maxA)
        session.rememberNirSetpoint(clamped)
        guard snap.bench.hostControlReadiness.nirBlockedReason == .none else { return }
        pending = true
        Task {
            defer { pending = false }
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
