import Foundation
import Observation
import BSLProtocol

/// Top-level app state. Owns the transport, the merged `DeviceSnapshot`, and
/// command dispatch. Safety-authoritative verdicts (allowNir, blockedReason,
/// fault.latched) are always read from the firmware — `DeviceSession` NEVER
/// synthesizes an allow/deny.
@MainActor
@Observable
final class DeviceSession: TransportObserver {

    enum Connection: Equatable {
        case disconnected(reason: String)
        case connecting(url: String)
        case connected(url: String)
        case failed(reason: String)
    }

    /// Live merged snapshot. Safe to read from SwiftUI views.
    var snapshot: DeviceSnapshot = DeviceSnapshot()

    /// Monotonically-increasing timestamp of the last inbound frame. Views
    /// use this to render a "stale" chip when nothing has arrived in 2 s.
    var lastFrameAt: Date?

    var connection: Connection = .disconnected(reason: "not yet connected")

    /// The most recent firmware error string, displayed VERBATIM in a banner
    /// until the operator dismisses it. Per the plan and AGENT.md: never
    /// translate, never soften.
    var lastFirmwareError: String?

    /// Transient success banner — shown for a few seconds after an Apply
    /// succeeds so the operator sees positive confirmation instead of a
    /// silent sheet dismiss. Populated by `reportSuccess`.
    var lastSuccessMessage: String?

    /// `wsUrl` that was resolved during the Connect flow. Persisted in
    /// `UserDefaults` under this key so a subsequent launch can retry the
    /// same station IP before falling back to the AP.
    static let lastWsUrlDefaultsKey = "BSLRemote.lastWsUrl"

    /// UserDefaults keys for the last host-commanded setpoints. Firmware
    /// currently does NOT persist bench-runtime setpoints across a power
    /// cycle — only the safety policy is in NVS. The app remembers the
    /// last locally-commanded value and, on reconnect, replays it back to
    /// firmware so the operator does not have to re-arm the slider.
    private static let nirSetpointKey = "BSLRemote.persist.nirSetpointA"
    private static let ledDutyKey = "BSLRemote.persist.ledDutyPct"
    private static let ledEnabledKey = "BSLRemote.persist.ledEnabled"
    private static let wavelengthKey = "BSLRemote.persist.wavelengthNm"

    /// Last commanded values the app will offer to restore on reconnect.
    /// Set by the commit paths in `NirHeroCard`, `LedStripCard`, and
    /// `WavelengthEditorSheet`. Nil means "never set by this app" —
    /// nothing to restore.
    private(set) var persistedNirSetpointA: Double? = nil
    private(set) var persistedLedDutyPct: Int? = nil
    private(set) var persistedLedEnabled: Bool? = nil
    private(set) var persistedWavelengthNm: Double? = nil

    private let transport = WebSocketTransport()
    private var connectedURL: URL?
    private var userRequestedDisconnect: Bool = false
    private var reconnectAttempt: Int = 0
    private var reconnectTask: Task<Void, Never>? = nil
    private var successClearTask: Task<Void, Never>? = nil

    /// Background poll that refreshes the snapshot at 2 Hz (user directive
    /// 2026-04-19) — guarantees TEC temp / PD draw / laser current read
    /// fresh even when unsolicited `live_telemetry` pushes from firmware
    /// are slower than the operator expects. Only runs while connected.
    private var pollTask: Task<Void, Never>? = nil
    private static let pollIntervalNs: UInt64 = 500_000_000   // 2 Hz

    /// Has the app ever successfully seen a `.connected` state for this URL?
    /// Views use this to decide "keep the dashboard visible during a reconnect
    /// attempt" vs "drop back to ConnectView because we never got in".
    private(set) var hasEverConnected: Bool = false

    /// Guard flag — true between a fresh `connected` event and the moment we
    /// have replayed persisted setpoints. Prevents the restore path from
    /// firing repeatedly during reconnect storms.
    private var restoreInFlight: Bool = false

    init() {
        transport.attach(observer: self)
        loadPersistedSetpoints()
    }

