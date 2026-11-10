# gr4-present

Presentation platform for and in C++:
Markdown and SVG slides that host live [GNU Radio 4.0](https://github.com/fair-acc/gnuradio4) flow graphs and [OpenDigitizer](https://github.com/fair-acc/opendigitizer) charts, natively and in the browser via WebAssembly.

**Live demo:** <https://ralphsteinhagen.github.io/gr4-present>

> **Status:** proof of concept, work in progress.

## Why

Talks about signal processing show static screenshots of software that is live.
Here the slides run the real thing:
a GNU Radio 4.0 flow graph computes what the slide shows, and OpenDigitizer draws it, with the same C++ code natively and compiled to WASM for any browser.
The content stays outside the code -- Markdown, SVG, YAML and `.grc` workflows, edited with ordinary tools, without recompiling the viewer.

## Build

GCC 15 or Clang 22, CMake 3.27 and Ninja;
for the browser an [emsdk](https://emscripten.org/) environment with `$EMSDK` set.
GNU Radio 4.0, OpenDigitizer and the other dependencies are fetched at the revisions pinned in `cmake/DependenciesSHAs.cmake`.

| preset                                                               | builds                                                                    |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `Core-Only`                                                          | presentation model and its unit tests                                     |
| `GCC15-Debug` / `GCC15-Release`, `Clang22-Debug` / `Clang22-Release` | the native viewer and all tests                                           |
| `WASM-Minimal`                                                       | the browser viewer without the live GNU Radio 4.0 / OpenDigitizer runtime |
| `WASM-Debug` / `WASM-Release`                                        | the browser viewer with it                                                |

```bash
cmake --preset GCC15-Debug
cmake --build cmake-build-GCC15-Debug
ctest --test-dir cmake-build-GCC15-Debug --output-on-failure
```

## Run

```bash
./cmake-build-GCC15-Debug/src/app/gr4-present --windowed
node devtools/serve.mjs --directory cmake-build-WASM-Release/viewer-web   # then open the address it prints
```

The browser needs the COOP/COEP headers `devtools/serve.mjs` sets; a static host that sends none (GitHub Pages) gets them from the page's service worker, which reloads once.
The same worker keeps the viewer and every deck it showed, so a page visited once online opens offline afterwards, and Edge, Chrome and Safari install it as an app from the address bar.
`--presentation=<directory, URL or .tar.gz>` (or `?presentation=` in the address, relative to the page where it names no scheme) opens another deck, `--theme=dark` (or `?theme=`) forces the colour scheme the system would otherwise choose;
without it the demo in `presentations/default` is shown.
"download viewer + deck" in the side menu saves `gr4-present-<deck name>.tar.gz`: the site's files as served, the deck shown under `default/`, a `README.txt` and a start script per platform (`serve.cmd` for Windows, PowerShell behind it; `serve.sh` for macOS and Linux, python3 or ruby or php).
Unpacked anywhere, the script serves the folder on that computer and opens the viewer in the browser, no network and no install needed; opened as a file by double-click no browser grants the threaded runtime its memory, and the page says what to start instead, or to open the site, which works offline once visited.
Where no script interpreter runs, `launch-chromium.cmd` (Windows) or `sh launch-chromium.sh` (macOS, Linux) starts Chrome, Edge, Chromium or Brave on the page as a file with `--allow-file-access-from-files` and `--enable-features=SharedArrayBuffer`, in a profile of its own deleted when its window closes: both switches weaken what the browser protects, so they never reach the browser used day to day; the page copies the deck beside it (`default/` by the list the script writes, or a named `.tar.gz`) into memory before the viewer starts.
A Chrome update may drop the second switch; the server scripts rely on web standards only.
"dark colours" in the side menu forces the other colour scheme, whatever the computer's own setting: a projector's room need not match the presenter's desktop.
Load at the top of the side menu (`Ctrl+O`) opens a deck from this computer without a server: natively its `index.yml`, a lone `.md` or a `.tar.gz`, in a browser its folder; a `.md` or a `.tar.gz` dropped onto the window opens as well, and a deck's folder natively and in Chromium.
A folder without `index.yml` opens its least nested `.md`.
Save (`Ctrl+S`) writes the deck shown as `<deck name>.tar.gz` into the downloads folder.
A deck on this computer is named by its path, `file:` before it optional as `http://` or `https://` before a remote one: natively `--presentation=file:/home/me/talks/tour`; in Chromium `?presentation=file:/home/me/talks/tour` is chosen once in the picker, whose folder (or `.md`) must carry the path's last name, and F5 reopens it.
"Follow edits" reloads the deck when one of its files changes (natively on by default; in Chromium, whose file handles also let F5 reopen `?presentation=local:<name>`).
A page from the web reads a deck served on this computer (`node devtools/serve.mjs --cors`) only once Chrome is allowed local network access:
Chrome blocks a public page's requests to `localhost` unless _Local network access_ is allowed for the page's site (the lock icon in the address bar, or `chrome://settings/content/localNetworkAccess`), or `chrome://flags/#local-network-access-check` is disabled;
until then the viewer keeps showing its built-in deck, and Load (`Ctrl+O`) opens the deck's folder without a server.

## Releases

Every push to main and every version tag publishes the web viewer as two archives with one `SHA256SUMS`: `full`, with live GNU Radio 4 graphs (OpenDigitizer) and the PDF export, and `minimal`, the viewer alone (about 3 MB packed).
`https://github.com/RalphSteinhagen/gr4-present/releases/download/<version>/gr4-present-web-<flavour>-<version>.tar.gz`, where `<version>` is a tag such as `v0.1.0` or `latest`, the rolling build of main;
`releases/latest/download/…` is GitHub's own redirect to the newest tagged release instead.
Each unpacks to a folder of its name holding the viewer, the start scripts and `README.txt` of "download viewer + deck" above, and a one-slide deck under `default/` saying how to show a deck of one's own:

- **offline:** put the deck's folder or `.tar.gz` beside `index.html` and run `sh serve.sh` (or `serve.cmd`) and open `?presentation=<name>`, or `sh launch-chromium.sh <name>` without a server; or replace `default/` with the deck;
- **on another repository's GitHub Pages**, pinned to a release and independent of this site:

```yaml
# .github/workflows/pages.yml in the deck's repository; Settings > Pages > Source: GitHub Actions
name: Pages
on:
  push:
    branches: [main]
jobs:
  pages:
    runs-on: ubuntu-latest
    permissions:
      pages: write
      id-token: write
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@v6
      - uses: RalphSteinhagen/gr4-present/.github/actions/deck-site@v0.1.0
        with:
          deck: talk # the folder holding index.yml; `.` for the repository's root
          version: v0.1.0 # the viewer's release; latest follows main
          flavour: full # or minimal
      - uses: actions/upload-pages-artifact@v3
        with:
          path: _site
      - id: deployment
        uses: actions/deploy-pages@v4
```

Without Actions, the same in a shell (`.github/actions/deck-site/deck-site.sh` does it, checksum included):

```bash
v=v0.1.0; f=full; base=https://github.com/RalphSteinhagen/gr4-present/releases/download/$v
curl -fsSLO "$base/gr4-present-web-$f-$v.tar.gz" -fsSLO "$base/SHA256SUMS"
grep " gr4-present-web-$f-$v.tar.gz\$" SHA256SUMS | sha256sum -c - && tar -xzf "gr4-present-web-$f-$v.tar.gz"
rm -rf "gr4-present-web-$f-$v/default" && cp -r talk "gr4-present-web-$f-$v/default"
```

The page's service worker supplies the isolation headers GitHub Pages does not send; `devtools/ci/web-archive.sh` makes an archive from a local WASM build.

## License and Copyright

SPDX: `GPL-3.0-or-later` `CC-BY-4.0`

Copyright (C) Dr. Ralph J. Steinhagen, GSI/FAIR<br>
Copyright (C) GSI Helmholtzzentrum für Schwerionenforschung, Darmstadt, Germany<br>
Copyright (C) FAIR - Facility for Antiproton & Ion Research, Darmstadt, Germany<br>
