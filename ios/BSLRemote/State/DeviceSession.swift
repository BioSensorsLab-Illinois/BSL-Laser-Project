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

    /// `wsUrl` that was resolved during the Connect flow. Persisted in
    /// `UserDefaults` under this key so a subsequent launch can retry the
    /// same station IP before falling back to the AP.
    static let lastWsUrlDefaultsKey = "BSLRemote.lastWsUrl"

    private let transport = WebSocketTransport()
    private var connectedURL: URL?
    private var userRequestedDisconnect: Bool = false
    private var reconnectAttempt: Int = 0
    private var reconnectTask: Task<Void, Never>? = nil

    /// Has the app ever successfully seen a `.connected` state for this URL?
    /// Views use this to decide "keep the dashboard visible during a reconnect
    /// attempt" vs "drop back to ConnectView because we never got in".
    private(set) var hasEverConnected: Bool = false

    init() {
        transport.attach(observer: self)
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
        self.hasEverConnected = false
        transport.disconnect(reason: "user requested")
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
                self.lastFirmwareError = resp.error ?? "Firmware rejected the command."
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
}

// SnapshotMerge.overlay now lives in BSLProtocol so both the app and the
// sanity-check harness can exercise it. See
// BSLProtocol/Sources/BSLProtocol/Models/SnapshotMerge.swift.
