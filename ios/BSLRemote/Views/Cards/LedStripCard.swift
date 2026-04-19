import SwiftUI
import BSLProtocol

/// Compact visible-LED strip. Tap the bulb icon to toggle on/off; slider
/// adjusts duty cycle. Mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:830-853`.
///
/// Wire protocol: `operate.set_led { enable, duty_cycle_pct }`. Firmware
/// clamps to `safety.maxTofLedDutyCyclePct` and rejects when deployment
/// is not active.
struct LedStripCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    @State private var lastNonZero: Int = 50
    @State private var pendingCommit: Bool = false
    @State private var localValue: Double = 0

    private var bench: BenchControlStatus { session.snapshot.bench }
    private var cap: Int { session.snapshot.safety.maxTofLedDutyCyclePct }
    private var requested: Int { bench.requestedLedDutyCyclePct }
    private var blocked: Bool { bench.hostControlReadiness.ledBlockedReason != .none }

    var body: some View {
        BSLCard(pad: 14) {
            HStack(spacing: 12) {
                Button(action: toggle) {
                    Image(systemName: requested > 0 ? "lightbulb.fill" : "lightbulb")
                        .font(.system(size: 18, weight: .medium))
                        .foregroundStyle(requested > 0 ? Color.white : Color.blue)
                        .frame(width: 36, height: 36)
                        .background(
                            RoundedRectangle(cornerRadius: 10)
                                .fill(requested > 0 ? Color.blue : (t.dark ? Color.blue.opacity(0.15) : Color(red: 0.933, green: 0.945, blue: 0.976)))
                        )
                        .shadow(color: requested > 0 ? Color.blue.opacity(0.35) : .clear, radius: 4, x: 0, y: 2)
                }
                .buttonStyle(.plain)
                .disabled(blocked)
                .accessibilityLabel(requested > 0 ? "Turn LED off" : "Turn LED on")

                VStack(spacing: 4) {
                    HStack {
                        Text("VISIBLE LED")
                            .font(.system(size: 11, weight: .heavy))
                            .tracking(1)
                            .foregroundStyle(t.muted)
                        Spacer()
                        HStack(alignment: .firstTextBaseline, spacing: 1) {
                            Text(session.isStale ? "—" : "\(requested)")
                                .font(.system(size: 14, weight: .bold).monospacedDigit())
                                .foregroundStyle(t.ink)
                            Text("%")
                                .font(.system(size: 11, weight: .medium))
                                .foregroundStyle(t.muted)
                        }
                    }
                    Slider(
                        value: $localValue,
                        in: 0...Double(max(cap, 1)),
                        step: 1,
                        onEditingChanged: { editing in
                            if !editing { commit() }
                        }
                    )
                    .tint(Color.blue)
                    .disabled(blocked)
                }
            }
        }
        .onAppear { localValue = Double(requested) }
        .onChange(of: requested) { _, new in
            if !pendingCommit { localValue = Double(new) }
            if new > 0 { lastNonZero = new }
        }
    }

    private func toggle() {
        let newValue = requested > 0 ? 0 : max(lastNonZero, 10)
        Task {
            pendingCommit = true
            defer { pendingCommit = false }
            _ = await session.sendCommand(
                "operate.set_led",
                args: [
                    "enable": .bool(newValue > 0),
                    "duty_cycle_pct": .int(min(newValue, cap)),
                ]
            )
        }
    }

    private func commit() {
        let clamped = min(max(Int(localValue.rounded()), 0), cap)
        Task {
            pendingCommit = true
            defer { pendingCommit = false }
            _ = await session.sendCommand(
                "operate.set_led",
                args: [
                    "enable": .bool(clamped > 0),
                    "duty_cycle_pct": .int(clamped),
                ]
            )
        }
    }
}
