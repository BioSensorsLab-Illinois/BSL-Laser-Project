import SwiftUI
import UIKit
import BSLProtocol

/// Ten per-interlock switches plus the ToF low-bound-only flag.
/// Redesigned 2026-04-19 against the design bundle's
/// `SettingsStack.jsx::InterlocksForm`. Grouped rows, custom BSLToggle,
/// success banner + success toast + error haptic feedback are preserved
/// from the prior iteration.
struct InterlockToggles: View {
    @Environment(DeviceSession.self) private var session
    @Environment(\.dismiss) private var dismiss
    @Environment(\.bslTheme) private var t

    @State private var mask: InterlockEnableMask = InterlockEnableMask()
    @State private var applying = false
    @State private var resultBanner: String?
    @State private var bannerIsError: Bool = false

    private var safety: SafetyStatus { session.snapshot.safety }

    /// True when the draft diverges from firmware — controls the Apply button.
    private var dirty: Bool { mask != safety.interlocks }

    var body: some View {
        BSLThemeProvider {
            ZStack {
                t.bg.ignoresSafeArea()
                VStack(spacing: 0) {
                    BSLNavBar(title: "Active interlocks", onBack: { dismiss() }) {
                        Button {
                            Task { await apply() }
                        } label: {
                            if applying {
                                ProgressView().tint(BSL.orange)
                            } else {
                                Text("Apply")
                                    .font(.system(size: 15, weight: .semibold))
                                    .foregroundStyle((applying || !dirty) ? t.dim : BSL.orange)
                            }
                        }
                        .buttonStyle(.plain)
                        .disabled(applying || !dirty)
                    }
                    ScrollView {
                        content
                            .padding(.horizontal, 18)
                            .padding(.top, 18)
                            .padding(.bottom, 24)
                            .frame(maxWidth: 560)
                            .frame(maxWidth: .infinity, alignment: .center)
                    }
                }
            }
            .onAppear { mask = safety.interlocks }
            .onChange(of: safety.interlocks) { _, new in
                // 2026-04-20: only adopt the incoming firmware state when
                // the local draft is not dirty. Otherwise every 1 s live
                // telemetry tick would revert the operator's mid-edit
                // toggles before they could tap Apply (user report:
                // "unable to change nor save interlock selection").
                //
                // Once Apply succeeds, firmware echoes the new mask back,
                // `dirty` flips to false, and the next telemetry tick
                // harmlessly re-adopts the same values.
                if !applying && !dirty { mask = new }
            }
        }
    }

