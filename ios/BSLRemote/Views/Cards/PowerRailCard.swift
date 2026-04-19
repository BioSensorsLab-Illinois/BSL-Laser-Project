import SwiftUI
import BSLProtocol

/// Compact USB-PD / rail health row. Mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:884-898`.
struct PowerRailCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    private var pd: PdStatus { session.snapshot.pd }
    private var rails: RailsBlock { session.snapshot.rails }
    private var stale: Bool { session.isStale }

    var body: some View {
        BSLCard(pad: 14) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 10) {
                    Image(systemName: "bolt.circle")
                        .font(.system(size: 18, weight: .medium))
                        .foregroundStyle(pd.contractValid ? BSL.ok : t.dim)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(headerLine)
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundStyle(t.ink)
                            .lineLimit(1)
                        Text(detailLine)
                            .font(.system(size: 11))
                            .foregroundStyle(t.muted)
                            .lineLimit(1)
                    }
                    Spacer(minLength: 8)
                    BSLPill(pdTone) { Text(pdLabel) }
                }
                ProgressView(value: drawRatio)
                    .tint(BSL.ok)
                    .frame(height: 4)
            }
        }
    }

    private var headerLine: String {
        if stale { return "USB-PD · —" }
        return String(format: "USB-PD · %.0f V / %.2f A", pd.sourceVoltageV, pd.sourceCurrentA)
    }
    private var detailLine: String {
        if stale { return "—" }
        let avail = pd.negotiatedPowerW
        let draw  = pd.sourceVoltageV * pd.sourceCurrentA
        return String(format: "%.1f W avail · %.1f W draw", avail, draw)
    }
    private var drawRatio: Double {
        let draw = pd.sourceVoltageV * pd.sourceCurrentA
        guard pd.negotiatedPowerW > 0 else { return 0 }
        return min(1.0, draw / pd.negotiatedPowerW)
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