    private func loadPersistedSetpoints() {
        let d = UserDefaults.standard
        if d.object(forKey: Self.nirSetpointKey) != nil {
            persistedNirSetpointA = d.double(forKey: Self.nirSetpointKey)
        }
        if d.object(forKey: Self.ledDutyKey) != nil {
            persistedLedDutyPct = d.integer(forKey: Self.ledDutyKey)
        }
        if d.object(forKey: Self.ledEnabledKey) != nil {
            persistedLedEnabled = d.bool(forKey: Self.ledEnabledKey)
        }
        if d.object(forKey: Self.wavelengthKey) != nil {
            persistedWavelengthNm = d.double(forKey: Self.wavelengthKey)
        }
    }

    func rememberNirSetpoint(_ currentA: Double) {
        persistedNirSetpointA = currentA
        UserDefaults.standard.set(currentA, forKey: Self.nirSetpointKey)
    }

    func rememberLed(dutyPct: Int, enabled: Bool) {
        persistedLedDutyPct = dutyPct
        persistedLedEnabled = enabled
        UserDefaults.standard.set(dutyPct, forKey: Self.ledDutyKey)
        UserDefaults.standard.set(enabled, forKey: Self.ledEnabledKey)
    }

    func rememberWavelength(_ nm: Double) {
        persistedWavelengthNm = nm
        UserDefaults.standard.set(nm, forKey: Self.wavelengthKey)
    }

