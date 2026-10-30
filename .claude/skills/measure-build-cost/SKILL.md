---
name: measure-build-cost
description: Measure the peak memory and wall time of a single translation unit or link step, so build parallelism can be sized from evidence. Use before changing GR_BUILD_PARALLEL_LEVEL or when diagnosing an out-of-memory build.
---

# Measure build cost

Memory, not core count, bounds this build: GR4's and the viewer's translation units have exceeded 7 GB.

## Recipe

```bash
cmake --preset <preset> [flags]            # or cmake -S . -B <tree> -G Ninja -DCMAKE_BUILD_TYPE=<type> -DCMAKE_CXX_COMPILER=<cxx>
cd cmake-build-<preset>
o=$(grep -o "[^ ]*/<name>\.cpp\.o" build.ninja | head -1)
ninja -j4 "$o"                                     # build dependencies FIRST
rm -f "$o"
CCACHE_DISABLE=1 /usr/bin/time -f "%M %e" ninja -j1 "$o"   # KB peak RSS, seconds
```

## Why each step

**Pre-build the dependencies.** `/usr/bin/time` on a `ninja` invocation charges the whole dependency chain to the target.
Skipping this produced a 149 s figure for a 21 s translation unit and a 2.7 GB figure for a 0.2 GB link — both wrong, both used to justify wrong conclusions.

**`CCACHE_DISABLE=1`**, or a cache hit reports the cost of copying a file.

**Delete only the target**, never the whole tree.

## Flags that change the answer

Match the CI flavour exactly (`devtools/ci/flavours.json`): build type, compiler, `-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`, and `-DGR4_PRESENT_COVERAGE=ON` on the Clang lane, whose instrumentation adds its own peak RSS.

## Sizing

Sum the peak RSS of the translation units that appear together in the failing log, add the links, and compare against ~16 GB.
Set `-DGR_BUILD_PARALLEL_LEVEL=<N>` from that sum; CI's flavours use 3.
