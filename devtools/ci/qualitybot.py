#!/usr/bin/env python3
"""The quality bots' reports, made from the tools' own output: per bot one sticky PR comment, one status and one row of
figures for the history kept on the `ci-metrics` branch. Every comment leads with what the change does: a header with
the 🟢/🟡/🔴 verdict, the change and, in brackets, the absolute figure; a line with the commits compared and the reason;
a table of only what moved and one chart over main (Mermaid, which GitHub renders); the rest collapsed; a footer.
usage: devtools/ci/qualitybot.py <bot> --out <dir> [options]
writes <dir>/comment.md, <dir>/status.json and <dir>/figures.json; runs the same locally, where nothing is posted."""

import argparse
import csv
import datetime
import difflib
import hashlib
import io
import json
import os
import random
import time
import re
import shutil
import subprocess
import sys
from pathlib import Path

# the project's values; another repository changes these and keeps the rest
SETTINGS = {
    "historyRef": "origin/ci-metrics",
    "chartPoints": 30,
    "tableRows": 30,
    "diffLines": 300,
    "keptReports": 30,
    "formatters": {"clang-format": (".cpp", ".hpp", ".h", ".cc"), "cmake-format": ("CMakeLists.txt", ".cmake")},
    "namingArguments": ["--root", "src", "--root", "test", "--root", "devtools", "--exclude", "devtools/test/naming-check", "--exclude", "devtools/test/code-metrics"],
    # the sums CLAUDE.md (measured quality) names; the ratios move with any deletion, so they are shown, not counted
    "codeStyleCounted": ["cognitive complexity", "split-invariant complexity", "tokens + comment words", "repeated expressions", "nested ternaries", "write-only fields",
                         "unused functions", "flag arguments", "restating comments", "lines over 160 characters"],
    "codeStyleChart": "cognitive complexity",
    # this project's code by area; tests, fixtures and dependencies are not measured
    "coverageAreas": {"core": "src/core/", "app": "src/app/", "export": "src/export/", "devtools": "devtools/"},
    "coverageExcluded": r"(_deps|third_party|/test/|devtools/test/|cmake-build)",
    "patchMinimum": 80.0,
    "llvm": "-22",
    "hotPathBinaries": ["src/app/gr4-present"],
    "hotPathLimit": 3.0,  # % rise named in the commit message (CLAUDE.md, measured quality)
    "hotPathNoise": 0.1,  # % below which a change reads ±0; callgrind repeats to about 0.01 %
}

# `tool`: the files and SETTINGS that make a bot's figures, not how it reports them; a hash of their content is the
# tool version a history row records
BOTS = {
    "formatting": {"title": "Formatting", "context": "formatting/files", "tool": ["formatFiles.sh", ".clang-format", ".cmake-format.yaml"]},
    "naming": {"title": "Naming", "context": "naming/violations", "tool": ["devtools/naming-check.cpp", "devtools/NameSpelling.hpp", "devtools/Libclang.hpp", "namingArguments"]},
    "code-style": {"title": "Code Style", "context": "code-style/compare", "tool": ["devtools/code-metrics.cpp", "devtools/code-metrics.sh", "devtools/Figures.hpp", "devtools/Ccd.hpp", "devtools/Libclang.hpp"]},
    "coverage": {"title": "Coverage", "context": "coverage/patch", "tool": ["devtools/ci/flavours.json", "docker/Dockerfile", "coverageAreas", "coverageExcluded"]},
    "hot-paths": {"title": "Hot Paths", "context": "hot-paths/instructions", "tool": ["devtools/ci/flavours.json", "docker/Dockerfile", "hotPathBinaries"]},
    # main only: pull requests build WASM-Minimal, whose size does not compare
    "wasm-size": {"title": "WASM size", "context": "hot-paths/wasm-size", "tool": ["devtools/ci/flavours.json", "docker/Dockerfile"]},
}

# the tools are read beside this script, so a tree checked out elsewhere is measured with them
kRepository = Path(__file__).resolve().parents[2]


def run(command, check=True):
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode not in (0, 1):
        sys.exit(f"{' '.join(command)} failed ({result.returncode}):\n{result.stderr}")
    return result


GREEN, YELLOW, RED = "🟢", "🟡", "🔴"


def marker(bot):
    return f"<!-- gr4-present:{bot} -->"


def details(summary, body):
    return f"<details><summary>{summary}</summary>\n\n{body.strip()}\n\n</details>\n"


def fenced(text, language=""):
    return f"```{language}\n{text.rstrip()}\n```"


def plural(count, noun):
    return f"{count} {noun}{'' if count == 1 else 's'}"


def history(bot):
    """main's rows for `bot`, oldest first; empty before the first main commit has been recorded"""
    shown = run(["git", "show", f"{SETTINGS['historyRef']}:{bot}/history.csv"], check=False)
    if shown.returncode != 0:
        return []
    return list(csv.DictReader(io.StringIO(shown.stdout)))


def tool_version(bot):
    """a hash of the bot's tools' content: a change of method breaks the history, a squash merge does not"""
    content = lambda name: json.dumps(SETTINGS[name]).encode() if name in SETTINGS else (kRepository / name).read_bytes()
    return hashlib.sha1(b"".join(content(name) for name in BOTS[bot]["tool"])).hexdigest()[:10]


