import SwiftUI
import UIKit
import BSLProtocol

/// Sticky bottom action bar.
///   - Disarmed: "HOLD TO ARM LASER" (1 s hold triggers `deployment.enter`)
///   - Armed:    STOP (left) + "ARMED" status chip (right)
/// Mirrors `bsl-laser-system/project/BSL Laser Controller.html:1935-2025`.
///
/// The firmware is the safety authority: this view issues commands only;
/// actual state flips when the firmware confirms via telemetry.
struct DeployBar: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    let mode: LaserMode
    let connected: Bool
    let hasWarning: Bool

    @State private var holdProgress: Double = 0
    @State private var holdTask: Task<Void, Never>? = nil
    @State private var sending: Bool = false

    var body: some View {
        VStack(spacing: 10) {
            content
            footer
        }
        .padding(.horizontal, 14)
        .padding(.top, 12)
        .padding(.bottom, 18)
        .background(bgGradient, ignoresSafeAreaEdges: .bottom)
        .overlay(
            Rectangle().fill(t.border).frame(height: 0.5), alignment: .top
        )
    }

    @ViewBuilder private var content: some View {
        switch mode {
        case .disarmed:
            armButton
        case .armed, .lasing:
            HStack(spacing: 8) {
                stopButton
                armedStatus
            }
        }
    }

    private var armButton: some View {
        ZStack(alignment: .leading) {
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(armEnabled ? BSL.orange : t.trackFill)
            if holdProgress > 0 {
                GeometryReader { geo in
                    Rectangle()
                        .fill(Color.white.opacity(0.18))
                        .frame(width: geo.size.width * holdProgress)
                }
                .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
            }
            HStack {
                Spacer()
                Text(armLabel)
                    .font(.system(size: 15, weight: .bold))
                    .tracking(0.5)
                    .foregroundStyle(armEnabled ? Color.white : t.dim)
                Spacer()
            }
        }
        .frame(height: 56)
        .shadow(color: armEnabled ? BSL.orange.opacity(0.3) : .clear, radius: 12, x: 0, y: 6)
        .contentShape(Rectangle())
        .gesture(pressAndHold)
    }

    private var stopButton: some View {
        Button {
            Task { await sendStop() }
        } label: {
            Text("STOP")
                .font(.system(size: 12, weight: .heavy))
                .tracking(1)
                .foregroundStyle(.white)
                .frame(width: 66, height: 56)
                .background(BSL.warn)
                .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .strokeBorder(BSL.warn, lineWidth: 2)
                )
                .shadow(color: BSL.warn.opacity(0.3), radius: 12, x: 0, y: 6)
        }
        .buttonStyle(.plain)
        .disabled(sending)
    }

    private var armedStatus: some View {
        let tint = mode == .lasing ? BSL.warn : BSL.ok
        let softBg = mode == .lasing
            ? (t.dark ? BSL.warn.opacity(0.10) : BSL.warnSoft)
            : (t.dark ? BSL.ok.opacity(0.10)   : BSL.okSoft)
        return HStack(spacing: 10) {
            LiveDot(color: tint, size: 8)
            Text(mode == .lasing ? "LASING" : "ARMED")
                .font(.system(size: 15, weight: .heavy))
                .tracking(2)
        }
        .frame(maxWidth: .infinity)
        .frame(height: 56)
        .foregroundStyle(tint)
        .background(softBg)
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(tint, lineWidth: 1.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private var footer: some View {
        HStack(spacing: 14) {
            Label("Interlocks", systemImage: "checkmark")
            Label("TEC", systemImage: "checkmark")
            Label("Tilt", systemImage: "checkmark")
        }
        .labelStyle(.titleAndIcon)
        .font(.system(size: 10, weight: .semibold))
        .foregroundStyle(t.muted)
    }

    private var bgGradient: LinearGradient {
        LinearGradient(
            colors: [t.surface, t.bg],
            startPoint: .top, endPoint: .bottom
        )
    }

    private var armEnabled: Bool { connected && !hasWarning && !sending }

    private var armLabel: String {
        if sending { return "ENTERING DEPLOYMENT…" }
        if !connected { return "AWAITING LINK" }
        if hasWarning { return "CLEAR FAULT FIRST" }
        if holdProgress > 0 { return "HOLD TO ARM · \(Int(holdProgress * 100))%" }
        return "HOLD TO ARM LASER"
    }

    private var pressAndHold: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { _ in
                guard holdTask == nil, armEnabled else { return }
                holdTask = Task {
                    let start = Date()
                    while !Task.isCancelled {
                        let elapsed = Date().timeIntervalSince(start)
                        let p = min(1.0, elapsed / 1.0)
                        await MainActor.run { self.holdProgress = p }
                        if p >= 1.0 {
                            await sendArm()
                            break
                        }
                        try? await Task.sleep(nanoseconds: 16_000_000)
                    }
                    await MainActor.run {
                        self.holdProgress = 0
                        self.holdTask = nil
                    }
                }
            }
            .onEnded { _ in
                holdTask?.cancel()
                holdTask = nil
                withAnimation(.easeOut(duration: 0.2)) { holdProgress = 0 }
            }
    }

    /// Hold-to-arm = enter deployment mode + start the deployment checklist.
    /// The firmware runs the checklist asynchronously; when the controller
    /// lands in ready-idle (`deployment.active && deployment.ready &&
    /// deployment.readyIdle`), the mode recomputes to `.armed` and the bar
    /// redraws automatically. If deployment drops out of ready (mode reverts
    /// to `.disarmed`), the HOLD TO ARM button reappears by the same path.
    /// The app cannot enable emission — that requires the physical main
    /// button per the firmware contract.
    private func sendArm() async {
        sending = true
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        let enterResult = await session.sendCommand("deployment.enter")
        if case .success(let resp) = enterResult, !resp.ok {
            // Firmware already reported the reason via lastFirmwareError;
            // don't try to run the checklist.
            sending = false
            return
        }
        _ = await session.sendCommand("deployment.run")
        sending = false
    }

    private func sendStop() async {
        sending = true
        UINotificationFeedbackGenerator().notificationOccurred(.warning)
        _ = await session.sendCommand(
            "operate.set_output",
            args: ["enable": .bool(false), "current_a": .double(0)]
        )
        _ = await session.sendCommand("deployment.exit")
        sending = false
    }
}
