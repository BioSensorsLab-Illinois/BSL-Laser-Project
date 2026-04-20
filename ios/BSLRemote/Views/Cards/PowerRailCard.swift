import SwiftUI
import BSLProtocol

/// Compact USB-PD / rail health row with a two-bar power-budget view.
///
/// USB-PD "draw" in firmware telemetry is typically 0 when the USB-PD PHY is
/// not polled — the iOS app mirrored that and showed 0.0 W indefinitely. We
/// now compute the *estimated* input power using the same model the desktop
/// web console uses (`host-console/src/lib/bench-model.ts`) and add a 1 W
/// idle floor per the 2026-04-19 operator directive.
///
/// Two bars:
///   1. PD draw — total estimated draw against the negotiated contract
///   2. TEC input — the TEC-alone portion of the draw, which the operator
///      monitors separately when tuning wavelength / settling behavior.
struct PowerRailCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    private var snap: DeviceSnapshot { session.snapshot }
    private var pd: PdStatus { snap.pd }
    private var rails: RailsBlock { snap.rails }
    private var stale: Bool { session.isStale }
    private var estimate: BenchEstimate.Estimate { BenchEstimate.derive(from: snap) }

    var body: some View {
        BSLCard(pad: 14) {
            VStack(alignment: .leading, spacing: 12) {
                header
                pdBar
                ldBar
                tecBar
                footer
            }
        }
    }

    private var header: some View {
        HStack(spacing: 10) {
            Image(systemName: "bolt.circle")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(pd.contractValid ? BSL.ok : t.dim)
            VStack(alignment: .leading, spacing: 1) {
                Text(headerLine)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(t.ink)
                    .lineLimit(1)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.35), value: pd.sourceVoltageV)
                    .animation(.easeInOut(duration: 0.35), value: pd.sourceCurrentA)
                Text(detailLine)
                    .font(.system(size: 11))
                    .foregroundStyle(t.muted)
                    .lineLimit(1)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.35), value: estimate.totalEstimatedInputPowerW)
            }
            Spacer(minLength: 8)
            BSLPill(pdTone) { Text(pdLabel) }
        }
    }

    // MARK: - Bars

    private var pdBar: some View {
        PowerBar(
            label: "PD draw",
            valueText: stale ? "—" : String(format: "%.1f / %.1f W", estimate.totalEstimatedInputPowerW, max(pd.negotiatedPowerW, 0.0)),
            ratio: pdRatio,
            tint: pdBarTint
        )
    }

    private var ldBar: some View {
        PowerBar(
            label: "LD input",
            valueText: stale ? "—" : String(format: "%.1f W", estimate.laserInputPowerW),
            ratio: ldRatio,
            tint: BSL.orange
        )
    }

    private var tecBar: some View {
        PowerBar(
            label: "TEC input",
            valueText: stale ? "—" : String(format: "%.1f W", estimate.tecInputPowerW),
            ratio: tecRatio,
            tint: BSL.nir
        )
    }

    private var footer: some View {
        HStack(spacing: 14) {
            Label(
                String(format: "LED %.1f W", estimate.tofLedInputPowerW),
                systemImage: "lightbulb"
            )
            Label(
                String(format: "Idle %.1f W", estimate.idlePowerW),
                systemImage: "cpu"
            )
            Label(
                String(format: "Total %.1f W", estimate.totalEstimatedInputPowerW),
                systemImage: "sum"
            )
        }
        .labelStyle(.titleAndIcon)
        .font(.system(size: 10, weight: .semibold))
        .foregroundStyle(t.muted)
    }

    // MARK: - Ratios + labels

    private var pdRatio: Double {
        guard pd.negotiatedPowerW > 0 else { return 0 }
        return min(1.0, max(0.0, estimate.totalEstimatedInputPowerW / pd.negotiatedPowerW))
    }
    private var ldRatio: Double {
        let ceiling = pd.negotiatedPowerW > 0 ? pd.negotiatedPowerW : 15.0
        return min(1.0, max(0.0, estimate.laserInputPowerW / ceiling))
    }
    private var tecRatio: Double {
        // Bar scales against the PD contract so the two bars read against
        // the same ceiling. Falls back to a nominal 15 W scale when no
        // contract is negotiated (USB-only bench).
        let ceiling = pd.negotiatedPowerW > 0 ? pd.negotiatedPowerW : 15.0
        return min(1.0, max(0.0, estimate.tecInputPowerW / ceiling))
    }
    private var pdBarTint: Color {
        if !pd.contractValid { return BSL.warn }
        if pdRatio >= 0.9 { return BSL.caution }
        return BSL.ok
    }

    private var headerLine: String {
        if stale { return "USB-PD · —" }
        return String(format: "USB-PD · %.0f V / %.2f A source", pd.sourceVoltageV, pd.sourceCurrentA)
    }
    private var detailLine: String {
        if stale { return "—" }
        let avail = pd.negotiatedPowerW
        let draw = estimate.totalEstimatedInputPowerW
        return String(format: "%.1f W est draw · %.1f W contract", draw, avail)
    }
    private var pdTone: BSLPill<Text>.Tone {
        if !pd.contractValid { return .warn }
        if rails.ld.pgood && rails.tec.pgood { return .ok }
        return .caution
    }
    private var pdLabel: String {
        if !pd.contractValid { return "NO PD" }
        if rails.ld.pgood && rails.tec.pgood { return "NOMINAL" }
        return "CHECK RAILS"
    }
}

/// Single labeled bar — fixed label on the left, value on the right, and a
/// 4 pt progress bar below. Animated so telemetry changes glide instead of
/// snap.
private struct PowerBar: View {
    let label: String
    let valueText: String
    let ratio: Double
    let tint: Color

    @Environment(\.bslTheme) private var t

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack {
                Text(label)
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                Spacer()
                Text(valueText)
                    .font(.system(size: 11, weight: .semibold).monospacedDigit())
                    .foregroundStyle(t.ink)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.35), value: valueText)
            }
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(t.trackFill)
                    RoundedRectangle(cornerRadius: 2)
                        .fill(tint)
                        .frame(width: max(0, min(1, ratio)) * geo.size.width)
                        .animation(.easeInOut(duration: 0.45), value: ratio)
                }
            }
            .frame(height: 4)
        }
    }
}
