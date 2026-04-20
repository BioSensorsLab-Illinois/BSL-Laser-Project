import SwiftUI
import UIKit

/// Four-digit PIN gate modelled after `SettingsStack.jsx::PinGate`. Navy
/// lock tile, four orange dots, 3×4 numeric keypad with a dedicated
/// backspace key, and a shake on a wrong entry. Stays client-side only —
/// `AuthGate` verifies the SHA-256 of `<pin><salt>`. The firmware remains
/// the authoritative safety gate.
struct PinGate: View {
    @Environment(AuthGate.self) private var auth
    @Environment(\.dismiss) private var dismiss
    @Environment(\.bslTheme) private var t

    /// Optional action invoked immediately after a successful unlock. Used
    /// by call sites that want the sheet to dismiss and downstream work to
    /// start in one gesture (e.g. the Clear Faults flow). If set, the
    /// closure is responsible for dismissing the sheet — we don't dismiss
    /// automatically in that path.
    var onUnlock: (() -> Void)? = nil

    @State private var pin: String = ""
    @State private var shake: Bool = false
    @State private var shakeOffset: CGFloat = 0

    var body: some View {
        BSLThemeProvider {
            ZStack(alignment: .top) {
                t.bg.ignoresSafeArea()
                VStack(spacing: 0) {
                    BSLNavBar(title: "Access", backLabel: "Cancel", onBack: { dismiss() })
                    content
                }
            }
        }
    }

    private var content: some View {
        ScrollView {
            VStack(spacing: 20) {
                Spacer(minLength: 8)

                ZStack {
                    RoundedRectangle(cornerRadius: 32, style: .continuous).fill(BSL.navy)
                    Image(systemName: "lock.fill")
                        .font(.system(size: 26, weight: .semibold))
                        .foregroundStyle(.white)
                        .overlay(
                            Circle()
                                .fill(BSL.orange)
                                .frame(width: 5, height: 5)
                                .offset(y: 5)
                        )
                }
                .frame(width: 64, height: 64)

                VStack(spacing: 6) {
                    Text("Enter access PIN")
                        .font(.system(size: 17, weight: .semibold))
                        .foregroundStyle(t.ink)
                    Text("Required for safety edits and fault clears. This gate is client-side only; the firmware remains the authoritative safety gate.")
                        .font(.system(size: 12))
                        .foregroundStyle(t.muted)
                        .multilineTextAlignment(.center)
                        .lineSpacing(3)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: 320)
                        .padding(.horizontal, 24)
                }

                pinDots
                    .offset(x: shakeOffset)
                    .animation(shake
                                ? .interpolatingSpring(stiffness: 600, damping: 8)
                                : .default, value: shakeOffset)

                keypad
                    .frame(maxWidth: 320)
                    .padding(.horizontal, 24)

                Text("shipping PIN 2012")
                    .font(.system(size: 10.5))
                    .tracking(0.3)
                    .foregroundStyle(t.dim)

                Spacer(minLength: 24)
            }
            .padding(.vertical, 16)
            .frame(maxWidth: .infinity)
        }
    }

    private var pinDots: some View {
        HStack(spacing: 12) {
            ForEach(0..<4, id: \.self) { i in
                ZStack {
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(pin.count > i ? BSL.orange : t.border, lineWidth: 1.5)
                        .background(
                            RoundedRectangle(cornerRadius: 12, style: .continuous).fill(t.rowFill)
                        )
                    if pin.count > i {
                        Circle().fill(BSL.orange).frame(width: 12, height: 12)
                    }
                }
                .frame(width: 48, height: 56)
                .animation(.easeInOut(duration: 0.15), value: pin.count)
            }
        }
    }

    private var keypad: some View {
        let keys: [[String]] = [
            ["1", "2", "3"],
            ["4", "5", "6"],
            ["7", "8", "9"],
            ["",  "0", "⌫"],
        ]
        return VStack(spacing: 10) {
            ForEach(keys, id: \.self) { row in
                HStack(spacing: 10) {
                    ForEach(row, id: \.self) { k in
                        if k.isEmpty {
                            Color.clear.frame(maxWidth: .infinity, maxHeight: .infinity)
                        } else {
                            KeypadKey(label: k) { press(k) }
                        }
                    }
                }
                .frame(height: 52)
            }
        }
    }

    private func press(_ k: String) {
        UIImpactFeedbackGenerator(style: .light).impactOccurred()
        if k == "⌫" {
            pin = String(pin.dropLast())
            return
        }
        guard pin.count < 4 else { return }
        pin += k
        if pin.count == 4 {
            submit()
        }
    }

    private func submit() {
        let candidate = pin
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 180_000_000)
            if auth.tryUnlock(candidate: candidate) {
                UINotificationFeedbackGenerator().notificationOccurred(.success)
                pin = ""
                if let onUnlock {
                    onUnlock()
                } else {
                    dismiss()
                }
            } else {
                UINotificationFeedbackGenerator().notificationOccurred(.error)
                shakeOffset = -10
                withAnimation(.easeOut(duration: 0.08)) { shakeOffset = 10 }
                withAnimation(.easeOut(duration: 0.08).delay(0.08)) { shakeOffset = -6 }
                withAnimation(.easeOut(duration: 0.08).delay(0.16)) { shakeOffset = 0 }
                try? await Task.sleep(nanoseconds: 240_000_000)
                pin = ""
            }
        }
    }
}

/// Single keypad key — subtle fill, thin border, tabular-digit label.
private struct KeypadKey: View {
    let label: String
    let action: () -> Void

    @Environment(\.bslTheme) private var t

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.system(size: 20, weight: .regular).monospacedDigit())
                .foregroundStyle(t.ink)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(t.rowFill)
                .overlay(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(t.border, lineWidth: 0.5)
                )
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
        .buttonStyle(.plain)
    }
}
