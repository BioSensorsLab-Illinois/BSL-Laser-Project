---
name: calibration-provisioning
description: Required whenever TEC / wavelength LUTs, DAC80502 codes, or IMU transform matrices are written to or read from the controller. Enforces write-then-read-back-with-CRC before declaring calibration stored; preserves firmware-side NVS versioning (v3→v4→v5→v6 auto-migration). Trigger on "calibrate", "write LUT", "new DAC codes", "IMU axis matrix", "tec_cal", "lambda_cal", or any change that touches stored calibration data.
---

# calibration-provisioning

Calibration data governs physical laser behavior — wrong LUTs produce wrong wavelength, wrong TEC setpoint, and wrong current. Always write-then-read-back-with-CRC; never declare "calibration stored" off a write-only round trip.

## The Four Calibration Surfaces

Source: `project_firmware_internals.md` (memory), `docs/datasheet-programming-notes.md`, and the firmware config blob.

| Surface | Shape | Where It Lives |
|---|---|---|
| **TEC LUT** | 14-point voltage-to-temperature lookup (0.180V / 6°C → 2.429V / 65°C) | service profile blob in NVS (`laser_ctrl` namespace) |
| **Wavelength LUT** | 16-point temperature-to-lambda (5°C / 771.2nm → 65°C / 790nm) | service profile blob in NVS |
| **DAC80502 codes** | 16-bit dual-channel codes; LD current and TEC setpoint | service profile + runtime target fields |
| **IMU 3×3 transform matrix** | axis remap / orientation compensation | service profile blob |

## The NVS Version Chain

The service profile blob is versioned. Firmware auto-migrates old records on boot (v3 → v4 → v5 → v6 as of recent sessions). When adding a new calibration field:

1. Increment the blob version by one.
2. Add a migration entry in `laser_controller_service.c` that converts the previous version into the new shape with sane defaults.
3. Keep the old-reader path for at least one version so old NVS records still load.

If you skip the migration step, devices in the field that already have an older blob will fail to load service profile at boot.

## The Write-Then-Read-Back-With-CRC Rule (Non-Negotiable)

Every calibration-provisioning action must follow this sequence:

1. **Write**: send the calibration command through the protocol (`integrate.set_safety`, dedicated calibration command if one exists, or a provisioning flow).
2. **Persist confirmation**: wait for the `integrate.save_profile` / `integrate.set_safety` response indicating persistence succeeded (`lastSaveOk: true`, `persistenceDirty: false`, `lastSavedAtMs` updated).
3. **Read back**: issue a `status.get` or the corresponding read command to fetch the stored values.
4. **Compute CRC** over the read-back bytes and compare against the CRC of the written payload.
5. Only if CRC matches **and** the persistence response indicated success do you consider the calibration stored.

If CRC mismatches or persistence response is missing / false, treat the write as **failed** and:

- Do not advance to the next calibration surface.
- Retry once.
- If retry fails, log the exact written vs read-back bytes, mark the calibration as BLOCKED in `Status.md`, and stop provisioning.

## Reboot-Survives Verification

A calibration is not "stored" until the device reboots and still reports the same values. Add a reboot step:

1. Run the write + read-back + CRC sequence.
2. Issue a soft reset (power cycle or `esp_restart`-equivalent command if exposed; otherwise unplug / replug).
3. Reconnect and issue `status.get`.
4. Verify the calibration values are identical to what you wrote.

The existing validation scenario `safety-persistence-reboot` in `host-console/scripts/live_controller_validation.py` is the template for reboot-survives checks. Extend it for calibration-specific fields rather than rolling a new one-off script.

## DAC80502 Specifics

The DAC lives on the shared I²C bus (GPIO4/5) at address `0x48`. Per `docs/datasheet-programming-notes.md`:

- DAC outputs (both channels) must be at zero-safe codes **before any rail enables**.
- After a rail enable, updating DAC codes mid-flight is allowed but must go through the usual control-task path — never write the DAC directly from another task.
- CRC the stored codes, not just the desired values; the DAC readback must match bit-for-bit.

## IMU 3×3 Transform

The LSM6DSO is on SPI. The transform matrix sits in the config blob. Rules:

- Any change to the matrix invalidates prior inertial logs interpreted against the old matrix.
- After changing the matrix, log the change in `Status.md` with the old and new 9 values.
- Run an orientation-verification flow: place the device in a known orientation, read raw IMU axes, verify the transformed values match expectation.

## Things Not To Do

- Do NOT declare calibration stored after a bare write with no read-back.
- Do NOT skip CRC comparison because the values "look right."
- Do NOT write DAC codes without a rail-safe state for the affected channel.
- Do NOT edit LUTs or matrices in memory and assume NVS was updated — always invoke the save path and wait for `lastSaveOk`.
- Do NOT bump the NVS blob version without a migration entry for the prior version.
- Do NOT provision in bulk without the reboot-survives check between surfaces.
