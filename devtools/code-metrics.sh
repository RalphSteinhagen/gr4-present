#!/usr/bin/env bash
# Code-shape figures for comparing two states of the tree (CLAUDE.md, "Measured quality"): run before and after.
# devtools/code-metrics.sh --help [<build-dir>] explains every figure and option. Run from the repository root.
set -euo pipefail

cognitiveScores() { # "<complexity> <penalised increments> <file>:<line> <name>" per function under src/, each definition once
    local units
    units=$(grep -o '"file": *"[^"]*"' "$build/compile_commands.json" | sed 's/"file": *"//; s/"$//' | grep "^$PWD/src/" | sort -u)
    local config="{Checks: '-*,readability-function-cognitive-complexity', WarningsAsErrors: '', CheckOptions: {readability-function-cognitive-complexity.Threshold: 0, readability-function-cognitive-complexity.DescribeBasicIncrements: true}}"
    local tidy
    tidy=$(command -v clang-tidy-22 || command -v clang-tidy-20 || command -v clang-tidy)
    local perUnit # one file per unit: parallel runs must not interleave a function's notes
    perUnit=$(mktemp -d)
    export tidy build config perUnit
    xargs -P "$jobs" -I{} sh -c '"$tidy" -p "$build" --config="$config" --header-filter="^$PWD/src/" --quiet "$1" > "$perUnit/$(printf %s "$1" | tr / _).txt" 2>/dev/null' _ {} <<<"$units" || true
    cat "$perUnit"/*.txt | awk '
        function flush() { if (function_ != "") print score, penalised, location, function_; function_ = "" }
        / warning: function .* has cognitive complexity of / {
            flush()
            location = $1; sub(/:[0-9]+:$/, "", location)
            function_ = $0; sub(/^[^\047]*\047/, "", function_); sub(/\047.*$/, "", function_)
            score = $0; sub(/.* has cognitive complexity of /, "", score); sub(/ .*/, "", score)
            penalised = 0
            next
        }
        / warning: lambda has cognitive complexity / { flush(); next }
        / warning: / { flush(); next }
        /including nesting penalty of/ { if (function_ != "") penalised++ }
        END { flush() }' | sort -u -k3,3
    rm -r "$perUnit"
}

numberOf() { # "43.9k" -> 43900
    awk -v value="$1" 'BEGIN { scale = 1; if (value ~ /k$/) scale = 1e3; if (value ~ /M$/) scale = 1e6; if (value ~ /G$/) scale = 1e9; printf "%.10g", value * scale }'
}

