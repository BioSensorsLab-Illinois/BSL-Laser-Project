import SwiftUI
import UIKit
import BSLProtocol

/// Safety-parameters editor. Redesigned 2026-04-19 against the design
/// bundle's `SettingsStack.jsx::SafetyForm`. Inset-grouped cards with
/// label/value/unit rows, Apply chip in the custom nav bar, and the
/// realtime firmware-range validator preserved from the prior iteration
/// (mirrors `laser_controller_config_validate_runtime_safety` in
/// `components/laser_controller/src/laser_controller_config.c:196-296`).
struct SafetyParametersForm: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.dismiss) private var dismiss
    @Environment(\.bslTheme) private var t

    @State private var form: Form = Form()
    @State private var applying = false
    @State private var resultBanner: String?
    @State private var bannerIsError: Bool = false

    private var safety: SafetyStatus { session.snapshot.safety }
    private var issues: [Issue] { form.validate() }
    private var dirty: Bool { form.differs(from: safety) }

    var body: some View {
        BSLThemeProvider {
            ZStack {
                t.bg.ignoresSafeArea()
                VStack(spacing: 0) {
                    BSLNavBar(title: "Safety parameters", onBack: { dismiss() }) {
                        if applying {
                            ProgressView().tint(BSL.orange)
                        } else {
                            Button { Task { await apply() } } label: {
                                Text("Apply")
                                    .font(.system(size: 15, weight: .semibold))
                                    .foregroundStyle(applyDisabled ? t.dim : BSL.orange)
                            }
                            .buttonStyle(.plain)
                            .disabled(applyDisabled)
                        }
                    }
                    ScrollView {
                        content
                            .padding(.horizontal, 18)
                            .padding(.top, 18)
                            .padding(.bottom, 32)
                            .frame(maxWidth: 560)
                            .frame(maxWidth: .infinity, alignment: .center)
                    }
                }
            }
            .onAppear { form.load(from: safety) }
            .onChange(of: safety) { _, new in
                // 2026-04-20: do not clobber operator edits while dirty.
                // Live telemetry arrives every 1 s; adopting the firmware
                // state mid-edit would revert every typed character.
                if !applying && !dirty { form.load(from: new) }
            }
        }
    }

    private var applyDisabled: Bool { applying || !issues.isEmpty || !dirty }

    private var content: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("All fields go to integrate.set_safety on Apply. Firmware rejects while the beam is active.")
                .font(.system(size: 11.5))
                .foregroundStyle(t.muted)
                .lineSpacing(2)
                .padding(.horizontal, 4)
                .padding(.bottom, 14)
                .fixedSize(horizontal: false, vertical: true)

            horizonGroup
            tofGroup
            stalenessGroup
            driftGroup
            ldGroup
            tecCommandGroup
            currentsGroup

            validitySection
            if let banner = resultBanner {
                banner2(banner, isError: bannerIsError)
                    .padding(.top, 4)
            }
        }
    }

    // MARK: - Groups

    private var horizonGroup: some View {
        BSLListGroup(label: "Horizon") {
            doubleRow("Threshold", .double(\.horizonThresholdDeg), key: .horizonThresholdDeg, unit: "°")
            doubleRow("Hysteresis", .double(\.horizonHysteresisDeg), key: .horizonHysteresisDeg, unit: "°", isLast: true)
        }
    }
    private var tofGroup: some View {
        BSLListGroup(label: "ToF distance") {
            doubleRow("Min range", .double(\.tofMinRangeM), key: .tofMinRangeM, unit: "m")
            doubleRow("Max range", .double(\.tofMaxRangeM), key: .tofMaxRangeM, unit: "m")
            doubleRow("Hysteresis", .double(\.tofHysteresisM), key: .tofHysteresisM, unit: "m", isLast: true)
        }
    }
    private var stalenessGroup: some View {
        BSLListGroup(label: "Staleness") {
            intRow("IMU stale", .int(\.imuStaleMs), key: .imuStaleMs, unit: "ms")
            intRow("ToF stale", .int(\.tofStaleMs), key: .tofStaleMs, unit: "ms")
            intRow("Rail good timeout", .int(\.railGoodTimeoutMs), key: .railGoodTimeoutMs, unit: "ms", isLast: true)
        }
    }
    private var driftGroup: some View {
        BSLListGroup(label: "Lambda drift") {
            doubleRow("Limit", .double(\.lambdaDriftLimitNm), key: .lambdaDriftLimitNm, unit: "nm")
            doubleRow("Hysteresis", .double(\.lambdaDriftHysteresisNm), key: .lambdaDriftHysteresisNm, unit: "nm")
            intRow("Hold", .int(\.lambdaDriftHoldMs), key: .lambdaDriftHoldMs, unit: "ms", isLast: true)
        }
    }
    private var ldGroup: some View {
        BSLListGroup(label: "LD protection") {
            doubleRow("Overtemp limit", .double(\.ldOvertempLimitC), key: .ldOvertempLimitC, unit: "°C")
            doubleRow("TEC ADC trip", .double(\.tecTempAdcTripV), key: .tecTempAdcTripV, unit: "V")
            doubleRow("TEC ADC hyst", .double(\.tecTempAdcHysteresisV), key: .tecTempAdcHysteresisV, unit: "V")
            intRow("TEC ADC hold", .int(\.tecTempAdcHoldMs), key: .tecTempAdcHoldMs, unit: "ms", isLast: true)
        }
    }
    private var tecCommandGroup: some View {
        BSLListGroup(label: "TEC command") {
            doubleRow("Min command", .double(\.tecMinCommandC), key: .tecMinCommandC, unit: "°C")
            doubleRow("Max command", .double(\.tecMaxCommandC), key: .tecMaxCommandC, unit: "°C")
            doubleRow("Ready tolerance", .double(\.tecReadyToleranceC), key: .tecReadyToleranceC, unit: "°C", isLast: true)
        }
    }
    private var currentsGroup: some View {
        BSLListGroup(label: "Currents + duty") {
            doubleRow("Max laser current", .double(\.maxLaserCurrentA), key: .maxLaserCurrentA, unit: "A")
            doubleRow("Off-current thresh", .double(\.offCurrentThresholdA), key: .offCurrentThresholdA, unit: "A")
            intRow("Max ToF LED duty", .int(\.maxTofLedDutyCyclePct), key: .maxTofLedDutyCyclePct, unit: "%")
            doubleRow("LIO offset", .double(\.lioVoltageOffsetV), key: .lioVoltageOffsetV, unit: "V", isLast: true)
        }
    }

    @ViewBuilder private var validitySection: some View {
        if issues.isEmpty {
            HStack(spacing: 8) {
                Image(systemName: "checkmark.seal.fill").foregroundStyle(BSL.ok)
                Text("All values within firmware policy ranges.")
                    .font(.system(size: 12))
                    .foregroundStyle(t.muted)
            }
            .padding(12)
            .background(BSL.okSoft.opacity(t.dark ? 0.15 : 1.0))
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .padding(.bottom, 8)
        } else {
            VStack(alignment: .leading, spacing: 8) {
                Text("OUT-OF-RANGE · APPLY DISABLED")
                    .font(.system(size: 10, weight: .heavy))
                    .tracking(0.6)
                    .foregroundStyle(BSL.warn)
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(issues) { issue in
                        HStack(alignment: .top, spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundStyle(BSL.warn)
                                .font(.system(size: 11, weight: .bold))
                            VStack(alignment: .leading, spacing: 2) {
                                Text(issue.label)
                                    .font(.system(size: 12, weight: .semibold))
                                    .foregroundStyle(t.ink)
                                Text(issue.message)
                                    .font(.system(size: 11))
                                    .foregroundStyle(t.muted)
                            }
                        }
                    }
                }
            }
            .padding(12)
            .background(BSL.warnSoft.opacity(t.dark ? 0.15 : 1.0))
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .strokeBorder(BSL.warn.opacity(0.35), lineWidth: 0.5)
            )
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            .padding(.bottom, 8)
        }
    }

    @ViewBuilder private func banner2(_ msg: String, isError: Bool) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: isError ? "exclamationmark.triangle.fill" : "checkmark.circle.fill")
                .foregroundStyle(isError ? BSL.warn : BSL.ok)
            Text(msg)
                .font(.system(size: 12))
                .foregroundStyle(t.ink)
                .lineSpacing(2)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(12)
        .background((isError ? BSL.warnSoft : BSL.okSoft).opacity(t.dark ? 0.18 : 1.0))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder((isError ? BSL.warn : BSL.ok).opacity(0.35), lineWidth: 0.5)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }

    // MARK: - Numeric rows

    /// A single number row in a BSLListGroup — label, text field (right-aligned,
    /// monospaced tabular digits) with select-all-on-focus, unit badge.
    /// `key` wires into the realtime validator so the row flashes red + the
    /// Apply chip greys out.
    @ViewBuilder private func doubleRow(
        _ label: String,
        _ access: Access,
        key: FieldKey,
        unit: String,
        isLast: Bool = false
    ) -> some View {
        let invalid = issues.contains(where: { $0.key == key })
        BSLListRow(label: label, isLast: isLast) {
            HStack(spacing: 6) {
                SelectAllTextField<Double>.makeDouble(
                    value: accessBinding(access),
                    textColor: uiColor(invalid ? BSL.warn : t.ink)
                )
                .frame(width: 72, height: 26)
                .padding(.horizontal, 8)
                .padding(.vertical, 5)
                .background(t.surface2)
                .overlay(
                    RoundedRectangle(cornerRadius: 7, style: .continuous)
                        .strokeBorder(invalid ? BSL.warn : t.border, lineWidth: 0.5)
                )
                .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
                Text(unit)
                    .font(.system(size: 11))
                    .foregroundStyle(t.muted)
                    .frame(minWidth: 22, alignment: .leading)
            }
        }
    }

    @ViewBuilder private func intRow(
        _ label: String,
        _ access: Access,
        key: FieldKey,
        unit: String,
        isLast: Bool = false
    ) -> some View {
        let invalid = issues.contains(where: { $0.key == key })
        BSLListRow(label: label, isLast: isLast) {
            HStack(spacing: 6) {
                SelectAllTextField<Int>.makeInt(
                    value: accessIntBinding(access),
                    textColor: uiColor(invalid ? BSL.warn : t.ink)
                )
                .frame(width: 72, height: 26)
                .padding(.horizontal, 8)
                .padding(.vertical, 5)
                .background(t.surface2)
                .overlay(
                    RoundedRectangle(cornerRadius: 7, style: .continuous)
                        .strokeBorder(invalid ? BSL.warn : t.border, lineWidth: 0.5)
                )
                .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
                Text(unit)
                    .font(.system(size: 11))
                    .foregroundStyle(t.muted)
                    .frame(minWidth: 22, alignment: .leading)
            }
        }
    }

    /// SwiftUI Color doesn't cross the UIKit boundary cleanly for semi-
    /// transparent theme values. We resolve against the current trait
    /// collection so the text color tracks light/dark/OR appearance.
    private func uiColor(_ color: Color) -> UIColor {
        UIColor(color).resolvedColor(
            with: UITraitCollection(userInterfaceStyle: t.dark ? .dark : .light)
        )
    }

    // MARK: - Key-path shims

    /// Enum carrying the writable key-path so we can reuse `doubleRow`/
    /// `intRow` without type-level gymnastics.
    private enum Access {
        case double(WritableKeyPath<Form, Double>)
        case int(WritableKeyPath<Form, Int>)
    }

    private func accessBinding(_ access: Access) -> Binding<Double> {
        guard case .double(let kp) = access else { return .constant(0) }
        return Binding(
            get: { form[keyPath: kp] },
            set: { form[keyPath: kp] = $0 }
        )
    }
    private func accessIntBinding(_ access: Access) -> Binding<Int> {
        guard case .int(let kp) = access else { return .constant(0) }
        return Binding(
            get: { form[keyPath: kp] },
            set: { form[keyPath: kp] = $0 }
        )
    }

    // MARK: - Apply

    private func apply() async {
        applying = true
        defer { applying = false }
        let args: [String: CommandArg] = form.buildArgs(currentInterlocks: safety.interlocks)
        let result = await session.sendCommand("integrate.set_safety", args: args)
        switch result {
        case .success(let resp):
            if resp.ok {
                resultBanner = "Applied. Firmware persisted to NVS."
                bannerIsError = false
                UINotificationFeedbackGenerator().notificationOccurred(.success)
                session.reportSuccess("Safety parameters applied.")
            } else {
                resultBanner = "Rejected: \(resp.error ?? "unknown reason")"
                bannerIsError = true
                UINotificationFeedbackGenerator().notificationOccurred(.error)
            }
        case .failure(let err):
            resultBanner = "Failed to send: \(err.localizedDescription)"
            bannerIsError = true
            UINotificationFeedbackGenerator().notificationOccurred(.error)
        }
    }
}

