#!/usr/bin/env bash
# Builds and pushes one CI flavour's build image: runs devtools/ci/prewarm.sh in the toolchain image with the checkout
# mounted where CI's container jobs find theirs, then puts its output on top of the toolchain image.
# Runs on the runner host, from the repository root; refuses to push an image without prewarmed objects.
# usage: devtools/ci/prewarm-image.sh <toolchain image> <flavour image> <preset> [extra configure arguments...]
set -euo pipefail

toolchain=${1:?toolchain image}
image=${2:?flavour image}
preset=${3:?CMake preset}
shift 3

repository=${GITHUB_REPOSITORY:?}
workspace="/__w/${repository#*/}/${repository#*/}"
context="${RUNNER_TEMP:?}/image"
mkdir -p "$context/prewarm"

# the toolchain image's entrypoint is `bash -c`, which would take only a first word as the command
docker run --rm --user root --entrypoint bash -v "$PWD:$workspace" -v "$context/prewarm:/opt/gr4-present" -w "$workspace" "$toolchain" \
  -c "git config --global --add safe.directory '*' && devtools/ci/prewarm.sh /opt/gr4-present $preset $*"

objects=$(find "$context/prewarm/ccache-deps" -type f | wc -l)
echo "prewarmed objects: $objects"
((objects > 100)) || { echo "::error::$preset: the prewarm left only $objects objects in its store"; exit 1; }

emCache=""
[[ $preset == WASM-* ]] && emCache=/opt/gr4-present/emscripten-cache
docker build -f docker/dependencies.Dockerfile --build-arg TOOLCHAIN="$toolchain" --build-arg EM_CACHE="$emCache" -t "$image" "$context"
docker push "$image"
