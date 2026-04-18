import SwiftUI
import BSLProtocol

/// Wavelength display card. Tap opens `WavelengthEditorSheet`. Target comes
/// from `tec.targetLambdaNm` and actual from `tec.actualLambdaNm`. Drift is
/// the gated safety interlock — displayed here for awareness.
struct WavelengthCard: View {
    @Environment(DeviceSession.self) private var session
    let onTap: () -> Void

    private var tec: TecStatus { session.snapshot.tec }
    private var safety: SafetyStatus { session.snapshot.safety }
    private var stale: Bool { session.isStale }

    var body: some View {
        CardShell {
            HStack {
                Text("Wavelength")
                Spacer()
                if safety.lambdaDriftBlocked {
                    PillIndicator(label: "DRIFT", on: true, onColor: .orange)
                }
                Image(systemName: "chevron.right")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        } content: {
            VStack(spacing: 10) {
                ReadoutRow(label: "Target", value: String(format: "%.2f nm", tec.targetLambdaNm), stale: stale)
                ReadoutRow(
                    label: "Actual",
                    value: String(format: "%.2f nm", tec.actualLambdaNm),
                    tone: safety.lambdaDriftBlocked ? .warn : .good,
                    stale: stale
                )
                ReadoutRow(
                    label: "Drift",
                    value: String(format: "%+.2f nm", safety.lambdaDriftNm),
                    stale: stale
                )
                ReadoutRow(
                    label: "Drift limit",
                    value: String(format: "±%.1f nm", safety.lambdaDriftLimitNm),
                    stale: stale
                )
            }
            .contentShape(Rectangle())
            .onTapGesture { onTap() }
        }
    }
}
