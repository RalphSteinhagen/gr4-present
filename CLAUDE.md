# CLAUDE.md (AGENTS.md) — gr4-present

> For LLM coding agents. gr4-present is a GR4 application, and GR4's rules apply unchanged.
> This file adds only what differs here.

## 1 · Rules

GR4's [`CLAUDE.md`](https://github.com/fair-acc/gnuradio4/blob/main/CLAUDE.md) is authoritative: the working agreement, design, naming, layout, documentation, C++23, GR4 conventions, testing, commits and the pre-submit checklist.
Read it first.

The local copies below are taken verbatim from GR4 `f0f48976` (2026-10-29), the pin in `cmake/DependenciesSHAs.cmake`, and are re-synced with each GR4 bump:

| File                            | Purpose                                                 |
| ------------------------------- | ------------------------------------------------------- |
| `CORE_DEVELOPMENT_GUIDELINE.md` | rationale                                               |
| `CORE_NAMING_GUIDELINE.md`      | naming                                                  |
| `CORE_AGENT_WORKFLOW.md`        | orchestration                                           |
| `CORE_REVIEW_GUIDELINE.md`      | review                                                  |
| `.claude/commands/`             | review commands and the refactor and fix-style commands |

Where these files name GR4 modules (`core`, `meta`, `algorithm`, `blocks/*`), `src/core` is the library here and `src/app` and `src/export` count as leaves.
This file and the copies MUST NOT be changed without the author's express approval, which expires with each approved change.

## 2 · What differs here

- **Static analysers.**
  CodeFactor and Sonar findings are ignored unless they are a real defect.
- **Ambient input** — time, viewport, fonts, events, environment — is read once where the frame starts and passed down as data, so the code below it is deterministic and testable.
  A function receives the data of the system it acts on, `const` unless it writes it;
  it reads no other global, singleton or function-local static, the immediate-mode ImGui/GL/SDL context of drawing code excepted.
- **One jump away.**
  A cross-file function with one call site and no test is an indirection to inline, unless it keeps state or a format private to its own file, or is the entry point of a system (input, slides, live graphs, export) called from the frame loop.
- **Core.**
  `src/core` depends on the standard library and GR4 only;
  a convenience function there MUST decompose into public lower-level calls a caller could write.
- **Cold paths** inside hot code (errors, diagnostics, logging, first-use set-up) are `GR4_PRESENT_NOINLINE` and their branches `[[unlikely]]`.
- **Errors:** `Diagnostics` for what the presenter must see, `gr::log` for the operator, `std::expected` across `src/core` APIs;
  one channel per failure; library code MUST NOT throw.
- **Threads:** GR4 graphs and their schedulers own their threads; everything else runs on the frame thread.
  The teardown order in `main.cpp` is a contract (brief there).
- **Frame budget:** the display's refresh interval; effects degrade (`tendEffects`) before frames drop.
- **Deck keywords are a user-facing language:**
  a renamed or removed keyword keeps working for one release with a `Diagnostics` note naming the new spelling.
  The `.grc` workflows' settings keys are wire keys: GR4 drops a key no setting answers to without a word, so a renamed GR4 setting is no compile error;
  `qa_LiveGraph` checks every key the shipped workflows set.
- **Boundaries:** a change that adds or widens a project boundary (deck files, network, launch options, browser address) names it in the commit message.
- **Tests:** frame-thread code takes time as a parameter; the GUI suites start their own Xvfb or need `DISPLAY`.
  Recorded counter-example (2026-10-15): the end-to-end check of a blend between two pictures allows one step of 255 (`kBlendRoundingSteps`), as Mesa's llvmpipe in CI's image rounds alpha blending; exact composition would need a shader pass for a difference no one sees.
- **Toolchains:** GCC 15 (libstdc++), Clang 22 (libc++), Emscripten.
  Before committing: build and test with GCC 15 and Emscripten, plus Clang and the browser suites where the change reaches them (toolchain order GCC 15, Clang, Emscripten, GCC 14).
