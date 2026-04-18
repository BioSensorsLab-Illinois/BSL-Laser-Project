# BSLRemote — iOS companion for the BSL laser controller

A compact, read-write SwiftUI dashboard that talks to the laser controller over
Wi-Fi. Native iOS 17+, no third-party runtime dependencies. See
`.agent/runs/powered-ready-console/` for the initiative context, and
`docs/protocol-spec.md` for the wire protocol this app binds to.

The firmware is the safety authority. This app **mirrors** firmware-computed
verdicts (`bench.hostControlReadiness.*BlockedReason`, `safety.allow*`,
`fault.latched*`) and **never** derives an allow/deny on the client.

## Layout

    ios/
    ├── BSLProtocol/          # SPM library: Codable models + WS transport
    │   ├── Package.swift
    │   ├── Sources/BSLProtocol/
    │   └── Tests/BSLProtocolTests/
    ├── BSLRemote/            # SwiftUI app target source
    │   ├── BSLRemoteApp.swift
    │   ├── Resources/Info.plist
    │   ├── Resources/BSLRemote.entitlements
    │   ├── State/
    │   └── Views/
    ├── project.yml           # XcodeGen project spec
    └── tools/
        └── mock-ws-server.py # Headless controller simulator for tests

## Build

Two options.

### 1. Command-line sanity checks (no Xcode required)

    cd ios/BSLProtocol
    swift run bsl-protocol-check

Builds `BSLProtocol` and the `bsl-protocol-check` executable, then runs a
plain-Swift sanity suite: canonical `status_snapshot` fixture decode,
`live_telemetry` partial-patch decode, command-envelope encode shape,
unknown-enum-token tolerance, malformed-line safety, and
`NirBlockedReason` exhaustiveness. Exits non-zero on any failure.

Runs against the macOS Command Line Tools SDK — no full Xcode install
required.

### 2. Full app (Xcode required)

Prerequisite: Xcode 15.3+ (iOS 17 SDK, Swift 5.9).

XcodeGen generates the Xcode project deterministically from `project.yml`:

    brew install xcodegen     # one-time
    cd ios
    xcodegen generate
    open BSLRemote.xcodeproj

Select an iPhone 15 simulator (or an iOS 17+ device) and press Run.

If you prefer not to use XcodeGen, import the sources into a new Xcode iOS App
target manually (deployment target iOS 17.0, SwiftUI interface) and add
`BSLProtocol` as a local Swift Package dependency.

## Connecting to the controller

On first launch the `ConnectView`:

1. Reads the current Wi-Fi SSID (entitlement:
   `com.apple.developer.networking.HotspotConfiguration`).
2. If not already on `BSL-HTLS-Bench`, offers a one-tap join via
   `NEHotspotConfiguration` with the shipped password `bslbench2026`.
3. Probes `GET http://192.168.4.1/meta` (and any stored station IP) to resolve
   the WebSocket URL.
4. Opens `ws://<ip>/ws` and awaits the controller's auto-pushed
   `status_snapshot` (~20 ms per firmware).

## Settings gate

The PIN is compile-time shared — stored as a SHA-256 hash + per-build salt in
`BSLRemote/State/AuthGate.swift`. The plaintext PIN is never bundled in the
IPA, but a copy of the binary can still be brute-forced against a short PIN.
**Known trade-off, per the plan.** Replace the hash + salt at build time to
rotate the PIN.

Unlock lasts for the foreground session and auto-locks after 60 s in the
background.

## Testing against the mock server

Without a controller, start the Python mock in one terminal:

    python3 ios/tools/mock-ws-server.py

It listens on `ws://localhost:8080/ws` with a `GET /meta` handler on the same
port. Point the app at `ws://localhost:8080/ws` via the Connect screen's
manual-URL field. The mock emits a canned `status_snapshot` plus a 1 Hz
`live_telemetry` with varying `tec.tempC` and `laser.measuredCurrentA`.

## What this app does NOT do

- Flash firmware (USB CDC only).
- Run the deployment checklist (lives on desktop console + physical button
  board).
- Integrate-workspace actions: PD refresh, STUSB NVM burn, GPIO inspector,
  I2C/SPI scan, bring-up profile editing.
- Claim powered-ready. Per `.agent/AGENT.md`, only Powered Phase 2 on-bench
  evidence establishes readiness; this app does not substitute.
