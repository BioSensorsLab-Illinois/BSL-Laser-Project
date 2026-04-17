#!/usr/bin/env python3
"""
BSL Host Console — portable static server.

Serves the prebuilt React app (../app/) at http://127.0.0.1:5173/ and opens
the default browser. Uses only the Python 3 standard library.

Usage:
    python3 serve.py            # default port 5173
    python3 serve.py 8080       # custom port
"""
import http.server
import socketserver
import sys
import threading
import webbrowser
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
APP_ROOT = (SCRIPT_DIR.parent / "app").resolve()
DEFAULT_PORT = 5173
BIND = "127.0.0.1"

# Extend MIME map so Vite's woff2 / mjs / wasm outputs are served with the
# correct Content-Type. Without this, some browsers refuse to load fonts
# or module scripts.
EXTRA_MIME = {
    ".js": "application/javascript",
    ".mjs": "application/javascript",
    ".css": "text/css",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
    ".ttf": "font/ttf",
    ".otf": "font/otf",
    ".svg": "image/svg+xml",
    ".json": "application/json",
    ".wasm": "application/wasm",
    ".map": "application/json",
}


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        **EXTRA_MIME,
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(APP_ROOT), **kwargs)

    def log_message(self, fmt, *args):
        # Silence per-request access log; uncomment the next line to debug.
        # sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))
        return


def open_browser_later(url: str) -> None:
    # Small delay so the socket is accepting before the tab points at it.
    threading.Timer(0.4, lambda: webbrowser.open(url)).start()


def main() -> int:
    if not APP_ROOT.is_dir():
        print(f"error: app directory not found: {APP_ROOT}", file=sys.stderr)
        return 1

    port = DEFAULT_PORT
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print(f"error: port must be an integer, got {sys.argv[1]!r}", file=sys.stderr)
            return 2

    socketserver.TCPServer.allow_reuse_address = True
    try:
        with socketserver.TCPServer((BIND, port), Handler) as httpd:
            url = f"http://{BIND}:{port}/"
            print(f"BSL Console serving {APP_ROOT}")
            print(f"  {url}")
            print("Press Ctrl+C to stop.")
            open_browser_later(url)
            try:
                httpd.serve_forever()
            except KeyboardInterrupt:
                print("\nShutting down.")
    except OSError as e:
        print(f"error: could not bind {BIND}:{port} — {e}", file=sys.stderr)
        print("  Another process may already be using that port.", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
