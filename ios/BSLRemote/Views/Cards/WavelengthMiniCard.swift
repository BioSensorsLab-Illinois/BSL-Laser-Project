import SwiftUI
import BSLProtocol

/// Compact wavelength tile. Tappable — opens the wavelength editor sheet.
/// Mirrors `bsl-laser-system/project/BSL Laser Controller.html:857-867`.
struct WavelengthMiniCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t
    let onTap: () -> Void

    private var tec: TecStatus { session.snapshot.tec }
    private var safety: SafetyStatus { session.snapshot.safety }
    private var stale: Bool { session.isStale }

    var body: some View {
        BSLCard(pad: 14) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    WaveGlyph().frame(width: 16, height: 16).foregroundStyle(BSL.nir)
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(t.dim)
                }
                Text("WAVELENGTH")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                HStack(alignment: .firstTextBaseline, spacing: 3) {
                    let value = stale ? "—" : String(format: "%.1f", tec.targetLambdaNm)
                    Text(value)
                        .font(.system(size: 26, weight: .bold).monospacedDigit())
                        .foregroundStyle(t.ink)
                        .contentTransition(.numericText())
                        .animation(.easeInOut(duration: 0.35), value: value)
                    Text("nm")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(t.muted)
                }
                if safety.lambdaDriftBlocked {
                    Text(String(format: "drift %+.2f nm", safety.lambdaDriftNm))
                        .font(.system(size: 10))
                        .foregroundStyle(BSL.caution)
                } else {
                    Text("Tap to change")
                        .font(.system(size: 10))
                        .foregroundStyle(t.muted)
                }
            }
        }
        .contentShape(Rectangle())
        .onTapGesture(perform: onTap)
    }
}

private struct WaveGlyph: View {
    var body: some View {
        Canvas { ctx, size in
            var p = Path()
            let w = size.width, h = size.height
            p.move(to: CGPoint(x: w * 0.08, y: h * 0.5))
            p.addCurve(
                to: CGPoint(x: w * 0.33, y: h * 0.5),
                control1: CGPoint(x: w * 0.17, y: h * 0.2),
                control2: CGPoint(x: w * 0.25, y: h * 0.2)
            )
            p.addCurve(
                to: CGPoint(x: w * 0.58, y: h * 0.5),
                control1: CGPoint(x: w * 0.42, y: h * 0.8),
                control2: CGPoint(x: w * 0.50, y: h * 0.8)
            )
            p.addCurve(
                to: CGPoint(x: w * 0.83, y: h * 0.5),
                control1: CGPoint(x: w * 0.66, y: h * 0.2),
                control2: CGPoint(x: w * 0.75, y: h * 0.2)
            )
            p.addCurve(
                to: CGPoint(x: w * 0.95, y: h * 0.5),
                control1: CGPoint(x: w * 0.88, y: h * 0.65),
                control2: CGPoint(x: w * 0.92, y: h * 0.65)
            )
            ctx.stroke(p, with: .color(.primary), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
        }
    }
}
