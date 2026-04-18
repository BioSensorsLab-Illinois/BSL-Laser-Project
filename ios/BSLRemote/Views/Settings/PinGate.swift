import SwiftUI

struct PinGate: View {
    @Environment(AuthGate.self) private var auth
    @Environment(\.dismiss) private var dismiss

    /// Optional action to run immediately after a successful unlock. Used
    /// by call sites that want the sheet to dismiss and the work to start in
    /// the same gesture (e.g. `clear_faults`).
    var onUnlock: (() -> Void)?

    @State private var pin: String = ""
    @State private var errorShown: Bool = false
    @FocusState private var focused: Bool

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                Spacer()
                Image(systemName: "lock.shield.fill")
                    .font(.system(size: 48))
                    .foregroundStyle(.secondary)

                VStack(spacing: 6) {
                    Text("Enter access PIN")
                        .font(.headline)
                    Text("Required for safety edits and fault clears. This gate is client-side only; the firmware remains the authoritative safety gate.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 24)
                }

                SecureField("PIN", text: $pin)
                    .keyboardType(.numberPad)
                    .textContentType(.oneTimeCode)
                    .focused($focused)
                    .font(.title2.monospacedDigit())
                    .multilineTextAlignment(.center)
                    .padding(10)
                    .background(Color(.secondarySystemBackground))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .padding(.horizontal, 32)

                if errorShown {
                    Text("Incorrect PIN")
                        .font(.footnote)
                        .foregroundStyle(.red)
                }

                Button {
                    if auth.tryUnlock(candidate: pin) {
                        pin = ""
                        errorShown = false
                        onUnlock?()
                        dismiss()
                    } else {
                        errorShown = true
                        pin = ""
                    }
                } label: {
                    Text("Unlock")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .padding(.horizontal, 32)
                .disabled(pin.isEmpty)

                Spacer()
            }
            .navigationTitle("Access")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Cancel") { dismiss() }
                }
            }
            .onAppear { focused = true }
        }
    }
}
