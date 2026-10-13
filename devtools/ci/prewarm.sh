#!/usr/bin/env bash
# Compiles one CI flavour once, so its dependency objects can ship in that flavour's build image (docker/dependencies.Dockerfile).
# Runs inside the toolchain image with the checkout at the path CI's container jobs use (/__w/<repo>/<repo>): ccache
# hashes absolute include paths, so a later CI build only hits these objects when every path is the same.
# Precompiled headers are off in CI (devtools/ci/flavours.json): a PCH is rebuilt with different bytes in every build tree,
# and every unit that includes one would miss the prewarmed objects.
# usage: devtools/ci/prewarm.sh <output directory> <preset> [extra configure arguments...]
# output: <output>/ccache-deps (a `file:` ccache store, read-only in CI), <output>/deps-src (the fetched sources and their
# stamps, copied into the build directory before CI configures), <output>/emscripten-cache (WASM presets only)
set -euo pipefail

output=${1:?output directory}
preset=${2:?CMake preset}
shift 2

mkdir -p "$output/ccache-deps" "$output/deps-src"
export CCACHE_CONFIGPATH="$PWD/devtools/ccache.conf"
# written as a store, not a local cache: CI reads it through `remote_storage`, whose layout differs from a local cache's
export CCACHE_REMOTE_ONLY=true
export CCACHE_REMOTE_STORAGE="file:$output/ccache-deps"
ccache --version | head -n 1

if [[ $preset == WASM-* ]]; then
  source "$EMSDK_HOME/emsdk_env.sh"
  # the ports (zlib, SDL3) are fetched once here instead of on every CI run; the cache's path is part of every compile
  # command (--sysroot), so CI points EM_CACHE at the same place
  cp -a "$(em-config CACHE)" "$output/emscripten-cache"
  export EM_CACHE="$output/emscripten-cache"
fi

cmake --preset "$preset" -DGR_BUILD_PARALLEL_LEVEL=3 "$@"
cmake --build "cmake-build-$preset"
cp -a "cmake-build-$preset"/_deps/*-src "cmake-build-$preset"/_deps/*-subbuild "$output/deps-src/"
du -sh "$output"/*
