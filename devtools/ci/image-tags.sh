#!/usr/bin/env bash
# The build images' names, as `key=value` lines for $GITHUB_OUTPUT: the toolchain image is tagged by the hash of
# docker/Dockerfile, each flavour's image (<preset>-<tag>) by the hash of everything its prewarmed dependencies depend on.
# CI builds an image only when its tag is missing; the bot workflows run in the same images.
set -euo pipefail

owner=$(echo "${GITHUB_REPOSITORY_OWNER:-ralphsteinhagen}" | tr '[:upper:]' '[:lower:]')
inputs=(docker/Dockerfile docker/dependencies.Dockerfile devtools/ci/prewarm.sh devtools/ci/prewarm-image.sh devtools/ci/flavours.json
  cmake/DependenciesSHAs.cmake cmake/Dependencies.cmake CMakePresets.json)

echo "repository=ghcr.io/${owner}/gr4-present-build-container"
echo "toolchain=toolchain-$(sha256sum docker/Dockerfile | cut -c1-16)"
echo "tag=$(cat "${inputs[@]}" | sha256sum | cut -c1-16)"