private enum FieldKey: Hashable {
    case horizonThresholdDeg, horizonHysteresisDeg
    case tofMinRangeM, tofMaxRangeM, tofHysteresisM
    case imuStaleMs, tofStaleMs, railGoodTimeoutMs
    case lambdaDriftLimitNm, lambdaDriftHysteresisNm, lambdaDriftHoldMs
    case ldOvertempLimitC
    case tecTempAdcTripV, tecTempAdcHysteresisV, tecTempAdcHoldMs
    case tecMinCommandC, tecMaxCommandC, tecReadyToleranceC
    case maxLaserCurrentA, offCurrentThresholdA
    case maxTofLedDutyCyclePct, lioVoltageOffsetV
}

private struct Issue: Identifiable {
    let id = UUID()
    let key: FieldKey
    let label: String
    let message: String
}

private struct Form: Equatable {
    var horizonThresholdDeg: Double = 0
    var horizonHysteresisDeg: Double = 3
    var tofMinRangeM: Double = 0.2
    var tofMaxRangeM: Double = 1.0
    var tofHysteresisM: Double = 0.02
    var imuStaleMs: Int = 50
    var tofStaleMs: Int = 100
    var railGoodTimeoutMs: Int = 250
    var lambdaDriftLimitNm: Double = 5.0
    var lambdaDriftHysteresisNm: Double = 0.5
    var lambdaDriftHoldMs: Int = 2000
    var ldOvertempLimitC: Double = 55.0
    var tecTempAdcTripV: Double = 2.45
    var tecTempAdcHysteresisV: Double = 0.05
    var tecTempAdcHoldMs: Int = 2000
    var tecMinCommandC: Double = 15.0
    var tecMaxCommandC: Double = 35.0
    var tecReadyToleranceC: Double = 0.25
    var maxLaserCurrentA: Double = 5.0
    var offCurrentThresholdA: Double = 0.2
    var maxTofLedDutyCyclePct: Int = 50
    var lioVoltageOffsetV: Double = 0.07

