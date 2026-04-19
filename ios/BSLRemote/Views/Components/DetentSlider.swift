import SwiftUI
import UIKit

/// Horizontal 10-detent power slider with haptic tick on detent crossings.
/// Mirrors `bsl-laser-system/project/BSL Laser Controller.html:1346-1466`.
///
/// - `value` is expressed in the same unit as `upperBound` (Amps for NIR
///   current). Internally the control snaps to `steps` equal detents and
///   reports the snapped value.
/// - `dimmed` = still interactive, subdued visuals (setpoint-editable while
///   disarmed).
/// - `disabled` = fully inert (while lasing the setpoint is locked).
struct DetentSlider: View {
    @Binding var value: Double
    var upperBound: Double
    var steps: Int = 10
    var color: Color = BSL.orange
    var disabled: Bool = false
    var dimmed: Bool = false
    var onCommit: (() -> Void)? = nil

    @Environment(\.bslTheme) private var t
    @State private var isDragging = false
    @State private var lastStep: Int = -1

    private let haptics = UIImpactFeedbackGenerator(style: .light)

    var body: some View {
        VStack(spacing: 4) {
            HStack {
                Text("0%")
                Spacer(); Text("25")
                Spacer(); Text("50")
                Spacer(); Text("75")
                Spacer(); Text("MAX")
            }
            .font(.system(size: 9, weight: .semibold))
            .tracking(0.5)
            .foregroundStyle(t.muted)

            GeometryReader { geo in
                let w = geo.size.width
                let pct = clamp01(value / max(upperBound, .ulpOfOne))
                let fillW = w * pct

                ZStack(alignment: .leading) {
                    // Track
                    RoundedRectangle(cornerRadius: 4)
                        .fill(t.trackFill)
                        .frame(height: 8)
                        .padding(.vertical, 14)

                    // Fill
                    RoundedRectangle(cornerRadius: 4)
                        .fill(fillColor)
                        .opacity(dimmed ? 0.45 : 1.0)
                        .frame(width: max(fillW, 0), height: 8)
                        .padding(.vertical, 14)
                        .animation(isDragging ? nil : .easeOut(duration: 0.18), value: value)

                    // Notches
                    ZStack(alignment: .leading) {
                        ForEach(0...steps, id: \.self) { i in
                            let x = CGFloat(i) / CGFloat(steps) * w
                            let isMajor = i % 5 == 0
                            let currentStep = Int((pct * Double(steps)).rounded())
                            let hit = i <= currentStep
                            Rectangle()
                                .fill(notchColor(hit: hit, isCurrent: i == currentStep))
                                .frame(width: 2, height: isMajor ? 16 : 10)
                                .offset(x: x - 1)
                        }
                    }
                    .frame(height: 36)
                    .allowsHitTesting(false)

                    // Thumb
                    Circle()
                        .fill(Color.white)
                        .overlay(
                            Circle().stroke(dimmed ? t.muted : activeColor, lineWidth: 2)
                        )
                        .frame(width: 24, height: 24)
                        .shadow(color: .black.opacity(0.15), radius: 2, x: 0, y: 1)
                        .offset(x: fillW - 12, y: 0)
                        .animation(isDragging ? nil : .easeOut(duration: 0.18), value: value)
                        .padding(.vertical, 6)
                        .allowsHitTesting(false)
                }
                .frame(height: 36)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { g in
                            guard !disabled else { return }
                            isDragging = true
                            commit(at: g.location.x, width: w)
                        }
                        .onEnded { _ in
                            isDragging = false
                            if !disabled { onCommit?() }
                        }
                )
            }
            .frame(height: 36)
            .opacity(disabled ? 0.45 : 1.0)

            HStack {
                Text(dimmed ? "SETPOINT" : "STEP")
                    .foregroundStyle(t.muted)
                    .font(.system(size: 10, weight: .semibold))
                + Text(" ")
                + Text("\(currentStep)/\(steps)")
                    .font(.system(size: 10, weight: .bold).monospacedDigit())
                    .foregroundStyle(t.ink)
                Spacer()
                Text(dimmed ? "Preset target · arm to emit" : "Drag to adjust · 10 % detents")
                    .font(.system(size: 10, weight: .regular))
                    .foregroundStyle(t.muted)
            }
        }
    }

    private var activeColor: Color { disabled ? t.dim : (dimmed ? t.muted : color) }
    private var fillColor:   Color { dimmed ? t.muted : color }

    private var currentStep: Int {
        let pct = clamp01(value / max(upperBound, .ulpOfOne))
        return Int((pct * Double(steps)).rounded())
    }

    private func notchColor(hit: Bool, isCurrent: Bool) -> Color {
        if isCurrent { return .clear }
        if hit { return Color.white.opacity(0.6) }
        return t.dark ? Color.white.opacity(0.25) : BSL.navy.opacity(0.22)
    }

    private func commit(at x: CGFloat, width: CGFloat) {
        let ratio = max(0, min(1, Double(x / max(width, 1))))
        let step = Int((ratio * Double(steps)).rounded())
        let snapped = Double(step) / Double(steps) * upperBound
        if step != lastStep {
            haptics.prepare()
            haptics.impactOccurred(intensity: 0.5)
            lastStep = step
        }
        value = snapped
    }

    private func clamp01(_ x: Double) -> Double { max(0, min(1, x)) }
}