def baseline(bot):
    """the main commit this change lands on, and the bot's figures there. A pull request's checkout is its test merge,
    whose first parent is main; a main commit's base is its parent; elsewhere the merge-base with origin/main. Without a
    row of that commit the nearest recorded ancestor stands in, named in `note`; `row` stays empty when there is none or
    the bot's tools differ from the row's (figures of two methods do not compare). `rows`: main's history up to it."""
    head, *parents = run(["git", "rev-list", "--parents", "-n", "1", "HEAD"]).stdout.split()
    mergeBase = run(["git", "merge-base", "HEAD", "origin/main"], check=False).stdout.strip()
    commit = parents[0] if parents and (len(parents) > 1 or mergeBase in ("", head)) else mergeBase
    rows = history(bot)
    indexOf = {row["commit"]: k for k, row in enumerate(rows)}
    ancestors = run(["git", "rev-list", "--first-parent", "--max-count=500", commit], check=False).stdout.split() if commit else []
    recorded = next((sha for sha in ancestors if sha in indexOf), None)
    if recorded is None:
        return {"commit": commit, "row": {}, "rows": [], "note": "not recorded yet"}
    row = rows[indexOf[recorded]]
    note = "" if recorded == commit else f"not recorded; compared with `{recorded[:8]}`"
    if row["tool"] != tool_version(bot):
        return {"commit": commit, "row": {}, "rows": [], "note": f"tools changed since `{recorded[:8]}`: not comparable"}
    return {"commit": commit, "row": row, "rows": rows[: indexOf[recorded] + 1], "note": note}


def number(text):
    try:
        return float(str(text).replace("%", "").replace(",", "").strip())
    except ValueError:
        return None


def chart(title, rows, figure, current):
    """a line of `figure` over main's last commits, this run's value last; nothing until there is a value to draw"""
    points = [(row["commit"][:7], number(row.get(figure))) for row in rows[-SETTINGS["chartPoints"] :]]
    points = [(label, value) for label, value in points if value is not None]
    if current is not None:
        points.append(("this", current))
    if not points:
        return ""
    labels = ", ".join(f'"{label}"' for label, _ in points)
    values = ", ".join(f"{value:.15g}" for _, value in points)
    return fenced(f'xychart-beta\n    title "{title}"\n    x-axis [{labels}]\n    y-axis "{figure}"\n    line [{values}]', "mermaid") + "\n"


def links():
    """this run's head commit and the addresses every comment's footer points at"""
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text()) if os.environ.get("GITHUB_EVENT_PATH") else {}
    head = event.get("pull_request", {}).get("head", {}).get("sha") or os.environ.get("GITHUB_SHA") or run(["git", "rev-parse", "HEAD"]).stdout.strip()
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    runUrl = f"{server}/{repository}/actions/runs/{os.environ['GITHUB_RUN_ID']}" if repository and os.environ.get("GITHUB_RUN_ID") else ""
    owner, _, name = repository.partition("/")
    pages = f"https://{owner.lower()}.github.io/{name}" if name else ""
    return {"head": head, "run": runUrl, "pages": pages}


def write(out, bot, dot, headline, reason, body, legend, state, description, figures, base, extraLinks=()):
    """the shared frame: `### dot Title · headline`, then the commits compared and the reason for the verdict, the
    bot's body, and a footer with the legend and links (this run and its artefacts, the history; `extraLinks` first)"""
    out.mkdir(parents=True, exist_ok=True)
    where = links()
    onto = f" → main @ `{base['commit'][:8]}`" if base["commit"] else ""
    note = f" ({base['note']})" if base["note"] else ""
    header = f"{marker(bot)}\n### {dot + ' ' if dot else ''}{BOTS[bot]['title']} · {headline}\n`{where['head'][:8]}`{onto}{note}{f' · {reason}' if reason else ''}\n\n"
    footer = [legend, *extraLinks]
    if where["run"]:
        footer.append(f"[run and artefacts]({where['run']})")
    if where["pages"]:
        footer.append(f"[history]({where['pages']}/metrics/)")
    (out / "comment.md").write_text(header + body.strip() + "\n\n" + " · ".join(part for part in footer if part) + "\n")
    (out / "status.json").write_text(json.dumps({"context": BOTS[bot]["context"], "state": state, "description": description[:140]}))
    (out / "figures.json").write_text(json.dumps({"bot": bot, "tool": tool_version(bot), "figures": figures}))
    print((out / "comment.md").read_text())


def changed_lines(base):
    """{file: set of lines} added or changed since `base`"""
    changed, current = {}, None
    for line in run(["git", "diff", "-U0", "--no-color", f"{base}...HEAD"], check=False).stdout.splitlines():
        if line.startswith("+++ "):
            current = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("@@") and current:
            start, _, count = re.search(r"\+(\d+)(,(\d+))?", line).groups()
            first, length = int(start), int(count) if count is not None else 1
            changed.setdefault(current, set()).update(range(first, first + length))
    return changed


