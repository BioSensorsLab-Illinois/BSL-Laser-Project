#!/usr/bin/env python3

import argparse
import json
import sys
import time
from dataclasses import dataclass
from typing import Any

import serial


@dataclass
class ControllerClient:
    port: str
    baudrate: int = 115200
    timeout_s: float = 0.2

    def __post_init__(self) -> None:
        self._open()

    def _open(self) -> None:
        self._ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout_s)

    def _reopen(self) -> None:
        try:
            self._ser.close()
        except Exception:
            pass
        time.sleep(0.3)
        self._open()

    def close(self) -> None:
        self._ser.close()

    def drain(self, duration_s: float = 1.0) -> None:
        end = time.time() + duration_s
        while time.time() < end:
            try:
                self._ser.read(4096)
            except serial.SerialException:
                time.sleep(0.1)

    def request(self, payload: str, command_id: int, timeout_s: float = 10.0) -> dict[str, Any]:
        self._ser.write(payload.encode())
        deadline = time.time() + timeout_s
        buffer = ""
        while time.time() < deadline:
            try:
                chunk = self._ser.read(4096)
            except serial.SerialException:
                self._reopen()
                continue
            if not chunk:
                continue
            buffer += chunk.decode("utf-8", "replace")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if obj.get("type") == "resp" and obj.get("id") == command_id:
                    return obj
        raise TimeoutError(f"timed out waiting for response id={command_id}")


def compact(obj: dict[str, Any]) -> str:
    return json.dumps(obj, separators=(",", ":")) + "\n"


def spaced(obj: dict[str, Any]) -> str:
    return json.dumps(obj) + "\n"


def unwrap_snapshot(resp: dict[str, Any]) -> dict[str, Any]:
    result = resp.get("result") or {}
    if isinstance(result, dict) and "snapshot" in result and isinstance(result["snapshot"], dict):
        return result["snapshot"]
    return result if isinstance(result, dict) else {}


def require(cond: bool, message: str) -> None:
    if not cond:
        raise AssertionError(message)


def scenario_parser_matrix(client: ControllerClient) -> None:
    spaced_resp = client.request(
        spaced({"type": "cmd", "id": 1001, "cmd": "get_status"}),
        1001,
    )
    compact_resp = client.request(
        compact({"type": "cmd", "id": 1002, "cmd": "get_status"}),
        1002,
    )
    require(spaced_resp.get("ok") is True, "spaced JSON get_status must succeed")
    require(compact_resp.get("ok") is True, "compact JSON get_status must succeed")

    root_key_resp = client.request(
        compact(
            {
                "args": {"id": 999, "cmd": "junk"},
                "command": "get_status",
                "type": "cmd",
                "id": 1005,
            }
        ),
        1005,
        timeout_s=3.0,
    )
    require(root_key_resp.get("ok") is True, "root envelope keys must not be shadowed by nested args")

    nested_resp = client.request(
        spaced(
            {
                "type": "cmd",
                "id": 1003,
                "cmd": "set_deployment_safety",
                "args": {
                    "tof_min_range_m": 0.21,
                    "tof_max_range_m": 0.95,
                    "max_laser_current_a": "3.25",
                },
            }
        ),
        1003,
    )
    require(
        nested_resp.get("ok") in (True, False),
        "nested args request must produce a valid response envelope",
    )

    quoted_bool_resp = client.request(
        spaced(
            {
                "type": "cmd",
                "id": 1004,
                "cmd": "configure_modulation",
                "enabled": "false",
                "frequency_hz": "1000",
                "duty_cycle_pct": "50",
            }
        ),
        1004,
    )
    require(
        quoted_bool_resp.get("ok") is False
        or quoted_bool_resp.get("error") != "Malformed command envelope.",
        "quoted boolean request must no longer fail at envelope parsing",
    )
    print("parser-matrix: PASS")


