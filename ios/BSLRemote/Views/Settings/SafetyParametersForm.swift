import SwiftUI
import BSLProtocol

/// Mirrors the safety-form payload from the desktop
/// `host-console/src/components/BringupWorkbench.tsx:2201-2317`. Every field
/// goes into one `integrate.set_safety` envelope on Apply. Firmware rejects
/// while NIR or alignment is emitting — the response is surfaced verbatim.
struct SafetyParametersForm: View {
    @Environment(DeviceSession.self) private var session

    @State private var form: Form = Form()
    @State private var applying = false
    @State private var resultBanner: String?

    private var safety: SafetyStatus { session.snapshot.safety }

    var body: some View {
        List {
            Section("Horizon") {
                number("Threshold (°)", $form.horizonThresholdDeg)
                number("Hysteresis (°)", $form.horizonHysteresisDeg)
            }
            Section("ToF distance") {
                number("Min range (m)", $form.tofMinRangeM)
                number("Max range (m)", $form.tofMaxRangeM)
                number("Hysteresis (m)", $form.tofHysteresisM)
            }
            Section("Staleness") {
                integer("IMU stale (ms)", $form.imuStaleMs)
                integer("ToF stale (ms)", $form.tofStaleMs)
                integer("Rail good timeout (ms)", $form.railGoodTimeoutMs)
            }
            Section("Lambda drift") {
                number("Limit (nm)", $form.lambdaDriftLimitNm)
                number("Hysteresis (nm)", $form.lambdaDriftHysteresisNm)
                integer("Hold (ms)", $form.lambdaDriftHoldMs)
            }
            Section("LD protection") {
                number("Overtemp limit (°C)", $form.ldOvertempLimitC)
                number("TEC ADC trip (V)", $form.tecTempAdcTripV)
                number("TEC ADC hysteresis (V)", $form.tecTempAdcHysteresisV)
                integer("TEC ADC hold (ms)", $form.tecTempAdcHoldMs)
            }
            Section("TEC command") {
                number("Min command (°C)", $form.tecMinCommandC)
                number("Max command (°C)", $form.tecMaxCommandC)
                number("Ready tolerance (°C)", $form.tecReadyToleranceC)
            }
            Section("Currents + duty") {
                number("Max laser current (A)", $form.maxLaserCurrentA)
                number("Off-current threshold (A)", $form.offCurrentThresholdA)
                integer("Max ToF LED duty (%)", $form.maxTofLedDutyCyclePct)
                number("LIO offset (V)", $form.lioVoltageOffsetV)
            }
            if let banner = resultBanner {
                Section {
                    Text(banner)
                        .font(.footnote)
                        .foregroundStyle(.primary)
                }
            }
        }
        .navigationTitle("Safety")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    Task { await apply() }
                } label: {
                    if applying {
                        ProgressView()
                    } else {
                        Text("Apply")
                    }
                }
                .disabled(applying)
            }
        }
        .onAppear { form.load(from: safety) }
        .onChange(of: safety) { _, new in
            if !applying { form.load(from: new) }
        }
    }

    @ViewBuilder
    private func number(_ label: String, _ binding: Binding<Double>) -> some View {
        HStack {
            Text(label)
            Spacer()
            TextField(label, value: binding, format: .number)
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 120)
        }
    }

    @ViewBuilder
    private func integer(_ label: String, _ binding: Binding<Int>) -> some View {
        HStack {
            Text(label)
            Spacer()
            TextField(label, value: binding, format: .number)
                .keyboardType(.numberPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 120)
        }
    }

    private func apply() async {
        applying = true
        defer { applying = false }
        let args: [String: CommandArg] = form.buildArgs(currentInterlocks: safety.interlocks)
        let result = await session.sendCommand("integrate.set_safety", args: args)
        switch result {
        case .success(let resp):
            resultBanner = resp.ok
                ? "Applied. Firmware persisted to NVS."
                : "Rejected: \(resp.error ?? "unknown reason")"
        case .failure(let err):
            resultBanner = "Failed to send: \(err.localizedDescription)"
        }
    }
}

private struct Form {
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

    func buildArgs(currentInterlocks locks: InterlockEnableMask) -> [String: CommandArg] {
        // We send EVERY field including current interlock flags so the
        // firmware has a deterministic policy snapshot. The firmware treats
        // missing fields as non-destructive (leave current value alone), but
        // iOS does not rely on that — we always send the full shape.
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
