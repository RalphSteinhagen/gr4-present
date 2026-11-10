#include "PresenterBundle.hpp"

#include <gr4-present/DeckFiles.hpp>
#include <gr4-present/Tar.hpp>

#include <algorithm>
#include <deque>
#include <format>
#include <ranges>
#include <span>
#include <utility>

namespace gr::present {

namespace {

constexpr std::string_view kSiteMeta = R"(<meta name="gr4-site" content="" />)";

// POSIX sh: python3 serving with the headers the threaded viewer needs (its service worker then adds nothing and keeps
// no copy, so a re-unpacked bundle is what runs), else ruby or php without them (the worker adds them, one reload).
// On a Mac without the developer tools python3 is a stub that opens their installer, so ruby comes first there.
constexpr std::string_view kServeSh = R"gr4(#!/bin/sh
# Serves this folder on this computer and opens the viewer in the browser; a page opened as a file cannot start it.
# Leave this running while presenting; Ctrl+C stops it.
cd "$(dirname "$0")" || exit 1
open_browser() { (sleep 1; xdg-open "$1" >/dev/null 2>&1 || open "$1" >/dev/null 2>&1) & }
if [ "$(uname)" = Darwin ] && ! xcode-select -p >/dev/null 2>&1 && command -v ruby >/dev/null 2>&1; then
  echo "gr4-present at http://localhost:8000/  (Ctrl+C stops it)"
  open_browser http://localhost:8000/
  exec ruby -run -e httpd . -p 8000 -b 127.0.0.1
fi
if command -v python3 >/dev/null 2>&1; then
  exec python3 -u - <<'PY'
import http.server, sys, threading, webbrowser

class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript", ".webmanifest": "application/manifest+json", ".yml": "text/yaml", ".grc": "text/yaml", ".md": "text/markdown", ".svg": "image/svg+xml"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, *arguments):
        pass

for port in range(8000, 8010):
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        break
    except OSError:
        continue
else:
    sys.exit("ports 8000 to 8009 are all taken")
address = f"http://localhost:{port}/"
print(f"gr4-present at {address}  (Ctrl+C stops it)")
threading.Timer(0.5, webbrowser.open, [address]).start()
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
PY
fi
if command -v ruby >/dev/null 2>&1; then
  echo "gr4-present at http://localhost:8000/  (Ctrl+C stops it)"
  open_browser http://localhost:8000/
  exec ruby -run -e httpd . -p 8000 -b 127.0.0.1
fi
if command -v php >/dev/null 2>&1; then
  echo "gr4-present at http://localhost:8000/  (Ctrl+C stops it)"
  open_browser http://localhost:8000/
  exec php -S 127.0.0.1:8000
fi
echo "none of python3, ruby or php is installed: serve this folder with any static web server and open its address" >&2
exit 1
)gr4";

// Windows: a double-click on the .cmd runs the PowerShell beside it, whatever the execution policy says about scripts
constexpr std::string_view kServeCmd = R"gr4(@echo off
rem Serves this folder on this computer and opens the viewer in the browser; a page opened as a file cannot start it.
rem Leave this window open while presenting; closing it stops the server.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0serve.ps1"
if errorlevel 1 pause
)gr4";

