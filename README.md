# gr4-present

Presentation system around GNU Radio 4: Markdown and SVG slides that can host live GNU Radio 4.0 / OpenDigitizer
widgets, running in one persistent C++/WASM runtime in the browser. Content stays external — Markdown, SVG, YAML and
`.grc` workflows are authored with ordinary tools and never require recompiling the viewer.

**Live demo:** <https://ralphsteinhagen.github.io/gr4-present>

> **Status: scaffolding.** The build system, presentation model and launch screen exist. The resource resolver,
> Markdown/SVG rendering and live OpenDigitizer regions do not yet. `functional_spec_wip.md` is the functional brief.

## Prerequisites

GCC 15 or Clang 22, CMake 3.27, Ninja, and an OpenGL-capable display for the GUI tests. The WASM build needs an
[emsdk](https://emscripten.org/) environment with `$EMSDK` set; `inkscape` only to regenerate the logo textures.

Dependencies (GNU Radio 4, OpenDigitizer, SDL3, Dear ImGui, ImPlot) are fetched by CMake at the revisions pinned in
`cmake/DependenciesSHAs.cmake`, so the first configure takes a while.

## Build

| preset | what it builds |
| --- | --- |
| `Core-Only` | presentation model and its unit tests, no OpenDigitizer — minutes |
| `GCC15-Debug` / `GCC15-Release` | everything native, GCC 15 |
| `Clang22-Debug` / `Clang22-Release` | everything native, Clang 22 |
| `WASM-Minimal` | WASM viewer without the OpenDigitizer runtime — the fast WASM path |
| `WASM-Debug` / `WASM-Release` | WASM with the full OpenDigitizer runtime — very long |

```bash
cmake --preset GCC15-Debug
cmake --build cmake-build-GCC15-Debug
ctest --test-dir cmake-build-GCC15-Debug --output-on-failure
```

Memory, not cores, is the binding constraint. `GR_BUILD_PARALLEL_LEVEL` (4 locally, 3 in CI) sizes the ninja job
pools and is the only knob to touch.

## Run

```bash
./cmake-build-GCC15-Debug/src/app/gr4-present
```

In the browser:

```bash
cmake --preset WASM-Minimal
cmake --build cmake-build-WASM-Minimal
python3 devtools/serve.py --directory cmake-build-WASM-Minimal/viewer-web
```

`devtools/serve.py` sets the COOP and COEP headers that `SharedArrayBuffer`, and therefore the threaded GR4 runtime,
requires; a plain static file server will not do.

The colour scheme follows the desktop or the browser's `prefers-color-scheme`; `GR4_PRESENT_COLOUR_SCHEME=light|dark`
overrides it for a projector.

### Launch parameters

The viewer starts full screen. The same parameters are accepted on the command line and in the URL, matched
case-insensitively; a command line overrides a `#fragment`, which overrides a `?query`.

```text
gr4-present --windowed --load=file:/abs/path/to/demo
index.html?load=http://host/talks/demo#mode=windowed
```

`load` names the base directory of a presentation package; the viewer appends `index.yml` and resolves every asset
reference against it, so the same package works from disk, from a web server or from another host. Without it the
viewer opens `default`. Reading goes through GNU Radio 4's `gr::algorithm::fileio` and is polled a step per frame
rather than waited on, because a browser forbids blocking the main thread; the launch screen's content bar reports
that progress and shows a failure there rather than leaving a blank window.

A browser grants full screen only from a user gesture, so a requested full screen is entered on the first click or
key press. Controls live in a menu parked off the left edge that slides out as the pointer approaches it.

## Tests

`ctest` runs unit tests plus screenshot regression tests driven by
[imgui_test_engine](https://github.com/ocornut/imgui_test_engine), rendered into an offscreen framebuffer through
SDL's `offscreen` video driver — no window appears and no display server is needed. Screenshots are compared
tolerantly: a coherent blob of differing pixels is a regression, scattered anti-aliasing noise is not. References
live in `src/app/test/reference/`; re-record them when a UI change is intended:

```bash
GR4_PRESENT_UPDATE_REFERENCES=1 ctest --test-dir cmake-build-GCC15-Debug -R qa_LaunchScreen
```

## Layout

```
src/core/       presentation model: manifest, navigation, package paths, live-region bindings, package loading
src/viewer/     DOM to OpenDigitizer bridge
src/app/        the viewer application, native and WASM
devtools/       serve.py, render_logo.py, ccache.conf
assets/         logos, including the combined GSI/FAIR mark
presentations/  packages; the build stages their contents at the viewer root, so `presentations/default/`
                is addressed as `default`
```

## Continuous integration

`.github/workflows/ci.yml` checks formatting, builds and tests natively with both compilers, builds the WASM viewer
and publishes it to GitHub Pages from `main`. Pull requests build `WASM-Minimal` for fast feedback; `main` publishes
`WASM-Release` with the full GNU Radio 4 and OpenDigitizer runtime. Screenshot diffs from a failed run are uploaded
as an artifact.

Jobs run in `gr4-present-build-container`, built by `docker/Dockerfile` from the container gnuradio4, gr-digitizers
and OpenDigitizer share. Compiler caching uses `devtools/ccache.conf` through a launcher generated by
`cmake/Cache.cmake`; both the compiler cache and the fetched dependency sources are keyed on
`cmake/DependenciesSHAs.cmake`.

> Publishing requires **Settings → Pages → Source: GitHub Actions** to be set once by hand; a workflow cannot do it.

## License and Copyright

SPDX: `GPL-3.0-or-later`

Copyright (C) Dr. Ralph J. Steinhagen, GSI/FAIR
Copyright (C) GSI Helmholtzzentrum für Schwerionenforschung, Darmstadt, Germany<br>
Copyright (C) FAIR - Facility for Antiproton & Ion Research, Darmstadt, Germany<br>