    mutating func load(from s: SafetyStatus) {
        horizonThresholdDeg = s.horizonThresholdDeg
        horizonHysteresisDeg = s.horizonHysteresisDeg
        tofMinRangeM = s.tofMinRangeM
        tofMaxRangeM = s.tofMaxRangeM
        tofHysteresisM = s.tofHysteresisM
        imuStaleMs = s.imuStaleMs
        tofStaleMs = s.tofStaleMs
        railGoodTimeoutMs = s.railGoodTimeoutMs
        lambdaDriftLimitNm = s.lambdaDriftLimitNm
        lambdaDriftHysteresisNm = s.lambdaDriftHysteresisNm
        lambdaDriftHoldMs = s.lambdaDriftHoldMs
        ldOvertempLimitC = s.ldOvertempLimitC
        tecTempAdcTripV = s.tecTempAdcTripV
        tecTempAdcHysteresisV = s.tecTempAdcHysteresisV
        tecTempAdcHoldMs = s.tecTempAdcHoldMs
        tecMinCommandC = s.tecMinCommandC
        tecMaxCommandC = s.tecMaxCommandC
        tecReadyToleranceC = s.tecReadyToleranceC
        maxLaserCurrentA = s.maxLaserCurrentA
        offCurrentThresholdA = s.offCurrentThresholdA
        maxTofLedDutyCyclePct = s.maxTofLedDutyCyclePct
        lioVoltageOffsetV = s.lioVoltageOffsetV
    }

