import SwiftUI

/// Small status strip: EXT TRIGGER · FOOTSWITCH with dynamic label + pill.
/// Mirrors `bsl-laser-system/project/BSL Laser Controller.html:1285-1323`.
struct TriggerStrip: View {
    let mode: LaserMode
    var compact: Bool = false

    @Environment(\.bslTheme) private var t

    var body: some View {
        HStack(spacing: 8) {
            ZStack {
                RoundedRectangle(cornerRadius: 6)
                    .fill(iconBg)
                    .frame(width: 22, height: 22)
                if mode == .armed {
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(style: StrokeStyle(lineWidth: 1, dash: [2, 2]))
                        .foregroundStyle(BSL.orange)
                        .frame(width: 22, height: 22)
                }
                // lightning-bolt-like glyph from the HTML
                BoltGlyph(color: iconColor)
                    .frame(width: 12, height: 12)
            }
            VStack(alignment: .leading, spacing: 1) {
                Text("EXT TRIGGER · FOOTSWITCH")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                Text(stateLine)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(t.ink)
                    .lineLimit(1)
            }
            Spacer(minLength: 8)
            BSLPill(pillTone) { Text(pillLabel) }
        }
        .padding(.top, compact ? 8 : 12)
        .overlay(
            Rectangle().fill(t.border).frame(height: 0.5), alignment: .top
        )
    }

    private var iconBg: Color {
        switch mode {
        case .lasing: return BSL.orange
        case .armed:  return BSL.orange.opacity(0.12)
        case .disarmed: return t.dark ? Color.white.opacity(0.06) : Color(red: 0.945, green: 0.952, blue: 0.972)
        }
    }
    private var iconColor: Color {
        switch mode {
        case .lasing:   return .white
        case .armed:    return BSL.orange
        case .disarmed: return t.dim
        }
    }
    private var stateLine: String {
        switch mode {
        case .lasing:   return "Depressed — emission active"
        case .armed:    return "Awaiting depression to emit"
        case .disarmed: return "Inactive — arm to enable"
        }
    }
    private var pillLabel: String {
        switch mode {
        case .lasing:   return "ACTIVE"
        case .armed:    return "READY"
        case .disarmed: return "OFF"
        }
    }
    private var pillTone: BSLPill<Text>.Tone {
        switch mode {
        case .lasing:   return .brand
        case .armed:    return .ok
        case .disarmed: return .neutral
        }
    }
}

private struct BoltGlyph: View {
    var color: Color
    var body: some View {
        Canvas { ctx, size in
            var p = Path()
            let w = size.width, h = size.height
            p.move(to: CGPoint(x: w * 0.16, y: h * 0.5))
            p.addLine(to: CGPoint(x: w * 0.41, y: h * 0.5))
            p.addLine(to: CGPoint(x: w * 0.41, y: h * 0.16))
            p.addLine(to: CGPoint(x: w * 0.83, y: h * 0.5))
            p.addLine(to: CGPoint(x: w * 0.58, y: h * 0.5))
            p.addLine(to: CGPoint(x: w * 0.58, y: h * 0.83))
            p.closeSubpath()
            ctx.fill(p, with: .color(color))
        }
    }
}