    private var content: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Each toggle disables one interlock. The master service-mode override still sits above these — entering service mode short-circuits all ten.")
                .font(.system(size: 11.5))
                .foregroundStyle(t.muted)
                .lineSpacing(2)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 4)
                .padding(.bottom, 14)

            geometricGroup
            driftGroup
            imuGroup
            tofGroup
            ldGroup

            if let banner = resultBanner {
                bannerView(banner, isError: bannerIsError)
            }
        }
    }

    // MARK: - Groups

    private var geometricGroup: some View {
        BSLListGroup(label: "Geometric") {
            interlockRow(.horizon)
            interlockRow(.distance)
            interlockRow(.tofLowBoundOnly, isLast: true, disabled: !mask.distanceEnabled)
        }
    }
    private var driftGroup: some View {
        BSLListGroup(label: "Drift + ADC") {
            interlockRow(.lambdaDrift)
            interlockRow(.tecTempAdc, isLast: true)
        }
    }
    private var imuGroup: some View {
        BSLListGroup(label: "IMU") {
            interlockRow(.imuInvalid)
            interlockRow(.imuStale, isLast: true)
        }
    }
    private var tofGroup: some View {
        BSLListGroup(label: "ToF") {
            interlockRow(.tofInvalid)
            interlockRow(.tofStale, isLast: true)
        }
    }
    private var ldGroup: some View {
        BSLListGroup(label: "Laser driver") {
            interlockRow(.ldOvertemp)
            interlockRow(.ldLoopBad, isLast: true)
        }
    }

    // MARK: - Row machinery

    private enum Kind {
        case horizon, distance, tofLowBoundOnly
        case lambdaDrift, tecTempAdc
        case imuInvalid, imuStale
        case tofInvalid, tofStale
        case ldOvertemp, ldLoopBad

        var label: String {
            switch self {
            case .horizon: return "Horizon"
            case .distance: return "Distance (ToF range)"
            case .tofLowBoundOnly: return "ToF low-bound only"
            case .lambdaDrift: return "Lambda drift"
            case .tecTempAdc: return "TEC temp ADC"
            case .imuInvalid: return "IMU invalid"
            case .imuStale: return "IMU stale"
            case .tofInvalid: return "ToF invalid"
            case .tofStale: return "ToF stale"
            case .ldOvertemp: return "LD overtemp"
            case .ldLoopBad: return "LD loop bad"
            }
        }

        var sublabel: String? {
            switch self {
            case .horizon: return "IMU tilt guard against emission while off-axis."
            case .tofLowBoundOnly: return "Arm only when at/below the close limit."
            case .lambdaDrift: return "Wavelength drift beyond operator limit."
            case .tecTempAdc: return "Analog overtemp rail backup."
            case .ldLoopBad: return "Closed-loop fault from the driver."
            default: return nil
            }
        }
    }

    @ViewBuilder private func interlockRow(_ kind: Kind, isLast: Bool = false, disabled: Bool = false) -> some View {
        BSLListRow(label: kind.label, sublabel: kind.sublabel, isLast: isLast) {
            BSLToggle(isOn: binding(for: kind), disabled: disabled)
        }
    }

    private func binding(for kind: Kind) -> Binding<Bool> {
        switch kind {
        case .horizon:           return Binding(get: { mask.horizonEnabled },      set: { mask.horizonEnabled = $0 })
        case .distance:          return Binding(get: { mask.distanceEnabled },     set: { mask.distanceEnabled = $0 })
        case .tofLowBoundOnly:   return Binding(get: { mask.tofLowBoundOnly },     set: { mask.tofLowBoundOnly = $0 })
        case .lambdaDrift:       return Binding(get: { mask.lambdaDriftEnabled },  set: { mask.lambdaDriftEnabled = $0 })
        case .tecTempAdc:        return Binding(get: { mask.tecTempAdcEnabled },   set: { mask.tecTempAdcEnabled = $0 })
        case .imuInvalid:        return Binding(get: { mask.imuInvalidEnabled },   set: { mask.imuInvalidEnabled = $0 })
        case .imuStale:          return Binding(get: { mask.imuStaleEnabled },     set: { mask.imuStaleEnabled = $0 })
        case .tofInvalid:        return Binding(get: { mask.tofInvalidEnabled },   set: { mask.tofInvalidEnabled = $0 })
        case .tofStale:          return Binding(get: { mask.tofStaleEnabled },     set: { mask.tofStaleEnabled = $0 })
        case .ldOvertemp:        return Binding(get: { mask.ldOvertempEnabled },   set: { mask.ldOvertempEnabled = $0 })
        case .ldLoopBad:         return Binding(get: { mask.ldLoopBadEnabled },    set: { mask.ldLoopBadEnabled = $0 })
        }
    }

    private func bannerView(_ msg: String, isError: Bool) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: isError ? "exclamationmark.triangle.fill" : "checkmark.circle.fill")
                .foregroundStyle(isError ? BSL.warn : BSL.ok)
            Text(msg)
                .font(.system(size: 12))
                .foregroundStyle(isError
                                 ? (t.dark ? Color(red: 1.0, green: 0.545, blue: 0.494) : Color(red: 0.607, green: 0.113, blue: 0.074))
                                 : (t.dark ? Color(red: 0.427, green: 0.851, blue: 0.604) : Color(red: 0.017, green: 0.353, blue: 0.219)))
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
        .padding(.top, 4)
    }

    // MARK: - Apply

    private func apply() async {
        applying = true
        defer { applying = false }
        let args = Self.fullPolicyArgs(safety: safety, mask: mask)
        let result = await session.sendCommand("integrate.set_safety", args: args)
        switch result {
        case .success(let resp):
            if resp.ok {
                resultBanner = "Interlocks updated. Firmware persisted to NVS."
                bannerIsError = false
                UINotificationFeedbackGenerator().notificationOccurred(.success)
                session.reportSuccess("Interlock mask applied.")
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

    /// Builds a complete `integrate.set_safety` payload using the current
    /// live safety thresholds plus the edited mask. Mask-only edit — every
    /// numeric threshold is forwarded unchanged so a toggle never widens a
    /// safety limit by omission.
    static func fullPolicyArgs(safety s: SafetyStatus, mask m: InterlockEnableMask) -> [String: CommandArg] {
        [
            "horizon_threshold_deg": .double(s.horizonThresholdDeg),
            "horizon_hysteresis_deg": .double(s.horizonHysteresisDeg),
            "tof_min_range_m": .double(s.tofMinRangeM),
            "tof_max_range_m": .double(s.tofMaxRangeM),
            "tof_hysteresis_m": .double(s.tofHysteresisM),
            "imu_stale_ms": .int(s.imuStaleMs),
            "tof_stale_ms": .int(s.tofStaleMs),
            "rail_good_timeout_ms": .int(s.railGoodTimeoutMs),
            "lambda_drift_limit_nm": .double(s.lambdaDriftLimitNm),
            "lambda_drift_hysteresis_nm": .double(s.lambdaDriftHysteresisNm),
            "lambda_drift_hold_ms": .int(s.lambdaDriftHoldMs),
            "ld_overtemp_limit_c": .double(s.ldOvertempLimitC),
            "tec_temp_adc_trip_v": .double(s.tecTempAdcTripV),
            "tec_temp_adc_hysteresis_v": .double(s.tecTempAdcHysteresisV),
            "tec_temp_adc_hold_ms": .int(s.tecTempAdcHoldMs),
            "tec_min_command_c": .double(s.tecMinCommandC),
            "tec_max_command_c": .double(s.tecMaxCommandC),
            "tec_ready_tolerance_c": .double(s.tecReadyToleranceC),
            "max_laser_current_a": .double(s.maxLaserCurrentA),
            "off_current_threshold_a": .double(s.offCurrentThresholdA),
            "max_tof_led_duty_cycle_pct": .int(s.maxTofLedDutyCyclePct),
            "lio_voltage_offset_v": .double(s.lioVoltageOffsetV),
            "interlock_horizon_enabled": .bool(m.horizonEnabled),
            "interlock_distance_enabled": .bool(m.distanceEnabled),
            "interlock_lambda_drift_enabled": .bool(m.lambdaDriftEnabled),
            "interlock_tec_temp_adc_enabled": .bool(m.tecTempAdcEnabled),
            "interlock_imu_invalid_enabled": .bool(m.imuInvalidEnabled),
            "interlock_imu_stale_enabled": .bool(m.imuStaleEnabled),
            "interlock_tof_invalid_enabled": .bool(m.tofInvalidEnabled),
            "interlock_tof_stale_enabled": .bool(m.tofStaleEnabled),
            "interlock_ld_overtemp_enabled": .bool(m.ldOvertempEnabled),
            "interlock_ld_loop_bad_enabled": .bool(m.ldLoopBadEnabled),
            "tof_low_bound_only": .bool(m.tofLowBoundOnly),
        ]
    }
}