def formatting(arguments):
    base = baseline("formatting")
    formatterOf = lambda name: next((tool for tool, endings in SETTINGS["formatters"].items() if name.endswith(endings)), None)
    # what this change touches, read before the formatters touch anything
    touched = {name for name in run(["git", "diff", "--name-only", "--diff-filter=d", base["commit"]]).stdout.splitlines() if formatterOf(name)} if base["commit"] else set()
    # a file counts when the formatters change it, so a local run on a tree with edits reports only what they did
    modified = lambda: {name: Path(name).read_text() for name in run(["git", "diff", "--name-only"]).stdout.splitlines() if Path(name).exists()}
    committed = lambda name: run(["git", "show", f"HEAD:{name}"]).stdout
    before = modified()
    subprocess.run(["./formatFiles.sh"], check=True, capture_output=True)
    after = modified()
    contentBefore = lambda name: before[name] if name in before else committed(name)
    contentAfter = lambda name: after[name] if name in after else committed(name)
    files = sorted(name for name in before.keys() | after.keys() if contentBefore(name) != contentAfter(name))
    patch = "".join("".join(difflib.unified_diff(contentBefore(name).splitlines(True), contentAfter(name).splitlines(True), f"a/{name}", f"b/{name}")) for name in files)
    arguments.out.mkdir(parents=True, exist_ok=True)
    (arguments.out / "formatting.patch").write_text(patch)
    mine = [name for name in files if name in touched]
    elsewhere = [name for name in files if name not in touched]
    table = lambda names: "| file | formatter |\n| --- | --- |\n" + "".join(f"| `{name}` | {formatterOf(name) or '?'} |\n" for name in names) + "\n"
    body = ""
    if mine:
        body += "Run `./formatFiles.sh` and commit the result; the full patch is this run's `formatting-patch` artefact.\n\n" + table(mine)
    body += chart("files left unformatted on main", base["rows"], "files", len(files))
    if elsewhere:
        body += details(f"{plural(len(elsewhere), 'file')} this change does not touch, not counted", table(elsewhere))
    if files:
        diffLines = patch.splitlines()
        shown = "\n".join(diffLines[: SETTINGS["diffLines"]])
        more = f"\n\n… {len(diffLines) - SETTINGS['diffLines']} more lines in the artefact" if len(diffLines) > SETTINGS["diffLines"] else ""
        body += details("The changes the formatters make", fenced(shown, "diff") + more)
    # the formatters formatFiles.sh resolves: the newest clang-format the machine has, then cmake-format
    found = [shutil.which(tool) for tool in ("clang-format-18", "clang-format-20", "clang-format", "cmake-format")]
    clangFormat = next((tool for tool in found[:3] if tool), None)
    versions = "\n".join(run([tool, "--version"], check=False).stdout.strip() for tool in (clangFormat, found[3]) if tool)
    body += details("What is checked", f"`./formatFiles.sh` runs clang-format on C++ files and cmake-format on CMake files, with the repository's configuration.\n\n{fenced(versions)}")
    headline = f"{len(mine)} of {len(touched)} changed files need formatting ({len(files)} in all)"
    reason = "run `./formatFiles.sh`" if mine else ""
    write(arguments.out, "formatting", RED if mine else GREEN, headline, reason, body, f"{GREEN} the changed files are formatted · {RED} a changed file needs formatting",
          "failure" if mine else "success", f"{len(mine)} of {len(touched)} changed files to format; {len(files)} in all", {"files": len(files)}, base)


def naming(arguments):
    base = baseline("naming")
    tool = str(arguments.build / "devtools" / "naming-check")
    listing = run([tool, str(arguments.build), *SETTINGS["namingArguments"], "--jobs", "2", "--list"]).stdout
    counted = re.search(r"^translation units checked: (\d+) of (\d+)$", listing, flags=re.M)
    if counted is None:
        sys.exit(f"naming-check gave no unit count:\n{listing}")
    checked, units = (int(group) for group in counted.groups())
    lines = listing.splitlines()
    violations = []
    for line in lines[lines.index("violations") + 1 :] if "violations" in lines else []:
        found = re.match(r"^  (\S+) (.+?) '([^']*)' \((R\d+): (.*)\)(?: -> (\S+))?$", line)
        if found:
            where, kind, name, rule, _, proposal = found.groups()
            file, _, position = where.partition(":")
            violations.append({"where": where, "file": file, "line": int(position.split(":")[0]) if position else 0, "kind": kind, "name": name, "rule": rule, "proposal": f"`{proposal}`" if proposal else "—"})
    changed = changed_lines(base["commit"]) if base["commit"] else {}
    # a file's or directory's name (line 0) is this change's when the base has no such path
    isNew = lambda path: bool(base["commit"]) and run(["git", "cat-file", "-e", f"{base['commit']}:{path}"], check=False).returncode != 0
    mine = [violation for violation in violations if (isNew(violation["file"]) if violation["line"] == 0 else violation["line"] in changed.get(violation["file"], ()))]
    perRule = {}
    for violation in violations:
        perRule[violation["rule"]] = perRule.get(violation["rule"], 0) + 1
    table = lambda selected: "| where | rule | kind | name | proposed |\n| --- | --- | --- | --- | --- |\n" + "".join(
        f"| `{v['where']}` | {v['rule']} | {v['kind']} | `{v['name']}` | {v['proposal']} |\n" for v in selected[: SETTINGS["tableRows"]])
    body = table(mine) + "\n" if mine else ""
    body += chart("naming violations on main", base["rows"], "violations", len(violations))
    if violations:
        body += details(f"All {plural(len(violations), 'violation')}", table(violations) + (f"\n… {len(violations) - SETTINGS['tableRows']} more: `naming-check --list`" if len(violations) > SETTINGS["tableRows"] else ""))
        body += details("By rule", "".join(f"- {rule}: {count}\n" for rule, count in sorted(perRule.items(), key=lambda item: int(item[0][1:]))))
    body += details("The rules (CLAUDE.md §3)", fenced(run([tool, "--rules"]).stdout))
    body += details("Method", f"`naming-check {' '.join(SETTINGS['namingArguments'])}` on the Clang build's compile database; {checked} of {units} units checked; this change's lines: since {base['commit'][:8] or '—'}.")
    figures = {"violations": len(violations), "units": units, **{rule: perRule.get(rule, 0) for rule in (f"R{n}" for n in range(1, 13))}}
    headline = f"{plural(len(mine), 'violation')} on this change's lines ({len(violations)} in all)"
    write(arguments.out, "naming", RED if mine else GREEN, headline, "rename them" if mine else "", body, f"{GREEN} no violation on a changed line · {RED} violations on changed lines",
          "failure" if mine else "success", f"{len(mine)} on changed lines; {len(violations)} in {checked} units", figures, base)


