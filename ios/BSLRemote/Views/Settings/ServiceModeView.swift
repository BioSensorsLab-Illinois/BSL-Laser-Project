import SwiftUI
import BSLProtocol

/// Service-mode page. NIR-purple warning card, state row, Enter/Exit list,
/// confirm dialog. Mirrors the design bundle's
/// `SettingsStack.jsx::ServiceModeForm`. The firmware owns the master gate —
/// this UI only stages the command.
struct ServiceModeView: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.dismiss) private var dismiss
    @Environment(\.bslTheme) private var t
    @Environment(\.colorScheme) private var scheme

    @State private var pending: Action? = nil

    private enum Action {
        case enter, exit
    }

    /// Is service mode active on the firmware right now? Derived from the
    /// live session state; no local state required.
    private var serviceOn: Bool { session.snapshot.session.state == .serviceMode }

    var body: some View {
        BSLThemeProvider {
            ZStack {
                t.bg.ignoresSafeArea()
                VStack(spacing: 0) {
                    BSLNavBar(title: "Service mode", onBack: { dismiss() })
                    ScrollView {
                        VStack(alignment: .leading, spacing: 0) {
                            warningCard.padding(.bottom, 16)
                            stateGroup
                            controlsGroup
                        }
                        .padding(.horizontal, 18)
                        .padding(.vertical, 18)
                        .frame(maxWidth: 560)
                        .frame(maxWidth: .infinity, alignment: .center)
                    }
                }
            }
            .overlay {
                if let pending {
                    BSLConfirmDialog(
                        title: pending == .enter ? "Enter service mode?" : "Exit service mode?",
                        message: pending == .enter
                            ? "Only enter with the laser safed and with intent to perform bench work."
                            : "Exits service mode and restores normal operator gating.",
                        confirmLabel: pending == .enter ? "Enter" : "Exit",
                        confirmTint: pending == .enter ? BSL.nir : BSL.orange,
                        onConfirm: { commit(pending) },
                        onCancel: { self.pending = nil }
                    )
                    .transition(.opacity)
                }
            }
            .animation(.easeOut(duration: 0.15), value: pending)
        }
    }

    // MARK: - Slices

    private var warningCard: some View {
        HStack(alignment: .top, spacing: 10) {
            ZStack {
                RoundedRectangle(cornerRadius: 4, style: .continuous).fill(BSL.nir)
                Text("!")
                    .font(.system(size: 12, weight: .heavy))
                    .foregroundStyle(.white)
            }
            .frame(width: 20, height: 20)

            Text("Service mode is the master interlock override. **All ten interlocks are short-circuited while active.** Use only with the laser safed and with intent to perform bench work.")
                .font(.system(size: 12))
                .foregroundStyle(scheme == .dark
                                  ? Color(red: 0.78, green: 0.60, blue: 0.98)
                                  : Color(red: 0.40, green: 0.08, blue: 0.75))
                .lineSpacing(3)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(14)
        .background(BSL.nir.opacity(scheme == .dark ? 0.10 : 0.08))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(BSL.nir.opacity(scheme == .dark ? 0.40 : 0.30), lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    private var stateGroup: some View {
        BSLListGroup(label: "State") {
            BSLListRow(label: "Current state", isLast: true) {
                Text(serviceOn ? "SERVICE" : "OPERATOR")
                    .font(.system(size: 13, weight: .bold))
                    .tracking(0.5)
                    .foregroundStyle(serviceOn ? BSL.nir : t.muted)
            }
        }
    }

    private var controlsGroup: some View {
        BSLListGroup(footer: "enter_service_mode relaxes the interlock master gate. The firmware keeps beam-enable GPIOs safe while in service mode but accepts bench-only commands.") {
            BSLListRow(
                icon: Image(systemName: "bolt.fill"),
                iconTone: .navy,
                label: "Enter service mode",
                sublabel: serviceOn ? "Already in service mode" : "Disable all ten interlocks",
                action: serviceOn ? nil : { pending = .enter }
            )
            BSLListRow(
                icon: Image(systemName: "arrow.uturn.left"),
                iconTone: serviceOn ? .brand : .subtle,
                label: "Exit service mode",
                sublabel: serviceOn ? "Restore operator gating" : "Not currently in service mode",
                isLast: true,
                action: serviceOn ? { pending = .exit } : nil
            )
        }
    }

    private func commit(_ action: Action) {
        self.pending = nil
        Task {
            let cmd = action == .enter ? "enter_service_mode" : "exit_service_mode"
            let result = await session.sendCommand(cmd)
            if case .success(let resp) = result, resp.ok {
                session.reportSuccess(action == .enter
                                      ? "Service mode entered."
                                      : "Service mode exited.")
            }
        }
    }
}
