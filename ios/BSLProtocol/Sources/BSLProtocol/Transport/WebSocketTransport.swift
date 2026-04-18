import Foundation

/// Observer protocol used by the UI layer. All callbacks arrive on the main
/// actor so SwiftUI `@Observable` writes are free of thread-hopping.
@MainActor
public protocol TransportObserver: AnyObject {
    func transportDidChange(state: WebSocketTransport.State)
    func transportDidReceive(frame: InboundFrame)
}

/// Newline-delimited JSON WebSocket transport. Mirrors
/// `host-console/src/lib/websocket-transport.ts:12-245` in shape: open,
/// probe the controller with `get_status` / `ping` a few times to elicit the
/// auto-pushed snapshot, split inbound frames on `\n`, and surface every
/// frame as an `InboundFrame` to the observer.
public final class WebSocketTransport: @unchecked Sendable {

    public enum State: Sendable, Equatable {
        case idle
        case connecting(URL)
        case connected(URL)
        case disconnected(reason: String)
        case failed(reason: String)
    }

    public enum SendError: LocalizedError {
        case notConnected
        case encodeFailed(String)
        case sessionClosed

        public var errorDescription: String? {
            switch self {
            case .notConnected: return "Transport is not connected."
            case .encodeFailed(let r): return "Command encode failed: \(r)"
            case .sessionClosed: return "Transport session closed."
            }
        }
    }

    private weak var observer: TransportObserver?
    private let queue = DispatchQueue(label: "cc.metadata.bsl.ws", qos: .userInitiated)
    private var task: URLSessionWebSocketTask?
    private var session: URLSession?
    private var active = false
    private var currentURL: URL?
    private var handshakeProbesScheduled: [DispatchWorkItem] = []
    private var protocolReady = false
    private var commandIdCounter: Int = 0
    private var pendingCommands: [Int: CheckedContinuation<ResponseEnvelope, Error>] = [:]
    private var pendingTimers: [Int: DispatchWorkItem] = [:]
    private let commandTimeoutSeconds: Double = 7.0

    public init() {}

    @MainActor
    public func attach(observer: TransportObserver) {
        self.observer = observer
    }

    public func connect(url: URL) {
        queue.async { [weak self] in
            guard let self else { return }
            self.disconnectLocked(reason: "opening new connection")
            let session = URLSession(configuration: .ephemeral)
            let task = session.webSocketTask(with: url)
            task.maximumMessageSize = 1_048_576 * 2 // 2 MiB — larger than any status_snapshot.
            self.session = session
            self.task = task
            self.currentURL = url
            self.active = true
            self.protocolReady = false

            self.notify(state: .connecting(url))
            task.resume()
            self.scheduleHandshakeProbes()
            self.receiveLoop()
        }
    }

    public func disconnect(reason: String = "user requested") {
        queue.async { [weak self] in
            self?.disconnectLocked(reason: reason)
        }
    }

    private func disconnectLocked(reason: String) {
        guard active || task != nil else { return }
        active = false
        for probe in handshakeProbesScheduled { probe.cancel() }
        handshakeProbesScheduled.removeAll()
        for (_, timer) in pendingTimers { timer.cancel() }
        pendingTimers.removeAll()
        for (_, cont) in pendingCommands {
            cont.resume(throwing: SendError.sessionClosed)
        }
        pendingCommands.removeAll()
        task?.cancel(with: .goingAway, reason: nil)
        task = nil
        session?.invalidateAndCancel()
        session = nil
        protocolReady = false
        notify(state: .disconnected(reason: reason))
    }

    // MARK: - Receive loop

    private func receiveLoop() {
        guard let task else { return }
        task.receive { [weak self] result in
            guard let self else { return }
            self.queue.async {
                switch result {
                case .success(let message):
                    switch message {
                    case .string(let text):
                        self.ingest(text: text)
                    case .data(let data):
                        if let text = String(data: data, encoding: .utf8) {
                            self.ingest(text: text)
                        }
                    @unknown default:
                        break
                    }
                    if self.active {
                        self.receiveLoop()
                    }
                case .failure(let error):
                    self.notify(state: .failed(reason: error.localizedDescription))
                    self.disconnectLocked(reason: error.localizedDescription)
                }
            }
        }
    }

