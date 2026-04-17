# BSL Host Console — Portable Distribution

Prebuilt operator console for the BSL 785 nm laser controller. Runs locally in
a web browser and connects to the controller over **WiFi WebSocket**.

> Safety note: this UI reflects firmware state. Firmware remains the safety
> authority. Do not install this on an untrusted machine that shares a network
> with the controller.

---

## Quick Start

Unzip this folder to any location you have write access to, then double-click
the launcher for your OS:

| OS      | Launcher              | Requirement                             |
|---------|-----------------------|-----------------------------------------|
| Windows | `run-windows.bat`     | Windows 10+ (PowerShell 5 is built in)  |
| macOS   | `run-macos.command`   | Xcode Command Line Tools (for `python3`)|
| Linux   | `./run-linux.sh`      | `python3` on PATH                       |

The launcher starts a local static server at <http://127.0.0.1:5173> and opens
your default browser. Stop it with **Ctrl + C** in the terminal it opens, or
just close the terminal window.

**Use a Chromium-based browser** (Chrome, Edge, Brave). The console has been
exercised on Chrome. Safari and Firefox may render but some APIs behave
differently.

---

## Connecting to the Controller

1. Power the controller and let it come up in WiFi mode (default SSID and IP
   are configured in firmware; ask the device owner if unsure).
2. Join the controller's WiFi network from the laptop running this console.
3. In the console UI, open the connection panel and enter the WebSocket URL,
   e.g. `ws://192.168.4.1/ws` (default AP address).
4. The console should transition from OFF → CONNECTED and start showing
   telemetry.

If telemetry stays `INVALID` or `OFF`, that is the controller's actual state —
not a UI bug. Verify from the controller directly.

---

## File Layout

```
dist-portable/
├── app/                 # prebuilt web app (do not edit)
│   ├── index.html
│   └── assets/…
├── server/
│   ├── serve.py         # Python 3 static server (macOS, Linux)
│   └── serve.ps1        # PowerShell static server (Windows)
├── run-windows.bat
├── run-macos.command
├── run-linux.sh
└── README.md
```

Nothing in `app/` depends on the OS — the same build runs everywhere. Only the
launcher and server differ.

---

## Changing the Port

If 5173 is in use, pass a different port to the launcher:

```bash
# macOS / Linux
./run-linux.sh 8080

# Windows
run-windows.bat -Port 8080
```

---

## Troubleshooting

**Windows: "script is not digitally signed"**
The launcher invokes PowerShell with `-ExecutionPolicy Bypass` which should
avoid this. If it still appears, open PowerShell as the current user and run
once:

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

**macOS: "command not found: python3"**
Run once in Terminal:

```bash
xcode-select --install
```

Accept the Apple dialog; no Apple ID required. Then try the launcher again.

**macOS: "cannot be opened because it is from an unidentified developer"**
Right-click the launcher → Open → Open. You only have to do this once per
machine.

**"Address already in use"**
Another process is holding 5173. Close it, or start on a different port (see
above).

**Console UI loads but won't connect to the controller**
The distribution only ships with WebSocket transport support. USB serial is
disabled in this build. Verify:

- The laptop is on the controller's WiFi network.
- The WebSocket URL is reachable (try `ws://<ip>/ws`, not `http://`).
- No firewall is blocking the connection.

---

## What This Distribution Does Not Include

- **USB flashing** (esptool-js). The code ships in the bundle but requires
  Chrome/Edge + a secure context; the local server provides that, so flashing
  may still work on Chromium. Not officially supported in this distribution.
- **Live-controller validation harness** (`host-console/scripts/`). That's a
  Python CLI used on the bench, not shipped here.
- **Source code**. This is the compiled output only.

---

## Version

Built from `host-console/` in the `BSL-Laser` repository. For the source and
full docs see the repository's `.agent/AGENT.md`.
