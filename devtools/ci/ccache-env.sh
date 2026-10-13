#!/usr/bin/env bash
# ccache's settings for a CI job in a flavour's build image, as lines for $GITHUB_ENV. They need the job's own
# workspace path, which only the running container knows: `${{ github.workspace }}` names the runner host's.
# ccache keeps nothing locally: the dependencies come read-only from the image's store, and only this project's own
# objects go to the writable store that actions/cache saves -- a local cache beside the image's store would copy every
# dependency hit into itself.
set -euo pipefail

echo "CCACHE_DIR=$GITHUB_WORKSPACE/.ccache"
# cmake/Cache.cmake exports this for the compiles it launches, but nothing else in a job inherits it, so a bare `ccache`
# call would report the built-in defaults rather than what the build used
echo "CCACHE_CONFIGPATH=$GITHUB_WORKSPACE/devtools/ccache.conf"
echo "CCACHE_REMOTE_ONLY=true"
echo "CCACHE_REMOTE_STORAGE=file:/opt/gr4-present/ccache-deps|read-only file:$GITHUB_WORKSPACE/.ccache-own"
# packing harder than the repository default is worth it where the store is uploaded against the 10 GB quota
echo "CCACHE_COMPRESSLEVEL=6"