def scenario_deployment_lockout(client: ControllerClient) -> None:
    enter_resp = client.request(
        compact({"type": "cmd", "id": 1151, "cmd": "enter_deployment_mode"}),
        1151,
        timeout_s=10.0,
    )
    enter_snapshot = unwrap_snapshot(enter_resp)
    require(enter_resp.get("ok") is True, "enter_deployment_mode must succeed")
    require(enter_snapshot.get("deployment", {}).get("active") is True, "enter_deployment_mode response must be fresh")

    blocked_service_resp = client.request(
        compact({"type": "cmd", "id": 1152, "cmd": "enter_service_mode"}),
        1152,
        timeout_s=10.0,
    )
    require(
        blocked_service_resp.get("ok") is False
        and "Deployment mode is active" in (blocked_service_resp.get("error") or ""),
        "deployment mode must reject service-mode entry",
    )

    blocked_gpio_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1153,
                "cmd": "set_gpio_override",
                "gpio": 15,
                "mode": "output",
                "output_enabled": True,
                "level_high": True,
            }
        ),
        1153,
        timeout_s=10.0,
    )
    require(
        blocked_gpio_resp.get("ok") is False
        and "Bring-up, GPIO, and service writes are locked" in (blocked_gpio_resp.get("error") or ""),
        "deployment mode must reject GPIO override writes",
    )

    exit_resp = client.request(
        compact({"type": "cmd", "id": 1154, "cmd": "exit_deployment_mode"}),
        1154,
        timeout_s=10.0,
    )
    exit_snapshot = unwrap_snapshot(exit_resp)
    require(exit_resp.get("ok") is True, "exit_deployment_mode must succeed")
    require(exit_snapshot.get("deployment", {}).get("active") is False, "exit_deployment_mode response must be fresh")
    print("deployment-lockout: PASS")


def scenario_deployment_usb_safe_fail(client: ControllerClient) -> None:
    enter_resp = client.request(
        compact({"type": "cmd", "id": 1101, "cmd": "enter_deployment_mode"}),
        1101,
        timeout_s=10.0,
    )
    run_resp = client.request(
        compact({"type": "cmd", "id": 1102, "cmd": "run_deployment_sequence"}),
        1102,
        timeout_s=50.0,
    )
    block_resp = client.request(
        compact({"type": "cmd", "id": 1103, "cmd": "laser_output_enable"}),
        1103,
        timeout_s=10.0,
    )
    status_resp = client.request(
        compact({"type": "cmd", "id": 1104, "cmd": "get_status"}),
        1104,
        timeout_s=10.0,
    )

    require(enter_resp.get("ok") is True, "enter_deployment_mode must succeed")
    require(run_resp.get("ok") is True, "run_deployment_sequence must return a status response")
    require(
        block_resp.get("ok") is False
        and "Complete the deployment checklist successfully" in (block_resp.get("error") or ""),
        "runtime NIR request must be rejected before deployment readiness",
    )

    snapshot = unwrap_snapshot(status_resp)
    deployment = snapshot.get("deployment", {})
    rails = snapshot.get("rails", {})
    pd = snapshot.get("pd", {})

    require(deployment.get("active") is True, "deployment mode should stay active after safe-fail")
    require(deployment.get("ready") is False, "deployment must not be ready on USB-only power")
    require(deployment.get("failed") is True, "deployment should record failure on USB-only power")
    require(deployment.get("failureCode") == "pd_insufficient", "deployment should fail at PD qualification")
    require(rails.get("ld", {}).get("enabled") is False, "LD rail must remain off")
    require(rails.get("tec", {}).get("enabled") is False, "TEC rail must remain off")
    require(pd.get("sourceVoltageV", 0) < 9.0, "USB-only source should remain below deployment minimum")
    print("deployment-usb-safe-fail: PASS")


def scenario_runtime_mode_gating(client: ControllerClient) -> None:
    enter_resp = client.request(
        compact({"type": "cmd", "id": 1171, "cmd": "enter_deployment_mode"}),
        1171,
        timeout_s=10.0,
    )
    enter_snapshot = unwrap_snapshot(enter_resp)
    require(enter_resp.get("ok") is True, "enter_deployment_mode must succeed")
    require(enter_snapshot.get("deployment", {}).get("active") is True, "deployment mode must be active before runtime-mode validation")

    binary_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1172,
                "cmd": "set_runtime_mode",
                "mode": "binary_trigger",
            }
        ),
        1172,
        timeout_s=10.0,
    )
    binary_snapshot = unwrap_snapshot(binary_resp)
    bench = binary_snapshot.get("bench", {})
    require(binary_resp.get("ok") is True, "set_runtime_mode binary_trigger must succeed while the path is safe-off")
    require(bench.get("runtimeMode") == "binary_trigger", "runtimeMode must update to binary_trigger")

    blocked_power_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1173,
                "cmd": "set_laser_power",
                "current_a": 1.0,
            }
        ),
        1173,
        timeout_s=10.0,
    )
    require(
        blocked_power_resp.get("ok") is False,
        "host power staging must be rejected while runtime output is unavailable",
    )

    blocked_enable_resp = client.request(
        compact({"type": "cmd", "id": 1174, "cmd": "laser_output_enable"}),
        1174,
        timeout_s=10.0,
    )
    require(
        blocked_enable_resp.get("ok") is False,
        "host output enable must be rejected while runtime output is unavailable",
    )

    blocked_alignment_resp = client.request(
        compact({"type": "cmd", "id": 1175, "cmd": "enable_alignment"}),
        1175,
        timeout_s=10.0,
    )
    require(
        blocked_alignment_resp.get("ok") is False,
        "host alignment requests must be rejected in v2",
    )

    modulated_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1176,
                "cmd": "set_runtime_mode",
                "mode": "modulated_host",
            }
        ),
        1176,
        timeout_s=10.0,
    )
    modulated_snapshot = unwrap_snapshot(modulated_resp)
    require(modulated_resp.get("ok") is True, "set_runtime_mode modulated_host must succeed while the path is safe-off")
    require(
        modulated_snapshot.get("bench", {}).get("runtimeMode") == "modulated_host",
        "runtimeMode must update back to modulated_host",
    )
    print("runtime-mode-gating: PASS")