def figure_lines(report):
    """`  name: value (meaning)` lines of a code-metrics --list report, as {name: value}"""
    return {name: value for name, value in re.findall(r"^  ([^:\n]+): ([0-9][0-9.,]*(?: %)?)", report, flags=re.M)}


def compared(text):
    """`(g) name before -> after change verdict` lines of a --compare report, each with the indented lines beneath it"""
    rows = []
    for line in text.splitlines():
        found = re.match(r"^\((\d)\) (.+?)\s{2,}(\S+(?: %)?|—)\s+->\s+(\S+(?: %)?)\s+(\S+ %|\+\d+|-\d+)?\s*(\S.*)$", line)
        if found:
            group, name, before, after, change, verdict = found.groups()
            rows.append({"group": group, "name": name.strip(), "before": before, "after": after, "change": change or "", "verdict": verdict.strip(), "places": []})
        elif rows and line.startswith("      "):
            rows[-1]["places"].append(line.strip())
    return rows


def code_style(arguments):
    base = baseline("code-style")
    script = [str(kRepository / "devtools" / "code-metrics.sh"), str(arguments.build), "--cognitive", "--jobs", "2"]
    report = run([*script, "--list"]).stdout
    unparsed = re.search(r"translation units with parse errors \(their figures are incomplete\): (\d+)", report)
    unparsedCount = int(unparsed.group(1)) if unparsed else 0
    arguments.out.mkdir(parents=True, exist_ok=True)
    (arguments.out / "code-style-list.txt").write_text(report)
    current = figure_lines(report)
    key = SETTINGS["codeStyleChart"]
    body = ""
    # main's report of the same commit as its figures, so what moved a figure is this change's
    baseReport = run(["git", "show", f"{SETTINGS['historyRef']}:code-style/reports/{base['row']['commit']}.txt"], check=False) if base["row"] else None
    worse = better = 0
    comparable = baseReport is not None and baseReport.returncode == 0
    if not comparable:
        body += chart(f"{key} on main", base["rows"], key, number(current.get(key)))
        body += details("All figures", "| figure | value |\n| --- | ---: |\n" + "".join(f"| {name} | {value} |\n" for name, value in current.items()))
        headline = f"{len(current)} figures measured ({key} {current.get(key, '—')})"
    else:
        (arguments.out / "base-list.txt").write_text(baseReport.stdout)
        comparison = compared(run([*script, "--compare", str(arguments.out / "base-list.txt"), "--list"]).stdout)
        isCounted = lambda row: row["name"] in SETTINGS["codeStyleCounted"]
        moved = sorted((row for row in comparison if row["verdict"] in ("worse", "better")), key=lambda row: (not isCounted(row), row["verdict"] != "worse"))
        worse = sum(isCounted(row) and row["verdict"] == "worse" for row in comparison)
        better = sum(isCounted(row) and row["verdict"] == "better" for row in comparison)
        dotOf = lambda row: ({"better": GREEN, "worse": YELLOW}[row["verdict"]] if isCounted(row) else "")

        def delta(row):
            before, after = number(row["before"]), number(row["after"])
            if before is None or after is None:
                return row["change"]
            absolute = f"{after - before:+.1f} pp" if row["after"].endswith("%") else f"{after - before:+,g}"
            return f"{absolute} ({row['change']})" if row["change"].endswith("%") and not row["after"].endswith("%") else absolute

        if moved:
            body += "| | figure | Δ | now |\n| --- | --- | ---: | ---: |\n"
            body += "".join(f"| {dotOf(row)} | {row['name']}{'' if isCounted(row) else ' *(not counted)*'} | {delta(row)} | {row['after']} |\n" for row in moved) + "\n"
        body += chart(f"{key} on main", base["rows"], key, number(current.get(key)))
        for row in moved:
            if row["places"]:
                body += details(f"{dotOf(row)} {row['name']}: what moved it", fenced("\n".join(row["places"])))
        allRows = "| figure | main | this | change | |\n| --- | ---: | ---: | ---: | --- |\n" + "".join(f"| ({row['group']}) {row['name']} | {row['before']} | {row['after']} | {row['change']} | {row['verdict']} |\n" for row in comparison)
        body += details("All figures", allRows)
        headline = (f"{plural(worse, 'figure')} worse, {better} better" if worse or better else "no counted figure moved") + f" ({key} {current.get(key, '—')})"
    body += details("Counted figures", ", ".join(SETTINGS["codeStyleCounted"]) + ": the sums CLAUDE.md (measured quality) names. Ratios and the other figures are shown, not counted: a deletion alone moves them.")
    body += details("How to read the figures", fenced(run([str(kRepository / "devtools" / "code-metrics.sh"), "--figures"], check=False).stdout) + "\n\nEach figure's card: `devtools/code-metrics.sh --help`.")
    dot = YELLOW if worse else GREEN if comparable else ""
    reason = "name the worse ones in the commit message (CLAUDE.md)" if worse else ""
    if unparsedCount:
        # figures over a partly parsed tree are wrong, not merely incomplete: say so first and turn red
        body = f"> [!CAUTION]\n> {plural(unparsedCount, 'translation unit')} could not be parsed, so the figures are incomplete.\n\n" + body
        dot, reason = RED, f"{plural(unparsedCount, 'unit')} not parsed"
    legend = f"{GREEN} no counted figure worse · {YELLOW} a counted figure worse: name it in the commit message · {RED} units not parsed"
    write(arguments.out, "code-style", dot, headline, reason, body, legend, "failure" if unparsedCount else "success", headline if not unparsedCount else f"{reason}; {headline}",
          {name: number(value) for name, value in current.items()}, base)


