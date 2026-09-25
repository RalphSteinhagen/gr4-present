# Contributing

## Before you start

`CLAUDE.md` is the working agreement and is authoritative for code and style; `CORE_DEVELOPMENT_GUIDELINE.md` and
`CORE_NAMING_GUIDELINE.md` carry the rationale. `functional_spec_wip.md` holds the functional requirements.

## Sign-off

Every commit must be signed off under the [Developer Certificate of Origin](DCO.txt):

```bash
git commit -s
```

The sign-off is an assertion by the human author, who holds the copyright and remains answerable for the quality of
the code. Commits carry no other trailer: no bot co-author, no assistant attribution. That an assistant drafted a
change does not alter who is answerable for it.

## Before submitting

```bash
./formatFiles.sh                                    # clang-format, cmake-format, black
cmake --preset GCC15-Debug && cmake --build cmake-build-GCC15-Debug
ctest --test-dir cmake-build-GCC15-Debug --output-on-failure
```

The build must be warning-free under `-Werror` on both GCC 15 and Clang 20. `CLAUDE.md` §10 is the full checklist.

Screenshot references under `src/app/test/reference/` are committed. When a UI change is intended, re-record them and
say so in the pull request:

```bash
GR4_PRESENT_UPDATE_REFERENCES=1 ctest --test-dir cmake-build-GCC15-Debug -R qa_LaunchScreen
```

## Conduct

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).
