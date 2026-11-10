#!/usr/bin/env bash
# A deck's site with the gr4-present web viewer beside it, from a release archive (see action.yml beside this script).
#
#   deck-site.sh <deck folder> <full|minimal> <vX.Y.Z|latest|""> <out folder> [owner/repository] [action ref]
#
# An empty version takes the action's ref where it is a vX.Y.Z tag (inside a composite action the runner may report
# none), else `latest`, the rolling build of main, with a warning.
# GR4_PRESENT_ARCHIVE and GR4_PRESENT_SUMS name a local archive and SHA256SUMS in place of the release's, to try it
# offline. Needs curl, sha256sum and tar.
set -euo pipefail

if (($# < 4)); then
  sed -n '2,/^$/s/^# \{0,1\}//p' "$0" >&2
  exit 2
fi
deck=$1
flavour=$2
version=$3
out=$4
repository=${5:-RalphSteinhagen/gr4-present}
if [[ -z "$version" ]]; then
  if [[ "${6:-}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    version=$6
  else
    version=latest
    echo "::warning::deck-site: no version given and the action's ref '${6:-}' is no vX.Y.Z tag, so the viewer is the rolling 'latest' build; pass version: vX.Y.Z to pin it"
  fi
fi
name="gr4-present-web-${flavour}-${version}"

[[ -f "$deck/index.yml" ]] || {
  echo "$deck holds no index.yml: name the deck's folder" >&2
  exit 1
}
if [[ -e "$out" && -n "$(ls -A "$out")" ]]; then
  echo "$out exists and is not empty" >&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if [[ -n "${GR4_PRESENT_ARCHIVE:-}" ]]; then
  cp "$GR4_PRESENT_ARCHIVE" "$work/$name.tar.gz"
  cp "${GR4_PRESENT_SUMS:?GR4_PRESENT_SUMS names the SHA256SUMS that goes with GR4_PRESENT_ARCHIVE}" "$work/SHA256SUMS"
else
  base="https://github.com/${repository}/releases/download/${version}"
  curl -fsSL --retry 3 -o "$work/$name.tar.gz" "$base/$name.tar.gz"
  curl -fsSL --retry 3 -o "$work/SHA256SUMS" "$base/SHA256SUMS"
fi
# only the archive's own line: the file lists the other flavour too
(cd "$work" && grep -E "  ${name}\.tar\.gz\$" SHA256SUMS | sha256sum -c -)

tar -xzf "$work/$name.tar.gz" -C "$work"
mkdir -p "$out"
cp -r "$work/$name/." "$out/"
rm -rf "$out/default"
mkdir -p "$out/default"
# a deck at the repository's root (deck: .) leaves out the repository's own folders and the site being written
deckReal=$(cd "$deck" && pwd)
outReal=$(cd "$out" && pwd)
excludes=(--exclude=./.git --exclude=./.github)
[[ "$outReal" == "$deckReal"/* ]] && excludes+=("--exclude=./${outReal#"$deckReal"/}")
tar -C "$deck" "${excludes[@]}" -cf - . | tar -C "$out/default" -xf -
touch "$out/.nojekyll" # GitHub Pages would hide files beginning with an underscore
echo "$out: the $flavour viewer $version with $deck as default/"
