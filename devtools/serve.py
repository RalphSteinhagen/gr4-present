#!/usr/bin/env python3
"""Serve the Emscripten build of the viewer and open it in a browser.

The threaded GR4/OpenDigitizer WASM runtime needs SharedArrayBuffer, which browsers only expose to
cross-origin isolated pages.  That requires COOP and COEP response headers, which
``python3 -m http.server`` does not send, so the presentation would silently fall back to a
single-threaded runtime or fail outright.  localhost is treated as a secure context, so plain HTTP
is enough here.
"""

import argparse
import functools
import http.server
import pathlib
import socketserver
import sys
import threading
import webbrowser

DEFAULT_PORT = 8000
DEFAULT_PAGE = "index.html"


class CrossOriginIsolatedHandler(http.server.SimpleHTTPRequestHandler):
    """Static file handler that makes the page cross-origin isolated."""

    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))


class ReusableServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--directory", "-d", default="cmake-build-WASM-Release/viewer-web", help="directory holding index.html (default: %(default)s)")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT, help="TCP port to listen on (default: %(default)s)")
    parser.add_argument("--page", default=DEFAULT_PAGE, help="page to open (default: %(default)s)")
    parser.add_argument("--no-browser", action="store_true", help="serve only, do not open a browser")
    return parser.parse_args()


def main():
    arguments = parse_arguments()

    directory = pathlib.Path(arguments.directory).resolve()
    if not directory.is_dir():
        sys.exit(f"no such directory: {directory}\nbuild the Emscripten target first, e.g.\n" f"  cmake --preset WASM-Release && cmake --build cmake-build-WASM-Release")
    if not (directory / arguments.page).is_file():
        sys.exit(f"{directory / arguments.page} is missing — has the Emscripten target been built?")

    handler = functools.partial(CrossOriginIsolatedHandler, directory=str(directory))
    try:
        server = ReusableServer(("127.0.0.1", arguments.port), handler)
    except OSError as error:
        sys.exit(f"cannot listen on port {arguments.port}: {error}")

    url = f"http://localhost:{arguments.port}/{arguments.page}"
    print(f"serving {directory}")
    print(f"  {url}  (COOP/COEP set, so SharedArrayBuffer is available)")
    print("  press Ctrl-C to stop")

    if not arguments.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
