import SwiftUI
import BSLProtocol

/// Ten per-interlock boolean switches plus the ToF low-bound-only flag.
/// Mirrors `SafetyStatus.interlocks` in `host-console/src/types.ts:812-833`.
/// Writes via `integrate.set_safety` preserving the current safety
/// thresholds — mask-only edit.
struct InterlockToggles: View {
    @Environment(DeviceSession.self) private var session

    @State private var mask: InterlockEnableMask = InterlockEnableMask()
    @State private var applying = false
    @State private var resultBanner: String?

    private var safety: SafetyStatus { session.snapshot.safety }

    var body: some View {
        List {
            Section {
                Text("Each toggle disables ONE interlock. The master service-mode override still sits above these — setting `interlocksDisabled` true in service mode short-circuits all ten.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            Section("Geometric") {
                Toggle("Horizon", isOn: $mask.horizonEnabled)
                Toggle("Distance (ToF range)", isOn: $mask.distanceEnabled)
                Toggle("  ToF low-bound only", isOn: $mask.tofLowBoundOnly)
                    .disabled(!mask.distanceEnabled)
            }

            Section("Drift + ADC") {
                Toggle("Lambda drift", isOn: $mask.lambdaDriftEnabled)
                Toggle("TEC temp ADC", isOn: $mask.tecTempAdcEnabled)
            }

            Section("IMU") {
                Toggle("IMU invalid", isOn: $mask.imuInvalidEnabled)
                Toggle("IMU stale", isOn: $mask.imuStaleEnabled)
            }

            Section("ToF") {
                Toggle("ToF invalid", isOn: $mask.tofInvalidEnabled)
                Toggle("ToF stale", isOn: $mask.tofStaleEnabled)
            }

            Section("Laser driver") {
                Toggle("LD overtemp", isOn: $mask.ldOvertempEnabled)
                Toggle("LD loop bad", isOn: $mask.ldLoopBadEnabled)
            }

            if let banner = resultBanner {
                Section { Text(banner).font(.footnote) }
            }
        }
        .navigationTitle("Interlocks")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    Task { await apply() }
                } label: {
                    if applying { ProgressView() } else { Text("Apply") }
                }
                .disabled(applying)
            }
        }
        .onAppear { mask = safety.interlocks }
        .onChange(of: safety.interlocks) { _, new in
            if !applying { mask = new }
        }
    }

    private func apply() async {
        applying = true
        defer { applying = false }
        // Send with existing thresholds so the write is mask-only.
        let args = Self.fullPolicyArgs(safety: safety, mask: mask)
        let result = await session.sendCommand("integrate.set_safety", args: args)
        switch result {
        case .success(let resp):
            resultBanner = resp.ok
                ? "Interlocks updated. Firmware persisted to NVS."
                : "Rejected: \(resp.error ?? "unknown reason")"
        case .failure(let err):
            resultBanner = "Failed to send: \(err.localizedDescription)"
        }
    }

    /// Builds a complete `integrate.set_safety` payload using the current
    /// live safety thresholds plus the edited mask. Keeps every threshold
    /// unchanged at write time so a mask edit never accidentally widens a
    /// numeric limit.
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
