---
name: restyle
description: Check or apply the formatter set CI's formatting bot runs (clang-format, cmake-format, prettier for Markdown and YAML, shellcheck, whitespace, black) at the versions formatFiles.sh pins. Use before committing, or when the Formatting status is red.
---

# Restyle

`./formatFiles.sh` installs and runs the pinned formatters, the same ones CI's Formatting bot runs:

| file                       | tool         |
| -------------------------- | ------------ |
| `*.cpp` `*.hpp`            | clang-format |
| `CMakeLists.txt` `*.cmake` | cmake-format |
| `*.md` `*.yml`             | prettier     |
| `*.sh`                     | shellcheck   |
| `*.py`                     | black        |

It leaves `third_party/`, `assets/`, `presentations/` (deck Markdown is the viewer's own language), `test/fixtures/`, the licence texts, `CLAUDE.md` and the `CORE_*.md` as they are.

## Working tree

```bash
./formatFiles.sh --install              # once: the pinned versions into ~/.cache/gr4-formatters
./formatFiles.sh                        # the files changed since HEAD
./formatFiles.sh --since origin/main    # what the bot compares a pull request against
./formatFiles.sh --all                  # every tracked file, as CI
./formatFiles.sh --versions             # the versions found against the pins
```

A second run must be a no-op.

## Two traps

**Config resolves from the file's path.** Checking a copy under `/tmp` reports every file as mis-formatted, because the tool never finds `.clang-format` / `.cmake-format.yaml`.
Always check in place, or pass `--assume-filename`.

**`cmake-format` picks its dialect from the filename.** A copy named `CMakeLists.txt.bak` parses as something else and reports spurious differences.
Keep the basename.

## A specific commit

```bash
export PATH="$HOME/.cache/gr4-formatters/venv/bin:$PATH"
git show $c:$f | clang-format --assume-filename="$f" --dry-run --Werror
```

For CMake files, write to a scratch directory _inside the repo_ keeping the basename, format there, compare, delete.
