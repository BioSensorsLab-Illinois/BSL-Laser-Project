import SwiftUI

/// Apple-Activity-style arc gauge with optional setpoint tick and breathing
/// preview overlay. Mirrors the SVG gauge in
/// `bsl-laser-system/project/BSL Laser Controller.html:516-566`.
struct ArcGauge<Center: View>: View {
    var value: Double              // 0 ... max
    var maxValue: Double
    var size: CGFloat
    var stroke: CGFloat
    var color: Color
    var setpoint: Double?          // nil → no tick
    var setpointColor: Color
    var breathing: Bool            // soft overlay ring at setpoint
    var startAngle: Double         // degrees, screen coords (0 = right, +CCW would be backwards; we use +CW like the HTML)
    var sweep: Double              // degrees of sweep
    var center: () -> Center

    @Environment(\.bslTheme) private var t

    init(
        value: Double,
        maxValue: Double = 100,
        size: CGFloat = 160,
        stroke: CGFloat = 12,
        color: Color = BSL.orange,
        setpoint: Double? = nil,
        setpointColor: Color = BSL.orange,
        breathing: Bool = false,
        startAngle: Double = 140,
        sweep: Double = 260,
        @ViewBuilder center: @escaping () -> Center
    ) {
        self.value = value
        self.maxValue = maxValue
        self.size = size
        self.stroke = stroke
        self.color = color
        self.setpoint = setpoint
        self.setpointColor = setpointColor
        self.breathing = breathing
        self.startAngle = startAngle
        self.sweep = sweep
        self.center = center
    }

    var body: some View {
        ZStack {
            // Track
            ArcShape(start: startAngle, sweep: sweep)
                .stroke(t.trackFill, style: StrokeStyle(lineWidth: stroke, lineCap: .round))
            // Value — use an interpolating spring so discrete telemetry
            // steps (integer % rounds, 0.1 A detents) animate as a
            // continuous glide instead of a visible snap.
            ArcShape(start: startAngle, sweep: sweep * clamped)
                .stroke(color, style: StrokeStyle(lineWidth: stroke, lineCap: .round))
                .animation(.interpolatingSpring(stiffness: 90, damping: 16), value: value)
                .animation(.easeInOut(duration: 0.35), value: maxValue)
            // Breathing preview (armed-but-not-lasing)
            if breathing, let sp = setpoint, sp > 0 {
                BreathingArc(start: startAngle, sweep: sweep * clamp(sp / maxValue),
                             color: BSL.orange, stroke: stroke)
            }
            // Setpoint tick
            if let sp = setpoint, sp > 0 {
                SetpointTick(
                    size: size,
                    stroke: stroke,
                    setpointPct: clamp(sp / maxValue),
                    startAngle: startAngle,
                    sweep: sweep,
                    color: setpointColor
                )
            }
            // Center content
            center()
        }
        .frame(width: size, height: size)
    }

    private var clamped: Double { clamp(value / maxValue) }
    private func clamp(_ x: Double) -> Double { max(0, min(1, x)) }
}

/// The arc path (both track and value share it).
private struct ArcShape: Shape {
    /// Degrees measured clockwise from 12 o'clock (SwiftUI-native: Angle.degrees
    /// with 0 = trailing edge). We translate the HTML's `startAngle` where
    /// `(d-90)*π/180` is applied — i.e. the HTML's angle is also measured
    /// clockwise from 12 o'clock. So we can use it directly, offset by -90° to
    /// re-origin to 3 o'clock for `Angle`.
    let start: Double
    let sweep: Double

    func path(in rect: CGRect) -> Path {
        var p = Path()
        let c = CGPoint(x: rect.midX, y: rect.midY)
        let r = min(rect.width, rect.height) / 2
        p.addArc(
            center: c,
            radius: r,
            startAngle: .degrees(start - 90),
            endAngle:   .degrees(start - 90 + sweep),
            clockwise: false
        )
        return p
    }
}

private struct SetpointTick: View {
    let size: CGFloat
    let stroke: CGFloat
    let setpointPct: Double
    let startAngle: Double
    let sweep: Double
    let color: Color

    var body: some View {
        Canvas { ctx, size in
            let c = CGPoint(x: size.width / 2, y: size.height / 2)
            let r = min(size.width, size.height) / 2
            let rIn  = r - stroke / 2 - 2
            let rOut = r + stroke / 2 + 2
            let a = (startAngle + sweep * setpointPct - 90) * .pi / 180
            let x1 = c.x + rIn  * CGFloat(cos(a))
            let y1 = c.y + rIn  * CGFloat(sin(a))
            let x2 = c.x + rOut * CGFloat(cos(a))
            let y2 = c.y + rOut * CGFloat(sin(a))
            var tick = Path()
            tick.move(to: CGPoint(x: x1, y: y1))
            tick.addLine(to: CGPoint(x: x2, y: y2))
            ctx.stroke(tick, with: .color(color.opacity(0.85)),
                       style: StrokeStyle(lineWidth: 2.5, lineCap: .round))
        }
    }
}

private struct BreathingArc: View {
    let start: Double
    let sweep: Double
    let color: Color
    let stroke: CGFloat
    @State private var phase = false

    var body: some View {
        ArcShape(start: start, sweep: sweep)
            .stroke(color,
                    style: StrokeStyle(lineWidth: stroke, lineCap: .round))
            .opacity(phase ? 0.55 : 0.25)
            .onAppear {
                withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: true)) {
                    phase = true
                }
            }
    }
}