// PowerShell 5.1 (every Windows since 7): HttpListener on localhost needs no administrator, serving with the headers
constexpr std::string_view kServePs1 = R"gr4(# Serves this folder on this computer and opens the viewer in the browser; a page opened as a file cannot start it.
# Leave this window open while presenting; closing it stops the server.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$root = (Get-Location).Path
$types = @{
  '.html' = 'text/html; charset=utf-8'; '.js' = 'text/javascript'; '.mjs' = 'text/javascript'; '.wasm' = 'application/wasm';
  '.json' = 'application/json'; '.webmanifest' = 'application/manifest+json'; '.yml' = 'text/yaml'; '.yaml' = 'text/yaml';
  '.grc' = 'text/yaml'; '.md' = 'text/markdown; charset=utf-8'; '.txt' = 'text/plain; charset=utf-8'; '.css' = 'text/css';
  '.svg' = 'image/svg+xml'; '.png' = 'image/png'; '.jpg' = 'image/jpeg'; '.jpeg' = 'image/jpeg'; '.gif' = 'image/gif';
  '.webp' = 'image/webp'; '.webm' = 'video/webm'; '.mp4' = 'video/mp4'; '.ogv' = 'video/ogg'; '.oga' = 'audio/ogg';
  '.ttf' = 'font/ttf'; '.otf' = 'font/otf'
}
$listener = New-Object System.Net.HttpListener
$port = 8000
while ($true) {
  $listener.Prefixes.Clear()
  $listener.Prefixes.Add("http://localhost:$port/")
  try { $listener.Start(); break } catch { if ($port -ge 8009) { throw }; $port++ }
}
$address = "http://localhost:$port/"
Write-Host "gr4-present at $address  (close this window to stop it)"
Start-Process $address
while ($listener.IsListening) {
  $context = $listener.GetContext()
  $response = $context.Response
  try {
    $path = [System.Uri]::UnescapeDataString($context.Request.Url.AbsolutePath)
    if ($path.EndsWith('/')) { $path += 'index.html' }
    $file = [System.IO.Path]::GetFullPath((Join-Path $root ($path.TrimStart('/') -replace '/', '\')))
    if ($file.StartsWith($root.TrimEnd('\') + '\') -and (Test-Path -LiteralPath $file -PathType Leaf)) {
      $bytes = [System.IO.File]::ReadAllBytes($file)
      $extension = [System.IO.Path]::GetExtension($file).ToLower()
      $response.ContentType = if ($types.ContainsKey($extension)) { $types[$extension] } else { 'application/octet-stream' }
      $response.Headers.Add('Cross-Origin-Opener-Policy', 'same-origin')
      $response.Headers.Add('Cross-Origin-Embedder-Policy', 'require-corp')
      $response.Headers.Add('Cross-Origin-Resource-Policy', 'cross-origin')
      $response.Headers.Add('Cache-Control', 'no-cache')
      $response.ContentLength64 = $bytes.Length
      $response.OutputStream.Write($bytes, 0, $bytes.Length)
    } else {
      $response.StatusCode = 404
    }
  } catch {
    # the browser closed the connection halfway: the next request is served as usual
  }
  $response.Close()
}
)gr4";

// Without a server: a Chromium (Chrome, Edge, Chromium, Brave) started with the two switches a page opened as a file
// needs to read the files beside it and to start threads. Both weaken what the browser protects, so it runs with a
// profile of its own, deleted when its window closes. The viewer reads a `file:` address from its in-memory file
// system, so the page copies the deck there first: a `.tar.gz` as it is, a folder by the `<deck>.files` list the
// script writes (shell.html)
constexpr std::string_view kLaunchChromiumSh = R"gr4(#!/bin/sh
# Opens the viewer from this folder without a server, in Chrome, Edge, Chromium or Brave (or the browser
# GR4_PRESENT_BROWSER names) started with two switches: --allow-file-access-from-files lets a page opened as a file
# read the files beside it, --enable-features=SharedArrayBuffer lets it start threads. Both weaken what the browser
# protects, so it runs with a fresh profile of its own, deleted when its window closes: the browser used day to day,
# its logins and its tabs never see the switches. serve.sh needs neither; this is for a computer without python3.
# Usage: sh launch-chromium.sh [deck]   a folder or .tar.gz beside index.html, default/ if none is named
cd "$(dirname "$0")" || exit 1
deck=${1:-default}
deck=${deck%/}
if [ ! -e "$deck" ]; then
  echo "there is no $deck beside index.html" >&2
  exit 1
fi
# the page cannot list a folder: it reads this list of the deck's files
if [ -d "$deck" ]; then
  (cd "$deck" && find . -type f ! -name '.*' | sed 's|^\./||') >"$deck.files" || exit 1
fi
browser=${GR4_PRESENT_BROWSER:-}
if [ -z "$browser" ]; then
  for candidate in google-chrome google-chrome-stable chromium chromium-browser microsoft-edge microsoft-edge-stable brave-browser \
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" "$HOME/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" "/Applications/Chromium.app/Contents/MacOS/Chromium" \
    "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      browser=$candidate
      break
    fi
  done
fi
if [ -z "$browser" ]; then
  echo "none of Chrome, Edge, Chromium or Brave was found: run sh serve.sh, or name the browser in GR4_PRESENT_BROWSER" >&2
  exit 1
fi
encoded() { printf '%s' "$1" | sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/#/%23/g' -e 's/?/%3F/g' -e 's/&/%26/g'; }
url="file://$(encoded "$(pwd)")/index.html"
if [ "$deck" != default ]; then
  url="$url?presentation=$(encoded "$deck")"
fi
profile=$(mktemp -d "${TMPDIR:-/tmp}/gr4-present-browser.XXXXXX") || exit 1
trap 'rm -rf "$profile"' EXIT
trap 'exit 130' INT TERM
echo "gr4-present in $browser, with a profile of its own (closing its window ends it)"
"$browser" --user-data-dir="$profile" --no-first-run --no-default-browser-check --allow-file-access-from-files \
  --enable-features=SharedArrayBuffer --app="$url"
)gr4";

// Windows: Edge is always there; `start /wait` holds the window until the browser closes, then the profile goes
constexpr std::string_view kLaunchChromiumCmd = R"gr4(@echo off
rem Opens the viewer from this folder without a server, in Chrome or Edge (or the browser GR4_PRESENT_BROWSER names)
rem started with two switches: --allow-file-access-from-files lets a page opened as a file read the files beside it,
rem --enable-features=SharedArrayBuffer lets it start threads. Both weaken what the browser protects, so it runs with
rem a fresh profile of its own, deleted when its window closes: the browser used day to day, its logins and its tabs
rem never see the switches. serve.cmd needs neither and is the first choice.
rem Usage: launch-chromium.cmd [deck]   a folder or .tar.gz beside index.html, default if none is named
setlocal
cd /d "%~dp0"
set "deck=%~1"
if "%deck%"=="" set "deck=default"
if not exist "%deck%" (
  echo there is no %deck% beside index.html
  pause
  exit /b 1
)
rem the page cannot list a folder: it reads this list of the deck's files
if exist "%deck%\" dir /s /b /a-d "%deck%" > "%deck%.files"
set "browser=%GR4_PRESENT_BROWSER%"
if not defined browser for %%b in ("%ProgramFiles%\Google\Chrome\Application\chrome.exe" "%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe" "%LocalAppData%\Google\Chrome\Application\chrome.exe" "%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe" "%ProgramFiles%\Microsoft\Edge\Application\msedge.exe") do if not defined browser if exist %%b set "browser=%%~b"
if not defined browser (
  echo neither Chrome nor Edge was found: double-click serve.cmd instead
  pause
  exit /b 1
)
set "url=file:///%~dp0index.html"
set "url=%url:\=/%"
if /i not "%deck%"=="default" set "url=%url%?presentation=%deck%"
set "profile=%TEMP%\gr4-present-browser-%RANDOM%%RANDOM%"
echo gr4-present in %browser%, with a profile of its own (closing its window ends it)
start "" /wait "%browser%" --user-data-dir="%profile%" --no-first-run --no-default-browser-check --allow-file-access-from-files --enable-features=SharedArrayBuffer --app="%url%"
rem the browser's helpers may hold a file for a moment after its window closes
rmdir /s /q "%profile%" 2>nul
if exist "%profile%" timeout /t 2 /nobreak >nul
if exist "%profile%" rmdir /s /q "%profile%" 2>nul
)gr4";

constexpr std::string_view kReadme = R"gr4(gr4-present: the viewer with the deck "{0}"

This folder is the presentation site as served at {1}, to present without a network.

START
  Windows:        double-click serve.cmd
  macOS, Linux:   open a terminal in this folder and run:  sh serve.sh
The script serves this folder on this computer and opens http://localhost:8000/ in the browser (Windows and python3
take the next free port if 8000 is taken; the script prints the address). Leave it running while presenting.
Double-clicking index.html does not work: a browser lets a page start the viewer's threads only when the page is
served, not when it is opened as a file.
Without a server, with Chrome, Edge, Chromium or Brave only:
  Windows:        double-click launch-chromium.cmd
  macOS, Linux:   sh launch-chromium.sh
It starts the browser with two switches that let this page read its folder and start threads, in a profile of its own
that is deleted when the window closes; the head of the script says what the switches do. A Chrome update may drop
the second switch: serve.sh and serve.cmd rely on web standards only.

WHAT IS HERE
  index.html, index.js, index.wasm                      the viewer
  gr4-serviceworker.js, manifest.webmanifest, icon.png  its page: the headers on a plain host, an offline copy, installable
  default/                                              the deck; the viewer opens it without any address parameter
  serve.cmd, serve.ps1                                  the small server for Windows (PowerShell, built in)
  serve.sh                                              the same for macOS and Linux (python3, else ruby or php)
  launch-chromium.cmd, launch-chromium.sh              no server: the browser with two switches, a throwaway profile
  default.files                                         written by launch-chromium: the deck's files, for the page

ANOTHER DECK
  Put its folder or .tar.gz beside index.html and open http://localhost:8000/?presentation=<name>, or drop it onto the
  page; Load (Ctrl+O) in the side menu opens a folder.
  Without a server: sh launch-chromium.sh <name>, or launch-chromium.cmd <name>.

NOTES
  Windows 10 cannot open a .tar.gz by double-click (Windows 11 can): in a command prompt in the Downloads folder,
  tar -xzf <this archive>, or use 7-Zip.
  Windows may ask once whether to run a downloaded script (SmartScreen: "More info", then "Run anyway").
  macOS runs serve.sh from the terminal without asking.
  The site itself works without a network once it has been visited: {1}
)gr4";

[[nodiscard]] std::string_view textOf(std::span<const std::uint8_t> bytes) { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }

/// Windows reads a script line by line and wants each ended by CR LF
[[nodiscard]] std::string withCrLf(std::string_view text) {
    std::string lines;
    for (const char c : text) {
        if (c == '\n') {
            lines += '\r';
        }
        lines += c;
    }
    return lines;
}

/// the page with its `gr4-site` meta naming `site`; a page without the meta as it is
[[nodiscard]] std::string pageNamingSite(std::string_view page, std::string_view site) {
    std::string named{page};
    if (const auto at = named.find(kSiteMeta); at != std::string::npos) {
        named.replace(at, kSiteMeta.size(), std::format(R"(<meta name="gr4-site" content="{}" />)", site));
    }
    return named;
}

} // namespace

