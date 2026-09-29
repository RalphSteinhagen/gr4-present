#!/usr/bin/env bash
# Runs every formatter the project mandates over the working tree, or over the staged/modified files only.
#
# CLAUDE.md requires clang-format and cmake-format to have been run before committing, and forbids adjusting
# whitespace by hand. Running them separately is easy to get half-right, so they live here together.

set -euo pipefail

cd "$(dirname "$0")"

CLANG_FORMAT="${CLANG_FORMAT:-$(command -v clang-format-18 || command -v clang-format-20 || command -v clang-format || true)}"
CMAKE_FORMAT="${CMAKE_FORMAT:-$(command -v cmake-format || true)}"

changed_only=false
if [[ "${1:-}" == "--changed" ]]; then
    changed_only=true
fi

collect() { # <find-pattern>
    # vendored upstream is excluded: reformatting it makes every future update a conflict, and the licence notices
    # in third_party say those files are carried unmodified
    if [[ "${changed_only}" == true ]]; then
        git diff --name-only --diff-filter=ACMR HEAD -- "$1" 2>/dev/null | grep -v '^third_party/' || true
    else
        git ls-files -- "$1" | grep -v '^third_party/' || true
    fi
}

format() { # <tool> <label> <pattern> <args...>
    local tool=$1 label=$2 pattern=$3
    shift 3
    if [[ -z "${tool}" || ! -x "$(command -v "${tool}" || echo "${tool}")" ]]; then
        echo "  skipping ${label}: not installed"
        return
    fi
    mapfile -t files < <(collect "${pattern}")
    if [[ ${#files[@]} -eq 0 ]]; then
        echo "  ${label}: nothing to do"
        return
    fi
    "${tool}" "$@" "${files[@]}"
    echo "  ${label}: ${#files[@]} file(s)"
}

echo "formatting$([[ "${changed_only}" == true ]] && echo " changed files" || echo " all tracked files")"
format "${CLANG_FORMAT}" "clang-format" '*.cpp' -i
format "${CLANG_FORMAT}" "clang-format" '*.hpp' -i
format "${CMAKE_FORMAT}" "cmake-format" '*CMakeLists.txt' -i
format "${CMAKE_FORMAT}" "cmake-format" '*.cmake' -i
