import SwiftUI
import BSLProtocol

/// App-wide banner that MUST render whenever the controller is running with
/// the USB-Debug Mock layer active. Synthesized rail PGOOD and telemetry
/// values are not real — the operator must know. Per the safety-visibility
/// rule in `.agent/AGENT.md` and `host-console/src/types.ts:206-224`.
struct UsbMockBanner: View {
    let status: UsbDebugMockStatus

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Image(systemName: "bolt.slash.fill")
                Text("USB-DEBUG MOCK ACTIVE")
                    .font(.footnote.weight(.bold))
            }
            Text("TEC and LD rail PGOOD, current, and temperatures are SYNTHESIZED. Outputs are not real. Do not use telemetry to infer hardware state.")
                .font(.caption)
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.orange)
        .foregroundStyle(.black)
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}