    func differs(from s: SafetyStatus) -> Bool {
        if horizonThresholdDeg != s.horizonThresholdDeg { return true }
        if horizonHysteresisDeg != s.horizonHysteresisDeg { return true }
        if tofMinRangeM != s.tofMinRangeM { return true }
        if tofMaxRangeM != s.tofMaxRangeM { return true }
        if tofHysteresisM != s.tofHysteresisM { return true }
        if imuStaleMs != s.imuStaleMs { return true }
        if tofStaleMs != s.tofStaleMs { return true }
        if railGoodTimeoutMs != s.railGoodTimeoutMs { return true }
        if lambdaDriftLimitNm != s.lambdaDriftLimitNm { return true }
        if lambdaDriftHysteresisNm != s.lambdaDriftHysteresisNm { return true }
        if lambdaDriftHoldMs != s.lambdaDriftHoldMs { return true }
        if ldOvertempLimitC != s.ldOvertempLimitC { return true }
        if tecTempAdcTripV != s.tecTempAdcTripV { return true }
        if tecTempAdcHysteresisV != s.tecTempAdcHysteresisV { return true }
        if tecTempAdcHoldMs != s.tecTempAdcHoldMs { return true }
        if tecMinCommandC != s.tecMinCommandC { return true }
        if tecMaxCommandC != s.tecMaxCommandC { return true }
        if tecReadyToleranceC != s.tecReadyToleranceC { return true }
        if maxLaserCurrentA != s.maxLaserCurrentA { return true }
        if offCurrentThresholdA != s.offCurrentThresholdA { return true }
        if maxTofLedDutyCyclePct != s.maxTofLedDutyCyclePct { return true }
        if lioVoltageOffsetV != s.lioVoltageOffsetV { return true }
        return false
    }

