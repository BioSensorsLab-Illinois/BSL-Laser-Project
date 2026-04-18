import Foundation
import BSLProtocol

// Sanity + aggressive-hunt tests for the BSLProtocol library. Exits with
// code 0 when every check passes, 1 otherwise. Kept outside XCTest/Testing
// so it runs against the Command Line Tools SDK without a full Xcode.

// MARK: - Check harness -------------------------------------------------

enum Check {
    nonisolated(unsafe) static var failed = 0
    nonisolated(unsafe) static var passed = 0

    static func expect(
        _ condition: @autoclosure () -> Bool,
        _ message: String,
        file: StaticString = #file,
        line: UInt = #line
    ) {
        if condition() {
            passed += 1
        } else {
            failed += 1
            print("FAIL \(file):\(line) — \(message)")
        }
    }
}

private func fixtureURL(_ name: String) -> URL {
    let bundle = Bundle.module
    guard let url = bundle.url(forResource: name, withExtension: "json", subdirectory: "Fixtures") else {
        fatalError("missing fixture \(name)")
    }
    return url
}

// MARK: - Fixture decode -----------------------------------------------

func checkStatusSnapshotDecode() throws {
    let url = fixtureURL("status_snapshot_sample")
    let line = try String(contentsOf: url, encoding: .utf8)
        .replacingOccurrences(of: "\n", with: " ")
    switch FrameParser.parse(line: line) {
    case .statusSnapshot(let snap):
        Check.expect(snap.session.state == .serviceMode, "session.state")
        Check.expect(snap.session.powerTier == .full, "session.powerTier")
        Check.expect(snap.rails.tec.enabled, "rails.tec.enabled")
        Check.expect(snap.rails.tec.pgood, "rails.tec.pgood")
        Check.expect(!snap.rails.ld.enabled, "rails.ld.enabled")
        Check.expect(abs(snap.tof.distanceM - 0.42) < 1e-6, "tof.distanceM")
        Check.expect(abs(snap.tec.targetLambdaNm - 785.0) < 1e-6, "tec.targetLambdaNm")
        Check.expect(abs(snap.tec.actualLambdaNm - 784.7) < 1e-6, "tec.actualLambdaNm")
        Check.expect(abs(snap.bench.requestedCurrentA - 1.5) < 1e-6, "bench.requestedCurrentA")
        Check.expect(snap.bench.appliedLedOwner == .operateRuntime, "bench.appliedLedOwner")
        Check.expect(snap.bench.hostControlReadiness.nirBlockedReason == .deploymentOff, "nirBlockedReason")
        Check.expect(snap.bench.hostControlReadiness.ledBlockedReason == .none, "ledBlockedReason")
        Check.expect(abs(snap.safety.maxLaserCurrentA - 4.5) < 1e-6, "safety.maxLaserCurrentA")
        Check.expect(snap.safety.maxTofLedDutyCyclePct == 50, "safety.maxTofLedDutyCyclePct")
        Check.expect(snap.safety.interlocks.horizonEnabled, "interlocks.horizonEnabled")
        Check.expect(!snap.safety.interlocks.tofLowBoundOnly, "interlocks.tofLowBoundOnly")
        Check.expect(!snap.fault.latched, "fault.latched")
        Check.expect(snap.fault.activeCode == "none", "fault.activeCode")
        Check.expect(snap.deployment.phase == .inactive, "deployment.phase")
    default:
        Check.expect(false, "did not parse as statusSnapshot")
    }
}

func checkLiveTelemetryEnvelope() {
    let line = """
    {"type":"event","event":"live_telemetry","timestamp_ms":1,"payload":{\
    "laser":{"measuredCurrentA":2.25,"nirEnabled":true,"loopGood":true},\
    "tec":{"tempC":27.5,"targetTempC":25.0,"tempGood":false,"settlingSecondsRemaining":2}\
    }}
    """
    switch FrameParser.parse(line: line) {
    case .liveTelemetry(let snap, let present):
        Check.expect(snap.laser.nirEnabled, "live.laser.nirEnabled")
        Check.expect(abs(snap.laser.measuredCurrentA - 2.25) < 1e-6, "live.laser.measuredCurrentA")
        Check.expect(!snap.tec.tempGood, "live.tec.tempGood")
        Check.expect(snap.tec.settlingSecondsRemaining == 2, "live.tec.settlingSecondsRemaining")
        Check.expect(present.contains("laser") && present.contains("tec"), "presentKeys cover laser + tec")
        Check.expect(!present.contains("rails"), "presentKeys does not include absent blocks")
    default:
        Check.expect(false, "did not parse as liveTelemetry")
    }
}

