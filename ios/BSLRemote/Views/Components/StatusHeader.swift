import SwiftUI
import BSLProtocol

/// Top bar: brand glyph + "BioSensors Lab" + state line + pills + gear.
/// Mirrors `bsl-laser-system/project/BSL Laser Controller.html:631-688`.
struct StatusHeader: View {
    let connected: Bool
    let mode: LaserMode
    let hasFault: Bool
    let hasCaution: Bool
    let sessionSeconds: Int
    let onOpenSettings: () -> Void

    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 8) {
            HStack(spacing: 10) {
                BrandGlyph(size: 32)
                VStack(alignment: .leading, spacing: 1) {
                    Text("BioSensors Lab")
                        .font(.system(size: 10, weight: .semibold))
                        .tracking(0.4)
                        .foregroundStyle(t.muted)
                        .lineLimit(1)
                    HStack(spacing: 5) {
                        LiveDot(color: dotColor, size: 6)
                        Text("HHLS · \(stateText)")
                            .font(.system(size: 12, weight: .bold))
                            .tracking(0.3)
                            .foregroundStyle(t.ink)
                            .lineLimit(1)
                    }
                }
                .frame(minWidth: 0, alignment: .leading)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            HStack(spacing: 5) {
                BSLPill(connected ? .ok : .neutral) {
                    Text(connected ? "LINKED" : "OFFLINE")
                }
                BSLPill(.neutral) {
                    Text(timeLabel)
                        .monospacedDigit()
                }
                Button(action: onOpenSettings) {
                    Image(systemName: "gearshape")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(t.muted)
                        .frame(width: 30, height: 30)
                        .background(t.surface)
                        .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
                        .overlay(
                            RoundedRectangle(cornerRadius: 9, style: .continuous)
                                .strokeBorder(t.border, lineWidth: 0.5)
                        )
                }
                .accessibilityLabel("Settings")
            }
            .fixedSize(horizontal: true, vertical: false)
        }
        .padding(.horizontal, 16)
        .padding(.top, 14)
        .padding(.bottom, 10)
    }

    private var stateText: String {
        if hasFault    { return "SYSTEM FAULT" }
        if hasCaution  { return "CAUTION" }
        if !connected  { return "DISCONNECTED" }
        return mode.label
    }

    private var dotColor: Color {
        if hasFault   { return BSL.warn }
        if hasCaution { return BSL.caution }
        if !connected { return t.dim }
        switch mode {
        case .lasing:   return BSL.orange
        case .armed:    return BSL.ok
        case .disarmed: return t.muted
        }
    }

    private var timeLabel: String {
        let m = sessionSeconds / 60
        let s = sessionSeconds % 60
        return String(format: "%02d:%02d", m, s)
    }
}
