#pragma once

#include <stdbool.h>

#include "laser_controller_board.h"
#include "laser_controller_config.h"
#include "laser_controller_faults.h"
#include "laser_controller_types.h"

typedef struct {
    laser_controller_time_ms_t now_ms;
    bool boot_complete;
    bool config_valid;
    bool fault_latched;
    bool service_mode_requested;
    bool service_mode_active;
    bool deployment_active;
    bool deployment_running;
    bool deployment_ready;
    bool deployment_ready_idle;
    bool interlocks_disabled;
    bool allow_missing_imu;
    bool allow_missing_tof;
    bool allow_missing_buttons;
    bool host_request_alignment;
    bool host_request_nir;
    /*
     * Runtime mode (BINARY_TRIGGER vs MODULATED_HOST) is part of the safety
     * snapshot so the safety evaluator can route button presses only when
     * the operator is in BINARY_TRIGGER mode. In MODULATED_HOST mode,
     * stage1/stage2 are advisory only — the host request is the source of
     * truth. This resolves the prior inconsistency between the runtime NIR
     * gate (which silently honored buttons in any mode) and the published
     * nir_blocked_reason (which said "not-modulated-host" when not in host
     * mode).
     */
    laser_controller_runtime_mode_t runtime_mode;
    /*
     * `button_nir_lockout` is set by the control task when an interlock
     * fires while the operator is holding stage1 (and possibly stage2).
     * It forces `request_nir` to false until BOTH stages are released, so
     * that an auto-clearing interlock cannot cause NIR to immediately
     * resume firing the moment the interlock condition lifts — the user
     * must physically release the trigger and re-press. Per user directive
     * 2026-04-15.
     */
    bool button_nir_lockout;
    bool last_horizon_blocked;
    bool last_distance_blocked;
    bool last_lambda_drift_blocked;
    bool last_tec_temp_adc_blocked;
    bool driver_operate_expected;
    bool ready_idle_bias_allowed;
    laser_controller_power_tier_t power_tier;
    laser_controller_nm_t target_lambda_nm;
    laser_controller_nm_t actual_lambda_nm;
    /*
     * Settle timers passed in from the control task.
     *
     * `ld_rail_pgood_for_ms` = milliseconds `ld_rail_pgood` has been TRUE.
     *    0 when the rail is currently off.
     * `sbdn_not_off_for_ms` = milliseconds the committed `sbdn_state` in the
     *    previous-tick outputs has been STANDBY or ON (i.e. != OFF).
     *    0 when SBDN is currently OFF.
     *
     * Used to gate `LD_OVERTEMP` detection. The ADC thermistor reading is
     * only valid after both timers cross 2 s — rail power must be stable and
     * the three-state SBDN pin must have settled out of fast-shutdown. User
     * directive 2026-04-15 (late): "ld temp reading only valid after 2 s
     * from LD power supply ON AND 2 s after SBDN set to Standby or ON,
     * whichever comes later".
     */
    uint32_t ld_rail_pgood_for_ms;
    uint32_t sbdn_not_off_for_ms;
    const laser_controller_board_inputs_t *hw;
} laser_controller_safety_snapshot_t;

typedef struct {
    bool request_alignment;
    bool request_nir;
    bool allow_alignment;
    bool allow_nir;
    bool alignment_output_enable;
    bool nir_output_enable;
    bool horizon_blocked;
    bool distance_blocked;
    bool lambda_drift_blocked;
    bool tec_temp_adc_blocked;
    /*
     * Three-state SBDN posture the safety decision wants. See the enum
     * definition in `laser_controller_board.h` for the OFF/ON/STANDBY
     * semantics. The board layer is the only code that translates this
     * into a physical pin operation.
     */
    laser_controller_sbdn_state_t sbdn_state;
    bool fault_present;
    laser_controller_fault_code_t fault_code;
    laser_controller_fault_class_t fault_class;
    const char *fault_reason;
    /*
     * Per-tick diagnostic payload for LD_OVERTEMP trips. Populated by
     * safety_evaluate whenever the overtemp check fires. `valid == false`
     * when no overtemp trip occurred this tick. The control task in app.c
     * copies this into `context->active_fault_diag` on the rising edge
     * only (so the captured values are the values observed AT TRIP, not
     * a rolling snapshot that drifts while the fault stays latched).
     */
    bool ld_overtemp_diag_valid;
    float ld_overtemp_measured_c;
    float ld_overtemp_voltage_v;
    float ld_overtemp_limit_c;
    uint32_t ld_overtemp_pgood_for_ms;
    uint32_t ld_overtemp_sbdn_for_ms;
} laser_controller_safety_decision_t;

void laser_controller_safety_evaluate(
    const laser_controller_config_t *config,
    const laser_controller_safety_snapshot_t *snapshot,
    laser_controller_safety_decision_t *decision);
