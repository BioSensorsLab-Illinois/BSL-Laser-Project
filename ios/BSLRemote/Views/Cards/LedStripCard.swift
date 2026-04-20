import SwiftUI
import BSLProtocol

/// Compact visible-LED strip. Tap the bulb to toggle on/off; slider adjusts
/// duty cycle. Mirrors `bsl-laser-system/project/BSL Laser Controller.html:830-853`.
///
/// Wire protocol: `operate.set_led { enable, duty_cycle_pct }`. Firmware
/// clamps to `safety.maxTofLedDutyCyclePct` and rejects when deployment is
/// not active.
///
/// Interaction rules:
///   - While the user is dragging, telemetry does NOT override `localValue`
///     (prevents the slider from "snapping back" mid-gesture).
///   - `commit()` only fires on drag end. The firmware confirms via a new
///     `bench.requestedLedDutyCyclePct` value which then re-syncs.
///   - Toggle respects `lastNonZero` so tapping off-then-on returns to the
///     previous brightness.
struct LedStripCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    @State private var localValue: Double = 0
    @State private var isDragging: Bool = false
    @State private var awaitingAck: Bool = false
    @State private var lastNonZero: Int = 50

    private var bench: BenchControlStatus { session.snapshot.bench }
    private var cap: Int { max(session.snapshot.safety.maxTofLedDutyCyclePct, 1) }
    private var requested: Int { bench.requestedLedDutyCyclePct }
    private var blocked: Bool { bench.hostControlReadiness.ledBlockedReason != .none }
    private var stale: Bool { session.isStale }

    private var isOn: Bool {
        // Treat either the enable flag or a non-zero duty as "on" — firmware
        // conventions vary during state transitions.
        bench.requestedLedEnabled || requested > 0
    }

    var body: some View {
        BSLCard(pad: 14) {
            HStack(spacing: 12) {
                toggleButton
                VStack(spacing: 4) {
                    headerRow
                    Slider(
                        value: $localValue,
                        in: 0...Double(cap),
                        step: 1,
                        onEditingChanged: { editing in
                            isDragging = editing
                            if !editing { commit(dutyPct: Int(localValue.rounded())) }
                        }
                    )
                    .tint(Color.blue)
                    .disabled(blocked)
                }
            }
        }
        .onAppear {
            if requested > 0 {
                localValue = Double(requested)
                lastNonZero = requested
            } else if let remembered = session.persistedLedDutyPct, remembered > 0 {
                // Seed the slider from the last operator-accepted duty so
                // the bulb matches what the controller actually has (the
                // DeviceSession replays this to firmware during post-connect
                // sync). Prevents the "visible LED is on but iOS shows 0 %"
                // startup gap.
                localValue = Double(min(remembered, cap))
                lastNonZero = remembered
            }
        }
        .onChange(of: requested) { _, new in
            // Only accept telemetry updates while the user isn't actively
            // dragging. Otherwise the slider thumb fights the gesture.
            if !isDragging && !awaitingAck {
                localValue = Double(new)
            }
            if new > 0 { lastNonZero = new }
        }
    }

    private var toggleButton: some View {
        Button(action: toggle) {
            Image(systemName: isOn ? "lightbulb.fill" : "lightbulb")
                .font(.system(size: 18, weight: .medium))
                .foregroundStyle(isOn ? Color.white : Color.blue)
                .frame(width: 36, height: 36)
                .background(
                    RoundedRectangle(cornerRadius: 10)
                        .fill(isOn ? Color.blue : (t.dark ? Color.blue.opacity(0.15) : Color(red: 0.933, green: 0.945, blue: 0.976)))
                )
                .shadow(color: isOn ? Color.blue.opacity(0.35) : .clear, radius: 4, x: 0, y: 2)
        }
        .buttonStyle(.plain)
        .disabled(blocked || awaitingAck)
        .accessibilityLabel(isOn ? "Turn LED off" : "Turn LED on")
    }

    private var headerRow: some View {
        HStack {
            Text("VISIBLE LED")
                .font(.system(size: 11, weight: .heavy))
                .tracking(1)
                .foregroundStyle(t.muted)
            Spacer()
            HStack(alignment: .firstTextBaseline, spacing: 1) {
                let rounded = Int(localValue.rounded())
                Text(stale ? "—" : "\(rounded)")
                    .font(.system(size: 14, weight: .bold).monospacedDigit())
                    .foregroundStyle(t.ink)
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.25), value: rounded)
                Text("%")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(t.muted)
            }
        }
    }

    private func toggle() {
        let turnOn = !isOn
        let duty = turnOn ? min(max(lastNonZero, 10), cap) : 0
        localValue = Double(duty)
        commit(dutyPct: duty)
    }

    private func commit(dutyPct: Int) {
        let clamped = min(max(dutyPct, 0), cap)
        // Predictively pin localValue while the firmware ack is in flight so
        // it doesn't visibly snap to a stale telemetry value during the
        // 1-RTT window.
        awaitingAck = true
        localValue = Double(clamped)
        // Remember locally regardless — lets the slider survive a
        // reconnect — but don't send the runtime `operate.set_led` when
        // firmware would reject it (deployment not active, checklist
        // running, …). The rejection is noisy and the operator already
        // sees the locked slider state.
        session.rememberLed(dutyPct: clamped, enabled: clamped > 0)
        guard bench.hostControlReadiness.ledBlockedReason == .none else {
            awaitingAck = false
            return
        }
        Task {
            defer { awaitingAck = false }
            let result = await session.sendCommand(
                "operate.set_led",
                args: [
                    "enable": .bool(clamped > 0),
                    "duty_cycle_pct": .int(clamped),
                ]
            )
            if case .success(let resp) = result, resp.ok {
                session.rememberLed(dutyPct: clamped, enabled: clamped > 0)
            }
            try? await Task.sleep(nanoseconds: 300_000_000)
        }
    }
}
