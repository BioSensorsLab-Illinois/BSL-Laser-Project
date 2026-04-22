import SwiftUI
import UIKit
import BSLProtocol

/// Sticky bottom action bar.
///
/// Modes (driven by `LaserMode` + the firmware deployment block):
///   - Disarmed, checklist idle: "HOLD TO ARM LASER" (1 s hold issues
///     `deployment.enter` + `deployment.run`)
///   - Disarmed, checklist running: a disabled, greyed chip that shows the
///     live checklist step so the operator sees progress instead of a
///     stuck enabled button
///   - Armed:    STOP (left) + "ARMED" status chip (right)
///   - Lasing:   STOP (left) + "LASING" status chip (right)
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

    private var deployment: DeploymentSnapshot { session.snapshot.deployment }

    /// True when the firmware is mid-checklist or transitioning through an
    /// entry phase. Any of these states means "do not fire another
    /// deployment.enter / deployment.run — we are already in the middle of
    /// one". Note that `.failed` must NOT qualify as busy — the operator
    /// needs a path back (retry or exit) when the checklist failed.
    private var checklistBusy: Bool {
        if deployment.phase == .failed { return false }
        if deployment.running { return true }
        if deployment.active && !deployment.ready { return true }
        switch deployment.phase {
        case .entry, .checklist: return true
        case .readyIdle, .failed, .inactive, .unknown: return false
        }
    }

    /// True when firmware has parked in the failed-deployment state.
    /// Surfaces the RETRY / EXIT pair in place of HOLD TO ARM.
    private var deploymentFailed: Bool {
        deployment.phase == .failed
    }

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
            if deploymentFailed {
                retryRow
            } else if checklistBusy {
                checklistBusyButton
            } else {
                armButton
            }
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
                .animation(.linear(duration: 0.05), value: holdProgress)
            }
            HStack {
                Spacer()
                Text(armLabel)
                    .font(.system(size: 15, weight: .bold))
                    .tracking(0.5)
                    .foregroundStyle(armEnabled ? Color.white : t.dim)
                    .contentTransition(.opacity)
                Spacer()
            }
        }
        .frame(height: 56)
        .shadow(color: armEnabled ? BSL.orange.opacity(0.3) : .clear, radius: 12, x: 0, y: 6)
        .contentShape(Rectangle())
        .gesture(pressAndHold)
        .animation(.easeInOut(duration: 0.2), value: armEnabled)
    }

    /// Shown in place of HOLD TO ARM when the firmware has parked in
    /// `deployment.phase == .failed`. Left button (orange-filled) re-runs
    /// the checklist; right button (outlined warn) drops the deployment
    /// mode entirely so the operator is back to DISARMED baseline.
    private var retryRow: some View {
        HStack(spacing: 8) {
            Button {
                Task { await sendRetry() }
            } label: {
                HStack(spacing: 6) {
                    if sending {
                        ProgressView().tint(.white)
                    } else {
                        Image(systemName: "arrow.clockwise")
                            .font(.system(size: 14, weight: .semibold))
                    }
                    Text(sending ? "RETRYING…" : "RETRY CHECKLIST")
                        .font(.system(size: 14, weight: .bold))
                        .tracking(0.5)
                }
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity)
                .frame(height: 56)
                .background(BSL.orange)
                .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
                .shadow(color: BSL.orange.opacity(0.30), radius: 12, x: 0, y: 6)
            }
            .buttonStyle(.plain)
            .disabled(sending || !connected)

            Button {
                Task { await sendExit() }
            } label: {
                Text("EXIT")
                    .font(.system(size: 12, weight: .heavy))
                    .tracking(1)
                    .foregroundStyle(BSL.warn)
                    .frame(width: 66, height: 56)
                    .background(t.surface)
                    .overlay(
                        RoundedRectangle(cornerRadius: 16, style: .continuous)
                            .strokeBorder(BSL.warn, lineWidth: 1.5)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
            }
            .buttonStyle(.plain)
            .disabled(sending)
        }
    }

    /// Disabled arm-button replacement that shows the live checklist step
    /// and a DETERMINATE progress strip driven by the firmware-reported
    /// `currentStepIndex`, so the operator has concrete feedback that the
    /// controller is progressing through the 11-step checklist. Rendered
    /// whenever `checklistBusy == true`. 2026-04-20 (issue 2): replaces
    /// the old indeterminate sweep that left the operator guessing
    /// whether anything was happening.
    private var checklistBusyButton: some View {
        ZStack(alignment: .leading) {
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(t.trackFill)
            determinateProgressFill
                .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
            HStack(spacing: 10) {
                ProgressView()
                    .progressViewStyle(.circular)
                    .tint(BSL.orange)
                    .scaleEffect(0.7)
                VStack(alignment: .leading, spacing: 2) {
                    Text(busyHeadline)
                        .font(.system(size: 12, weight: .heavy))
                        .tracking(0.8)
                        .foregroundStyle(t.ink)
                    Text(busyDetail)
                        .font(.system(size: 10, weight: .medium))
                        .foregroundStyle(t.muted)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                Spacer(minLength: 0)
                Text(stepCountLine)
                    .font(.system(size: 11, weight: .bold).monospacedDigit())
                    .foregroundStyle(t.muted)
            }
            .padding(.horizontal, 14)
            .frame(maxWidth: .infinity)
        }
        .frame(height: 56)
        .accessibilityLabel("Deployment checklist running, \(busyDetail), \(stepCountLine)")
    }

    /// Determinate-progress fill scaled to `currentStepIndex / total`.
    /// Hides when the step index is 0 / NONE so the bar doesn't flash
    /// full-orange at the very start of deployment entry.
    private var determinateProgressFill: some View {
        let total = max(1, DeploymentStepLabel.total)
        let idx = deployment.currentStepIndex
        let ratio = min(1.0, max(0.0, Double(idx) / Double(total)))
        return GeometryReader { geo in
            Rectangle()
                .fill(BSL.orange.opacity(0.20))
                .frame(width: geo.size.width * ratio)
                .animation(.easeInOut(duration: 0.3), value: ratio)
        }
    }

    private var stepCountLine: String {
        let total = DeploymentStepLabel.total
        let idx = deployment.currentStepIndex
        guard idx > 0 else { return "— / \(total)" }
        return "\(idx) / \(total)"
    }

    /// Soft left-to-right sweep highlight. Gives a "working" feel without
    /// breaking the Uncodixfy ban on glass/gradient-heavy chrome; only a
    /// single semi-transparent bar pans back and forth.
    private var indeterminateSweep: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: false)) { timeline in
            let t0 = timeline.date.timeIntervalSinceReferenceDate
            let phase = (t0.truncatingRemainder(dividingBy: 1.6)) / 1.6
            GeometryReader { geo in
                let w = geo.size.width
                Rectangle()
                    .fill(BSL.orange.opacity(0.18))
                    .frame(width: max(40, w * 0.30))
                    .offset(x: CGFloat(phase) * (w + 60) - 60, y: 0)
            }
        }
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

    /// Hard disable rule: the arm button only becomes active when the
    /// controller is connected, no master-warning fault is latched, we are
    /// not mid-send, and the firmware is NOT already running a deployment
    /// entry / checklist. The UI reflects the firmware state; it does not
    /// race around it.
    private var armEnabled: Bool {
        connected && !hasWarning && !sending && !checklistBusy
    }

    private var armLabel: String {
        if sending { return "ENTERING DEPLOYMENT…" }
        if !connected { return "AWAITING LINK" }
        if hasWarning { return "CLEAR FAULT FIRST" }
        if checklistBusy { return "DEPLOYMENT · WORKING" }
        if holdProgress > 0 { return "HOLD TO ARM · \(Int(holdProgress * 100))%" }
        return "HOLD TO ARM LASER"
    }

    /// Top line of the busy button. Prefers the deployment phase over the
    /// raw flag bits because operators scanning quickly want the word, not
    /// the state math.
    private var busyHeadline: String {
        switch deployment.phase {
        case .entry:     return "ENTERING DEPLOYMENT"
        case .checklist: return "RUNNING CHECKLIST"
        case .readyIdle: return "FINALIZING"
        case .failed:    return "DEPLOYMENT FAILED"
        case .inactive, .unknown:
            return deployment.running ? "DEPLOYMENT BUSY" : "DEPLOYMENT WORKING"
        }
    }

    /// Second line — surfaces the current step label from firmware's
    /// `deployment.currentStepKey` (e.g. RAIL_SEQUENCE → "Sequencing TEC
    /// → LD rails"). 2026-04-20 (issue 2): was previously a generic
    /// "please wait" — operators asked for concrete step names so they
    /// can correlate with the actual hardware sequence.
    private var busyDetail: String {
        if deployment.phase == .failed {
            if !deployment.primaryFailureReason.isEmpty {
                return "Failed: \(deployment.primaryFailureReason)"
            }
            return "Tap the fault banner for details."
        }
        let stepKey = deployment.currentStepKey
        if !stepKey.isEmpty && stepKey.uppercased() != "NONE" {
            return DeploymentStepLabel.label(for: stepKey)
        }
        if deployment.active && !deployment.ready {
            return "Stabilizing rails and TEC before ready-idle"
        }
        return "Please wait"
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

    /// Hold-to-arm = stage wavelength, enter deployment mode, start the
    /// checklist.
    ///
    /// User directive 2026-04-19: the checklist MUST settle to the
    /// operator's staged wavelength, not the firmware default of 25 °C.
    /// We re-send `operate.set_target` right before `deployment.enter` so
    /// the firmware's wavelength-LUT→TEC target is correct at the moment
    /// the deployment snapshot captures it. Prefer the persisted value
    /// (what the operator chose in WavelengthEditorSheet); fall back to
    /// the live firmware target if we have one; otherwise skip and let
    /// firmware use its prior staged target.
    private func sendArm() async {
        sending = true
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        await stageWavelengthIfNeeded()
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

    /// Retry after a failed checklist. We re-stage the wavelength (in case
    /// firmware dropped the prior target during failure handling) and then
    /// call `deployment.run` without re-entering — the firmware is still
    /// in deployment mode on a failed checklist. If firmware already
    /// reset back to inactive, a fresh enter+run fires as well.
    private func sendRetry() async {
        sending = true
        UIImpactFeedbackGenerator(style: .medium).impactOccurred()
        await stageWavelengthIfNeeded()
        let snap = session.snapshot
        if snap.deployment.active {
            _ = await session.sendCommand("deployment.run")
        } else {
            let enterResult = await session.sendCommand("deployment.enter")
            if case .success(let resp) = enterResult, !resp.ok {
                sending = false
                return
            }
            _ = await session.sendCommand("deployment.run")
        }
        sending = false
    }

    /// Operator-chose exit from a failed state — drops deployment, bar
    /// reverts to HOLD TO ARM.
    private func sendExit() async {
        sending = true
        _ = await session.sendCommand("deployment.exit")
        sending = false
    }

    private func stageWavelengthIfNeeded() async {
        let persisted = session.persistedWavelengthNm ?? 0
        let live = session.snapshot.tec.targetLambdaNm
        let nm: Double = persisted > 0 ? persisted : live
        guard nm > 0 else { return }
        // 2026-04-20 (issue diagnosis): firmware's `operate.set_target`
        // parser keys on `target_mode`, not `mode`. The old `mode` key
        // was silently ignored — the command still worked because
        // `lambda_nm` presence triggers the lambda branch as a
        // fallback, but the intent was ambiguous. Sending `target_mode`
        // here to match firmware (`laser_controller_comms.c:5938-5945`).
        _ = await session.sendCommand(
            "operate.set_target",
            args: [
                "target_mode": .string("lambda"),
                "lambda_nm": .double(nm),
            ]
        )
    }

    private func sendStop() async {
        sending = true
        UINotificationFeedbackGenerator().notificationOccurred(.warning)
        // 2026-04-20 (issue diagnosis: "stop button fails, reverts to
        // armed"): swap command order so `deployment.exit` lands FIRST.
        // Exit is the authoritative off-switch — it synchronously sets
        // `deployment.active=false`, drives all outputs to safe, and
        // clears the bench request block. Firing `operate.set_output`
        // first was purely a belt-and-braces safety zero of the
        // commanded current, but it also meant a 7 s transport timeout
        // on `operate.set_output` would delay the actual exit, leaving
        // the operator with an armed device longer than necessary.
        // Exit-first guarantees the hardware drops regardless of what
        // happens on the next command.
        _ = await session.sendCommand("deployment.exit")
        _ = await session.sendCommand(
            "operate.set_output",
            args: ["enable": .bool(false), "current_a": .double(0)]
        )
        sending = false
    }
}