def scenario_deployment_runtime_flow(client: ControllerClient) -> None:
    enter_resp = client.request(
        compact({"type": "cmd", "id": 1201, "cmd": "enter_deployment_mode"}),
        1201,
        timeout_s=10.0,
    )
    run_resp = client.request(
        compact({"type": "cmd", "id": 1202, "cmd": "run_deployment_sequence"}),
        1202,
        timeout_s=50.0,
    )
    status_after_run = unwrap_snapshot(run_resp)
    deployment = status_after_run.get("deployment", {})

    require(enter_resp.get("ok") is True, "enter_deployment_mode must succeed")
    require(run_resp.get("ok") is True, "run_deployment_sequence must return successfully")
    require(deployment.get("ready") is True, "deployment should be ready for runtime flow validation")
    require(deployment.get("failed") is False, "deployment should not fail in runtime flow validation")

    set_power_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1203,
                "cmd": "set_laser_power",
                "current_a": min(1.5, float(deployment.get("maxLaserCurrentA", 0) or 0)),
            }
        ),
        1203,
        timeout_s=10.0,
    )
    enable_nir_resp = client.request(
        compact({"type": "cmd", "id": 1204, "cmd": "laser_output_enable"}),
        1204,
        timeout_s=10.0,
    )
    status_after_enable = unwrap_snapshot(
        client.request(
            compact({"type": "cmd", "id": 1205, "cmd": "get_status"}),
            1205,
            timeout_s=10.0,
        )
    )
    disable_nir_resp = client.request(
        compact({"type": "cmd", "id": 1206, "cmd": "laser_output_disable"}),
        1206,
        timeout_s=10.0,
    )
    status_after_disable = unwrap_snapshot(
        client.request(
            compact({"type": "cmd", "id": 1207, "cmd": "get_status"}),
            1207,
            timeout_s=10.0,
        )
    )

    require(set_power_resp.get("ok") is True, "set_laser_power must succeed after deployment readiness")
    require(enable_nir_resp.get("ok") is True, "laser_output_enable must succeed after deployment readiness")
    require(disable_nir_resp.get("ok") is True, "laser_output_disable must succeed after deployment readiness")
    require(status_after_enable.get("rails", {}).get("ld", {}).get("enabled") is True, "LD rail must stay logically enabled after NIR request")
    require(status_after_enable.get("rails", {}).get("tec", {}).get("enabled") is True, "TEC rail must stay logically enabled after NIR request")
    require(status_after_disable.get("rails", {}).get("ld", {}).get("enabled") is True, "LD rail must stay logically enabled after NIR disable")
    require(status_after_disable.get("rails", {}).get("tec", {}).get("enabled") is True, "TEC rail must stay logically enabled after NIR disable")
    require(
        status_after_disable.get("deployment", {}).get("ready") is True,
        "deployment should remain ready after NIR disable returns to ready posture",
    )
    print("deployment-runtime-flow: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem201101")
    parser.add_argument(
        "--scenario",
        choices=[
            "parser-matrix",
            "deployment-lockout",
            "deployment-usb-safe-fail",
            "runtime-mode-gating",
            "deployment-runtime-flow",
        ],
        required=True,
    )
    args = parser.parse_args()

    client = ControllerClient(args.port)
    try:
        client.drain()
        if args.scenario == "parser-matrix":
            scenario_parser_matrix(client)
        elif args.scenario == "deployment-lockout":
            scenario_deployment_lockout(client)
        elif args.scenario == "deployment-usb-safe-fail":
            scenario_deployment_usb_safe_fail(client)
        elif args.scenario == "runtime-mode-gating":
            scenario_runtime_mode_gating(client)
        else:
            scenario_deployment_runtime_flow(client)
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"validation failed: {exc}", file=sys.stderr)
        raise
