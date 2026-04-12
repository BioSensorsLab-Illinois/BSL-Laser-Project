#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from typing import Any, Protocol

import serial
import websocket


class ControllerClient(Protocol):
    def close(self) -> None: ...
    def drain(self, duration_s: float = 1.0) -> None: ...
    def request(
        self,
        payload: str,
        command_id: int,
        timeout_s: float = 10.0,
    ) -> dict[str, Any]: ...


@dataclass
class SerialControllerClient:
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
            response = extract_response(buffer, command_id)
            if response is not None:
                return response
        raise TimeoutError(f"timed out waiting for response id={command_id}")


@dataclass
class WebSocketControllerClient:
    url: str
    timeout_s: float = 0.4

    def __post_init__(self) -> None:
        self._open()

    def _open(self) -> None:
        self._ws = websocket.create_connection(self.url, timeout=self.timeout_s)
        self._ws.settimeout(self.timeout_s)

    def _reopen(self) -> None:
        try:
            self._ws.close()
        except Exception:
            pass
        time.sleep(0.3)
        self._open()

    def close(self) -> None:
        self._ws.close()

    def drain(self, duration_s: float = 1.0) -> None:
        end = time.time() + duration_s
        while time.time() < end:
            try:
                _ = self._ws.recv()
            except websocket.WebSocketTimeoutException:
                continue
            except websocket.WebSocketConnectionClosedException:
                break

    def request(self, payload: str, command_id: int, timeout_s: float = 10.0) -> dict[str, Any]:
        self._ws.send(payload.rstrip("\n"))
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            try:
                raw = self._ws.recv()
            except websocket.WebSocketTimeoutException:
                continue
            except websocket.WebSocketConnectionClosedException:
                self._reopen()
                self._ws.send(payload.rstrip("\n"))
                continue

            for line in split_protocol_lines(raw):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if obj.get("type") == "resp" and obj.get("id") == command_id:
                    return obj

        raise TimeoutError(f"timed out waiting for response id={command_id}")


def split_protocol_lines(raw: Any) -> list[str]:
    if isinstance(raw, bytes):
        text = raw.decode("utf-8", "replace")
    else:
        text = str(raw)

    return [line.strip() for line in text.split("\n") if line.strip()]


def extract_response(buffer: str, command_id: int) -> dict[str, Any] | None:
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
    return None


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


def request_cmd(
    client: ControllerClient,
    command_id: int,
    cmd: str,
    args: dict[str, Any] | None = None,
    timeout_s: float = 10.0,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "type": "cmd",
        "id": command_id,
        "cmd": cmd,
    }
    if args:
        payload.update(args)
    return client.request(compact(payload), command_id, timeout_s=timeout_s)


def get_status(client: ControllerClient, command_id: int = 9001) -> dict[str, Any]:
    return unwrap_snapshot(request_cmd(client, command_id, "status.get"))


def wait_for_deployment_terminal(
    client: ControllerClient,
    timeout_s: float = 60.0,
    start_id: int = 9100,
) -> dict[str, Any]:
    deadline = time.time() + timeout_s
    command_id = start_id
    while time.time() < deadline:
        snapshot = get_status(client, command_id)
        deployment = snapshot.get("deployment", {})
        if deployment.get("active") is True and deployment.get("running") is False and (
            deployment.get("ready") is True or deployment.get("failed") is True
        ):
            return snapshot
        time.sleep(0.4)
        command_id += 1
    raise TimeoutError("timed out waiting for deployment terminal state")


def approx_equal(left: float, right: float, tolerance: float = 0.05) -> bool:
    return abs(left - right) <= tolerance


def require_no_stale_latch(snapshot: dict[str, Any]) -> None:
    fault = snapshot.get("fault", {})
    require(
        not (
            fault.get("latched") is True
            and str(fault.get("latchedCode", "none")) == "none"
        ),
        "fault latch must never remain active with latchedCode=none",
    )