def instrumented(build):
    """the build's executables that carry a coverage mapping: tests, tools and the application alike"""
    objdump = f"llvm-objdump{SETTINGS['llvm']}"
    found = []
    for path in sorted(build.rglob("*")):
        if path.is_file() and path.stat().st_mode & 0o111 and "_deps" not in path.parts and "CMakeFiles" not in path.parts:
            headers = run([objdump, "--section-headers", str(path)], check=False).stdout
            if "__llvm_covmap" in headers:
                found.append(str(path))
    return found


def lcov_lines(lcov, root):
    """{file relative to the repository: {line: hits}} from an LCOV export"""
    lines, current = {}, None
    for line in lcov.splitlines():
        if line.startswith("SF:"):
            path = Path(line[3:])
            current = str(path.relative_to(root)) if path.is_absolute() and path.is_relative_to(root) else str(path)
            lines.setdefault(current, {})
        elif line.startswith("DA:") and current is not None:
            number_, hits = line[3:].split(",")[:2]
            lines[current][int(number_)] = lines[current].get(int(number_), 0) + int(hits)
    return lines


def ranges_of(numbers):
    """`L3, L5–L9` for a set of line numbers"""
    numbers, spans = sorted(numbers), []
    for number_ in numbers:
        if spans and number_ == spans[-1][1] + 1:
            spans[-1][1] = number_
        else:
            spans.append([number_, number_])
    return ", ".join(f"L{low}" if low == high else f"L{low}–L{high}" for low, high in spans)


def listed(byFile, cap=20):
    """one `- file L1, L3–L5` line per file, at most `cap` files"""
    entries = [f"- `{name}` {ranges_of(numbers)}\n" for name, numbers in sorted(byFile.items()) if numbers]
    return "".join(entries[:cap]) + (f"- + {len(entries) - cap} more files, all in the artefact\n" if len(entries) > cap else "")


def executable_looking(text):
    """whether a line without coverage data looks as if it should have some: not blank, a comment, a preprocessor line,
    a brace or a declaration (GR4's coverage.sh heuristic); such a line is mostly a template no test instantiates"""
    text = text.strip()
    if not text or text.startswith(("//", "/*", "*", "#")) or re.fullmatch(r"[{}();,]*", text) or re.match(r"^}\s*//", text) or text in ("public:", "private:", "protected:"):
        return False
    return not re.match(r"^(template\s*<|using |struct |class |enum |namespace |friend |static_assert|requires|concept |typename |(gr::)?(Annotated|A)<|(gr::)?(PortIn|PortOut|MsgPortIn|MsgPortOut)<|GR_MAKE_REFLECTABLE|GR_REGISTER_BLOCK)", text)


def report_totals(command):
    """the TOTAL row of `llvm-cov report`, as {regions, functions, instantiations, lines, branches: percent}; each group's
    columns are its count, its missed count and its share, and the share's name repeats (`Cover`, `Executed`)"""
    report = run(command).stdout.splitlines()
    header = next((line for line in report if line.startswith("Filename")), "")
    total = next((line for line in report if line.startswith("TOTAL")), "")
    names, values = re.split(r"\s{2,}", header.strip())[1:], total.split()[1:]
    shareOf = lambda group: number(values[names.index(group) + 2]) if group in names and names.index(group) + 2 < len(values) else None
    return {key: (round(shareOf(group), 1) if shareOf(group) is not None else None) for key, group in (("regions", "Regions"), ("functions", "Functions"), ("instantiations", "Instantiations"), ("lines", "Lines"), ("branches", "Branches"))}


