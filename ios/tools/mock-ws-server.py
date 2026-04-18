#!/usr/bin/env python3
"""
Headless BSL-controller mock for iOS / BSLRemote integration testing.

Runs both an HTTP /meta endpoint and the WebSocket /ws endpoint on a single
port (default 8080). Emits a canned status_snapshot on WS connect, then a
live_telemetry every 1 s with varying tec.tempC and laser.measuredCurrentA.
Echoes `{id, ok: true}` for any `cmd` frame — enough to exercise the app's
dashboard, blocked-reason rendering, and settings payloads without a bench.

Requires Python 3.9+ and `websockets >= 10`:

    python3 -m pip install "websockets>=10"
    python3 ios/tools/mock-ws-server.py

Then on the iOS Simulator, enter `ws://localhost:8080/ws` in the Connect
screen's manual-URL field.
"""

from __future__ import annotations

import asyncio
import json
import math
import os
import time
from dataclasses import dataclass
from http import HTTPStatus

try:
    import websockets
    from websockets.server import serve
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "This mock requires `websockets >= 10`. Install with:\n"
        "    python3 -m pip install 'websockets>=10'\n"
        f"(import error: {exc})"
    )

PORT = int(os.environ.get("BSL_MOCK_PORT", "8080"))


@dataclass
class MockState:
    tec_temp_c: float = 24.8
    requested_current_a: float = 1.5
    measured_current_a: float = 0.0
    fault_latched: bool = False
    mock_active: bool = False
    nir_enabled: bool = False
    blocked_reason: str = "deployment-off"
    applied_duty_pct: int = 30
    requested_duty_pct: int = 30


STATE = MockState()