def scenario_parser_matrix(client: ControllerClient) -> None:
    spaced_resp = client.request(
        spaced({"type": "cmd", "id": 1001, "cmd": "status.get"}),
        1001,
    )
    compact_resp = client.request(
        compact({"type": "cmd", "id": 1002, "cmd": "status.get"}),
        1002,
    )
    require(spaced_resp.get("ok") is True, "spaced JSON get_status must succeed")
    require(compact_resp.get("ok") is True, "compact JSON get_status must succeed")

    root_key_resp = client.request(
        compact(
            {
                "args": {"id": 999, "cmd": "junk"},
                "command": "status.get",
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
                "cmd": "integrate.set_safety",
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
                "cmd": "operate.set_modulation",
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
    enter_resp = request_cmd(client, 1151, "deployment.enter", timeout_s=10.0)
    enter_snapshot = unwrap_snapshot(enter_resp)
    require(enter_resp.get("ok") is True, "deployment.enter must succeed")
    require(enter_snapshot.get("deployment", {}).get("active") is True, "deployment.enter response must be fresh")

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

    exit_resp = request_cmd(client, 1154, "deployment.exit", timeout_s=10.0)
    exit_snapshot = unwrap_snapshot(exit_resp)
    require(exit_resp.get("ok") is True, "deployment.exit must succeed")
    require(exit_snapshot.get("deployment", {}).get("active") is False, "deployment.exit response must be fresh")
    print("deployment-lockout: PASS")


def scenario_deployment_usb_safe_fail(client: ControllerClient) -> None:
    enter_resp = request_cmd(client, 1101, "deployment.enter", timeout_s=10.0)
    run_resp = request_cmd(client, 1102, "deployment.run", timeout_s=10.0)
    block_resp = client.request(
        compact({"type": "cmd", "id": 1103, "cmd": "operate.set_output", "enabled": True, "current_a": 1.0}),
        1103,
        timeout_s=10.0,
    )
    snapshot = wait_for_deployment_terminal(client, timeout_s=50.0, start_id=1104)

    require(enter_resp.get("ok") is True, "deployment.enter must succeed")
    require(run_resp.get("ok") is True, "deployment.run must acknowledge start")
    require(
        block_resp.get("ok") is False
        and "Complete the deployment checklist successfully" in (block_resp.get("error") or ""),
        "runtime NIR request must be rejected before deployment readiness",
    )

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
    enter_resp = request_cmd(client, 1171, "deployment.enter", timeout_s=10.0)
    enter_snapshot = unwrap_snapshot(enter_resp)
    require(enter_resp.get("ok") is True, "deployment.enter must succeed")
    require(enter_snapshot.get("deployment", {}).get("active") is True, "deployment mode must be active before runtime-mode validation")

    binary_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1172,
                "cmd": "operate.set_mode",
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
                "cmd": "operate.set_output",
                "enabled": True,
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

    modulated_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1176,
                "cmd": "operate.set_mode",
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
    enter_resp = request_cmd(client, 1201, "deployment.enter", timeout_s=10.0)
    run_resp = request_cmd(client, 1202, "deployment.run", timeout_s=10.0)
    status_after_run = wait_for_deployment_terminal(client, timeout_s=50.0, start_id=1203)
    deployment = status_after_run.get("deployment", {})

    require(enter_resp.get("ok") is True, "deployment.enter must succeed")
    require(run_resp.get("ok") is True, "deployment.run must acknowledge start")
    require(deployment.get("ready") is True, "deployment should be ready for runtime flow validation")
    require(deployment.get("failed") is False, "deployment should not fail in runtime flow validation")

    set_power_resp = client.request(
        compact(
            {
                "type": "cmd",
                "id": 1213,
                "cmd": "operate.set_output",
                "enabled": False,
                "current_a": min(1.5, float(deployment.get("maxLaserCurrentA", 0) or 0)),
            }
        ),
        1213,
        timeout_s=10.0,
    )
    disable_nir_resp = client.request(
        compact({"type": "cmd", "id": 1216, "cmd": "operate.set_output", "enabled": False}),
        1216,
        timeout_s=10.0,
    )
    status_after_disable = unwrap_snapshot(
        client.request(
            compact({"type": "cmd", "id": 1217, "cmd": "status.get"}),
            1217,
            timeout_s=10.0,
        )
    )

    require(set_power_resp.get("ok") is True, "set_laser_power must succeed after deployment readiness")
    require(disable_nir_resp.get("ok") is True, "laser_output_disable must succeed after deployment readiness")
    require(status_after_disable.get("rails", {}).get("ld", {}).get("enabled") is True, "LD rail must stay logically enabled after NIR disable")
    require(status_after_disable.get("rails", {}).get("tec", {}).get("enabled") is True, "TEC rail must stay logically enabled after NIR disable")
    require(
        status_after_disable.get("deployment", {}).get("ready") is True,
        "deployment should remain ready after NIR disable returns to ready posture",
    )
    print("deployment-runtime-flow: PASS")


def scenario_safety_persistence_reboot(client: ControllerClient) -> None:
    original_snapshot = get_status(client, 1301)
    original_limit = float(original_snapshot.get("safety", {}).get("maxLaserCurrentA", 0) or 0)
    requested_limit = round(original_limit - 0.25, 2) if original_limit > 0.5 else 0.75

    set_resp = request_cmd(
        client,
        1302,
        "integrate.set_safety",
        args={"max_laser_current_a": requested_limit},
        timeout_s=10.0,
    )
    require(set_resp.get("ok") is True, "integrate.set_safety must succeed")

    live_snapshot = get_status(client, 1303)
    require(
        abs(float(live_snapshot.get("safety", {}).get("maxLaserCurrentA", 0) or 0) - requested_limit) < 0.01,
        "runtime safety must update immediately before reboot",
    )

    request_cmd(client, 1304, "reboot", timeout_s=10.0)
    time.sleep(1.5)
    client.drain(1.0)
    rebooted_snapshot = get_status(client, 1305)
    require(
        abs(float(rebooted_snapshot.get("safety", {}).get("maxLaserCurrentA", 0) or 0) - requested_limit) < 0.01,
        "persisted safety must survive reboot",
    )
    print("safety-persistence-reboot: PASS")


def scenario_ready_invalidation_watch(client: ControllerClient) -> None:
    request_cmd(client, 1310, "deployment.enter", timeout_s=10.0)
    request_cmd(client, 1311, "deployment.run", timeout_s=10.0)
    ready_snapshot = wait_for_deployment_terminal(client, timeout_s=50.0, start_id=1312)
    require(ready_snapshot.get("deployment", {}).get("ready") is True, "deployment must reach ready before invalidation watch")

    deadline = time.time() + 20.0
    command_id = 1320
    while time.time() < deadline:
        snapshot = get_status(client, command_id)
        deployment = snapshot.get("deployment", {})
        rails = snapshot.get("rails", {})
        if deployment.get("ready") is False and deployment.get("failed") is True:
            require(rails.get("ld", {}).get("enabled") is False, "LD rail must drop after ready invalidation")
            print("ready-invalidation-watch: PASS")
            return
        time.sleep(0.5)
        command_id += 1

    raise AssertionError(
        "ready invalidation did not occur during the watch window. On real hardware, induce TEC loss during this scenario."
    )


def scenario_pd_passive_only_during_deployment(client: ControllerClient) -> None:
    refresh_resp = request_cmd(client, 1330, "refresh_pd_status", timeout_s=10.0)
    require(refresh_resp.get("ok") is True, "refresh_pd_status must succeed before deployment cache validation")

    pre_snapshot = get_status(client, 1331)
    pre_pd = pre_snapshot.get("pd", {})
    require(int(pre_pd.get("lastUpdatedMs", 0) or 0) > 0, "PD snapshot should have a timestamp after explicit refresh")

    request_cmd(client, 1332, "deployment.enter", timeout_s=10.0)
    request_cmd(client, 1333, "deployment.run", timeout_s=10.0)

    deadline = time.time() + 30.0
    command_id = 1334
    while time.time() < deadline:
        snapshot = get_status(client, command_id)
        deployment = snapshot.get("deployment", {})
        pd = snapshot.get("pd", {})
        require(
            pd.get("source") in ("cached", "none"),
            "deployment must not trigger explicit Integrate or boot-reconcile PD ownership while active",
        )
        if deployment.get("running") is False and (
            deployment.get("ready") is True or deployment.get("failed") is True
        ):
            print("pd-passive-only-during-deployment: PASS")
            return
        time.sleep(0.4)
        command_id += 1

    raise TimeoutError("timed out waiting for deployment to complete during PD ownership validation")


def scenario_aux_control_pass(client: ControllerClient) -> None:
    enter_resp = request_cmd(client, 1401, "deployment.enter", timeout_s=10.0)
    enter_snapshot = unwrap_snapshot(enter_resp)
    bench = enter_snapshot.get("bench", {})
    require(enter_resp.get("ok") is True, "deployment.enter must succeed for aux-control pass")
    require(enter_snapshot.get("deployment", {}).get("active") is True, "deployment must be active for aux-control pass")
    require(bench.get("requestedLedEnabled") is False, "GPIO6 LED must stay off immediately after deployment.enter")
    require(bench.get("appliedLedPinHigh") is False, "GPIO6 must read low immediately after deployment.enter")

    led_on_resp = request_cmd(
        client,
        1402,
        "operate.set_led",
        args={"enabled": True, "duty_cycle_pct": 25, "frequency_hz": 20000},
        timeout_s=10.0,
    )
    led_on_snapshot = unwrap_snapshot(led_on_resp)
    led_bench = led_on_snapshot.get("bench", {})
    require(led_on_resp.get("ok") is True, "operate.set_led enable must succeed after deployment.enter")
    require(led_bench.get("requestedLedEnabled") is True, "GPIO6 LED request must turn on")
    require(int(led_bench.get("requestedLedDutyCyclePct", 0) or 0) == 25, "GPIO6 LED duty request must persist")
    require(led_bench.get("appliedLedPinHigh") is True, "GPIO6 pin must read high after LED enable")

    led_off_resp = request_cmd(
        client,
        1403,
        "operate.set_led",
        args={"enabled": False, "duty_cycle_pct": 0, "frequency_hz": 20000},
        timeout_s=10.0,
    )
    led_off_snapshot = unwrap_snapshot(led_off_resp)
    require(led_off_resp.get("ok") is True, "operate.set_led disable must succeed")
    require(led_off_snapshot.get("bench", {}).get("requestedLedEnabled") is False, "GPIO6 LED request must clear")
    require(led_off_snapshot.get("bench", {}).get("appliedLedPinHigh") is False, "GPIO6 pin must return low after LED disable")

    alignment_on_resp = request_cmd(
        client,
        1404,
        "operate.set_alignment",
        args={"enabled": True},
        timeout_s=10.0,
    )
    alignment_on_snapshot = unwrap_snapshot(alignment_on_resp)
    require(alignment_on_resp.get("ok") is True, "operate.set_alignment enable must succeed after deployment.enter")
    require(
        alignment_on_snapshot.get("bench", {}).get("requestedAlignmentEnabled") is True,
        "green alignment request must persist even if output is safety-blocked",
    )
    require(
        alignment_on_snapshot.get("laser", {}).get("alignmentEnabled") is True
        or alignment_on_snapshot.get("safety", {}).get("allowAlignment") is False,
        "green alignment request must either apply or remain explicitly safety-blocked",
    )

    alignment_off_resp = request_cmd(
        client,
        1405,
        "operate.set_alignment",
        args={"enabled": False},
        timeout_s=10.0,
    )
    alignment_off_snapshot = unwrap_snapshot(alignment_off_resp)
    require(alignment_off_resp.get("ok") is True, "operate.set_alignment disable must succeed")
    require(
        alignment_off_snapshot.get("bench", {}).get("requestedAlignmentEnabled") is False,
        "green alignment request must clear after disable",
    )
    print("aux-control-pass: PASS")


def scenario_ready_runtime_pass(client: ControllerClient) -> None:
    request_cmd(client, 1501, "deployment.enter", timeout_s=10.0)
    request_cmd(client, 1502, "deployment.run", timeout_s=10.0)
    ready_snapshot = wait_for_deployment_terminal(client, timeout_s=60.0, start_id=1503)
    deployment = ready_snapshot.get("deployment", {})
    require(deployment.get("ready") is True, "deployment must reach ready for runtime pass")

    initial_lambda = float(ready_snapshot.get("tec", {}).get("targetLambdaNm", 0) or 0)
    temp_resp = request_cmd(
        client,
        1510,
        "operate.set_target",
        args={"target_mode": "temp", "temp_c": 26.0},
        timeout_s=10.0,
    )
    temp_snapshot = unwrap_snapshot(temp_resp)
    require(temp_resp.get("ok") is True, "operate.set_target temp must succeed after readiness")
    require(
        approx_equal(float(temp_snapshot.get("tec", {}).get("targetTempC", 0) or 0), 26.0, 0.2),
        "targetTempC must update after temp commit",
    )
    require(
        not approx_equal(float(temp_snapshot.get("tec", {}).get("targetLambdaNm", 0) or 0), initial_lambda, 0.01),
        "linked wavelength must update when temperature changes",
    )

    lambda_resp = request_cmd(
        client,
        1511,
        "operate.set_target",
        args={"target_mode": "lambda", "lambda_nm": 780.0},
        timeout_s=10.0,
    )
    lambda_snapshot = unwrap_snapshot(lambda_resp)
    require(lambda_resp.get("ok") is True, "operate.set_target lambda must succeed after readiness")
    require(
        approx_equal(float(lambda_snapshot.get("tec", {}).get("targetLambdaNm", 0) or 0), 780.0, 0.2),
        "targetLambdaNm must update after wavelength commit",
    )
    require(
        float(lambda_snapshot.get("tec", {}).get("targetTempC", 0) or 0) > 0,
        "linked temperature must remain available after wavelength commit",
    )

    stored_current = min(1.0, float(deployment.get("maxLaserCurrentA", 0) or 0))
    current_resp = request_cmd(
        client,
        1512,
        "operate.set_output",
        args={"enabled": False, "current_a": stored_current},
        timeout_s=10.0,
    )
    current_snapshot = unwrap_snapshot(current_resp)
    require(current_resp.get("ok") is True, "operate.set_output must store current while output is off")
    require(
        approx_equal(float(current_snapshot.get("bench", {}).get("requestedCurrentA", 0) or 0), stored_current, 0.02),
        "requestedCurrentA must persist while output is off",
    )
    require(
        current_snapshot.get("bench", {}).get("requestedNirEnabled") is False,
        "output enable must remain off while only updating stored current",
    )

    enable_resp = request_cmd(
        client,
        1513,
        "operate.set_output",
        args={"enabled": True, "current_a": 0.0},
        timeout_s=10.0,
    )
    enable_snapshot = unwrap_snapshot(enable_resp)
    require(enable_resp.get("ok") is True, "operate.set_output enable must succeed after readiness")
    require(enable_snapshot.get("bench", {}).get("requestedNirEnabled") is True, "NIR request must turn on")

    disable_resp = request_cmd(
        client,
        1514,
        "operate.set_output",
        args={"enabled": False, "current_a": 0.0},
        timeout_s=10.0,
    )
    disable_snapshot = unwrap_snapshot(disable_resp)
    require(disable_resp.get("ok") is True, "operate.set_output disable must succeed after readiness")
    require(disable_snapshot.get("bench", {}).get("requestedNirEnabled") is False, "NIR request must clear")

    modulation_resp = request_cmd(
        client,
        1515,
        "operate.set_modulation",
        args={"enabled": True, "frequency_hz": 1500, "duty_cycle_pct": 35},
        timeout_s=10.0,
    )
    modulation_snapshot = unwrap_snapshot(modulation_resp)
    require(modulation_resp.get("ok") is True, "operate.set_modulation must succeed after readiness")
    require(modulation_snapshot.get("bench", {}).get("modulationEnabled") is True, "modulation enable must persist")
    require(
        int(modulation_snapshot.get("bench", {}).get("modulationFrequencyHz", 0) or 0) == 1500,
        "modulation frequency must persist",
    )
    require(
        int(modulation_snapshot.get("bench", {}).get("modulationDutyCyclePct", 0) or 0) == 35,
        "modulation duty cycle must persist",
    )
    print("ready-runtime-pass: PASS")


def scenario_fault_edge_pass(client: ControllerClient) -> None:
    baseline = get_status(client, 1601)
    require_no_stale_latch(baseline)

    request_cmd(client, 1602, "refresh_pd_status", timeout_s=10.0)
    request_cmd(client, 1603, "deployment.enter", timeout_s=10.0)
    request_cmd(client, 1604, "deployment.run", timeout_s=10.0)

    deadline = time.time() + 35.0
    command_id = 1605
    saw_terminal = False
    while time.time() < deadline:
        snapshot = get_status(client, command_id)
        require_no_stale_latch(snapshot)
        deployment = snapshot.get("deployment", {})
        pd = snapshot.get("pd", {})
        require(
            pd.get("source") in ("cached", "none"),
            "deployment must only consume passive PD ownership while active",
        )
        if deployment.get("running") is False and (
            deployment.get("ready") is True or deployment.get("failed") is True
        ):
            saw_terminal = True
            break
        time.sleep(0.4)
        command_id += 1

    require(saw_terminal, "deployment must reach a terminal state during fault/edge pass")
    print("fault-edge-pass: PASS")


def make_client(args: argparse.Namespace) -> ControllerClient:
    if args.transport == "ws":
        return WebSocketControllerClient(args.ws_url, timeout_s=args.timeout_s)

    return SerialControllerClient(args.port, baudrate=args.baudrate, timeout_s=args.timeout_s)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--transport", choices=["serial", "ws"], default="serial")
    parser.add_argument("--port", default="/dev/cu.usbmodem201101")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--ws-url", default="ws://192.168.4.1/ws")
    parser.add_argument("--timeout-s", type=float, default=0.4)
    parser.add_argument(
        "--scenario",
        choices=[
            "parser-matrix",
            "deployment-lockout",
            "deployment-usb-safe-fail",
            "runtime-mode-gating",
            "deployment-runtime-flow",
            "safety-persistence-reboot",
            "ready-invalidation-watch",
            "pd-passive-only-during-deployment",
            "aux-control-pass",
            "ready-runtime-pass",
            "fault-edge-pass",
        ],
        required=True,
    )
    args = parser.parse_args()

    client = make_client(args)
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
        elif args.scenario == "safety-persistence-reboot":
            scenario_safety_persistence_reboot(client)
        elif args.scenario == "ready-invalidation-watch":
            scenario_ready_invalidation_watch(client)
        elif args.scenario == "pd-passive-only-during-deployment":
            scenario_pd_passive_only_during_deployment(client)
        elif args.scenario == "aux-control-pass":
            scenario_aux_control_pass(client)
        elif args.scenario == "ready-runtime-pass":
            scenario_ready_runtime_pass(client)
        elif args.scenario == "fault-edge-pass":
            scenario_fault_edge_pass(client)
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