    /// Mirrors `laser_controller_config_validate_runtime_safety`
    /// (`components/laser_controller/src/laser_controller_config.c:196-296`).
    /// Any rule here that does NOT also exist in firmware is a drift bug.
    func validate() -> [Issue] {
        var out: [Issue] = []

        if tofMinRangeM <= 0 {
            out.append(Issue(key: .tofMinRangeM, label: "Min range (m)", message: "Must be > 0."))
        }
        if tofMinRangeM > 5.0 {
            out.append(Issue(key: .tofMinRangeM, label: "Min range (m)", message: "Must be ≤ 5.0 m."))
        }
        if tofMaxRangeM <= tofMinRangeM {
            out.append(Issue(key: .tofMaxRangeM, label: "Max range (m)", message: "Must be > min range."))
        }
        if tofMaxRangeM > 10.0 {
            out.append(Issue(key: .tofMaxRangeM, label: "Max range (m)", message: "Must be ≤ 10.0 m."))
        }
        if tofHysteresisM <= 0 {
            out.append(Issue(key: .tofHysteresisM, label: "Hysteresis (m)", message: "Must be > 0."))
        }
        if horizonHysteresisDeg <= 0 {
            out.append(Issue(key: .horizonHysteresisDeg, label: "Hysteresis (°)", message: "Must be > 0."))
        }
        if lambdaDriftLimitNm <= 0 {
            out.append(Issue(key: .lambdaDriftLimitNm, label: "Limit (nm)", message: "Must be > 0."))
        }
        if lambdaDriftHysteresisNm < 0 {
            out.append(Issue(key: .lambdaDriftHysteresisNm, label: "Hysteresis (nm)", message: "Must be ≥ 0."))
        }
        if lambdaDriftHysteresisNm >= lambdaDriftLimitNm {
            out.append(Issue(key: .lambdaDriftHysteresisNm, label: "Hysteresis (nm)", message: "Must be < limit."))
        }
        if tecTempAdcTripV <= 0 {
            out.append(Issue(key: .tecTempAdcTripV, label: "TEC ADC trip (V)", message: "Must be > 0."))
        }
        if tecTempAdcTripV > 3.3 {
            out.append(Issue(key: .tecTempAdcTripV, label: "TEC ADC trip (V)", message: "Must be ≤ 3.3 V."))
        }
        if tecTempAdcHysteresisV < 0 {
            out.append(Issue(key: .tecTempAdcHysteresisV, label: "TEC ADC hysteresis (V)", message: "Must be ≥ 0."))
        }
        if tecTempAdcHysteresisV >= tecTempAdcTripV {
            out.append(Issue(key: .tecTempAdcHysteresisV, label: "TEC ADC hysteresis (V)", message: "Must be < trip."))
        }
        if tecMinCommandC >= tecMaxCommandC {
            out.append(Issue(key: .tecMinCommandC, label: "Min command (°C)", message: "Must be < max command."))
        }
        if tecReadyToleranceC <= 0 {
            out.append(Issue(key: .tecReadyToleranceC, label: "Ready tolerance (°C)", message: "Must be > 0."))
        }
        if maxLaserCurrentA <= 0 {
            out.append(Issue(key: .maxLaserCurrentA, label: "Max laser current (A)", message: "Must be > 0."))
        }
        if maxLaserCurrentA > 5.2 {
            out.append(Issue(key: .maxLaserCurrentA, label: "Max laser current (A)", message: "Hardware ceiling is 5.2 A."))
        }
        if offCurrentThresholdA < 0 {
            out.append(Issue(key: .offCurrentThresholdA, label: "Off-current threshold (A)", message: "Must be ≥ 0."))
        }
        if ldOvertempLimitC < 20 || ldOvertempLimitC > 120 {
            out.append(Issue(key: .ldOvertempLimitC, label: "Overtemp limit (°C)", message: "Must be between 20 and 120 °C."))
        }
        if maxTofLedDutyCyclePct < 0 || maxTofLedDutyCyclePct > 100 {
            out.append(Issue(key: .maxTofLedDutyCyclePct, label: "Max ToF LED duty (%)", message: "Must be 0..100."))
        }
        if lioVoltageOffsetV < -0.5 || lioVoltageOffsetV > 0.5 {
            out.append(Issue(key: .lioVoltageOffsetV, label: "LIO offset (V)", message: "Must be between −0.5 and +0.5 V."))
        }
        if imuStaleMs <= 0 {
            out.append(Issue(key: .imuStaleMs, label: "IMU stale (ms)", message: "Must be > 0."))
        }
        if tofStaleMs <= 0 {
            out.append(Issue(key: .tofStaleMs, label: "ToF stale (ms)", message: "Must be > 0."))
        }
        if railGoodTimeoutMs <= 0 {
            out.append(Issue(key: .railGoodTimeoutMs, label: "Rail good timeout (ms)", message: "Must be > 0."))
        }
        if lambdaDriftHoldMs <= 0 {
            out.append(Issue(key: .lambdaDriftHoldMs, label: "Lambda drift hold (ms)", message: "Must be > 0."))
        }
        if tecTempAdcHoldMs <= 0 {
            out.append(Issue(key: .tecTempAdcHoldMs, label: "TEC ADC hold (ms)", message: "Must be > 0."))
        }
        return out
    }

