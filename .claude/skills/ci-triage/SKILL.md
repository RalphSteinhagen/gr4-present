---
name: ci-triage
description: Fetch and classify a failed GitHub Actions job for this repo — distinguish out-of-memory kills, six-hour timeouts, a missing build image, configure errors and genuine test failures, and identify what was in flight. Use whenever a CI job fails.
---

# CI triage

## Fetching logs

`gh run view --log` refuses while the run is still in progress. This always works:

```bash
JID=$(gh run view <run-id> --json jobs -q '.jobs[]|select(.name=="<job>")|.databaseId')
gh api --allow-escape-sequences "repos/RalphSteinhagen/gr4-present/actions/jobs/$JID/logs" \
  | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' > job.log
```

Without `--allow-escape-sequences` the call fails; without the `sed` the log is unreadable.

## Classification

| signature                                                            | cause                                                                               |
| -------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `exit code 137` + "runner has received a shutdown signal"            | OOM — the kernel killed the runner                                                  |
| `Build` step with **no conclusion** at all                           | OOM so hard the step never reported                                                 |
| conclusion `cancelled` at ~360 min                                   | six-hour job timeout                                                                |
| a job fails at the container pull, `prewarm <preset>` red or missing | the flavour's image was not built: `devtools/ci/flavours.json` or `docker/` changed |
| `wait-for-image` runs out its minutes                                | the push's CI run did not build the image; rerun once CI's `images` job is green    |
| `CMake Error: generator : Ninja / Does not match ... Unix Makefiles` | a cached `_deps` sub-build from another generator                                   |
| `***Failed <n> sec`                                                  | the test's own bound, not a ctest timeout                                           |
| `***Timeout`                                                         | ctest `TIMEOUT` property; a GUI test without a display hangs until it               |
| a bot step red, the lane green                                       | the bots never gate; read the sticky comment                                        |
| `naming-check: <n> units not checked`                                | libclang's version differs from the compile database's Clang; CI pairs 22 with 22   |

## After an OOM

The runner is 4 cores / ~16 GB; the flavours configure with `-DGR_BUILD_PARALLEL_LEVEL=3`.
Read the last twenty lines: the compiles and links in flight are the peak.
Sum their measured peak RSS (the `measure-build-cost` skill) before changing the pool size — a job count sized from core count rather than memory is how this repo OOMs.

Also grep `Hits:` and the `misses by directory` list for the ccache hit rate: a low rate means the prewarmed image no longer matches the dependencies, and the misses say where.