tableRows() { # rows of the Markdown table under "## $2" in file $1, cells trimmed and separated by \x1f
    awk -v heading="## $2" '
        $0 == heading { inside = 1; next }
        /^## / { inside = 0 }
        inside && /^\| / && !/^\| *-+ *\|/ {
            gsub(/\\\|/, "\036")
            count = split($0, cells, "|")
            row = ""
            for (i = 2; i < count; ++i) {
                cell = cells[i]
                gsub(/^ +| +$|`/, "", cell)
                gsub(/\036/, "|", cell)
                row = row (i > 2 ? "\037" : "") cell
            }
            print row
        }' "$1" | tail -n +2
}

hotPaths() { # instructions per item (the harness's <CPU-I>) of every bm_, and the shipped binaries' sizes, each beside its recorded value when a baseline is given
    local release=$1 wasm=$2 baseline=$3
    local -A measured
    local -a measures
    local benchmark table column name instructions
    for benchmark in "$release"/test/benchmarks/bm_*; do
        table=$(BM_DIGITS=6 "$benchmark" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g')
        # the harness orders its columns as an unordered map does and leaves out the CPU counters without perf rights;
        # a row has one more field than the header, as it opens with a separator
        column=$(grep -m1 'benchmark:' <<<"$table" | awk -F'[┬│]' '{ for (i = 1; i <= NF; ++i) if ($i ~ /<CPU-I>/) print i + 1 }' || true)
        if [[ -z $column ]]; then
            echo "$benchmark: no <CPU-I> column (perf counters unavailable: /proc/sys/kernel/perf_event_paranoid)" >&2
            return 1
        fi
        while IFS=$'\t' read -r name instructions; do
            measures+=("$name")
            measured[$name]=$(numberOf "$instructions")
        done < <(grep '│.*PASS' <<<"$table" | awk -F'│' -v column="$column" '{ gsub(/^ +| +$/, "", $2); gsub(/ /, "", $column); print $2 "\t" $column }' || true)
    done
    measures+=("gr4-present (native, bytes)")
    measured["gr4-present (native, bytes)"]=$(stat -c %s "$release/src/app/gr4-present")
    if [[ -n $wasm ]]; then
        measures+=("index.wasm (bytes)")
        measured["index.wasm (bytes)"]=$(stat -c %s "$wasm/viewer-web/index.wasm")
    fi
    echo "hot paths"
    if [[ -z $baseline ]]; then
        for name in "${measures[@]}"; do
            echo "  $name: ${measured[$name]}"
        done
        return
    fi
    local measure recorded
    while IFS=$'\x1f' read -r measure recorded; do
        local now=${measured[$measure]:-}
        if [[ -z $now ]]; then
            echo "  $measure: not measured (recorded $recorded)"
            continue
        fi
        awk -v measure="$measure" -v now="$now" -v recorded="$recorded" 'BEGIN {
            change = recorded > 0 ? 100 * (now - recorded) / recorded : 0
            printf "  %s: %.10g (recorded %.10g, %+.1f %%)%s\n", measure, now, recorded, change, (change > 3 ? " REGRESSION: name it in the commit message" : "")
        }'
    done < <(tableRows "$baseline" "Hot paths")
}

seededFaults() { # applies each one-line fault of the table in turn, builds and runs its test, expects a failure and restores the source
    local table=$1 build=$2
    local caught=0 total=0
    local fault original seeded test
    while IFS=$'\x1f' read -r fault seededFile original seeded test; do
        total=$((total + 1))
        seededText=$(
            cat "$seededFile"
            printf x
        )
        seededText=${seededText%x}
        local without=${seededText//"$original"/}
        if ((${#seededText} - ${#without} != ${#original})); then
            echo "skipped: '$original' is not exactly once in $seededFile"
            continue
        fi
        # an interrupted build must not leave the fault in the tree; a trap returns to the loop unless it exits
        trap 'printf "%s" "$seededText" >"$seededFile"' EXIT
        trap 'exit 130' INT TERM
        printf '%s' "${seededText/"$original"/"$seeded"}" >"$seededFile"
        local result=MISSED
        if ! cmake --build "$build" --target "$test" >/dev/null 2>&1 || ! ctest --test-dir "$build" -R "^$test\$" >/dev/null 2>&1; then
            result=caught
            caught=$((caught + 1))
        fi
        printf '%s' "$seededText" >"$seededFile"
        trap - EXIT INT TERM
        echo "$result: $fault ($test)"
    done < <(tableRows "$table" "Seeded faults")
    cmake --build "$build" >/dev/null 2>&1
    echo "$caught of $total seeded faults caught"
    ((caught == total))
}


wrapperModes() {
    cat <<'EOF_MODES'

code-metrics.sh: the wrapper, adding what the binary cannot measure alone

usage: devtools/code-metrics.sh <clang-build-dir> [--cognitive] [--list] [--jobs <n>] [--summary | --table | --compare <report> [--table] [--list]]
       devtools/code-metrics.sh --hot-paths <release-build-dir> [<wasm-build-dir>] [--baseline <table.md>]
       devtools/code-metrics.sh --seeded-faults <table.md> <build-dir>
       devtools/code-metrics.sh --help [<build-dir>] | --figures [<build-dir>]
The build directory needs compile_commands.json and devtools/code-metrics (-DGR4_PRESENT_BUILD_DEVTOOLS=ON).

--cognitive       fills (2): runs clang-tidy's readability-function-cognitive-complexity over src/ (Campbell's score [2],
                  threshold 0, each increment described) and hands the scores to code-metrics, which sums them and
                  folds single-use callees for the split-invariant figure
--hot-paths       (5): instructions per item, the <CPU-I> column of every bm_<Unit> on GR4's benchmark harness
                  (needs perf counters: /proc/sys/kernel/perf_event_paranoid), and the shipped binaries' sizes;
                  with --baseline each beside its recorded value, a rise above 3 % marked for the commit message;
                  wall time is not compared: it varies with the machine, instructions do not
--seeded-faults   plants each one-line fault of a table ("## Seeded faults": fault | file | from | to | test) in
                  turn, builds and runs its named test, expects it to fail, restores the source, and counts the faults
                  caught: evidence that the tests can tell a wrong program from a right one
A table file holds Markdown tables under "## Hot paths" (measure | recorded) and "## Seeded faults".
EOF_MODES
}

codeMetricsBinary() { # the given build's binary, else the newest built one
    if [[ -n ${1:-} && -x $1/devtools/code-metrics ]]; then
        echo "$1/devtools/code-metrics"
        return
    fi
    find . -maxdepth 3 -path './cmake-build-*/devtools/code-metrics' -type f -executable -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-
}

case ${1:-} in
--help | -h | --figures)
    binary=$(codeMetricsBinary "${2:-}")
    if [[ -n $binary ]]; then
        "$binary" "$1"
    else
        echo "code-metrics is not built: configure a Clang build with -DGR4_PRESENT_BUILD_DEVTOOLS=ON and build devtools/code-metrics"
    fi
    [[ $1 == --figures ]] || wrapperModes
    exit
    ;;
--hot-paths)
    release=${2:?release build directory}
    shift 2
    wasm='' baseline=''
    while (($#)); do
        case $1 in
        --baseline)
            baseline=${2:?baseline table file}
            shift
            ;;
        *) wasm=$1 ;;
        esac
        shift
    done
    hotPaths "$release" "$wasm" "$baseline"
    exit
    ;;
--seeded-faults)
    seededFaults "${2:?seeded-fault table file}" "${3:?build directory}"
    exit
    ;;
esac
build=${1:?usage: devtools/code-metrics.sh <clang-build-dir> [options]; --help explains them}
shift
list=() cognitive=no jobs=4 modes=()
while (($#)); do
    case $1 in
    --list) list=(--list) ;;
    --cognitive) cognitive=yes ;;
    --jobs)
        jobs=$2
        shift
        ;;
    --summary | --table) modes+=("$1") ;;
    --compare)
        modes+=(--compare "${2:?earlier report}")
        shift
        ;;
    *)
        echo "unknown option $1" >&2
        exit 2
        ;;
    esac
    shift
done
scores=()
if [[ $cognitive == yes ]]; then
    scoresFile=$(mktemp)
    trap 'rm -f "$scoresFile"' EXIT
    cognitiveScores >"$scoresFile"
    scores=(--cognitive-scores "$scoresFile")
fi
"$build/devtools/code-metrics" "$build" --root "$PWD" --jobs "$jobs" "${list[@]}" "${scores[@]}" "${modes[@]}"
