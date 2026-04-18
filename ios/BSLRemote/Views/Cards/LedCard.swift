import SwiftUI
import BSLProtocol

/// ToF front-LED card. Set = `bench.requestedLedDutyCyclePct`; applied =
/// `bench.illuminationDutyCyclePct`. Slider ceiling is
/// `safety.maxTofLedDutyCyclePct` — firmware clamps any request above it.
/// Disabled per `bench.hostControlReadiness.ledBlockedReason`.
struct LedCard: View {
    @Environment(DeviceSession.self) private var session

    @State private var sliderValue: Double = 0
    @State private var userIsDragging: Bool = false

    private var bench: BenchControlStatus { session.snapshot.bench }
    private var safety: SafetyStatus { session.snapshot.safety }

    private var blocked: Bool { bench.hostControlReadiness.ledBlockedReason != .none }
    private var stale: Bool { session.isStale }

    var body: some View {
        CardShell {
            HStack {
                Text("ToF LED brightness")
                Spacer()
                if blocked {
                    BlockedReasonChip(
                        reason: bench.hostControlReadiness.ledBlockedReason.rawValue,
                        tooltip: bench.hostControlReadiness.ledBlockedReason.operatorText
                    )
                } else {
                    PillIndicator(
                        label: "owner: \(bench.appliedLedOwner.rawValue)",
                        on: bench.illuminationEnabled
                    )
                }
            }
        } content: {
            VStack(spacing: 10) {
                ReadoutRow(label: "Set", value: "\(bench.requestedLedDutyCyclePct) %", stale: stale)
                ReadoutRow(
                    label: "Applied",
                    value: "\(bench.illuminationDutyCyclePct) %",
                    tone: bench.appliedLedPinHigh ? .good : .normal,
                    stale: stale
                )
                ReadoutRow(label: "Cap", value: "\(safety.maxTofLedDutyCyclePct) %", stale: stale)

                Slider(
                    value: $sliderValue,
                    in: 0 ... Double(max(safety.maxTofLedDutyCyclePct, 1)),
                    step: 1,
                    onEditingChanged: { editing in
                        userIsDragging = editing
                        if !editing { commit() }
                    }
                )
                .disabled(blocked || stale)

                HStack {
                    Button {
                        Task { await toggle(enable: false) }
                    } label: { Label("Off", systemImage: "lightbulb.slash") }
                        .buttonStyle(.bordered)
                        .disabled(blocked || stale)

                    Spacer()

                    Button {
                        Task { await toggle(enable: true) }
                    } label: { Label("On", systemImage: "lightbulb") }
                        .buttonStyle(.borderedProminent)
                        .disabled(blocked || stale)
                }
            }
            .onChange(of: bench.requestedLedDutyCyclePct) { _, new in
                if !userIsDragging { sliderValue = Double(new) }
            }
            .onAppear { sliderValue = Double(bench.requestedLedDutyCyclePct) }
        }
    }

    private func commit() {
        let clamped = Int(min(max(sliderValue, 0), Double(safety.maxTofLedDutyCyclePct)))
        Task {
            _ = await session.sendCommand(
                "operate.set_led",
                args: [
                    "enable": .bool(session.snapshot.bench.requestedLedEnabled),
                    "duty_cycle_pct": .int(clamped),
                ]
            )
        }
    }

    private func toggle(enable: Bool) async {
        let clamped = Int(min(max(sliderValue, 0), Double(safety.maxTofLedDutyCyclePct)))
        _ = await session.sendCommand(
            "operate.set_led",
            args: [
                "enable": .bool(enable),
                "duty_cycle_pct": .int(clamped),
            ]
        )
    }
}