    /// Post a short success confirmation for the operator. Auto-clears
    /// after 2.4 s so the banner does not pile up across multiple applies.
    func reportSuccess(_ message: String) {
        lastSuccessMessage = message
        successClearTask?.cancel()
        successClearTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 2_400_000_000)
            if !Task.isCancelled {
                await MainActor.run { self?.lastSuccessMessage = nil }
            }
        }
    }

    /// Open the given WebSocket URL. Safe to call repeatedly — previous
    /// session is torn down.
    func connect(url: URL) {
        self.connectedURL = url
        self.userRequestedDisconnect = false
        self.reconnectAttempt = 0
        UserDefaults.standard.set(url.absoluteString, forKey: Self.lastWsUrlDefaultsKey)
        transport.connect(url: url)
    }

    func disconnect() {
        self.userRequestedDisconnect = true
        reconnectTask?.cancel()
        reconnectTask = nil
        pollTask?.cancel()
        pollTask = nil
        self.hasEverConnected = false
        transport.disconnect(reason: "user requested")
    }

    /// Spin up the 2 Hz poll task if it isn't already running. Calls are
    /// cheap — one `status.get` roundtrip every 500 ms — but we gate the
    /// actual send on `lastFrameAt` age so that when firmware is already
    /// pushing fast enough we stay silent.
    private func startTelemetryPollIfNeeded() {
        guard pollTask == nil else { return }
        pollTask = Task { [weak self] in
            while true {
                try? await Task.sleep(nanoseconds: Self.pollIntervalNs)
                guard let self else { return }
                if Task.isCancelled { return }
                let connected = await MainActor.run { self.isConnected }
                guard connected else { continue }
                // If a fresh frame landed in the last 400 ms, skip the
                // poll — the firmware already pushed for us.
                let shouldPoll: Bool = await MainActor.run {
                    guard let last = self.lastFrameAt else { return true }
                    return Date().timeIntervalSince(last) >= 0.4
                }
                if shouldPoll {
                    _ = await self.sendCommand("status.get")
                }
            }
        }
    }

    /// Schedule a reconnect attempt with exponential backoff after a
    /// transient transport failure. Cancelled by an explicit `disconnect()`.
    private func scheduleReconnect() {
        guard !userRequestedDisconnect, let url = connectedURL else { return }
        reconnectTask?.cancel()
        let attempt = reconnectAttempt
        reconnectAttempt += 1
        let delayMs = min(8_000, 500 * (1 << min(attempt, 4)))   // 0.5, 1, 2, 4, 8 s ceiling
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(delayMs) * 1_000_000)
            guard let self, !Task.isCancelled, !self.userRequestedDisconnect else { return }
            self.transport.connect(url: url)
        }
    }

    /// Sends a mutating command and awaits the controller's `resp`. Surfaces
    /// firmware error strings verbatim via `lastFirmwareError`.
    ///
    /// Transport-level transient errors (`sessionClosed`, `notConnected`) are
    /// NOT surfaced to `lastFirmwareError` — those are reconnect-loop noise,
    /// not firmware rejections. They still fall through as `.failure` so
    /// call sites can decide whether to retry.
    func sendCommand(
        _ cmd: String,
        args: [String: CommandArg]? = nil
    ) async -> Result<ResponseEnvelope, Error> {
        guard isConnected else {
            return .failure(WebSocketTransport.SendError.notConnected)
        }
        do {
            let resp = try await transport.send(command: cmd, args: args)
            if !resp.ok {
                // Firmware "runtime gated" rejections are EXPECTED during a
                // reconnect / restore when deployment isn't ready yet. They
                // should not scream at the operator as a red banner. The
                // call site still gets a .success(resp{ ok:false }) so it
                // can react if it wants to.
                if !Self.isExpectedGatingRejection(for: cmd, message: resp.error) {
                    self.lastFirmwareError = resp.error ?? "Firmware rejected the command."
                }
            }
            return .success(resp)
        } catch let err as WebSocketTransport.SendError {
            // Transport-close / not-connected errors are transient during a
            // Wi-Fi glitch or app backgrounding. Suppress the banner; the
            // connection UI already surfaces the disconnected state.
            switch err {
            case .sessionClosed, .notConnected:
                break
            case .encodeFailed(let r):
                self.lastFirmwareError = "Transport encode failed: \(r)"
            }
            return .failure(err)
        } catch {
            self.lastFirmwareError = error.localizedDescription
            return .failure(error)
        }
    }

    /// Is telemetry stale (no frame for ≥ 2 s)? Views collapse actual
    /// readouts to "—" when this returns true.
    var isStale: Bool {
        guard let last = lastFrameAt else { return true }
        return Date().timeIntervalSince(last) > 2.0
    }

    var isConnected: Bool {
        if case .connected = connection { return true }
        return false
    }

    // MARK: - TransportObserver

    nonisolated func transportDidChange(state: WebSocketTransport.State) {
        Task { @MainActor [weak self] in
            guard let self else { return }
            switch state {
            case .idle:
                self.connection = .disconnected(reason: "idle")
            case .connecting(let url):
                self.connection = .connecting(url: url.absoluteString)
            case .connected(let url):
                self.connection = .connected(url: url.absoluteString)
                self.hasEverConnected = true
                self.reconnectAttempt = 0
                self.beginPostConnectSync()
                self.startTelemetryPollIfNeeded()
            case .disconnected(let reason):
                self.connection = .disconnected(reason: reason)
                if self.hasEverConnected, !self.userRequestedDisconnect {
                    self.scheduleReconnect()
                }
            case .failed(let reason):
                self.connection = .failed(reason: reason)
                if self.hasEverConnected, !self.userRequestedDisconnect {
                    self.scheduleReconnect()
                }
            }
        }
    }

    nonisolated func transportDidReceive(frame: InboundFrame) {
        Task { @MainActor [weak self] in
            guard let self else { return }
            self.ingest(frame: frame)
        }
    }

    private func ingest(frame: InboundFrame) {
        switch frame {
        case .statusSnapshot(let snap):
            self.snapshot = snap
            self.lastFrameAt = Date()
        case .liveTelemetry(let incoming, let presentKeys, let rawPayload):
            self.snapshot = DeviceSnapshot.overlay(
                base: self.snapshot,
                patch: incoming,
                presentKeys: presentKeys,
                rawPayload: rawPayload
            )
            self.lastFrameAt = Date()
        case .commandResponse:
            // Handled by the transport's in-flight continuation map.
            self.lastFrameAt = Date()
        case .other:
            self.lastFrameAt = Date()
        case .decodeError(let reason):
            // Surface once — this is rare and points at a firmware/host
            // contract drift worth noticing.
            self.lastFirmwareError = "Decode: \(reason)"
        }
    }

    /// After every fresh connection we:
    ///   1. Send `status.get` explicitly so the snapshot is current within a
    ///      few hundred ms rather than whenever the next unsolicited
    ///      `live_telemetry` tick arrives. This plugs the "LED is physically
    ///      on but the app shows 0 %" gap the operator hit on startup.
    ///   2. Wait briefly for the snapshot, then if the app remembers a
    ///      non-zero setpoint and the firmware's value is 0 (i.e. firmware
    ///      booted fresh with no bench state) re-apply it.
    private func beginPostConnectSync() {
        guard !restoreInFlight else { return }
        restoreInFlight = true
        Task { [weak self] in
            guard let self else { return }
            _ = await self.sendCommand("status.get")
            // Let the snapshot overlay land before sampling it.
            try? await Task.sleep(nanoseconds: 350_000_000)
            await self.restorePersistedSetpointsIfUseful()
            self.restoreInFlight = false
        }
    }

    private func restorePersistedSetpointsIfUseful() async {
        // The whole class is `@MainActor`; properties read here are
        // already main-isolated because this async method inherits the
        // enclosing actor. Snapshotting into locals keeps the logic
        // readable and avoids any mid-await state drift.
        let snap = self.snapshot
        let nirMemo = self.persistedNirSetpointA
        let ledDuty = self.persistedLedDutyPct
        let ledEnabled = self.persistedLedEnabled
        let waveMemo = self.persistedWavelengthNm

        // Wavelength (`operate.set_target`) is the only restore that
        // firmware will accept BEFORE the deployment checklist has run —
        // it just stages the target, it does not run the beam. Everything
        // else (`operate.set_output`, `operate.set_led`) is runtime and
        // the firmware correctly rejects it pre-deployment. Skipping those
        // here prevents a noisy "complete the deployment checklist" banner
        // on every reconnect.
        if let nm = waveMemo,
           nm > 0,
           abs(snap.tec.targetLambdaNm - nm) > 0.05 {
            _ = await self.sendCommand(
                "operate.set_target",
                args: [
                    "mode": .string("lambda"),
                    "lambda_nm": .double(nm),
                ]
            )
        }

        let runtimeReady = snap.deployment.active && snap.deployment.ready && snap.deployment.readyIdle
        guard runtimeReady else { return }

        // NIR current — replay only when the firmware is ready-idle. We
        // never re-enable the NIR line remotely; that is a physical-
        // trigger gate.
        if let remembered = nirMemo,
           remembered > 0,
           snap.bench.requestedCurrentA <= 0.001,
           !snap.laser.nirEnabled,
           snap.bench.hostControlReadiness.nirBlockedReason == .none {
            _ = await self.sendCommand(
                "operate.set_output",
                args: [
                    "enable": .bool(false),
                    "current_a": .double(remembered),
                ]
            )
        }

        // Visible LED — replay only when the firmware is ready-idle and
        // the LED path is not blocked.
        if let dutyPct = ledDuty,
           let enabled = ledEnabled,
           snap.bench.requestedLedDutyCyclePct == 0,
           !snap.bench.requestedLedEnabled,
           enabled,
           dutyPct > 0,
           snap.bench.hostControlReadiness.ledBlockedReason == .none {
            _ = await self.sendCommand(
                "operate.set_led",
                args: [
                    "enable": .bool(true),
                    "duty_cycle_pct": .int(dutyPct),
                ]
            )
        }
    }

    /// Returns true when a non-ok firmware response is an EXPECTED gating
    /// rejection that should NOT raise a red banner. Specifically: runtime
    /// commands (`operate.set_output`, `operate.set_led`) are known to be
    /// rejected with "complete the deployment checklist" while deployment
    /// is not yet ready. Surfacing that as an error every reconnect /
    /// restore is user-hostile. The response still comes back to the call
    /// site so it can react if it cares.
    ///
    /// Matches the firmware's rejection string prefix rather than an error
    /// code because `comms.c` returns a plain `err` string — see
    /// `components/laser_controller/src/laser_controller_comms.c::reject_runtime_control`.
    private static func isExpectedGatingRejection(for cmd: String, message: String?) -> Bool {
        guard let message else { return false }
        let runtimeCommands: Set<String> = [
            "operate.set_output",
            "operate.set_led",
        ]
        guard runtimeCommands.contains(cmd) else { return false }
        let lower = message.lowercased()
        return lower.contains("complete the deployment checklist")
            || lower.contains("deployment checklist")
            || lower.contains("before using runtime control")
            || lower.contains("deployment not active")
            || lower.contains("deployment not ready")
    }
}

// SnapshotMerge.overlay now lives in BSLProtocol so both the app and the
// sanity-check harness can exercise it. See
// BSLProtocol/Sources/BSLProtocol/Models/SnapshotMerge.swift.
