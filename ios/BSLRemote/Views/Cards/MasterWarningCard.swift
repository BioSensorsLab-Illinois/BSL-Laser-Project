import SwiftUI
import BSLProtocol

/// Renders only when `fault.latched == true`. A latched fault is
/// non-auto-recoverable per `docs/protocol-spec.md:624-632`. The "Clear
/// faults" action is PIN-gated because the firmware will only clear if the
/// recovery criteria are genuinely met — but inadvertent clears while the
/// operator is not looking at the full context are still worth blocking.
struct MasterWarningCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth

    @State private var showingPin: Bool = false
    @State private var clearing: Bool = false

    private var fault: FaultSummary { session.snapshot.fault }

    var body: some View {
        if fault.latched {
            card
        } else {
            EmptyView()
        }
    }

    private var card: some View {
        CardShell {
            HStack {
                Text("Master Warning")
                Spacer()
                PillIndicator(label: "LATCHED", on: true, onColor: .red)
            }
        } content: {
            VStack(alignment: .leading, spacing: 10) {
                ReadoutRow(label: "Code", value: fault.latchedCode, tone: .bad)
                ReadoutRow(label: "Class", value: fault.latchedClass, tone: .bad)
                if !fault.latchedReason.isEmpty {
                    Text(fault.latchedReason)
                        .font(.footnote)
                        .foregroundStyle(.primary)
                        .padding(8)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(Color(.tertiarySystemBackground))
                        .clipShape(RoundedRectangle(cornerRadius: 6))
                }
                if let diag = fault.triggerDiag, !diag.expr.isEmpty {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Trip diagnostic")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                        Text(diag.expr)
                            .font(.caption.monospaced())
                        HStack(spacing: 12) {
                            Text("LD PG: \(diag.ldPgoodForMs) ms")
                            Text("SBDN ≠ OFF: \(diag.sbdnNotOffForMs) ms")
                        }
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                    }
                }
                ReadoutRow(label: "Trip count", value: "\(fault.tripCounter)")
                if let iso = fault.lastFaultAtIso {
                    ReadoutRow(label: "Last at", value: iso)
                }

                HStack {
                    Spacer()
                    Button {
                        if auth.isUnlocked {
                            Task { await clearFaults() }
                        } else {
                            showingPin = true
                        }
                    } label: {
                        if clearing {
                            ProgressView()
                        } else {
                            Label("Clear faults", systemImage: "arrow.counterclockwise")
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.red)
                    .disabled(clearing)
                }
            }
        }
        .sheet(isPresented: $showingPin) {
            PinGate {
                Task { await clearFaults() }
                showingPin = false
            }
            .environment(auth)
        }
    }

    private func clearFaults() async {
        clearing = true
        defer { clearing = false }
        _ = await session.sendCommand("clear_faults")
    }
}
