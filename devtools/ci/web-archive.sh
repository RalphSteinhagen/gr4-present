#!/usr/bin/env bash
# A release archive of the web viewer: gr4-present-web-<flavour>-<version>.tar.gz, unpacking to a folder of that name with
# the viewer's site files, the start scripts, README.txt and the one-slide stub deck under default/, packed by gr4-present-bundle.
#
#   devtools/ci/web-archive.sh <build dir> <full|minimal> <version> <out dir> [site]
#
# <build dir> is a built WASM tree (cmake-build-WASM-Release for full, cmake-build-WASM-Minimal for minimal); <version> is
# vX.Y.Z for a tag or `latest` for main's rolling build; [site] is the published full viewer, named in the stub deck and
# the README. GR4_PRESENT_BUILD overrides the build note, by default the commit and the day.
set -euo pipefail

if (($# < 4)); then
  sed -n '2,/^$/s/^# \{0,1\}//p' "$0" >&2
  exit 2
fi
build=$1
flavour=$2
version=$3
out=$4
site=${5:-https://ralphsteinhagen.github.io/gr4-present}
case "$flavour" in
  full | minimal) ;;
  *)
    echo "flavour must be full or minimal, not $flavour" >&2
    exit 2
    ;;
esac

here=$(cd "$(dirname "$0")" && pwd)
name="gr4-present-web-${flavour}-${version}"
note=${GR4_PRESENT_BUILD:-"built from $(git -C "$here" rev-parse --short HEAD) on $(date -u +%Y-%m-%d)"}

if [[ -f "$build/tools/gr4-present-bundle.js" ]]; then
  pack=("${EMSDK_NODE:-node}" "$build/tools/gr4-present-bundle.js")
elif [[ -x "$build/tools/gr4-present-bundle" ]]; then
  pack=("$build/tools/gr4-present-bundle")
else
  echo "$build/tools has no gr4-present-bundle: build that target first" >&2
  exit 1
fi

deck=$(mktemp -d)
trap 'rm -rf "$deck"' EXIT
cp -r "$here/stub-deck/." "$deck/"
for file in "$deck"/index.yml "$deck"/talk.md; do
  # | as the separator: an address holds slashes but never a bar
  sed -i -e "s|@VERSION@|${version}|g" -e "s|@FLAVOUR@|${flavour}|g" -e "s|@SITE@|${site}|g" -e "s|@BUILD@|${note}|g" "$file"
done

mkdir -p "$out"
"${pack[@]}" --viewer "$build/viewer-web" --deck "$deck" --site "$site" --title "gr4-present ${version} (${flavour})" \
  --folder "$name" --out "$out/$name.tar.gz"
