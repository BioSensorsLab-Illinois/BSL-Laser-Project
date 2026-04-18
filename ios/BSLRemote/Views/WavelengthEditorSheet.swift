import SwiftUI
import BSLProtocol

struct WavelengthEditorSheet: View {
    @Environment(DeviceSession.self) private var session
    @Environment(AuthGate.self) private var auth
    @Environment(\.dismiss) private var dismiss

    @State private var targetText: String = ""
    @State private var applying: Bool = false
    @State private var resultBanner: String?
    @State private var showingPin: Bool = false

    private var tec: TecStatus { session.snapshot.tec }
    private var safety: SafetyStatus { session.snapshot.safety }

    var body: some View {
        NavigationStack {
            List {
                Section("Live") {
                    LabeledContent("Target", value: String(format: "%.2f nm", tec.targetLambdaNm))
                    LabeledContent("Actual", value: String(format: "%.2f nm", tec.actualLambdaNm))
                    LabeledContent("Drift", value: String(format: "%+.2f nm", safety.lambdaDriftNm))
                    LabeledContent("Drift limit", value: String(format: "±%.2f nm", safety.lambdaDriftLimitNm))
                    LabeledContent("TEC target", value: String(format: "%.2f °C", tec.targetTempC))
                    LabeledContent("TEC actual", value: String(format: "%.2f °C", tec.tempC))
                }

                Section {
                    HStack {
                        Text("New target")
                        Spacer()
                        TextField("nm", text: $targetText)
                            .keyboardType(.decimalPad)
                            .multilineTextAlignment(.trailing)
                            .frame(maxWidth: 120)
                    }
                    Text("Firmware resolves this against the wavelength LUT and stages the TEC target. The firmware rejects the command unless deployment is active and ready, and clamps the request to the LUT range.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } header: {
                    Text("Edit")
                }

                if let banner = resultBanner {
                    Section { Text(banner).font(.footnote) }
                }
            }
            .navigationTitle("Wavelength")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Close") { dismiss() }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        if auth.isUnlocked {
                            Task { await apply() }
                        } else {
                            showingPin = true
                        }
                    } label: {
                        if applying { ProgressView() } else { Text("Apply") }
                    }
                    .disabled(applying || targetText.isEmpty)
                }
            }
            .onAppear {
                targetText = String(format: "%.2f", tec.targetLambdaNm)
            }
            .sheet(isPresented: $showingPin) {
                PinGate {
                    Task { await apply() }
                    showingPin = false
                }
                .environment(auth)
            }
        }
    }

    private func apply() async {
        guard let value = Double(targetText) else {
            resultBanner = "Enter a numeric nm value."
            return
        }
        applying = true
        defer { applying = false }
        let result = await session.sendCommand(
            "operate.set_target",
            args: [
                "mode": .string("lambda"),
                "lambda_nm": .double(value),
            ]
        )
        switch result {
        case .success(let resp):
            resultBanner = resp.ok ? "Staged." : "Rejected: \(resp.error ?? "unknown reason")"
        case .failure(let err):
            resultBanner = "Failed to send: \(err.localizedDescription)"
        }
    }
}