    private func ingest(text: String) {
        if !protocolReady {
            protocolReady = true
            for probe in handshakeProbesScheduled { probe.cancel() }
            handshakeProbesScheduled.removeAll()
            if let url = currentURL {
                notify(state: .connected(url))
            }
        }
        for rawLine in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let line = String(rawLine).trimmingCharacters(in: .whitespacesAndNewlines)
            if line.isEmpty { continue }
            let frame = FrameParser.parse(line: line)
            switch frame {
            case .commandResponse(let resp):
                self.completeCommand(id: resp.id, result: .success(resp))
            default:
                break
            }
            notify(frame: frame)
        }
    }

    // MARK: - Handshake probes

    private func scheduleHandshakeProbes() {
        let delays: [(Double, String)] = [
            (0.120, "get_status"),
            (0.900, "ping"),
            (2.200, "get_status"),
        ]
        for (delay, cmd) in delays {
            let probe = DispatchWorkItem { [weak self] in
                guard let self else { return }
                guard self.active, !self.protocolReady else { return }
                Task { [weak self] in
                    _ = try? await self?.send(command: cmd, args: nil, expectResponse: false)
                }
            }
            handshakeProbesScheduled.append(probe)
            queue.asyncAfter(deadline: .now() + delay, execute: probe)
        }
    }

    // MARK: - Send

    /// Fire-and-forget send that does not wait for a response.
    public func fireAndForget(command: String, args: [String: CommandArg]? = nil) {
        queue.async { [weak self] in
            guard let self else { return }
            self.commandIdCounter += 1
            let envelope = CommandEnvelope(id: self.commandIdCounter, cmd: command, args: args)
            self.sendEnvelope(envelope, continuation: nil)
        }
    }

    /// Send a command and await the matching `resp` frame. Throws on
    /// transport error, malformed response, or the 7 s timeout.
    public func send(
        command: String,
        args: [String: CommandArg]?,
        expectResponse: Bool = true
    ) async throws -> ResponseEnvelope {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<ResponseEnvelope, Error>) in
            queue.async { [weak self] in
                guard let self else {
                    continuation.resume(throwing: SendError.sessionClosed)
                    return
                }
                self.commandIdCounter += 1
                let id = self.commandIdCounter
                let envelope = CommandEnvelope(id: id, cmd: command, args: args)
                if expectResponse {
                    self.pendingCommands[id] = continuation
                    let timer = DispatchWorkItem { [weak self] in
                        guard let self else { return }
                        self.queue.async {
                            if let cont = self.pendingCommands.removeValue(forKey: id) {
                                self.pendingTimers.removeValue(forKey: id)?.cancel()
                                cont.resume(throwing: SendError.sessionClosed)
                            }
                        }
                    }
                    self.pendingTimers[id] = timer
                    self.queue.asyncAfter(deadline: .now() + self.commandTimeoutSeconds, execute: timer)
                    self.sendEnvelope(envelope, continuation: nil)
                } else {
                    self.sendEnvelope(envelope, continuation: nil)
                    // Synthesize a success for fire-and-forget so the
                    // continuation does not dangle.
                    let fake = try! JSONDecoder().decode(
                        ResponseEnvelope.self,
                        from: Data("{\"id\":\(id),\"ok\":true}".utf8)
                    )
                    continuation.resume(returning: fake)
                }
            }
        }
    }

    private func sendEnvelope(_ envelope: CommandEnvelope, continuation: CheckedContinuation<ResponseEnvelope, Error>?) {
        guard active, let task else {
            continuation?.resume(throwing: SendError.notConnected)
            return
        }
        do {
            let data = try JSONEncoder().encode(envelope)
            guard let text = String(data: data, encoding: .utf8) else {
                continuation?.resume(throwing: SendError.encodeFailed("non-UTF-8 JSON"))
                return
            }
            task.send(.string(text + "\n")) { [weak self] error in
                if let error {
                    self?.queue.async {
                        if let cont = self?.pendingCommands.removeValue(forKey: envelope.id) {
                            self?.pendingTimers.removeValue(forKey: envelope.id)?.cancel()
                            cont.resume(throwing: SendError.encodeFailed(error.localizedDescription))
                        } else {
                            continuation?.resume(throwing: SendError.encodeFailed(error.localizedDescription))
                        }
                    }
                }
            }
        } catch {
            continuation?.resume(throwing: SendError.encodeFailed(String(describing: error)))
        }
    }

    private func completeCommand(id: Int, result: Result<ResponseEnvelope, Error>) {
        guard let cont = pendingCommands.removeValue(forKey: id) else { return }
        pendingTimers.removeValue(forKey: id)?.cancel()
        switch result {
        case .success(let resp): cont.resume(returning: resp)
        case .failure(let err): cont.resume(throwing: err)
        }
    }

    // MARK: - Observer marshaling

    private func notify(state: State) {
        Task { @MainActor [weak self] in
            self?.observer?.transportDidChange(state: state)
        }
    }

    private func notify(frame: InboundFrame) {
        Task { @MainActor [weak self] in
            self?.observer?.transportDidReceive(frame: frame)
        }
    }
}