def coverage(arguments):
    base = baseline("coverage")
    root = Path.cwd()
    profiles = sorted(str(path) for path in arguments.profiles.glob("*.profraw"))
    if not profiles:
        sys.exit(f"no profiles in {arguments.profiles}")
    merged = arguments.out / "coverage.profdata"
    arguments.out.mkdir(parents=True, exist_ok=True)
    run([f"llvm-profdata{SETTINGS['llvm']}", "merge", "-sparse", *profiles, "-o", str(merged)])
    objects = instrumented(arguments.build)
    llvmCov = f"llvm-cov{SETTINGS['llvm']}"
    selection = [f"-instr-profile={merged}", f"-ignore-filename-regex={SETTINGS['coverageExcluded']}", objects[0], *(item for path in objects[1:] for item in ("-object", path))]
    lcov = run([llvmCov, "export", "-format=lcov", *selection]).stdout
    (arguments.out / "coverage.lcov").write_text(lcov)
    run([llvmCov, "show", "-format=html", f"-output-dir={arguments.out / 'html'}", "-show-line-counts-or-regions", "-show-instantiation-summary", *selection])
    totals = report_totals([llvmCov, "report", "-show-instantiation-summary", *selection])
    lines = lcov_lines(lcov, root)
    tally = lambda selected: (sum(len(hits) for hits in selected), sum(sum(1 for count in hits.values() if count > 0) for hits in selected))
    percent = lambda found, hit: round(100.0 * hit / found, 1) if found else None
    dotOf = lambda share: GREEN if share >= 90.0 else YELLOW if share >= SETTINGS["patchMinimum"] else RED
    signed = lambda now, before: "n/a" if now is None or before is None else ("±0.0" if abs(now - before) < 0.05 else f"{now - before:+.1f}")
    areas = {area: tally([hits for name, hits in lines.items() if name.startswith(prefix)]) for area, prefix in SETTINGS["coverageAreas"].items()}
    found, hit = tally([hits for name, hits in lines.items() if any(name.startswith(prefix) for prefix in SETTINGS["coverageAreas"].values())])
    total = percent(found, hit)
    # this change's lines: covered, not covered, and without data though they look executable
    covered, uncovered, noData = {}, {}, {}
    for name, numbers in (changed_lines(base["commit"]).items() if base["commit"] else []):
        if not any(name.startswith(prefix) for prefix in SETTINGS["coverageAreas"].values()) or re.search(SETTINGS["coverageExcluded"], "/" + name):
            continue
        counts = lines.get(name, {})
        source = Path(name).read_text(errors="replace").splitlines() if Path(name).exists() else []
        for number_ in sorted(numbers):
            if number_ in counts:
                (covered if counts[number_] > 0 else uncovered).setdefault(name, set()).add(number_)
            elif name.endswith((".cpp", ".hpp", ".h", ".cc")) and number_ <= len(source) and executable_looking(source[number_ - 1]):
                noData.setdefault(name, set()).add(number_)
    count = lambda byFile, prefix="": sum(len(numbers) for name, numbers in byFile.items() if name.startswith(prefix))
    changedTotal, changedCovered = count(covered) + count(uncovered), count(covered)
    patch = percent(changedTotal, changedCovered)
    previous = base["row"]
    project = f"project {total} %{f' ({signed(total, number(previous.get('total')))})' if previous else ''}"
    headline = f"changed lines {patch} % ({changedCovered} of {changedTotal}) · {project}" if changedTotal else f"no changed executable lines · {project}"
    noGo = patch is not None and patch < SETTINGS["patchMinimum"]
    reason = f"{plural(count(uncovered), 'changed line')} no test runs" if noGo else ""

    def areaRow(area, prefix):
        changed = count(covered, prefix) + count(uncovered, prefix)
        share, areaShare = percent(changed, count(covered, prefix)), percent(*areas[area])
        return f"| `{prefix}` | {changed or '–'} | {f'{dotOf(share)} {share} %' if changed else '–'} | {signed(areaShare, number(previous.get(area)))} | {areaShare} % |\n"

    tableHead = "| area | changed lines | covered | area Δ | area |\n|---|---:|---:|---:|---:|\n"
    measuredAreas = [(area, prefix) for area, prefix in SETTINGS["coverageAreas"].items() if areas[area][0]]
    touchedAreas = [(area, prefix) for area, prefix in measuredAreas if count(covered, prefix) + count(uncovered, prefix)]
    body = tableHead + "".join(areaRow(area, prefix) for area, prefix in touchedAreas) + "\n" if touchedAreas else ""
    body += chart("line coverage of the project on main (%)", base["rows"], "total", total)
    if uncovered:
        body += details(f"⚠️ {count(uncovered)} changed lines not covered", listed(uncovered))
    if noData:
        body += details(f"❔ {count(noData)} changed lines without coverage data (e.g. templates no test instantiates, code only a browser compiles; not counted)", listed(noData))
    allAreas = tableHead + "".join(areaRow(area, prefix) for area, prefix in measuredAreas)
    allAreas += f"| **total** | {changedTotal or '–'} | {f'{dotOf(patch)} {patch} %' if changedTotal else '–'} | {signed(total, number(previous.get('total')))} | {total} % |\n\n"
    allAreas += "| | main | this | Δ |\n|---|---:|---:|---:|\n"
    for key in ("regions", "functions", "branches", "instantiations"):
        before, now = number(previous.get(key)), totals.get(key)
        label = "instantiations *(informational)*" if key == "instantiations" else key
        allAreas += f"| {label} | {f'{before} %' if before is not None else 'n/a'} | {f'{now} %' if now is not None else 'n/a'} | {signed(now, before)} |\n"
    body += details("All areas; regions, functions, branches, instantiations", allAreas)
    version = run([llvmCov, "--version"], check=False).stdout.strip().splitlines()
    body += details("Method", f"The native tests of the instrumented Clang build (`GR4_PRESENT_COVERAGE`), {len(profiles)} profiles over {len(objects)} binaries; "
                    f"tests, fixtures and dependencies excluded (`{SETTINGS['coverageExcluded']}`). Code only a browser runs (`#ifdef __EMSCRIPTEN__`) counts as not covered. "
                    f"Changed lines: since {base['commit'][:8] or '—'}, those the compiler made executable; no-go below {SETTINGS['patchMinimum']:g} %; the project's change is shown, not counted. "
                    f"{version[1].strip() if len(version) > 1 else ''}")
    description = f"{patch} % of {changedTotal} changed lines; {project}" if changedTotal else f"no changed executable lines; {project}"
    figures = {"total": total, "patch": patch, **{area: percent(areaFound, areaHit) for area, (areaFound, areaHit) in areas.items()}, **{key: totals.get(key) for key in ("regions", "functions", "branches", "instantiations")}}
    pages = links()["pages"]
    legend = f"{GREEN} ≥ 90 % · {YELLOW} ≥ {SETTINGS['patchMinimum']:g} % · {RED} < {SETTINGS['patchMinimum']:g} % of the changed lines"
    write(arguments.out, "coverage", dotOf(patch) if changedTotal else GREEN, headline, reason, body, legend, "failure" if noGo else "success", description, figures, base,
          extraLinks=[f"[latest main report]({pages}/metrics/coverage/)"] if pages else [])


