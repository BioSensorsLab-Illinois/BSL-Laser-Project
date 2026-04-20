import SwiftUI
import BSLProtocol

/// The NIR-dominant hero card. Big arc gauge on the left, three metric rows
/// on the right (Optical / Drive / Duty), 10-detent horizontal slider below,
/// and an EXT TRIGGER status strip at the bottom. Layout mirrors
/// `bsl-laser-system/project/BSL Laser Controller.html:730-828`.
///
/// Wiring:
///   - Setpoint = `bench.requestedCurrentA`
///   - Actual   = `laser.measuredCurrentA`
///   - Ceiling  = `safety.maxLaserCurrentA`
///   - Command  = `operate.set_output { enable, current_a }`
///
/// The firmware rejects any set while deployment is not ready; the reason
/// chip surfaces `nirBlockedReason.operatorText`.
struct NirHeroCard: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.bslTheme) private var t

    @State private var localSet: Double = 0
    @State private var pendingCommit: Bool = false

    private var snap: DeviceSnapshot { session.snapshot }
    private var mode: LaserMode { snap.laserMode }
    private var maxA: Double { snap.nirMaxA }
    private var setA: Double { snap.nirSetpointA }
    /// Actual measured current. Values below 5 % of the ceiling are rounded
    /// to 0 to suppress floor-level ADC noise in the display (cosmetic only —
    /// the raw `measuredCurrentA` is still what `laserMode` checks against
    /// `safety.offCurrentThresholdA` for the armed/lasing classification).
    private var actualA: Double {
        let raw = snap.nirActualA
        return raw < 0.05 * maxA ? 0 : raw
    }
    private var blockedReason: NirBlockedReason { snap.bench.hostControlReadiness.nirBlockedReason }
    private var stale: Bool { session.isStale }

    var body: some View {
        BSLCard(pad: 18) {
            VStack(alignment: .leading, spacing: 0) {
                header
                Spacer().frame(height: 12)
                HStack(alignment: .center, spacing: 16) {
                    gauge.frame(width: 160, height: 160)
                    VStack(spacing: 10) {
                        BigMetricRow(
                            label: "Optical",
                            bigValue: wattsString(actualA),
                            bigUnit: "W",
                            setValue: mode == .lasing ? nil : wattsString(setA),
                            setUnit: "W set"
                        )
                        BigMetricRow(
                            label: "Drive",
                            bigValue: amperesString(actualA),
                            bigUnit: "A",
                            setValue: mode == .lasing ? nil : amperesString(setA),
                            setUnit: "A set"
                        )
                        BigMetricRow(label: "Duty", bigValue: "CW", bigUnit: "")
                    }
                }
                Spacer().frame(height: 16)
                DetentSlider(
                    value: $localSet,
                    upperBound: maxA,
                    steps: 10,
                    color: BSL.orange,
                    disabled: mode == .lasing,
                    dimmed: mode != .lasing,
                    onCommit: { commit() }
                )
                TriggerStrip(mode: mode)
            }
        }
        .onAppear {
            // Prefer the firmware-reported setpoint. If the firmware hasn't
            // had time to publish yet (fresh reconnect, snapshot empty) and
            // we have a persisted value from the previous session, show
            // that as a placeholder — DeviceSession will re-apply it to
            // firmware during post-connect sync.
            if setA > 0 {
                localSet = setA
            } else if let remembered = session.persistedNirSetpointA, remembered > 0 {
                localSet = min(remembered, maxA)
            }
        }
        .onChange(of: setA) { _, new in
            if !pendingCommit { localSet = new }
        }
    }

    @ViewBuilder private var header: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 2) {
                Text("NIR LASER")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(1.4)
                    .foregroundStyle(t.muted)
                Text("Output Power")
                    .font(.system(size: 18, weight: .bold))
                    .foregroundStyle(t.ink)
            }
            Spacer()
            statePill
        }
    }

    @ViewBuilder private var statePill: some View {
        switch mode {
        case .lasing:
            BSLPill(.warn) {
                LiveDot(color: BSL.warn, size: 6)
                Text("LASING")
            }
        case .armed:
            BSLPill(.ok) { Text("ARMED · READY") }
        case .disarmed:
            BSLPill(.neutral) { Text("DISARMED") }
        }
    }

    @ViewBuilder private var gauge: some View {
        let displayedA = actualA < 0.05 * maxA ? 0.0 : actualA
        ArcGauge(
            value: stale ? 0 : displayedA,
            maxValue: maxA,
            size: 160,
            stroke: 12,
            color: mode == .lasing ? BSL.warn : t.muted,
            setpoint: (mode != .lasing && setA > 0) ? setA : nil,
            setpointColor: BSL.orange,
            breathing: mode == .armed && setA > 0
        ) {
            VStack(spacing: 4) {
                Text("NIR")
                    .font(.system(size: 10, weight: .semibold))
                    .tracking(1)
                    .foregroundStyle(t.muted)
                let pct = maxA > 0 ? Int((displayedA / maxA * 100).rounded()) : 0
                HStack(alignment: .firstTextBaseline, spacing: 2) {
                    Text(stale ? "—" : "\(pct)")
                        .font(.system(size: 28, weight: .bold).monospacedDigit())
                        .foregroundStyle(t.ink)
                        .contentTransition(.numericText())
                        .animation(.easeInOut(duration: 0.35), value: pct)
                    Text("%")
                        .font(.system(size: 14, weight: .medium))
                        .foregroundStyle(t.muted)
                }
                Text(subLabel)
                    .font(.system(size: 11))
                    .foregroundStyle(t.muted)
            }
        }
    }

    private var subLabel: String {
        switch mode {
        case .lasing: return "Emitting"
        case .armed:  return setA > 0 ? "Primed · idle" : "Ready"
        case .disarmed: return "Standby"
        }
    }

    private func commit() {
        let clamped = min(max(localSet, 0), maxA)
        // Firmware will reject any `operate.set_output` that arrives while
        // the NIR path is blocked (deployment not ready, fault latched,
        // rails not good, …). We still want to remember the operator's
        // intent locally so the setpoint survives reconnect, but we skip
        // the network round-trip in blocked states — it would only surface
        // a firmware-rejected banner the operator already knows about.
        session.rememberNirSetpoint(clamped)
        guard blockedReason == .none else { return }
        pendingCommit = true
        Task {
            defer { pendingCommit = false }
            let result = await session.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(snap.bench.requestedNirEnabled),
                    "current_a": .double(clamped),
                ]
            )
            if case .success(let resp) = result, resp.ok {
                session.rememberNirSetpoint(clamped)
            }
        }
    }

    // Display-only optical estimate. Real slope (W/A) comes from LD
    // calibration; 1.0 W/A is a reasonable ballpark for a 785 nm single-mode
    // LD at mid-rated current. Replace with a calibrated value once the LUT
    // is wired to telemetry.
    private static let nirSlopeWpA: Double = 1.0
    private func wattsString(_ a: Double) -> String {
        String(format: "%.2f", a * Self.nirSlopeWpA)
    }
    private func amperesString(_ a: Double) -> String {
        String(format: "%.2f", a)
    }
}