def canned_status_snapshot() -> dict:
    return {
        "type": "event",
        "event": "status_snapshot",
        "timestamp_ms": int(time.time() * 1000),
        "payload": {
            "session": {
                "uptimeSeconds": int(time.time()) % 100000,
                "state": "SERVICE_MODE",
                "powerTier": "full",
            },
            "rails": {
                "ld": {"enabled": False, "pgood": False},
                "tec": {"enabled": True, "pgood": True},
            },
            "imu": {"valid": True, "fresh": True},
            "tof": {"valid": True, "fresh": True, "distanceM": 0.42},
            "laser": {
                "nirEnabled": STATE.nir_enabled,
                "alignmentEnabled": False,
                "measuredCurrentA": STATE.measured_current_a,
                "commandedCurrentA": STATE.requested_current_a if STATE.nir_enabled else 0.0,
                "loopGood": True,
                "driverTempC": 29.4,
                "driverStandby": not STATE.nir_enabled,
            },
            "tec": {
                "targetTempC": 25.0,
                "targetLambdaNm": 785.0,
                "actualLambdaNm": 784.7,
                "tempGood": abs(STATE.tec_temp_c - 25.0) < 0.3,
                "tempC": STATE.tec_temp_c,
                "currentA": 0.3,
                "voltageV": 2.1,
                "settlingSecondsRemaining": 0,
            },
            "pd": {
                "contractValid": True,
                "negotiatedPowerW": 45.0,
                "sourceVoltageV": 20.0,
                "sourceCurrentA": 2.25,
            },
            "bench": {
                "requestedNirEnabled": STATE.nir_enabled,
                "requestedCurrentA": STATE.requested_current_a,
                "requestedLedEnabled": True,
                "requestedLedDutyCyclePct": STATE.requested_duty_pct,
                "appliedLedOwner": "operate_runtime",
                "appliedLedPinHigh": True,
                "illuminationEnabled": True,
                "illuminationDutyCyclePct": STATE.applied_duty_pct,
                "hostControlReadiness": {
                    "nirBlockedReason": STATE.blocked_reason,
                    "ledBlockedReason": "none",
                    "sbdnState": "off",
                },
                "usbDebugMock": {
                    "active": STATE.mock_active,
                    "pdConflictLatched": False,
                    "lastDisableReason": "",
                },
            },
            "safety": {
                "allowAlignment": True,
                "allowNir": False,
                "horizonBlocked": False,
                "distanceBlocked": False,
                "lambdaDriftBlocked": False,
                "tecTempAdcBlocked": False,
                "horizonThresholdDeg": 0.0,
                "horizonHysteresisDeg": 3.0,
                "tofMinRangeM": 0.2,
                "tofMaxRangeM": 1.0,
                "tofHysteresisM": 0.02,
                "imuStaleMs": 50,
                "tofStaleMs": 100,
                "railGoodTimeoutMs": 250,
                "lambdaDriftLimitNm": 5.0,
                "lambdaDriftHysteresisNm": 0.5,
                "lambdaDriftHoldMs": 2000,
                "ldOvertempLimitC": 55.0,
                "tecTempAdcTripV": 2.45,
                "tecTempAdcHysteresisV": 0.05,
                "tecTempAdcHoldMs": 2000,
                "tecMinCommandC": 15.0,
                "tecMaxCommandC": 35.0,
                "tecReadyToleranceC": 0.25,
                "maxLaserCurrentA": 4.5,
                "offCurrentThresholdA": 0.2,
                "maxTofLedDutyCyclePct": 50,
                "lioVoltageOffsetV": 0.07,
                "actualLambdaNm": 784.7,
                "targetLambdaNm": 785.0,
                "lambdaDriftNm": 0.3,
                "tempAdcVoltageV": 1.115,
                "interlocks": {
                    "horizonEnabled": True,
                    "distanceEnabled": True,
                    "lambdaDriftEnabled": True,
                    "tecTempAdcEnabled": True,
                    "imuInvalidEnabled": True,
                    "imuStaleEnabled": True,
                    "tofInvalidEnabled": True,
                    "tofStaleEnabled": True,
                    "ldOvertempEnabled": True,
                    "ldLoopBadEnabled": True,
                    "tofLowBoundOnly": False,
                },
            },
            "fault": {
                "latched": STATE.fault_latched,
                "activeCode": "ld_overtemp" if STATE.fault_latched else "none",
                "activeClass": "system_major" if STATE.fault_latched else "none",
                "latchedCode": "ld_overtemp" if STATE.fault_latched else "none",
                "latchedClass": "system_major" if STATE.fault_latched else "none",
                "activeReason": "",
                "latchedReason": "LD overtemp exceeded." if STATE.fault_latched else "",
                "activeCount": 1 if STATE.fault_latched else 0,
                "tripCounter": 1 if STATE.fault_latched else 0,
                "triggerDiag": {
                    "code": "ld_overtemp",
                    "measuredC": 68.3,
                    "measuredVoltageV": 1.3776,
                    "limitC": 55.0,
                    "ldPgoodForMs": 4200,
                    "sbdnNotOffForMs": 3800,
                    "expr": "ld_temp_c > 55.0 C @ 68.3 C, 1.378 V",
                } if STATE.fault_latched else None,
            },
            "deployment": {
                "active": False,
                "running": False,
                "ready": False,
                "readyIdle": False,
                "phase": "inactive",
                "maxLaserCurrentA": 5.0,
                "targetLambdaNm": 785.0,
                "targetTempC": 25.0,
            },
        },
    }


def live_telemetry(tick: int) -> dict:
    STATE.tec_temp_c = 24.8 + 0.6 * math.sin(tick / 8.0)
    STATE.measured_current_a = (
        STATE.requested_current_a if STATE.nir_enabled else 0.0
    )
    return {
        "type": "event",
        "event": "live_telemetry",
        "timestamp_ms": int(time.time() * 1000),
        "payload": {
            "laser": {
                "nirEnabled": STATE.nir_enabled,
                "measuredCurrentA": STATE.measured_current_a,
                "commandedCurrentA": STATE.requested_current_a if STATE.nir_enabled else 0.0,
                "loopGood": True,
                "driverTempC": 29.4 + 0.3 * math.sin(tick / 15.0),
                "driverStandby": not STATE.nir_enabled,
                "alignmentEnabled": False,
            },
            "tec": {
                "targetTempC": 25.0,
                "targetLambdaNm": 785.0,
                "actualLambdaNm": 784.7 + 0.02 * math.sin(tick / 6.0),
                "tempGood": abs(STATE.tec_temp_c - 25.0) < 0.3,
                "tempC": STATE.tec_temp_c,
                "currentA": 0.3,
                "voltageV": 2.1,
                "settlingSecondsRemaining": 0,
            },
            "bench": {
                "requestedCurrentA": STATE.requested_current_a,
                "requestedNirEnabled": STATE.nir_enabled,
                "requestedLedDutyCyclePct": STATE.requested_duty_pct,
                "requestedLedEnabled": True,
                "illuminationEnabled": True,
                "illuminationDutyCyclePct": STATE.applied_duty_pct,
                "appliedLedOwner": "operate_runtime",
                "appliedLedPinHigh": True,
                "hostControlReadiness": {
                    "nirBlockedReason": STATE.blocked_reason,
                    "ledBlockedReason": "none",
                    "sbdnState": "off",
                },
                "usbDebugMock": {
                    "active": STATE.mock_active,
                    "pdConflictLatched": False,
                    "lastDisableReason": "",
                },
            },
        },
    }