def hot_paths(arguments):
    """instructions of each benchmark under callgrind (the whole run: its iterations dominate the setup) and the shipped
    binaries' sizes; hosted runners grant no hardware counters, and instruction counts do not vary with the machine"""
    base = baseline("hot-paths")
    figures = {}
    for benchmark in sorted((arguments.build / "test" / "benchmarks").glob("bm_*")):
        if not (benchmark.is_file() and benchmark.stat().st_mode & 0o111):
            continue
        out = arguments.out / f"{benchmark.name}.callgrind"
        arguments.out.mkdir(parents=True, exist_ok=True)
        run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={out}", str(benchmark)])
        total = re.search(r"^(?:summary|totals): (\d+)", out.read_text(errors="replace"), flags=re.M)
        if total:
            figures[benchmark.name] = int(total.group(1))
    for binary in SETTINGS["hotPathBinaries"]:
        path = arguments.build / binary
        if path.exists():
            figures[f"{Path(binary).name} bytes"] = path.stat().st_size
    limit, noise = SETTINGS["hotPathLimit"], SETTINGS["hotPathNoise"]
    changes = {name: (100.0 * (value - number(base["row"][name])) / number(base["row"][name]) if number(base["row"].get(name)) else None) for name, value in figures.items()}
    dotOf = lambda change: "" if change is None or abs(change) < noise else RED if change > limit else YELLOW if change > 0 else GREEN
    row = lambda name: f"| {dotOf(changes[name])} | {name} | {'±0' if changes[name] is not None and abs(changes[name]) < noise else f'{changes[name]:+.2f} %' if changes[name] is not None else 'n/a'} | {figures[name]:,} |\n"
    tableHead = "| | figure | Δ | now |\n|---|---|---:|---:|\n"
    moved = sorted((name for name in figures if dotOf(changes[name])), key=lambda name: -abs(changes[name]))
    above = sum(dotOf(changes[name]) == RED for name in figures)
    rising = sum(dotOf(changes[name]) == YELLOW for name in figures)
    body = tableHead + "".join(row(name) for name in moved) + "\n" if moved else ""
    charted = next((name for name in [*moved, *figures] if name.startswith("bm_")), None)
    body += chart(f"{charted} instructions on main", base["rows"], charted, figures[charted]) if charted else ""
    body += details("All figures", tableHead + "".join(row(name) for name in figures))
    body += details("Method", "Instructions: each `bm_` benchmark of the GCC15-Release build run once under `valgrind --tool=callgrind`, the whole process "
                           f"counted (its iterations dominate the set-up); reproducible to about 0.01 %, so a change below {noise:g} % reads ±0; wall time is not compared. "
                           f"Sizes: the release binaries as built. A rise above {limit:g} % is a no-go unless the commit message names it (CLAUDE.md, measured quality).")
    if not base["row"]:
        headline, dot = f"{plural(len(figures), 'figure')} measured", ""
    else:
        verdict = f"{above} up by more than {limit:g} %" if above else f"{rising} up by at most {limit:g} %" if rising else "no figure up"
        largest = f" · `{moved[0]}` {changes[moved[0]]:+.1f} %" if moved else ""
        headline, dot = verdict + largest, RED if above else YELLOW if rising else GREEN
    legend = f"{GREEN} down · {YELLOW} up by at most {limit:g} % · {RED} up by more than {limit:g} % · ±0 within {noise:g} %"
    write(arguments.out, "hot-paths", dot, headline, "name it in the commit message" if above else "", body, legend, "failure" if above else "success", headline, figures, base)


def wasm_size(arguments):
    sizes = {f"{path.name} bytes": path.stat().st_size for path in sorted((arguments.build / "viewer-web").glob("*.wasm"))}
    body = "".join(f"- `{name}`: {value:,}\n" for name, value in sizes.items())
    write(arguments.out, "wasm-size", "", f"{sum(sizes.values()):,} bytes", "", body, "", "success", "measured", sizes, {"commit": "", "note": ""})


