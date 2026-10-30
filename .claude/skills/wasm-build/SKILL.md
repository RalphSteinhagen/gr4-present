---
name: wasm-build
description: Configure and run the local Emscripten/WebAssembly build of the viewer as CI does, its unit tests under Node and the browser suites under headless Chrome. Use when reproducing a wasm lane failure or changing anything the browser build reaches.
---

# WASM build

Pull requests build `WASM-Minimal` (the viewer without the live GNU Radio 4 / OpenDigitizer runtime); main builds and publishes `WASM-Release`.
Both presets need an [emsdk](https://emscripten.org/) environment with `$EMSDK` set.

```bash
source "$EMSDK/emsdk_env.sh"
cmake --preset WASM-Minimal -DGR_BUILD_PARALLEL_LEVEL=<N>
cmake --build cmake-build-WASM-Minimal
ctest --test-dir cmake-build-WASM-Minimal --output-on-failure -E Browser   # the qa_ units, run under Node
```

The page lands in `cmake-build-<preset>/viewer-web/`; `devtools/serve.mjs <that directory>` serves it with the COOP/COEP headers the threaded runtime needs.

## Browser suites

`e2e/qa_Browser*.mjs` drive the page in headless Chrome through `devtools/browser_harness.mjs`.
CMake registers them when it finds Node and a Chrome (`GR4_PRESENT_NODE`, `GR4_PRESENT_BROWSER`); they need Node 22 or later, an older Node lacks the global `WebSocket` the harness uses:

```bash
cmake --preset WASM-Minimal -DGR4_PRESENT_NODE=<node 22>
ctest --test-dir cmake-build-WASM-Minimal --output-on-failure -R Browser -j 1
```

They are the slow part of a lane; run the one suite a change reaches first.
Redirect a looping test's output through `head -c`: a runaway page has filled a disk before.

## Dependencies

GR4 and OpenDigitizer come from the pins in `cmake/DependenciesSHAs.cmake`; a fresh tree fetches them.
A source directory of your own is passed with `-DFETCHCONTENT_SOURCE_DIR_GNURADIO4=<dir>` (and `..._OPENDIGITIZER`), checked out at the pinned full SHA with `GIT_LFS_SKIP_SMUDGE=1`.