func checkResponseEnvelopeOkAndError() {
    let ok = FrameParser.parse(line: #"{"type":"resp","id":42,"ok":true}"#)
    guard case .commandResponse(let okResp) = ok else {
        Check.expect(false, "ok resp did not parse"); return
    }
    Check.expect(okResp.id == 42, "resp.id == 42")
    Check.expect(okResp.ok, "resp.ok")
    Check.expect(okResp.error == nil, "resp.error == nil")

    let err = FrameParser.parse(line: #"{"type":"resp","id":9,"ok":false,"error":"Deployment mode is already active."}"#)
    guard case .commandResponse(let errResp) = err else {
        Check.expect(false, "err resp did not parse"); return
    }
    Check.expect(!errResp.ok, "!errResp.ok")
    Check.expect(errResp.error == "Deployment mode is already active.", "errResp.error verbatim")
}

func checkUnknownEnumTokens() {
    let line = #"{"type":"event","event":"live_telemetry","timestamp_ms":1,"payload":{"session":{"state":"NEW_STATE"},"bench":{"hostControlReadiness":{"nirBlockedReason":"new-token"}}}}"#
    switch FrameParser.parse(line: line) {
    case .liveTelemetry(let snap, _):
        Check.expect(snap.session.state == .unknown, "unknown session state → .unknown")
        Check.expect(snap.bench.hostControlReadiness.nirBlockedReason == .unknown, "unknown reason → .unknown")
    default:
        Check.expect(false, "decode must not fail on unknown tokens")
    }
}

func checkMalformedLine() {
    switch FrameParser.parse(line: "{not json") {
    case .decodeError: Check.passed += 1
    default: Check.expect(false, "expected decodeError for malformed line")
    }
}

func checkCommandEnvelopeShape() throws {
    let envelope = CommandEnvelope(
        id: 7,
        cmd: "operate.set_output",
        args: [
            "enable": .bool(true),
            "current_a": .double(2.5),
        ]
    )
    let data = try JSONEncoder().encode(envelope)
    guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
        Check.expect(false, "envelope did not serialize to object"); return
    }
    Check.expect(obj["id"] as? Int == 7, "envelope.id")
    Check.expect(obj["type"] as? String == "cmd", "envelope.type")
    Check.expect(obj["cmd"] as? String == "operate.set_output", "envelope.cmd")
    guard let args = obj["args"] as? [String: Any] else {
        Check.expect(false, "envelope.args not an object"); return
    }
    Check.expect(args["enable"] as? Bool == true, "args.enable")
    Check.expect(args["current_a"] as? Double == 2.5, "args.current_a")
}

func checkNirBlockedReasonExhaustive() {
    let allReal: [NirBlockedReason] = [
        .none, .notConnected, .faultLatched, .deploymentOff,
        .checklistRunning, .checklistNotReady, .readyNotIdle,
        .notModulatedHost, .powerNotFull, .railNotGood, .tecNotSettled,
    ]
    Check.expect(allReal.count == 11, "NirBlockedReason case count (none + 10)")
    for reason in allReal {
        Check.expect(!reason.operatorText.isEmpty, "operatorText for \(reason)")
    }
}

// MARK: - Adversarial frames ------------------------------------------

func checkAdversarialFrames() {
    // 1. Empty object.
    switch FrameParser.parse(line: "{}") {
    case .statusSnapshot, .other:
        Check.passed += 1 // either classification is fine; must not crash
    default:
        Check.expect(false, "empty object must not decodeError-out catastrophically")
    }

    // 2. Null literal.
    switch FrameParser.parse(line: "null") {
    case .decodeError, .other: Check.passed += 1
    default:
        Check.expect(false, "null must not classify as statusSnapshot")
    }

    // 3. Array, not object.
    switch FrameParser.parse(line: "[1,2,3]") {
    case .decodeError: Check.passed += 1
    default: Check.expect(false, "JSON array must return decodeError")
    }

    // 4. Event envelope with wrong-typed numeric fields — tempC as string.
    let wrongType = #"{"type":"event","event":"live_telemetry","timestamp_ms":1,"payload":{"tec":{"tempC":"not-a-number"}}}"#
    switch FrameParser.parse(line: wrongType) {
    case .liveTelemetry(let snap, _):
        // Firmware contract says tempC is Double; we keep the default rather
        // than crashing. Current SafetyStatus/TecStatus decoders use
        // decodeIfPresent(Double.self) which throws on a string — and our
        // parent decodeIfPresent<TecStatus> catches that throw. Either a
        // default TecStatus or liveTelemetry with empty present keys is OK;
        // a crash or .decodeError is not.
        Check.expect(snap.tec.tempC == 0, "wrong-type tempC falls back to default")
    case .decodeError:
        Check.passed += 1 // acceptable — we just must not crash
    default:
        Check.expect(false, "wrong-type payload must not classify as statusSnapshot")
    }

    // 5. Multiple JSON objects on one line (the transport splits on \n, but
    // a double-encoded mistake should still not crash).
    switch FrameParser.parse(line: "{\"type\":\"resp\",\"id\":1,\"ok\":true}{\"extra\":true}") {
    case .decodeError: Check.passed += 1
    default:
        Check.expect(false, "concatenated JSON must decodeError, not silently accept")
    }

    // 6. Unicode + embedded control chars in an error string.
    let utf = #"{"type":"resp","id":1,"ok":false,"error":"Żyła żółtego węgorza 🐍 — blocked"}"#
    switch FrameParser.parse(line: utf) {
    case .commandResponse(let resp):
        Check.expect(resp.error?.contains("🐍") == true, "utf-8 + emoji preserved")
    default:
        Check.expect(false, "utf-8 response did not parse")
    }

    // 7. Oversize-ish payload (500 KB of junk keys inside an event envelope).
    // The firmware would never emit this, but a rogue source shouldn't
    // crash us.
    var big = "{\"type\":\"event\",\"event\":\"live_telemetry\",\"timestamp_ms\":1,\"payload\":{\"tec\":{\"tempC\":99.9}"
    for i in 0..<2000 {
        big += ",\"k\(i)\":\"\(String(repeating: "x", count: 200))\""
    }
    big += "}}"
    switch FrameParser.parse(line: big) {
    case .liveTelemetry(let snap, _):
        Check.expect(abs(snap.tec.tempC - 99.9) < 1e-6, "oversize frame still extracts tec.tempC")
    default:
        Check.expect(false, "oversize frame should still decode the known fields")
    }

    // 8. Unknown event type.
    switch FrameParser.parse(line: #"{"type":"event","event":"future_event","timestamp_ms":1,"payload":{}}"#) {
    case .other(let name):
        Check.expect(name == "future_event", "unknown event keeps name")
    default:
        Check.expect(false, "unknown event must route to .other")
    }
}

// MARK: - Patch merge semantics ----------------------------------------

func checkPatchMergeSemantics() {
    var base = DeviceSnapshot()
    base.laser.nirEnabled = true
    base.laser.measuredCurrentA = 2.3
    base.tec.tempC = 30.0
    base.safety.maxLaserCurrentA = 5.0

    // Patch touches laser only.
    var patch = DeviceSnapshot()
    patch.laser.nirEnabled = false
    patch.laser.measuredCurrentA = 0.0
    // Safety left at defaults — must NOT clobber the base's 5.0 A ceiling.

    let merged = DeviceSnapshot.overlay(base: base, patch: patch, presentKeys: ["laser"])

    Check.expect(merged.laser.nirEnabled == false, "patch overlay updates laser.nirEnabled")
    Check.expect(merged.laser.measuredCurrentA == 0.0, "patch overlay updates laser.measuredCurrentA")
    Check.expect(merged.tec.tempC == 30.0, "patch overlay preserves tec.tempC (not in presentKeys)")
    Check.expect(merged.safety.maxLaserCurrentA == 5.0, "patch overlay preserves safety.maxLaserCurrentA (not in presentKeys)")

    // Presenting "safety" in the patch replaces whole safety block.
    var safetyPatch = DeviceSnapshot()
    safetyPatch.safety = base.safety
    safetyPatch.safety.maxLaserCurrentA = 4.0
    let merged2 = DeviceSnapshot.overlay(base: base, patch: safetyPatch, presentKeys: ["safety"])
    Check.expect(merged2.safety.maxLaserCurrentA == 4.0, "overlay replaces safety when key present")
    Check.expect(merged2.laser.nirEnabled == true, "overlay preserves laser when not in presentKeys")
}

// MARK: - Integration harness (mock WS server) --------------------------

struct CommandDispatcher {
    let transport: WebSocketTransport
    @MainActor
    static func build() -> WebSocketTransport {
        WebSocketTransport()
    }
}

@MainActor
final class IntegrationObserver: TransportObserver {
    var state: WebSocketTransport.State = .idle
    var frames: [InboundFrame] = []
    let onConnected: () -> Void
    var connectedSignalled = false

    init(onConnected: @escaping () -> Void) {
        self.onConnected = onConnected
    }

    func transportDidChange(state: WebSocketTransport.State) {
        self.state = state
        if case .connected = state, !connectedSignalled {
            connectedSignalled = true
            onConnected()
        }
    }

    func transportDidReceive(frame: InboundFrame) {
        frames.append(frame)
    }
}

/// Starts the Python mock as a child process on a given port and waits for
/// it to bind. Returns the Process so the caller can terminate it.
func startMock(port: Int, repoRoot: String) async throws -> Process {
    let mockPath = "\(repoRoot)/ios/tools/mock-ws-server.py"
    let process = Process()
    process.launchPath = "/usr/bin/env"
    process.arguments = ["python3", mockPath]
    var env = ProcessInfo.processInfo.environment
    env["BSL_MOCK_PORT"] = String(port)
    process.environment = env
    let devNull = FileHandle(forWritingAtPath: "/dev/null")
    if let devNull {
        process.standardOutput = devNull
        process.standardError = devNull
    }
    try process.run()

    // Poll the /meta endpoint until it answers or 4s elapses.
    let start = Date()
    let probe = MetaProbe(timeoutSeconds: 0.3)
    while Date().timeIntervalSince(start) < 4.0 {
        if let _ = try? await probe.probe(ip: "127.0.0.1:\(port)") {
            return process
        }
        try? await Task.sleep(nanoseconds: 100_000_000)
    }
    process.terminate()
    throw NSError(domain: "IntegrationHarness", code: 1, userInfo: [NSLocalizedDescriptionKey: "mock did not bind"])
}

// `MetaProbe.probe(ip:)` expects a bare IP; the mock URL uses a host:port.
// Add a small helper URL builder for the test harness.
extension MetaProbe {
    func probe(ip: String) async throws -> MetaResponse {
        // Support `ip` of either "1.2.3.4" or "host:port".
        guard let url = URL(string: "http://\(ip)/meta") else {
            throw MetaProbe.ProbeError.badURL
        }
        return try await probe(url: url)
    }
}

@MainActor
func runIntegration(port: Int) async {
    let url = URL(string: "ws://127.0.0.1:\(port)/ws")!

    // --- connect + handshake ---------------------------------------------
    let connectedSem = Semaphore()
    let obs = IntegrationObserver(onConnected: { connectedSem.signal() })
    let transport = WebSocketTransport()
    transport.attach(observer: obs)
    transport.connect(url: url)

    await connectedSem.wait(timeoutSeconds: 3)
    Check.expect(obs.connectedSignalled, "transport reached .connected within 3s")

    // Wait for the mock's first snapshot frame.
    let firstFrameDeadline = Date().addingTimeInterval(3)
    while obs.frames.isEmpty && Date() < firstFrameDeadline {
        try? await Task.sleep(nanoseconds: 50_000_000)
    }
    Check.expect(!obs.frames.isEmpty, "received a frame within 3s of connect")
    if case .statusSnapshot(let snap) = obs.frames.first ?? .decodeError("none") {
        Check.expect(snap.bench.hostControlReadiness.nirBlockedReason == .deploymentOff,
                     "mock snapshot reports nirBlockedReason=deployment-off")
    } else {
        Check.expect(false, "first frame was not a statusSnapshot: \(obs.frames.first.map(String.init(describing:)) ?? "?")")
    }

    // --- send a command + await resp -------------------------------------
    do {
        let resp = try await transport.send(
            command: "operate.set_output",
            args: ["enable": .bool(true), "current_a": .double(1.75)]
        )
        Check.expect(resp.ok, "operate.set_output ok")
        Check.expect(resp.id > 0, "resp.id > 0")
    } catch {
        Check.expect(false, "operate.set_output threw: \(error)")
    }

    // --- mock should now reflect the updated requested current ----------
    // Wait for the post-command snapshot.
    try? await Task.sleep(nanoseconds: 300_000_000)
    var sawUpdatedSnapshot = false
    for frame in obs.frames {
        if case .statusSnapshot(let s) = frame, abs(s.bench.requestedCurrentA - 1.75) < 1e-6 {
            sawUpdatedSnapshot = true
            break
        }
    }
    Check.expect(sawUpdatedSnapshot, "post-command snapshot carries requestedCurrentA=1.75")

    // --- service mode toggle unblocks the NIR path in the mock ----------
    _ = try? await transport.send(command: "enter_service_mode", args: nil)
    try? await Task.sleep(nanoseconds: 300_000_000)
    var sawUnblocked = false
    for frame in obs.frames.reversed() {
        if case .statusSnapshot(let s) = frame,
           s.bench.hostControlReadiness.nirBlockedReason == .none {
            sawUnblocked = true
            break
        }
    }
    Check.expect(sawUnblocked, "enter_service_mode unblocks nirBlockedReason in mock")

    // --- stress: 1000 rapid commands, all must get distinct IDs and ack --
    // We send in groups of 20 concurrently to avoid starving the Swift queue
    // but still exercise the id-correlation map under real contention.
    let stressTotal = 1000
    let batch = 20
    var acked = 0
    var ids = Set<Int>()
    for round in 0..<(stressTotal / batch) {
        await withTaskGroup(of: (Bool, Int).self) { group in
            for i in 0..<batch {
                let duty = (round * batch + i) % 50
                group.addTask {
                    do {
                        let resp = try await transport.send(
                            command: "operate.set_led",
                            args: ["enable": .bool(true), "duty_cycle_pct": .int(duty)]
                        )
                        return (resp.ok, resp.id)
                    } catch {
                        return (false, -1)
                    }
                }
            }
            for await (ok, id) in group {
                if ok { acked += 1 }
                if id > 0 { ids.insert(id) }
            }
        }
    }
    Check.expect(acked == stressTotal, "stress: \(stressTotal)/\(stressTotal) commands acked; got \(acked)")
    Check.expect(ids.count == stressTotal, "stress: \(stressTotal) distinct command IDs; got \(ids.count)")

    // --- live-telemetry flow: after ~2s the observer should have at least
    // one live_telemetry frame in addition to the snapshots the mock emits
    // after each command response.
    let framesBeforeWait = obs.frames.count
    try? await Task.sleep(nanoseconds: 1_800_000_000)
    let newLiveFrames = obs.frames.dropFirst(framesBeforeWait).contains { frame in
        if case .liveTelemetry = frame { return true }
        return false
    }
    Check.expect(newLiveFrames, "stability: live_telemetry frames arrive between commands")

    // --- multi-line frame: the mock does one JSON per WS message, but the
    // transport MUST correctly split a single message containing multiple
    // newline-separated JSON lines. Verify by feeding the FrameParser
    // directly — the transport splits on \n before calling it.
    let multi = #"{"type":"resp","id":101,"ok":true}"# + "\n" + #"{"type":"resp","id":102,"ok":false,"error":"rejected"}"#
    var partsSeen = 0
    for line in multi.split(separator: "\n") {
        let f = FrameParser.parse(line: String(line))
        if case .commandResponse = f { partsSeen += 1 }
    }
    Check.expect(partsSeen == 2, "multi-line frame: both JSON objects extracted; got \(partsSeen)")

    // --- disconnect mid-flight: send one, kill the socket --------------
    let dropTask: Task<ResponseEnvelope, Error> = Task {
        try await transport.send(command: "operate.set_output", args: ["enable": .bool(false), "current_a": .double(0)])
    }
    try? await Task.sleep(nanoseconds: 10_000_000)
    transport.disconnect(reason: "integration: drop test")
    do {
        _ = try await dropTask.value
        // The mock is fast enough that sometimes the command races in. Both
        // outcomes are fine — we just must not hang forever or crash.
        Check.passed += 1
    } catch {
        Check.passed += 1
    }

    // --- reconnect after a clean disconnect works --------------------
    // Wait a moment for the transport-level teardown to settle, then open
    // again against the same mock. The transport MUST accept a fresh
    // connect() after a prior disconnect() without crashing or silently
    // ignoring the call. Poll the observer state to avoid semaphore-reuse
    // pitfalls.
    try? await Task.sleep(nanoseconds: 400_000_000)
    obs.frames.removeAll()
    transport.connect(url: url)

    var reconnected = false
    let reconnectDeadline = Date().addingTimeInterval(5)
    while Date() < reconnectDeadline {
        if case .connected = obs.state { reconnected = true; break }
        try? await Task.sleep(nanoseconds: 50_000_000)
    }
    if !reconnected {
        print("DEBUG: observer state after reconnect window: \(obs.state)")
    }
    Check.expect(reconnected, "reconnect: transport state == .connected after fresh connect()")

    // Send a command through the re-opened socket and verify the ID
    // counter kept going (no silent reset to 1 that would collide with the
    // ID the mock already saw).
    if reconnected {
        do {
            let resp = try await transport.send(command: "enter_service_mode", args: nil)
            Check.expect(resp.ok, "reconnect: follow-up command acked")
            Check.expect(resp.id > stressTotal, "reconnect: id counter did not reset (got \(resp.id), expected > \(stressTotal))")
        } catch {
            Check.expect(false, "reconnect: follow-up command threw: \(error)")
        }
    }

    transport.disconnect(reason: "integration: done")
}

final class Semaphore: @unchecked Sendable {
    private let lock = NSLock()
    private var signalled = false
    private var waiters: [CheckedContinuation<Void, Never>] = []

    func signal() {
        lock.lock()
        signalled = true
        let pending = waiters
        waiters.removeAll()
        lock.unlock()
        for cont in pending { cont.resume() }
    }

    func wait(timeoutSeconds: Double) async {
        let deadline = Date().addingTimeInterval(timeoutSeconds)
        while true {
            lock.lock()
            if signalled { lock.unlock(); return }
            lock.unlock()
            if Date() >= deadline { return }
            try? await Task.sleep(nanoseconds: 20_000_000)
        }
    }
}

// MARK: - Driver -------------------------------------------------------

@MainActor
func run() async {
    do {
        try checkStatusSnapshotDecode()
        checkLiveTelemetryEnvelope()
        checkResponseEnvelopeOkAndError()
        checkUnknownEnumTokens()
        checkMalformedLine()
        try checkCommandEnvelopeShape()
        checkNirBlockedReasonExhaustive()
        checkAdversarialFrames()
        checkPatchMergeSemantics()
    } catch {
        print("EXCEPTION in offline checks: \(error)")
        Check.failed += 1
    }

    let integrationEnabled = ProcessInfo.processInfo.environment["BSL_INTEGRATION"] == "1"
    if integrationEnabled {
        let root = ProcessInfo.processInfo.environment["BSL_REPO_ROOT"]
            ?? "/Users/zz4/BSL/BSL-Laser"
        let port = 8090
        do {
            print("--- starting Python mock on port \(port) ---")
            let proc = try await startMock(port: port, repoRoot: root)
            defer {
                proc.terminate()
                proc.waitUntilExit()
            }
            await runIntegration(port: port)
        } catch {
            print("integration harness failed to start: \(error)")
            Check.failed += 1
        }
    } else {
        print("(integration suite skipped — set BSL_INTEGRATION=1 to enable)")
    }

    print("---")
    print("passed: \(Check.passed)")
    print("failed: \(Check.failed)")
    exit(Check.failed == 0 ? 0 : 1)
}

// Top-level driver: the offline checks and integration harness run on a
// detached Task. We use `dispatchMain()` to park the main thread in the
// libdispatch main queue so any MainActor-scheduled work can run. The task
// exits the process when done.
Task.detached {
    await run()
    // fall-through is unreachable — run() calls exit().
}
dispatchMain()