def record(arguments):
    """adds this commit's row to `<bot>/history.csv` on the ci-metrics branch, in place of a row of the same commit and
    tool version (a rerun), with its `--report` as `<bot>/reports/<commit>.txt`, keeping the reports of the last rows
    only. Built with git plumbing from the branch's current head and pushed without force: a writer that lost the race
    to another bot fetches the new head and tries again."""
    data = json.loads(arguments.figures.read_text())
    bot, branch = data["bot"], SETTINGS["historyRef"].split("/", 1)[1]
    commit = os.environ.get("GITHUB_SHA") or run(["git", "rev-parse", "HEAD"]).stdout.strip()
    subject = run(["git", "log", "-1", "--format=%s", commit]).stdout
    pullRequest = re.search(r"\(#(\d+)\)\s*$", subject)
    row = {"commit": commit, "date": run(["git", "log", "-1", "--format=%cI", commit]).stdout.strip(), "pr": pullRequest.group(1) if pullRequest else "", "tool": data["tool"]}
    row.update({name: "" if value is None else value for name, value in data["figures"].items()})
    environment = {**os.environ, "GIT_AUTHOR_NAME": "gr4-present metrics", "GIT_AUTHOR_EMAIL": "41898282+github-actions[bot]@users.noreply.github.com"}
    environment.update(GIT_COMMITTER_NAME=environment["GIT_AUTHOR_NAME"], GIT_COMMITTER_EMAIL=environment["GIT_AUTHOR_EMAIL"])
    for attempt in range(8):
        fetched = run(["git", "fetch", "--no-tags", "origin", f"refs/heads/{branch}"], check=False).returncode == 0
        parent = run(["git", "rev-parse", "FETCH_HEAD"]).stdout.strip() if fetched else None
        old = run(["git", "show", f"{parent}:{bot}/history.csv"], check=False).stdout if parent else ""
        rows = list(csv.DictReader(io.StringIO(old)))
        rerun = next((k for k, kept in enumerate(rows) if kept["commit"] == commit and kept["tool"] == row["tool"]), None)
        if rerun is None:
            rows.append(row)
        else:
            rows[rerun] = row
        # commit order, also when an older commit is recorded late
        rows.sort(key=lambda kept: datetime.datetime.fromisoformat(kept["date"]))
        columns = list(dict.fromkeys(["commit", "date", "pr", "tool", *(name for kept in rows for name in kept)]))
        text = io.StringIO()
        writer = csv.DictWriter(text, fieldnames=columns, restval="", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        index = arguments.out / "record.index"
        arguments.out.mkdir(parents=True, exist_ok=True)
        indexed = {**environment, "GIT_INDEX_FILE": str(index)}
        index.unlink(missing_ok=True)
        if parent:
            subprocess.run(["git", "read-tree", parent], env=indexed, check=True)
        files = {f"{bot}/history.csv": text.getvalue()}
        if arguments.report:
            files[f"{bot}/reports/{commit}.txt"] = arguments.report.read_text()
            kept = {f"{bot}/reports/{kept['commit']}.txt" for kept in rows[-SETTINGS["keptReports"] :]}
            stored = run(["git", "ls-tree", "--name-only", f"{parent}:{bot}/reports"], check=False).stdout.split() if parent else []
            for name in stored:
                if f"{bot}/reports/{name}" not in kept:
                    subprocess.run(["git", "update-index", "--force-remove", f"{bot}/reports/{name}"], env=indexed, check=True)
        for path, content in files.items():
            blob = subprocess.run(["git", "hash-object", "-w", "--stdin"], input=content, capture_output=True, text=True, check=True).stdout.strip()
            subprocess.run(["git", "update-index", "--add", "--cacheinfo", f"100644,{blob},{path}"], env=indexed, check=True)
        tree = subprocess.run(["git", "write-tree"], env=indexed, capture_output=True, text=True, check=True).stdout.strip()
        message = f"{bot}: {commit[:7]}"
        created = subprocess.run(["git", "commit-tree", tree, *(["-p", parent] if parent else []), "-m", message], env=environment, capture_output=True, text=True, check=True).stdout.strip()
        if run(["git", "push", "origin", f"{created}:refs/heads/{branch}"], check=False).returncode == 0:
            print(f"recorded {bot} for {commit[:7]} as {created[:7]}")
            return
        delay = 2 + attempt * 3 + random.uniform(0, 3)
        print(f"{branch} moved on; trying again in {delay:.0f} s")
        time.sleep(delay)
    sys.exit(f"could not record {bot} for {commit[:7]}: {branch} kept moving")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bot", choices=sorted([*BOTS, "record"]))
    parser.add_argument("--out", type=Path, required=True, help="where comment.md, status.json and figures.json go")
    parser.add_argument("--build", type=Path, help="the build directory (naming, code-style: Clang; coverage, hot-paths, wasm-size: their flavour's)")
    parser.add_argument("--profiles", type=Path, help="the directory of .profraw files the tests wrote (coverage)")
    parser.add_argument("--figures", type=Path, help="a bot's figures.json to add to its history (record)")
    parser.add_argument("--report", type=Path, help="the bot's full report, kept beside the history for comparing (record)")
    arguments = parser.parse_args()
    {"formatting": formatting, "naming": naming, "code-style": code_style, "coverage": coverage, "hot-paths": hot_paths, "wasm-size": wasm_size, "record": record}[arguments.bot](arguments)


if __name__ == "__main__":
    main()
