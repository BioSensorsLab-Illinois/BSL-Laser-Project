import SwiftUI
import BSLProtocol

/// Compact TEC/LD temperature tile. Mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:868-880`.
struct TemperatureMiniCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    private var tec: TecStatus { session.snapshot.tec }
    private var safety: SafetyStatus { session.snapshot.safety }
    private var stale: Bool { session.isStale }

    var body: some View {
        BSLCard(pad: 14) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    ThermGlyph().frame(width: 16, height: 16).foregroundStyle(Color.blue)
                    Spacer()
                    BSLPill(tempGood ? .ok : .caution) {
                        Text(tempGood ? "LOCKED" : "DRIFT")
                    }
                }
                Text("TEC · LD TEMP")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                HStack(alignment: .firstTextBaseline, spacing: 3) {
                    let value = stale ? "—" : String(format: "%.2f", tec.tempC)
                    Text(value)
                        .font(.system(size: 26, weight: .bold).monospacedDigit())
                        .foregroundStyle(t.ink)
                        .contentTransition(.numericText())
                        .animation(.easeInOut(duration: 0.35), value: value)
                    Text("°C")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(t.muted)
                }
                Text(String(format: "Target %.2f °C", tec.targetTempC))
                    .font(.system(size: 10))
                    .foregroundStyle(t.muted)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.35), value: tec.targetTempC)
            }
        }
    }

    private var tempGood: Bool {
        tec.tempGood && abs(tec.tempC - tec.targetTempC) <= safety.tecReadyToleranceC
    }
}

private struct ThermGlyph: View {
    var body: some View {
        Canvas { ctx, size in
            var p = Path()
            let w = size.width, h = size.height
            p.move(to: CGPoint(x: w * 0.40, y: h * 0.12))
            p.addQuadCurve(to: CGPoint(x: w * 0.60, y: h * 0.12), control: CGPoint(x: w * 0.50, y: h * 0.05))
            p.addLine(to: CGPoint(x: w * 0.60, y: h * 0.60))
            p.addArc(center: CGPoint(x: w * 0.50, y: h * 0.75), radius: w * 0.18,
                     startAngle: .degrees(-60), endAngle: .degrees(240), clockwise: false)
            p.addLine(to: CGPoint(x: w * 0.40, y: h * 0.12))
            ctx.stroke(p, with: .color(.primary), style: StrokeStyle(lineWidth: 1.6, lineJoin: .round))
            ctx.fill(
                Path(ellipseIn: CGRect(x: w * 0.42, y: h * 0.68, width: w * 0.16, height: w * 0.16)),
                with: .color(.primary)
            )
        }
    }
}