    func buildArgs(currentInterlocks locks: InterlockEnableMask) -> [String: CommandArg] {
        [
            "horizon_threshold_deg": .double(horizonThresholdDeg),
            "horizon_hysteresis_deg": .double(horizonHysteresisDeg),
            "tof_min_range_m": .double(tofMinRangeM),
            "tof_max_range_m": .double(tofMaxRangeM),
            "tof_hysteresis_m": .double(tofHysteresisM),
            "imu_stale_ms": .int(imuStaleMs),
            "tof_stale_ms": .int(tofStaleMs),
            "rail_good_timeout_ms": .int(railGoodTimeoutMs),
            "lambda_drift_limit_nm": .double(lambdaDriftLimitNm),
            "lambda_drift_hysteresis_nm": .double(lambdaDriftHysteresisNm),
            "lambda_drift_hold_ms": .int(lambdaDriftHoldMs),
            "ld_overtemp_limit_c": .double(ldOvertempLimitC),
            "tec_temp_adc_trip_v": .double(tecTempAdcTripV),
            "tec_temp_adc_hysteresis_v": .double(tecTempAdcHysteresisV),
            "tec_temp_adc_hold_ms": .int(tecTempAdcHoldMs),
            "tec_min_command_c": .double(tecMinCommandC),
            "tec_max_command_c": .double(tecMaxCommandC),
            "tec_ready_tolerance_c": .double(tecReadyToleranceC),
            "max_laser_current_a": .double(maxLaserCurrentA),
            "off_current_threshold_a": .double(offCurrentThresholdA),
            "max_tof_led_duty_cycle_pct": .int(maxTofLedDutyCyclePct),
            "lio_voltage_offset_v": .double(lioVoltageOffsetV),
            "interlock_horizon_enabled": .bool(locks.horizonEnabled),
            "interlock_distance_enabled": .bool(locks.distanceEnabled),
            "interlock_lambda_drift_enabled": .bool(locks.lambdaDriftEnabled),
            "interlock_tec_temp_adc_enabled": .bool(locks.tecTempAdcEnabled),
            "interlock_imu_invalid_enabled": .bool(locks.imuInvalidEnabled),
            "interlock_imu_stale_enabled": .bool(locks.imuStaleEnabled),
            "interlock_tof_invalid_enabled": .bool(locks.tofInvalidEnabled),
            "interlock_tof_stale_enabled": .bool(locks.tofStaleEnabled),
            "interlock_ld_overtemp_enabled": .bool(locks.ldOvertempEnabled),
            "interlock_ld_loop_bad_enabled": .bool(locks.ldLoopBadEnabled),
            "tof_low_bound_only": .bool(locks.tofLowBoundOnly),
        ]
    }
}