async def handle_ws(ws):
    await ws.send(json.dumps(canned_status_snapshot()))
    tick = 0
    sender_done = asyncio.Event()

    async def sender():
        nonlocal tick
        try:
            while not sender_done.is_set():
                await asyncio.sleep(1.0)
                tick += 1
                try:
                    await ws.send(json.dumps(live_telemetry(tick)))
                except Exception:
                    break
        finally:
            sender_done.set()

    send_task = asyncio.create_task(sender())
    try:
        async for message in ws:
            try:
                frame = json.loads(message)
            except json.JSONDecodeError:
                continue
            if frame.get("type") != "cmd":
                continue
            cmd = frame.get("cmd", "")
            ident = frame.get("id", 0)
            args = frame.get("args", {}) or {}
            apply_command(cmd, args)
            await ws.send(json.dumps({"type": "resp", "id": ident, "ok": True}))
            await ws.send(json.dumps(canned_status_snapshot()))
    finally:
        sender_done.set()
        send_task.cancel()


def apply_command(cmd: str, args: dict) -> None:
    if cmd == "operate.set_output":
        if "current_a" in args:
            try:
                STATE.requested_current_a = float(args["current_a"])
            except (TypeError, ValueError):
                pass
        if "enable" in args:
            STATE.nir_enabled = bool(args["enable"])
    elif cmd == "operate.set_led":
        if "duty_cycle_pct" in args:
            try:
                STATE.requested_duty_pct = int(args["duty_cycle_pct"])
                STATE.applied_duty_pct = STATE.requested_duty_pct
            except (TypeError, ValueError):
                pass
    elif cmd == "operate.set_target":
        # no-op in the mock; desktop would update tec.targetLambdaNm
        pass
    elif cmd == "clear_faults":
        STATE.fault_latched = False
    elif cmd == "enter_service_mode":
        STATE.blocked_reason = "none"
    elif cmd == "exit_service_mode":
        STATE.blocked_reason = "deployment-off"


async def process_request(path, request_headers):
    """websockets 10 process_request callback. Returns a 3-tuple to short-
    circuit the handshake with a plain HTTP response; returns None to let
    the WebSocket upgrade proceed."""
    if path == "/meta":
        host = request_headers.get("Host", f"localhost:{PORT}")
        body = json.dumps(
            {
                "mode": "softap",
                "ssid": "BSL-HTLS-Bench",
                "stationSsid": "",
                "wsUrl": f"ws://{host}/ws",
                "ipAddress": host.split(":")[0],
            }
        ).encode("utf-8")
        headers = [
            ("Content-Type", "application/json"),
            ("Access-Control-Allow-Origin", "*"),
            ("Content-Length", str(len(body))),
        ]
        return (HTTPStatus.OK, headers, body)
    if path == "/ws":
        return None  # proceed with WS upgrade
    return (HTTPStatus.NOT_FOUND, [], b"not found\n")


async def main() -> None:
    print(f"BSL mock: ws://0.0.0.0:{PORT}/ws  (HTTP /meta on same port)")
    print("Commands accepted: operate.set_output, operate.set_led,")
    print("                   operate.set_target, clear_faults,")
    print("                   enter_service_mode, exit_service_mode.")
    print("Ctrl-C to stop.")
    async with serve(
        handle_ws,
        "0.0.0.0",
        PORT,
        process_request=process_request,
    ):
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
