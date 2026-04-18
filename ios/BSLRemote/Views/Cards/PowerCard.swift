import SwiftUI
import BSLProtocol

/// NIR laser power card. Set value = `bench.requestedCurrentA`; actual =
/// `laser.measuredCurrentA`. Slider bounded by `safety.maxLaserCurrentA`
/// (the firmware-enforced ceiling). Disabled when the firmware reports a
/// blocked reason via `bench.hostControlReadiness.nirBlockedReason`. The
/// reason chip is tappable and surfaces the operator-facing explanation.
struct PowerCard: View {
    @Environment(DeviceSession.self) private var session

    @State private var sliderValue: Double = 0
    @State private var userIsDragging: Bool = false

    private var bench: BenchControlStatus { session.snapshot.bench }
    private var laser: LaserStatus { session.snapshot.laser }
    private var safety: SafetyStatus { session.snapshot.safety }

    private var blocked: Bool { bench.hostControlReadiness.nirBlockedReason != .none }
    private var stale: Bool { session.isStale }

    var body: some View {
        CardShell {
            HStack {
                Text("NIR power")
                Spacer()
                if blocked {
                    BlockedReasonChip(
                        reason: bench.hostControlReadiness.nirBlockedReason.rawValue,
                        tooltip: bench.hostControlReadiness.nirBlockedReason.operatorText
                    )
                } else if laser.nirEnabled {
                    PillIndicator(label: "ON AIR", on: true, onColor: .red)
                }
            }
        } content: {
            VStack(spacing: 10) {
                ReadoutRow(
                    label: "Set",
                    value: String(format: "%.2f A", bench.requestedCurrentA),
                    stale: stale
                )
                ReadoutRow(
                    label: "Actual",
                    value: String(format: "%.2f A", laser.measuredCurrentA),
                    tone: laser.loopGood ? .good : .warn,
                    stale: stale
                )
                ReadoutRow(
                    label: "Ceiling",
                    value: String(format: "%.2f A", safety.maxLaserCurrentA),
                    tone: .normal,
                    stale: stale
                )

                Slider(
                    value: $sliderValue,
                    in: 0 ... max(safety.maxLaserCurrentA, 0.1),
                    step: 0.1,
                    onEditingChanged: { editing in
                        userIsDragging = editing
                        if !editing { commit() }
                    }
                )
                .disabled(blocked || stale)

                HStack {
                    Button {
                        Task { await toggleOutput(enable: false) }
                    } label: {
                        Label("Off", systemImage: "power")
                    }
                    .buttonStyle(.bordered)
                    .disabled(blocked || stale)

                    Spacer()

                    Button {
                        Task { await toggleOutput(enable: true) }
                    } label: {
                        Label("On at slider", systemImage: "power.circle.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.red)
                    .disabled(blocked || stale)
                }
            }
            .onChange(of: bench.requestedCurrentA) { _, new in
                if !userIsDragging { sliderValue = new }
            }
            .onAppear {
                sliderValue = bench.requestedCurrentA
            }
        }
    }

    private func commit() {
        let clamped = min(max(sliderValue, 0), safety.maxLaserCurrentA)
        Task {
            _ = await session.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(session.snapshot.bench.requestedNirEnabled),
                    "current_a": .double(clamped),
                ]
            )
        }
    }

    private func toggleOutput(enable: Bool) async {
        let clamped = min(max(sliderValue, 0), safety.maxLaserCurrentA)
        _ = await session.sendCommand(
            "operate.set_output",
            args: [
                "enable": .bool(enable),
                "current_a": .double(clamped),
            ]
        )
    }
}