std::expected<std::vector<std::byte>, std::string> packPresenterBundle(std::string_view folder, const PresenterBundle& bundle) {
    std::deque<std::string>                 written; // the texts made here; the files are read where the caller holds them
    std::vector<gr::compression::TarSource> sources;
    const auto                              add  = [&](std::string_view path, std::span<const std::byte> bytes, std::uint32_t mode = 0644U) { sources.push_back({.path = std::format("{}/{}", folder, path), .bytes = bytes, .mode = mode}); };
    const auto                              text = [&written](std::string made) { return std::as_bytes(std::span{written.emplace_back(std::move(made))}); };
    for (const PackageFile& file : bundle.siteFiles) {
        add(file.path, file.path == "index.html" ? text(pageNamingSite(textOf(file.bytes), bundle.site)) : std::as_bytes(file.bytes));
    }
    for (const PackageFile& file : bundle.deckFiles) {
        add(std::format("default/{}", file.path), std::as_bytes(file.bytes));
    }
    add("serve.sh", std::as_bytes(std::span{kServeSh}), 0755U);
    add("serve.cmd", text(withCrLf(kServeCmd)));
    add("serve.ps1", text(withCrLf(kServePs1)));
    add("launch-chromium.sh", std::as_bytes(std::span{kLaunchChromiumSh}), 0755U);
    add("launch-chromium.cmd", text(withCrLf(kLaunchChromiumCmd)));
    add("README.txt", text(std::format(kReadme, bundle.title.empty() ? std::string_view{"untitled"} : bundle.title, bundle.site)));
    return packTarGz(sources);
}

} // namespace gr::present