- **Commit message** of an application change: a short example in the input it reads (e.g. ` ```markdown `), then `### Changes`.
- **GR4's headers** to verify an API against are `_deps/gnuradio4-src/core/include/gnuradio-4.0/` and each module's `include/` in a build tree.

## 3 · Repository

- **Layout.**
  `src/core` (the deck model: Markdown, manifest, layouts, HTML, plots), `src/app` (the ImGui/SDL3/OpenGL viewer, the live graphs with OpenDigitizer, the decks' own GR4 blocks), `src/export` (PDF);
  tests `test/{core,app,export}/qa_*.cpp`, `test/benchmarks/bm_*.cpp`, fixtures in `test/fixtures/`;
  `e2e/` drives the viewer as a person would, natively under Xvfb and in a headless browser;
  `presentations/default/` is the deck that ships, its live graphs are `.grc` workflows;
  `assets/` and `third_party/` are carried as they are; `devtools/ci/` holds the image flavours, ccache settings and the metrics page.
- **Options.**
  `GR4_PRESENT_ENABLE_TESTING`, `GR4_PRESENT_WITH_OPENDIGITIZER`, `GR4_PRESENT_ENABLE_EXPORT`, `GR4_PRESENT_COVERAGE`, `GR4_PRESENT_ENABLE_CLANG_TIDY`, `GR4_PRESENT_WARNINGS_AS_ERRORS`.
- **Where GR4 comes from.**
  FetchContent at `GR4_PRESENT_GIT_REF_GNURADIO4` in `cmake/DependenciesSHAs.cmake`, a full SHA (GitHub serves a commit by its full id only); the name differs from OpenDigitizer's `GIT_SHA_GNURADIO4`, which that file force-sets.
  OpenDigitizer at `GIT_REF_OPENDIGITIZER` builds against the same GR4, so the two pins move together.
  CI prewarms every dependency into an image per flavour (`devtools/ci/flavours.json`) and configures fully disconnected.
- **The viewer opens a window:** never launch `gr4-present` without `--export` or a virtual display.

## 4 · Building

```bash
cmake --preset GCC15-Debug -DGR_BUILD_PARALLEL_LEVEL=<N>   # GCC15-{Debug,Release}, Clang22-{Debug,Release}, Core-Only, WASM-{Minimal,Debug,Release}
cmake --build cmake-build-GCC15-Debug
ctest --test-dir cmake-build-GCC15-Debug --output-on-failure
```

Memory, not cores, binds: template-heavy TUs exceed 7 GB.
`-DGR_BUILD_PARALLEL_LEVEL=<N>` at configure time drives the Ninja pools (`--parallel` only bounds it); default 4, raised only after measuring peak RSS (the `measure-build-cost` skill).
CPU budget: a session gets `N` cores (asked for, else `nproc - 2`) at `i*N..i*N+N-1` for instance `i`;
run every compile, test, agent and helper as `taskset -c <range> nice -n 5 … -j <N>` (`lld`/`mold` ignore `-j`).
Never `pkill` by program name; match on the build directory.
Emscripten and the browser suites: the `wasm-build` skill.

## 5 · Quality checks

- `./formatFiles.sh` runs the pinned formatters over the files changed since `HEAD`, `--since <ref>`, or `--all`;
  it leaves `third_party/`, `assets/`, `presentations/`, `test/fixtures/`, the licence texts, this file and the `CORE_*.md` as they are.
  It MUST run before each commit (the `restyle` skill).
- In CI, GR4's quality bots run from a checkout of GR4's `devtools/` at the pin (`.github/actions/gr4-devtools`): Formatting, Metrics (naming and code style), Coverage on the instrumented Clang lane, Hot Paths and WASM size.
  Their scope is `.github/qualitybot.conf`; they post sticky comments and statuses but never gate a merge; their history is the `ci-metrics` branch, drawn at `/metrics/` of the Pages site.
- The same tools measure a change locally: `code-metrics.sh` and `naming-check` from a GR4 tree (`_deps/gnuradio4-src/devtools/`, built standalone with `cmake -S <that>/devtools -B <dir>`), on a Clang build with a compile database, with the roots `.github/qualitybot.conf` names.
- When a CI job fails, use the `ci-triage` skill.
