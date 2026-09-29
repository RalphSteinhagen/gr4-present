# gr4-present

Presentation platform for and in C++: Markdown and SVG slides that host live [GNU Radio 4.0](https://github.com/fair-acc/gnuradio4)
flow graphs and [OpenDigitizer](https://github.com/fair-acc/opendigitizer) charts, natively and in the browser via
WebAssembly.

**Live demo:** <https://ralphsteinhagen.github.io/gr4-present>

> **Status:** proof of concept, work in progress.

## Why

Talks about signal processing show static screenshots of software that is live. Here the slides run the real thing:
a GNU Radio 4.0 flow graph computes what the slide shows, and OpenDigitizer draws it, with the same C++ code natively
and compiled to WASM for any browser. The content stays outside the code -- Markdown, SVG, YAML and `.grc` workflows,
edited with ordinary tools, without recompiling the viewer.

## Build

GCC 15 or Clang 22, CMake 3.27 and Ninja; for the browser an [emsdk](https://emscripten.org/) environment with
`$EMSDK` set. GNU Radio 4.0, OpenDigitizer and the other dependencies are fetched at the revisions pinned in
`cmake/DependenciesSHAs.cmake`.

| preset | builds |
| --- | --- |
| `Core-Only` | presentation model and its unit tests |
| `GCC15-Debug` / `GCC15-Release`, `Clang22-Debug` / `Clang22-Release` | the native viewer and all tests |
| `WASM-Minimal` | the browser viewer without the live GNU Radio 4.0 / OpenDigitizer runtime |
| `WASM-Debug` / `WASM-Release` | the browser viewer with it |

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

The browser needs the COOP/COEP headers `devtools/serve.mjs` sets. `--presentation=<directory or URL>` (or
`?presentation=` in the address) opens another deck; without it the demo in `presentations/default` is shown.

## License and Copyright

SPDX: `GPL-3.0-or-later` `CC-BY-4.0`

Copyright (C) Dr. Ralph J. Steinhagen, GSI/FAIR<br>
Copyright (C) GSI Helmholtzzentrum für Schwerionenforschung, Darmstadt, Germany<br>
Copyright (C) FAIR - Facility for Antiproton & Ion Research, Darmstadt, Germany<br>
