---
name: device-flashing
description: Required whenever firmware needs to be placed on the controller hardware. Covers all three supported paths — serial via flash-firmware.command, serial via idf.py flash monitor, and Web Serial flash inside the host console via esptool-js. Documents the "Wi-Fi-connected board + no serial path ⇒ cannot flash" recurring friction. Trigger on "flash", "idf.py flash", "put firmware on device", "update the board", "push build to controller", or after a successful firmware build when the user wants to exercise it.
---

# device-flashing

This project has **three supported flashing paths** and one well-known friction mode. Use this skill any time the user needs to put a firmware build onto the controller.

## Hardware Prerequisites

- USB-C cable directly to the ESP32-S3 USB port, not through the USB-PD board's PD-only port.
- The board must be in a state where CDC is enumerated. If it's only on Wi-Fi (e.g. via SoftAP `ws://192.168.4.1/ws`) with no serial cable attached, flashing is **not possible** — see Friction Mode below.

## Path 1 — `./flash-firmware.command` (wrapper script, the usual path)

From the repository root:

    ./flash-firmware.command

This script (see `flash-firmware.command:1-22`) does:

1. Sources `$HOME/esp/esp-idf/export.sh`.
2. Runs `idf.py build`.
3. Auto-detects the first `/dev/cu.usbmodem*` device.
4. Runs `idf.py -p "$PORT" flash monitor`.

If no `/dev/cu.usbmodem*` is present, it exits with "No USB serial device found." Check the cable, the USB port, and `ls /dev/cu.*`.

## Path 2 — Direct `idf.py` (when you want control)

From the repository root with ESP-IDF v6.0 exported:

    . "$IDF_PATH/export.sh"
    idf.py build
    idf.py -p /dev/cu.usbmodemXYZ flash monitor

Use this when:
- You need a specific port (multiple boards attached).
- You want to skip `monitor` (`flash` only) — e.g. to keep the serial free for validation scripts.
- You want `fullclean` first: `idf.py fullclean && idf.py build`.

## Path 3 — Web Serial from the running host console

The host console ships `esptool-js` (`host-console/package.json:15`) for browser-based flashing. From a running host console (`cd host-console && npm run dev`):

1. Open the console in a Web-Serial-capable browser (Chrome / Edge).
2. Use the flashing entry point in the console UI to select the device.
3. Drop or select the pre-built firmware image `build/bsl_laser_controller.bin` plus bootloader and partition table from `build/`.

Use this when the user does not have ESP-IDF installed locally but the firmware is already built.

## Friction Mode — Wi-Fi-Connected Board, No Serial Path

This is explicitly called out in `.agent/runs/powered-ready-console/Status.md` as a recurring session-blocker:

> "local firmware image builds successfully, but there is no serial /dev/cu.usbmodem* path exposed in this session, so the rebuilt firmware cannot be flashed onto the live Wi-Fi board from here yet"

**Diagnosis**: If the session can only reach the board over Wi-Fi (e.g., you can hit `ws://192.168.4.1/ws` from the host console but `ls /dev/cu.usbmodem*` returns nothing), flashing **cannot** proceed from this session.

**Options**:

1. Physically recable: plug a USB cable from the dev machine to the ESP32-S3 USB port. This exposes CDC and gives you a `/dev/cu.usbmodem*` path.
2. Hand off to a session that already has the serial port exposed (see `context-handoff` skill — update `Status.md` so the next agent knows this is the blocker).
3. Wi-Fi OTA is **not supported by the current firmware**. Do not claim Wi-Fi flashing is possible — there is no OTA bootloader path implemented.

## Post-Flash Verification

After every successful flash:

1. Confirm the flashed image identity by reading `firmwareVersion` and `buildUtc` from a `status.get` response. This is how `.agent/runs/_archive/v2-rewrite-seed/Status.md` verified the flashed image (historical).
2. Run the USB Phase 1 smoke at minimum:
       python3 host-console/scripts/live_controller_validation.py --transport serial --port /dev/cu.usbmodem201101 --scenario parser-matrix
3. For powered-ready claims, run the three mandatory Powered Phase 2 passes — see `powered-bench-validation` skill.

## Things Not To Do

- Do NOT claim the board is flashed when only the build succeeded. Build success is compile success, not device state.
- Do NOT flash during deployment mode. Exit deployment first, idle the device, then flash.
- Do NOT rely on `./flash-firmware.command` to tell you which port it picked if multiple `/dev/cu.usbmodem*` are present — it grabs the first. Use Path 2 with an explicit `-p` for multi-board setups.
- Do NOT claim Wi-Fi OTA works. It doesn't yet.
