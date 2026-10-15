// The quality bots' reports from the tools' own output: per bot one sticky PR comment (verdict, what this change moved,
// main's trend, the rest collapsed), one status and one row of figures for the history on the `ci-metrics` branch.
// usage: qualitybot <bot> --out <dir> [options]; writes comment.md, status.json and figures.csv, locally as in CI, where
// .github/actions/report posts them and .github/actions/record keeps main's figures

#include "QualityBot.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using namespace devtools::bot;

namespace {

// the project's values; another repository changes these and keeps the rest
struct Settings {
    std::string                                                   historyRef      = "origin/ci-metrics";
    std::string                                                   marker          = "gr4-present";
    std::string                                                   author          = "gr4-present metrics";
    std::size_t                                                   chartPoints     = 30;
    std::size_t                                                   tableRows       = 30;
    std::size_t                                                   diffLines       = 300;
    std::size_t                                                   keptReports     = 30;
    std::vector<std::pair<std::string, std::vector<std::string>>> formatters      = {{"clang-format", {".cpp", ".hpp", ".h", ".cc"}}, {"cmake-format", {"CMakeLists.txt", ".cmake"}}};
    std::vector<std::string>                                      namingArguments = {"--root", "src", "--root", "test", "--root", "devtools", "--exclude", "devtools/test/naming-check", "--exclude", "devtools/test/code-metrics"};
    // libclang parses two of the heaviest units within the 16 GB runner (as before the port)
    std::size_t parseJobs = 2;
    // the sums CLAUDE.md (measured quality) names; the ratios move with any deletion, so they are shown, not counted
    std::vector<std::string>                         codeStyleCounted = {"cognitive complexity", "split-invariant complexity", "tokens + comment words", "repeated expressions", "nested ternaries", "write-only fields", "unused functions", "flag arguments", "restating comments", "lines over 160 characters"};
    std::string                                      codeStyleChart   = "cognitive complexity";
    std::vector<std::pair<std::string, std::string>> coverageAreas    = {{"core", "src/core/"}, {"app", "src/app/"}, {"export", "src/export/"}, {"devtools", "devtools/"}};
    std::string                                      coverageExcluded = R"((_deps|third_party|/test/|devtools/test/|cmake-build))";
    double                                           patchMinimum     = 80.0;
    std::string                                      llvm             = "-22";
    std::vector<std::string>                         hotPathBinaries  = {"src/app/gr4-present"};
    // what the browser loads of the viewer, from the WASM-Release build; each also as the `.gz` serve.mjs sends
    std::vector<std::string> wasmFiles    = {"viewer-web/index.wasm", "viewer-web/index.js", "viewer-web/index.html"};
    double                   hotPathLimit = 3.0; // % rise named in the commit message (CLAUDE.md, measured quality)
    double                   hotPathNoise = 0.1; // % below which a change reads ±0; callgrind repeats to about 0.01 %
};
const Settings kSettings;

// the text of a setting named in a bot's tool list, so a change of it changes the tool version
std::string settingText(std::string_view name) {
    const auto joined = [](const std::vector<std::string>& values) {
        std::string text;
        for (const std::string& value : values) {
            text += value + '\n';
        }
        return text;
    };
    if (name == "namingArguments") {
        return joined(kSettings.namingArguments);
    }
    if (name == "coverageAreas") {
        std::string text;
        for (const auto& [area, prefix] : kSettings.coverageAreas) {
            text += std::format("{}={}\n", area, prefix);
        }
        return text;
    }
    if (name == "coverageExcluded") {
        return kSettings.coverageExcluded;
    }
    if (name == "hotPathBinaries") {
        return joined(kSettings.hotPathBinaries);
    }
    return {};
}

struct BotInfo {
    std::string_view              title;
    std::string_view              context;
    std::vector<std::string_view> tool; // the files and settings that make the figures, not how they are reported
};

const std::map<std::string_view, BotInfo> kBots = {
    {"formatting", {"Formatting", "formatting/files", {"formatFiles.sh", ".clang-format", ".cmake-format.yaml"}}},
    {"naming", {"Naming", "naming/violations", {"devtools/naming-check.cpp", "devtools/NameSpelling.hpp", "devtools/Libclang.hpp", "namingArguments"}}},
    {"code-style", {"Code Style", "code-style/compare", {"devtools/code-metrics.cpp", "devtools/code-metrics.sh", "devtools/Figures.hpp", "devtools/Ccd.hpp", "devtools/Libclang.hpp"}}},
    {"coverage", {"Coverage", "coverage/patch", {"devtools/ci/flavours.json", "docker/Dockerfile", "coverageAreas", "coverageExcluded"}}},
    {"hot-paths", {"Hot Paths", "hot-paths/instructions", {"devtools/ci/flavours.json", "docker/Dockerfile", "devtools/ci/hot-path-benchmarks.txt", "hotPathBinaries"}}},
    // main only: pull requests build WASM-Minimal, whose size does not compare
    {"wasm-size", {"WASM size", "hot-paths/wasm-size", {"devtools/ci/flavours.json", "docker/Dockerfile"}}},
};

// the tools are read beside this binary's sources, so a tree checked out elsewhere is measured with them
const fs::path kRepository = fs::path{__FILE__}.parent_path().parent_path();

struct Ran {
    int         status = 0;
    std::string out;
    std::string err;
};

std::string readFile(const fs::path& path) {
    std::ifstream     file{path, std::ios::binary};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void writeFile(const fs::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary};
    file << text;
}

// runs `command` without a shell: `input` on its stdin, `environment` (NAME=value) over this process's own; its output
// goes through files, so a large one cannot block it. A command that cannot start reads as status 127, as in a shell
Ran run(const std::vector<std::string>& command, std::string_view input = {}, const std::vector<std::string>& environment = {}) {
    static std::atomic<unsigned> counter{0U};
    const fs::path               scratch = fs::temp_directory_path() / std::format("qualitybot-{}-{}", ::getpid(), counter++);
    fs::create_directories(scratch);
    const std::string inPath  = (scratch / "in").string();
    const std::string outPath = (scratch / "out").string();
    const std::string errPath = (scratch / "err").string();
    writeFile(inPath, input);
    std::vector<std::string> variables = environment;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view variable{*entry};
        const auto             overridden = [variable](const std::string& assignment) { return variable.substr(0, variable.find('=') + 1) == std::string_view{assignment}.substr(0, assignment.find('=') + 1); };
        if (std::ranges::none_of(environment, overridden)) {
            variables.emplace_back(variable);
        }
    }
    std::vector<std::string> words = command;
    std::vector<char*>       argv;
    std::vector<char*>       envp;
    for (std::string& word : words) {
        argv.push_back(word.data());
    }
    for (std::string& variable : variables) {
        envp.push_back(variable.data());
    }
    argv.push_back(nullptr);
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, inPath.c_str(), O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    pid_t      process = -1;
    int        status  = 0;
    const bool ended   = !words.empty() && ::posix_spawnp(&process, argv.front(), &actions, nullptr, argv.data(), envp.data()) == 0 && ::waitpid(process, &status, 0) == process;
    posix_spawn_file_actions_destroy(&actions);
    Ran ran{.status = ended && WIFEXITED(status) ? WEXITSTATUS(status) : 127, .out = readFile(outPath), .err = readFile(errPath)};
    if (!ended) {
        ran.err += std::format("cannot start {}\n", words.empty() ? std::string{"an empty command"} : words.front());
    }
    fs::remove_all(scratch);
    return ran;
}

// as `run`, but a failure other than "found something" (exit 1) ends the bot
Ran checked(const std::vector<std::string>& command, std::string_view input = {}, const std::vector<std::string>& environment = {}) {
    Ran ran = run(command, input, environment);
    if (ran.status != 0 && ran.status != 1) {
        std::string joined;
        for (const std::string& word : command) {
            joined += word + ' ';
        }
        std::println(stderr, "{}failed ({}):\n{}", joined, ran.status, ran.err);
        std::exit(2);
    }
    return ran;
}

std::string environmentOr(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string{value} : std::string{fallback};
}

std::string marker(std::string_view bot) { return std::format("<!-- {}:{} -->", kSettings.marker, bot); }

std::vector<Row> history(std::string_view bot) {
    const Ran shown = run({"git", "show", std::format("{}:{}/history.csv", kSettings.historyRef, bot)});
    return shown.status == 0 ? csvTable(shown.out).rows : std::vector<Row>{};
}

// a hash of the bot's tools' content: a change of method breaks the history, a squash merge does not
std::string toolVersion(std::string_view bot) {
    std::string content;
    for (const std::string_view name : kBots.at(bot).tool) {
        const std::string setting = settingText(name);
        content += setting.empty() ? readFile(kRepository / name) : setting;
    }
    return trimmed(checked({"git", "hash-object", "--stdin"}, content).out).substr(0, 10);
}

// the main commit this change lands on: a pull request's checkout is its test merge, whose first parent is main; a
// main commit's base is its parent; elsewhere the merge-base with origin/main
Base baseline(std::string_view bot) {
    std::istringstream       parentsLine{checked({"git", "rev-list", "--parents", "-n", "1", "HEAD"}).out};
    std::vector<std::string> shas{std::istream_iterator<std::string>{parentsLine}, std::istream_iterator<std::string>{}};
    const std::string        head      = shas.empty() ? std::string{} : shas.front();
    const std::string        mergeBase = trimmed(run({"git", "merge-base", "HEAD", "origin/main"}).out);
    const bool               onMain    = shas.size() > 2U || mergeBase.empty() || mergeBase == head;
    const std::string        commit    = shas.size() > 1U && onMain ? shas[1] : mergeBase;
    std::vector<std::string> ancestors;
    if (!commit.empty()) {
        std::istringstream listed{run({"git", "rev-list", "--first-parent", "--max-count=500", commit}).out};
        ancestors.assign(std::istream_iterator<std::string>{listed}, std::istream_iterator<std::string>{});
    }
    return baseFrom(commit, ancestors, history(bot), toolVersion(bot));
}

struct Links {
    std::string head;
    std::string run;
    std::string pages;
};

// this run's head commit (a pull request's, not its test merge's) and the addresses every comment's footer points at
Links links() {
    std::istringstream       parentsLine{run({"git", "rev-list", "--parents", "-n", "1", "HEAD"}).out};
    std::vector<std::string> shas{std::istream_iterator<std::string>{parentsLine}, std::istream_iterator<std::string>{}};
    const bool               pullRequest = environmentOr("GITHUB_EVENT_NAME", "") == "pull_request";
    const std::string        head        = pullRequest && shas.size() == 3U ? shas[2] : environmentOr("GITHUB_SHA", shas.empty() ? std::string{} : shas.front());
    const std::string repository = environmentOr("GITHUB_REPOSITORY", "");
    const std::string        server      = environmentOr("GITHUB_SERVER_URL", "https://github.com");
    const std::string        runId       = environmentOr("GITHUB_RUN_ID", "");
    const std::size_t        slash       = repository.find('/');
    std::string              owner       = repository.substr(0, slash);
    std::ranges::transform(owner, owner.begin(), [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
    return {.head = head, .run = !repository.empty() && !runId.empty() ? std::format("{}/{}/actions/runs/{}", server, repository, runId) : std::string{}, .pages = slash != std::string::npos ? std::format("https://{}.github.io/{}", owner, repository.substr(slash + 1)) : std::string{}};
}

using Figures = std::vector<std::pair<std::string, std::optional<double>>>;

void writeReport(const fs::path& out, std::string_view bot, std::string_view dot, std::string_view headline, std::string_view reason, std::string_view body, std::string_view legend, std::string_view state, std::string_view description, const Figures& figures, const Base& base, const std::vector<std::string>& extraLinks = {}) {
    fs::create_directories(out);
    const Links              where  = links();
    const std::string        onto   = base.commit.empty() ? std::string{} : std::format(" → main @ `{}`", base.commit.substr(0, 8));
    const std::string        note   = base.note.empty() ? std::string{} : std::format(" ({})", base.note);
    const std::string        header = std::format("{}\n### {}{}{} · {}\n`{}`{}{}{}\n\n", marker(bot), dot, dot.empty() ? "" : " ", kBots.at(bot).title, headline, where.head.substr(0, 8), onto, note, reason.empty() ? std::string{} : std::format(" · {}", reason));
    std::vector<std::string> footer{std::string{legend}};
    footer.insert(footer.end(), extraLinks.begin(), extraLinks.end());
    if (!where.run.empty()) {
        footer.push_back(std::format("[run and artefacts]({})", where.run));
    }
    if (!where.pages.empty()) {
        footer.push_back(std::format("[history]({}/metrics/)", where.pages));
    }
    std::string footerLine;
    for (const std::string& part : footer) {
        if (!part.empty()) {
            footerLine += (footerLine.empty() ? "" : " · ") + part;
        }
    }
    const std::string comment = header + trimmed(body) + "\n\n" + footerLine + "\n";
    writeFile(out / "comment.md", comment);
    writeFile(out / "status.json", std::format(R"({{"context": {}, "state": {}, "description": {}}})", jsonQuoted(kBots.at(bot).context), jsonQuoted(state), jsonQuoted(cutUtf8(description, 140U))));
    Table table{.columns = {"bot", "tool"}, .rows = {Row{{"bot", std::string{bot}}, {"tool", toolVersion(bot)}}}};
    for (const auto& [name, value] : figures) {
        table.columns.push_back(name);
        table.rows.front()[name] = value ? shortest(*value) : std::string{};
    }
    writeFile(out / "figures.csv", csvText(table));
    std::print("{}", comment);
}

std::string valueOf(const Row& row, std::string_view column) {
    const auto found = row.find(column);
    return found == row.end() ? std::string{} : found->second;
}

std::string formatterOf(std::string_view name) {
    for (const auto& [tool, endings] : kSettings.formatters) {
        if (std::ranges::any_of(endings, [name](const std::string& ending) { return name.ends_with(ending); })) {
            return tool;
        }
    }
    return {};
}

std::vector<std::string> wordsOf(std::string_view text) {
    std::istringstream stream{std::string{text}};
    return {std::istream_iterator<std::string>{stream}, std::istream_iterator<std::string>{}};
}

struct Arguments {
    std::string bot;
    fs::path    out;
    fs::path    build;
    fs::path    profiles;
    fs::path    figures;
    fs::path    report;
};

void formatting(const Arguments& arguments) {
    const Base                  base    = baseline("formatting");
    const std::set<std::string> touched = base.commit.empty() ? std::set<std::string>{} : [&] {
        // what this change touches that a formatter handles, read before the formatters touch anything
        std::set<std::string> names;
        for (const std::string& name : linesOf(checked({"git", "diff", "--name-only", "--diff-filter=d", base.commit}).out)) {
            if (!formatterOf(name).empty()) {
                names.insert(name);
            }
        }
        return names;
    }();
    // a file counts when the formatters change it, so a local run on a tree with edits reports only what they did
    const auto modified = [] {
        std::map<std::string, std::string> contents;
        for (const std::string& name : linesOf(checked({"git", "diff", "--name-only"}).out)) {
            if (fs::is_regular_file(name)) {
                contents[name] = readFile(name);
            }
        }
        return contents;
    };
    const auto before    = modified();
    const Ran  formatted = run({"./formatFiles.sh"});
    const auto after     = modified();
    fs::create_directories(arguments.out);
    writeFile(arguments.out / "formatFiles.log", formatted.out + formatted.err);
    if (formatted.status != 0) {
        std::println(stderr, "./formatFiles.sh failed ({}):\n{}", formatted.status, formatted.err);
        std::exit(2);
    }
    const fs::path        scratch = arguments.out / "before";
    std::set<std::string> names;
    for (const auto& [name, _] : before) {
        names.insert(name);
    }
    for (const auto& [name, _] : after) {
        names.insert(name);
    }
    std::vector<std::string> files;
    std::string              patch;
    for (const std::string& name : names) {
        const std::string contentBefore = before.contains(name) ? before.at(name) : checked({"git", "show", std::format("HEAD:{}", name)}).out;
        const std::string contentAfter  = after.contains(name) ? after.at(name) : checked({"git", "show", std::format("HEAD:{}", name)}).out;
        if (contentBefore == contentAfter) {
            continue;
        }
        files.push_back(name);
        fs::create_directories((scratch / name).parent_path());
        writeFile(scratch / name, contentBefore);
        patch += run({"diff", "-u", "--label", "a/" + name, "--label", "b/" + name, (scratch / name).string(), name}).out;
    }
    fs::remove_all(scratch);
    writeFile(arguments.out / "formatting.patch", patch);
    std::vector<std::string> mine;
    std::vector<std::string> elsewhere;
    for (const std::string& name : files) {
        (touched.contains(name) ? mine : elsewhere).push_back(name);
    }
    const auto table = [](const std::vector<std::string>& selected) {
        std::string text = "| file | formatter |\n| --- | --- |\n";
        for (const std::string& name : selected) {
            const std::string tool = formatterOf(name);
            text += std::format("| `{}` | {} |\n", name, tool.empty() ? "?" : tool);
        }
        return text + "\n";
    };
    std::string body;
    if (!mine.empty()) {
        body += "Run `./formatFiles.sh` and commit the result; the full patch is this run's `formatting-patch` artefact.\n\n" + table(mine);
    }
    body += chart("files left unformatted on main", base.rows, "files", static_cast<double>(files.size()), kSettings.chartPoints);
    if (!elsewhere.empty()) {
        body += details(std::format("{} this change does not touch, not counted", plural(elsewhere.size(), "file")), table(std::vector<std::string>(elsewhere.begin(), elsewhere.begin() + static_cast<std::ptrdiff_t>(std::min(elsewhere.size(), kSettings.tableRows)))));
    }
    if (!files.empty()) {
        const std::vector<std::string> diffLines = linesOf(patch);
        std::string                    shown;
        for (const std::string& line : diffLines | std::views::take(kSettings.diffLines)) {
            shown += line + '\n';
        }
        const std::string more = diffLines.size() > kSettings.diffLines ? std::format("\n\n… {} more lines in the artefact", diffLines.size() - kSettings.diffLines) : std::string{};
        body += details("The changes the formatters make", fenced(shown, "diff") + more);
    }
    // the formatters formatFiles.sh resolves: the first clang-format of its list the machine has, then cmake-format
    std::string versions;
    for (const std::vector<std::string_view>& candidates : {std::vector<std::string_view>{"clang-format-18", "clang-format-20", "clang-format"}, std::vector<std::string_view>{"cmake-format"}}) {
        const auto found = std::ranges::find_if(candidates, [](std::string_view tool) { return run({"sh", "-c", std::format("command -v {}", tool)}).status == 0; });
        if (found != candidates.end()) {
            versions += trimmed(run({std::string{*found}, "--version"}).out) + '\n';
        }
    }
    body += details("What is checked", "`./formatFiles.sh` runs clang-format on C++ files and cmake-format on CMake files, with the repository's configuration.\n\n" + fenced(trimmed(versions)));
    const std::string headline = std::format("{} of {} changed files need formatting ({} in all)", mine.size(), touched.size(), files.size());
    writeReport(arguments.out, "formatting", mine.empty() ? kGreen : kRed, headline, mine.empty() ? "" : "run `./formatFiles.sh`", body, std::format("{} the changed files are formatted · {} a changed file needs formatting", kGreen, kRed), mine.empty() ? "success" : "failure", std::format("{} of {} changed files to format; {} in all", mine.size(), touched.size(), files.size()), {{"files", static_cast<double>(files.size())}}, base);
}

struct Violation {
    std::string where;
    std::string file;
    unsigned    line = 0;
    std::string kind;
    std::string name;
    std::string rule;
    std::string proposal;
};

void naming(const Arguments& arguments) {
    const Base               base = baseline("naming");
    const std::string        tool = (arguments.build / "devtools" / "naming-check").string();
    std::vector<std::string> command{tool, arguments.build.string()};
    command.insert(command.end(), kSettings.namingArguments.begin(), kSettings.namingArguments.end());
    command.insert(command.end(), {"--jobs", std::to_string(kSettings.parseJobs), "--list"});
    const std::vector<std::string> lines = linesOf(checked(command).out);
    const std::regex               countPattern{R"(^translation units checked: (\d+) of (\d+)$)"};
    std::smatch                    counted;
    const auto                     countLine = std::ranges::find_if(lines, [&](const std::string& line) { return std::regex_match(line, counted, countPattern); });
    if (countLine == lines.end()) {
        std::println(stderr, "naming-check gave no unit count");
        std::exit(2);
    }
    const std::string      checkedUnits = counted[1].str();
    const std::string      units        = counted[2].str();
    const std::regex       violationPattern{R"(^  (\S+) (.+?) '([^']*)' \((R\d+): (.*)\)(?: -> (\S+))?$)"};
    std::vector<Violation> violations;
    const auto             listed = std::ranges::find(lines, std::string{"violations"});
    for (const std::string& line : std::ranges::subrange(listed == lines.end() ? lines.end() : listed + 1, lines.end())) {
        std::smatch found;
        if (!std::regex_match(line, found, violationPattern)) {
            continue;
        }
        const std::string where  = found[1].str();
        const std::size_t colon  = where.find(':');
        unsigned          number = 0;
        if (colon != std::string::npos) {
            std::from_chars(where.data() + colon + 1, where.data() + where.size(), number);
        }
        violations.push_back({.where = where, .file = where.substr(0, colon), .line = number, .kind = found[2].str(), .name = found[3].str(), .rule = found[4].str(), .proposal = found[6].matched ? std::format("`{}`", found[6].str()) : "—"});
    }
    const auto changed = base.commit.empty() ? std::map<std::string, std::set<unsigned>, std::less<>>{} : changedLinesIn(run({"git", "diff", "-U0", "--no-color", base.commit + "...HEAD"}).out);
    // a file's or directory's name (line 0) is this change's when the base has no such path
    const auto                         isNew = [&base](const std::string& path) { return !base.commit.empty() && run({"git", "cat-file", "-e", std::format("{}:{}", base.commit, path)}).status != 0; };
    std::vector<Violation>             mine;
    std::map<std::string, std::size_t> perRule;
    for (const Violation& violation : violations) {
        ++perRule[violation.rule];
        const auto changedInFile = changed.find(violation.file);
        if (violation.line == 0U ? isNew(violation.file) : changedInFile != changed.end() && changedInFile->second.contains(violation.line)) {
            mine.push_back(violation);
        }
    }
    const auto table = [](const std::vector<Violation>& selected) {
        std::string text = "| where | rule | kind | name | proposed |\n| --- | --- | --- | --- | --- |\n";
        for (const Violation& v : selected | std::views::take(kSettings.tableRows)) {
            text += std::format("| `{}` | {} | {} | `{}` | {} |\n", v.where, v.rule, v.kind, v.name, v.proposal);
        }
        return text;
    };
    std::string body = mine.empty() ? std::string{} : table(mine) + "\n";
    body += chart("naming violations on main", base.rows, "violations", static_cast<double>(violations.size()), kSettings.chartPoints);
    const std::string        rulesText = checked({tool, "--rules"}).out;
    std::vector<std::string> rules;
    for (const std::string& line : linesOf(rulesText)) {
        std::smatch rule;
        if (std::regex_search(line, rule, std::regex{R"(^(R\d+) )"})) {
            rules.push_back(rule[1].str());
        }
    }
    if (!violations.empty()) {
        const std::string more = violations.size() > kSettings.tableRows ? std::format("\n… {} more: `naming-check --list`", violations.size() - kSettings.tableRows) : std::string{};
        body += details(std::format("All {}", plural(violations.size(), "violation")), table(violations) + more);
        std::string byRule;
        for (const std::string& rule : rules) {
            if (perRule.contains(rule)) {
                byRule += std::format("- {}: {}\n", rule, perRule.at(rule));
            }
        }
        body += details("By rule", byRule);
    }
    body += details("The rules (CLAUDE.md §3)", fenced(rulesText));
    std::string namingArguments;
    for (const std::string& argument : kSettings.namingArguments) {
        namingArguments += (namingArguments.empty() ? "" : " ") + argument;
    }
    body += details("Method", std::format("`naming-check {}` on the Clang build's compile database; {} of {} units checked; this change's lines: since {}.", namingArguments, checkedUnits, units, base.commit.empty() ? "—" : base.commit.substr(0, 8)));
    Figures figures{{"violations", static_cast<double>(violations.size())}, {"units", numberIn(units)}};
    for (const std::string& rule : rules) {
        figures.emplace_back(rule, static_cast<double>(perRule.contains(rule) ? perRule.at(rule) : 0U));
    }
    const std::string headline = std::format("{} on this change's lines ({} in all)", plural(mine.size(), "violation"), violations.size());
    writeReport(arguments.out, "naming", mine.empty() ? kGreen : kRed, headline, mine.empty() ? "" : "rename them", body, std::format("{} no violation on a changed line · {} violations on changed lines", kGreen, kRed), mine.empty() ? "success" : "failure", std::format("{} on changed lines; {} in {} units", mine.size(), violations.size(), checkedUnits), figures, base);
}

struct Compared {
    std::string              group;
    std::string              name;
    std::string              before;
    std::string              after;
    std::string              change;
    std::string              verdict;
    std::vector<std::string> places;
};

// `(g) name before -> after change verdict` lines of a --compare report, each with the indented lines beneath it
std::vector<Compared> compared(std::string_view text) {
    std::vector<Compared> rows;
    const std::regex      pattern{R"(^\((\d)\) (.+?)\s{2,}(\S+(?: %)?|—)\s+->\s+(\S+(?: %)?)\s+(\S+ %|\+\d+|-\d+)?\s*(\S.*)$)"};
    for (const std::string& line : linesOf(text)) {
        std::smatch found;
        if (std::regex_match(line, found, pattern)) {
            rows.push_back({.group = found[1].str(), .name = trimmed(found[2].str()), .before = found[3].str(), .after = found[4].str(), .change = found[5].matched ? found[5].str() : "", .verdict = trimmed(found[6].str()), .places = {}});
        } else if (!rows.empty() && line.starts_with("      ")) {
            rows.back().places.push_back(trimmed(line));
        }
    }
    return rows;
}

void codeStyle(const Arguments& arguments) {
    const Base                     base = baseline("code-style");
    const std::vector<std::string> script{(kRepository / "devtools" / "code-metrics.sh").string(), arguments.build.string(), "--cognitive", "--jobs", std::to_string(kSettings.parseJobs)};
    const auto                     withScript = [&script](std::vector<std::string> extra) {
        std::vector<std::string> command = script;
        command.insert(command.end(), extra.begin(), extra.end());
        return command;
    };
    const std::string report = checked(withScript({"--list"})).out;
    std::smatch       unparsed;
    const std::size_t unparsedCount = std::regex_search(report, unparsed, std::regex{R"(translation units with parse errors \(their figures are incomplete\): (\d+))"}) ? std::stoul(unparsed[1].str()) : 0U;
    fs::create_directories(arguments.out);
    writeFile(arguments.out / "code-style-list.txt", report);
    const auto current = figureLines(report);
    // a tool that did not run leaves no figures, or no cognitive figure: a report of them would enter main's history
    if (current.empty() || std::ranges::find(current, kSettings.codeStyleChart, &std::pair<std::string, std::string>::first) == current.end()) {
        std::println(stderr, "code-metrics.sh gave no '{}' figure:\n{}", kSettings.codeStyleChart, report);
        std::exit(2);
    }
    const auto currentOf = [&current](std::string_view name) {
        const auto found = std::ranges::find(current, name, &std::pair<std::string, std::string>::first);
        return found == current.end() ? std::string{"—"} : found->second;
    };
    const std::string key = kSettings.codeStyleChart;
    std::string       body;
    // main's report of the same commit as its figures, so what moved a figure is this change's
    const Ran   baseReport = base.row.empty() ? Ran{.status = 1, .out = {}, .err = {}} : run({"git", "show", std::format("{}:code-style/reports/{}.txt", kSettings.historyRef, valueOf(base.row, "commit"))});
    const bool  comparable = baseReport.status == 0;
    std::size_t worse      = 0;
    std::size_t better     = 0;
    std::string headline;
    if (!comparable) {
        body += chart(std::format("{} on main", key), base.rows, key, numberIn(currentOf(key)), kSettings.chartPoints);
        std::string all = "| figure | value |\n| --- | ---: |\n";
        for (const auto& [name, value] : current) {
            all += std::format("| {} | {} |\n", name, value);
        }
        body += details("All figures", all);
        headline = std::format("{} figures measured ({} {})", current.size(), key, currentOf(key));
    } else {
        writeFile(arguments.out / "base-list.txt", baseReport.out);
        const std::vector<Compared> comparison = compared(checked(withScript({"--compare", (arguments.out / "base-list.txt").string(), "--list"})).out);
        const auto                  isCounted  = [](const Compared& row) { return std::ranges::contains(kSettings.codeStyleCounted, row.name); };
        std::vector<Compared>       moved;
        for (const Compared& row : comparison) {
            if (row.verdict == "worse" || row.verdict == "better") {
                moved.push_back(row);
            }
            worse += isCounted(row) && row.verdict == "worse" ? 1U : 0U;
            better += isCounted(row) && row.verdict == "better" ? 1U : 0U;
        }
        std::ranges::stable_sort(moved, {}, [&isCounted](const Compared& row) { return std::pair{!isCounted(row), row.verdict != "worse"}; });
        const auto dotOf = [&isCounted](const Compared& row) { return isCounted(row) ? (row.verdict == "better" ? kGreen : kYellow) : std::string_view{}; };
        const auto delta = [](const Compared& row) {
            const std::optional<double> before = numberIn(row.before);
            const std::optional<double> after  = numberIn(row.after);
            if (!before || !after) {
                return row.change;
            }
            const bool        percent  = row.after.ends_with("%");
            const std::string absolute = percent ? std::format("{:+.1f} pp", *after - *before) : std::format("{}{}", *after - *before >= 0.0 ? "+" : "", std::abs(*after - *before - std::round(*after - *before)) < 1e-9 ? withThousands(std::llround(*after - *before)) : shortest(*after - *before));
            return row.change.ends_with("%") && !percent ? std::format("{} ({})", absolute, row.change) : absolute;
        };
        if (!moved.empty()) {
            body += "| | figure | Δ | now |\n| --- | --- | ---: | ---: |\n";
            for (const Compared& row : moved) {
                body += std::format("| {} | {}{} | {} | {} |\n", dotOf(row), row.name, isCounted(row) ? "" : " *(not counted)*", delta(row), row.after);
            }
            body += "\n";
        }
        body += chart(std::format("{} on main", key), base.rows, key, numberIn(currentOf(key)), kSettings.chartPoints);
        for (const Compared& row : moved) {
            if (!row.places.empty()) {
                std::string places;
                for (const std::string& place : row.places) {
                    places += place + '\n';
                }
                body += details(std::format("{} {}: what moved it", dotOf(row), row.name), fenced(places));
            }
        }
        std::string all = "| figure | main | this | change | |\n| --- | ---: | ---: | ---: | --- |\n";
        for (const Compared& row : comparison) {
            all += std::format("| ({}) {} | {} | {} | {} | {} |\n", row.group, row.name, row.before, row.after, row.change, row.verdict);
        }
        body += details("All figures", all);
        headline = std::format("{} ({} {})", worse != 0U || better != 0U ? std::format("{} worse, {} better", plural(worse, "figure"), better) : std::string{"no counted figure moved"}, key, currentOf(key));
    }
    std::string counted;
    for (const std::string& name : kSettings.codeStyleCounted) {
        counted += (counted.empty() ? "" : ", ") + name;
    }
    body += details("Counted figures", counted + ": the sums CLAUDE.md (measured quality) names. Ratios and the other figures are shown, not counted: a deletion alone moves them.");
    body += details("How to read the figures", fenced(run({(kRepository / "devtools" / "code-metrics.sh").string(), "--figures"}).out) + "\n\nEach figure's card: `devtools/code-metrics.sh --help`.");
    std::string_view dot    = worse != 0U ? kYellow : comparable ? kGreen : std::string_view{};
    std::string      reason = worse != 0U ? "name the worse ones in the commit message (CLAUDE.md)" : "";
    if (unparsedCount != 0U) {
        // figures over a partly parsed tree are wrong, not merely incomplete: say so first and turn red
        body   = std::format("> [!CAUTION]\n> {} could not be parsed, so the figures are incomplete.\n\n", plural(unparsedCount, "translation unit")) + body;
        dot    = kRed;
        reason = std::format("{} not parsed", plural(unparsedCount, "unit"));
    }
    Figures figures;
    for (const auto& [name, value] : current) {
        figures.emplace_back(name, numberIn(value));
    }
    writeReport(arguments.out, "code-style", dot, headline, reason, body, std::format("{} no counted figure worse · {} a counted figure worse: name it in the commit message · {} units not parsed", kGreen, kYellow, kRed), unparsedCount != 0U ? "failure" : "success", unparsedCount != 0U ? std::format("{}; {}", reason, headline) : headline, figures, base);
}

template<typename TTask>
void inParallel(std::size_t count, TTask&& task) {
    std::atomic<std::size_t>  next{0U};
    std::vector<std::jthread> workers;
    for (unsigned k = 0; k < std::max(1U, std::thread::hardware_concurrency()); ++k) {
        workers.emplace_back([&] {
            for (std::size_t index = next++; index < count; index = next++) {
                task(index);
            }
        });
    }
}

bool isOwnBuildFile(const fs::path& path) {
    return std::ranges::none_of(path, [](const fs::path& part) { return part == "_deps" || part == "CMakeFiles"; });
}

// the build's executables and shared libraries that carry a coverage mapping llvm-cov reads, and those it rejects:
// Clang 20 emits a mapping for some units that llvm-cov refuses, and one such object would abort the whole report
std::pair<std::vector<std::string>, std::vector<std::string>> instrumented(const fs::path& build, const fs::path& merged) {
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::recursive_directory_iterator(build)) {
        if (entry.is_regular_file() && (entry.status().permissions() & fs::perms::owner_exec) != fs::perms::none && isOwnBuildFile(entry.path())) {
            candidates.push_back(entry.path());
        }
    }
    std::ranges::sort(candidates);
    enum class Mapping { none, readable, rejected };
    std::vector<Mapping> verdicts(candidates.size(), Mapping::none);
    inParallel(candidates.size(), [&](std::size_t k) {
        if (!run({"llvm-objdump" + kSettings.llvm, "--section-headers", candidates[k].string()}).out.contains("__llvm_covmap")) {
            return;
        }
        const Ran report = run({"llvm-cov" + kSettings.llvm, "report", "-instr-profile=" + merged.string(), candidates[k].string()});
        if (report.status == 0) {
            verdicts[k] = Mapping::readable;
        } else if (!report.err.contains("no coverage data found")) {
            verdicts[k] = Mapping::rejected;
        }
    });
    std::pair<std::vector<std::string>, std::vector<std::string>> objects;
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        if (verdicts[k] == Mapping::readable) {
            objects.first.push_back(candidates[k].string());
        } else if (verdicts[k] == Mapping::rejected) {
            objects.second.push_back(candidates[k].string());
        }
    }
    return objects;
}

// {file relative to the repository: {line: hits}} from an LCOV export
std::map<std::string, std::map<unsigned, unsigned long long>, std::less<>> lcovLines(std::string_view lcov, const fs::path& root) {
    std::map<std::string, std::map<unsigned, unsigned long long>, std::less<>> lines;
    std::string                                                                current;
    for (const std::string& line : linesOf(lcov)) {
        if (line.starts_with("SF:")) {
            const fs::path path{line.substr(3)};
            const auto     relative = path.lexically_relative(root).string();
            current                 = path.is_absolute() && !relative.starts_with("..") ? relative : path.string();
            lines[current];
        } else if (line.starts_with("DA:") && !current.empty()) {
            unsigned           number = 0;
            unsigned long long hits   = 0;
            const char*        end    = line.data() + line.size();
            const auto         parsed = std::from_chars(line.data() + 3, end, number);
            std::from_chars(parsed.ptr + 1, end, hits);
            lines[current][number] += hits;
        }
    }
    return lines;
}

std::string listedLines(const std::map<std::string, std::set<unsigned>, std::less<>>& byFile, std::size_t cap = 20) {
    std::vector<std::string> entries;
    for (const auto& [name, numbers] : byFile) {
        if (!numbers.empty()) {
            entries.push_back(std::format("- `{}` {}\n", name, rangesOf(numbers)));
        }
    }
    std::string text;
    for (const std::string& entry : entries | std::views::take(cap)) {
        text += entry;
    }
    return entries.size() > cap ? text + std::format("- + {} more files, all in the artefact\n", entries.size() - cap) : text;
}

// whether a line without coverage data looks as if it should have some: not blank, a comment, a preprocessor line, a
// brace or a declaration; such a line is mostly a template no test instantiates
bool executableLooking(std::string_view raw) {
    const std::string text = trimmed(raw);
    if (text.empty() || text.starts_with("//") || text.starts_with("/*") || text.starts_with("*") || text.starts_with("#") || std::regex_match(text, std::regex{R"([{}();,]*)"}) || std::regex_search(text, std::regex{R"(^\}\s*//)"}) || text == "public:" || text == "private:" || text == "protected:") {
        return false;
    }
    return !std::regex_search(text, std::regex{R"(^(template\s*<|using |struct |class |enum |namespace |friend |static_assert|requires|concept |typename |(gr::)?(Annotated|A)<|(gr::)?(PortIn|PortOut|MsgPortIn|MsgPortOut)<|GR_MAKE_REFLECTABLE|GR_REGISTER_BLOCK))"});
}

// the TOTAL row of `llvm-cov report`, as {regions, functions, instantiations, lines, branches: percent}; each group's
// columns are its count, its missed count and its share
std::map<std::string, std::optional<double>> reportTotals(const std::vector<std::string>& command) {
    const std::vector<std::string> report = linesOf(checked(command).out);
    const auto                     header = std::ranges::find_if(report, [](const std::string& line) { return line.starts_with("Filename"); });
    const auto                     total  = std::ranges::find_if(report, [](const std::string& line) { return line.starts_with("TOTAL"); });
    std::vector<std::string>       names;
    if (header != report.end()) {
        const std::string          trimmedHeader = trimmed(*header);
        const std::regex           gap{R"(\s{2,})"};
        std::sregex_token_iterator split{trimmedHeader.begin(), trimmedHeader.end(), gap, -1};
        names.assign(std::next(split), std::sregex_token_iterator{});
    }
    std::vector<std::string> values = total == report.end() ? std::vector<std::string>{} : wordsOf(*total);
    if (!values.empty()) {
        values.erase(values.begin());
    }
    std::map<std::string, std::optional<double>> totals;
    for (const auto& [key, group] : {std::pair{"regions", "Regions"}, {"functions", "Functions"}, {"instantiations", "Instantiations"}, {"lines", "Lines"}, {"branches", "Branches"}}) {
        const auto        found = std::ranges::find(names, std::string{group});
        const std::size_t index = static_cast<std::size_t>(found - names.begin()) + 2U;
        const auto        share = found != names.end() && index < values.size() ? numberIn(values[index]) : std::nullopt;
        totals[key]             = share ? std::optional<double>{std::round(*share * 10.0) / 10.0} : std::nullopt;
    }
    return totals;
}

std::optional<double> percent(std::size_t found, std::size_t hit) { return found == 0U ? std::nullopt : std::optional<double>{std::round(1000.0 * static_cast<double>(hit) / static_cast<double>(found)) / 10.0}; }

std::string shownPercent(std::optional<double> share) { return share ? std::format("{:.1f}", *share) : "None"; }

void coverage(const Arguments& arguments) {
    const Base               base = baseline("coverage");
    const fs::path           root = fs::current_path();
    std::vector<std::string> profiles;
    for (const auto& entry : fs::directory_iterator(arguments.profiles)) {
        if (entry.path().extension() == ".profraw") {
            profiles.push_back(entry.path().string());
        }
    }
    std::ranges::sort(profiles);
    if (profiles.empty()) {
        std::println(stderr, "no profiles in {}", arguments.profiles.string());
        std::exit(2);
    }
    fs::create_directories(arguments.out);
    const fs::path           merged = arguments.out / "coverage.profdata";
    std::vector<std::string> merge{"llvm-profdata" + kSettings.llvm, "merge", "-sparse", "-failure-mode=all"};
    merge.insert(merge.end(), profiles.begin(), profiles.end());
    merge.insert(merge.end(), {"-o", merged.string()});
    checked(merge);
    const auto [objects, rejected] = instrumented(arguments.build, merged);
    std::string skipped;
    for (const std::string& path : rejected) {
        skipped += path + '\n';
    }
    writeFile(arguments.out / "coverage-skipped.txt", skipped);
    if (objects.empty()) {
        std::println(stderr, "no binary with a readable coverage mapping in {}", arguments.build.string());
        std::exit(2);
    }
    const std::string        llvmCov = "llvm-cov" + kSettings.llvm;
    std::vector<std::string> selection{"-instr-profile=" + merged.string(), "-ignore-filename-regex=" + kSettings.coverageExcluded, objects.front()};
    for (const std::string& path : objects | std::views::drop(1)) {
        selection.insert(selection.end(), {"-object", path});
    }
    const auto llvmCovWith = [&](std::vector<std::string> verb) {
        verb.insert(verb.begin(), llvmCov);
        verb.insert(verb.end(), selection.begin(), selection.end());
        return verb;
    };
    const std::string lcov = checked(llvmCovWith({"export", "-format=lcov"})).out;
    writeFile(arguments.out / "coverage.lcov", lcov);
    checked(llvmCovWith({"show", "-format=html", "-output-dir=" + (arguments.out / "html").string(), "-show-instantiations=false"}));
    // Sonar reads llvm-cov's text report (sonar.cfamily.llvm-cov.reportPath)
    const auto totals = reportTotals(llvmCovWith({"report", "-show-instantiation-summary"}));
    const auto lines  = lcovLines(lcov, root);
    const auto inArea = [](std::string_view name) { return std::ranges::any_of(kSettings.coverageAreas, [name](const auto& area) { return name.starts_with(area.second); }); };
    const auto tally  = [&lines](auto&& wanted) {
        std::size_t found = 0;
        std::size_t hit   = 0;
        for (const auto& [name, hits] : lines) {
            if (wanted(name)) {
                found += hits.size();
                hit += static_cast<std::size_t>(std::ranges::count_if(hits, [](const auto& entry) { return entry.second > 0U; }));
            }
        }
        return std::pair{found, hit};
    };
    std::map<std::string, std::pair<std::size_t, std::size_t>> areas;
    for (const auto& [area, prefix] : kSettings.coverageAreas) {
        areas[area] = tally([&prefix](std::string_view name) { return name.starts_with(prefix); });
    }
    const auto [foundAll, hitAll]     = tally(inArea);
    const std::optional<double> total = percent(foundAll, hitAll);
    using ByFile                      = std::map<std::string, std::set<unsigned>, std::less<>>;
    ByFile           covered;
    ByFile           uncovered;
    ByFile           noData;
    const std::regex excluded{kSettings.coverageExcluded};
    const ByFile     changed = base.commit.empty() ? ByFile{} : changedLinesIn(run({"git", "diff", "-U0", "--no-color", base.commit + "...HEAD"}).out);
    for (const auto& [name, numbers] : changed) {
        if (!inArea(name) || std::regex_search("/" + name, excluded)) {
            continue;
        }
        const auto                     counts = lines.find(name);
        const std::vector<std::string> source = fs::exists(name) ? linesOf(readFile(name)) : std::vector<std::string>{};
        const bool                     isCpp  = name.ends_with(".cpp") || name.ends_with(".hpp") || name.ends_with(".h") || name.ends_with(".cc");
        for (const unsigned number : numbers) {
            if (counts != lines.end() && counts->second.contains(number)) {
                (counts->second.at(number) > 0U ? covered : uncovered)[name].insert(number);
            } else if (isCpp && number >= 1U && number <= source.size() && executableLooking(source[number - 1U])) {
                noData[name].insert(number);
            }
        }
    }
    const auto count = [](const ByFile& byFile, std::string_view prefix = "") {
        std::size_t counted = 0;
        for (const auto& [name, numbers] : byFile) {
            counted += name.starts_with(prefix) ? numbers.size() : 0U;
        }
        return counted;
    };
    const std::size_t           changedTotal   = count(covered) + count(uncovered);
    const std::size_t           changedCovered = count(covered);
    const std::optional<double> patch          = percent(changedTotal, changedCovered);
    const Row&                  previous       = base.row;
    const auto                  previousOf     = [&previous](std::string_view key) { return numberIn(valueOf(previous, key)); };
    const auto                  dotOf          = [](double share) { return share >= 90.0 ? kGreen : share >= kSettings.patchMinimum ? kYellow : kRed; };
    const std::string           project        = std::format("project {} %{}", shownPercent(total), previous.empty() ? std::string{} : std::format(" ({})", signedChange(total, previousOf("total"))));
    const std::string           headline       = changedTotal != 0U ? std::format("changed lines {} % ({} of {}) · {}", shownPercent(patch), changedCovered, changedTotal, project) : std::format("no changed executable lines · {}", project);
    const bool                  noGo           = patch && *patch < kSettings.patchMinimum;
    const std::string           reason         = noGo ? std::format("{} no test runs", plural(count(uncovered), "changed line")) : std::string{};
    const auto                  areaRow        = [&](const std::string& area, const std::string& prefix) {
        const std::size_t           changedHere = count(covered, prefix) + count(uncovered, prefix);
        const std::optional<double> share       = percent(changedHere, count(covered, prefix));
        const std::optional<double> areaShare   = percent(areas.at(area).first, areas.at(area).second);
        return std::format("| `{}` | {} | {} | {} | {} % |\n", prefix, changedHere != 0U ? std::to_string(changedHere) : "–", changedHere != 0U ? std::format("{} {} %", dotOf(*share), shownPercent(share)) : "–", signedChange(areaShare, previousOf(area)), shownPercent(areaShare));
    };
    const std::string tableHead = "| area | changed lines | covered | area Δ | area |\n|---|---:|---:|---:|---:|\n";
    std::string       touchedRows;
    std::string       allRows;
    for (const auto& [area, prefix] : kSettings.coverageAreas) {
        if (areas.at(area).first == 0U) {
            continue;
        }
        allRows += areaRow(area, prefix);
        touchedRows += count(covered, prefix) + count(uncovered, prefix) != 0U ? areaRow(area, prefix) : std::string{};
    }
    std::string body = touchedRows.empty() ? std::string{} : tableHead + touchedRows + "\n";
    body += chart("line coverage of the project on main (%)", base.rows, "total", total, kSettings.chartPoints);
    if (!uncovered.empty()) {
        body += details(std::format("⚠️ {} changed lines not covered", count(uncovered)), listedLines(uncovered));
    }
    if (!noData.empty()) {
        body += details(std::format("❔ {} changed lines without coverage data (e.g. templates no test instantiates, code only a browser compiles; not counted)", count(noData)), listedLines(noData));
    }
    std::string allAreas = tableHead + allRows;
    allAreas += std::format("| **total** | {} | {} | {} | {} % |\n\n", changedTotal != 0U ? std::to_string(changedTotal) : "–", changedTotal != 0U ? std::format("{} {} %", dotOf(*patch), shownPercent(patch)) : "–", signedChange(total, previousOf("total")), shownPercent(total));
    allAreas += "| | main | this | Δ |\n|---|---:|---:|---:|\n";
    for (const std::string key : {"regions", "functions", "branches", "instantiations"}) {
        const std::optional<double> before = previousOf(key);
        const std::optional<double> now    = totals.at(key);
        allAreas += std::format("| {} | {} | {} | {} |\n", key == "instantiations" ? "instantiations *(informational)*" : key, before ? std::format("{:.1f} %", *before) : "n/a", now ? std::format("{:.1f} %", *now) : "n/a", signedChange(now, before));
    }
    body += details("All areas; regions, functions, branches, instantiations", allAreas);
    const std::vector<std::string> versionLines = linesOf(run({llvmCov, "--version"}).out);
    const auto                     versionLine  = std::ranges::find_if(versionLines, [](const std::string& line) { return line.contains("version"); });
    const std::string              version      = versionLine == versionLines.end() ? std::string{} : trimmed(*versionLine);
    body += details("Method", std::format("The native tests of the instrumented Clang build (`GR4_PRESENT_COVERAGE`), {} profiles over {} binaries{}; tests, fixtures and dependencies excluded (`{}`). Code only a browser runs (`#ifdef __EMSCRIPTEN__`) counts as not covered. Changed lines: since {}, those the compiler made executable; no-go below {} %; the project's change is shown, not counted. {}", profiles.size(), objects.size(), rejected.empty() ? std::string{} : std::format(", {} more that llvm-cov rejects left out (`coverage-skipped.txt`)", rejected.size()), kSettings.coverageExcluded, base.commit.empty() ? "—" : base.commit.substr(0, 8), shortest(kSettings.patchMinimum), version));
    const std::string description = changedTotal != 0U ? std::format("{} % of {} changed lines; {}", shownPercent(patch), changedTotal, project) : std::format("no changed executable lines; {}", project);
    // `lines` is llvm-cov's share over every file not excluded, the figure coverage-data recorded; `total` the areas' only
    Figures figures{{"total", total}, {"patch", patch}};
    for (const auto& [area, prefix] : kSettings.coverageAreas) {
        figures.emplace_back(area, percent(areas.at(area).first, areas.at(area).second));
    }
    for (const std::string key : {"lines", "regions", "functions", "branches", "instantiations"}) {
        figures.emplace_back(key, totals.at(key));
    }
    figures.emplace_back("binaries", static_cast<double>(objects.size()));
    figures.emplace_back("skipped", static_cast<double>(rejected.size()));
    const std::string pages  = links().pages;
    const std::string legend = std::format("{} ≥ 90 % · {} ≥ {} % · {} < {} % of the changed lines", kGreen, kYellow, shortest(kSettings.patchMinimum), kRed, shortest(kSettings.patchMinimum));
    writeReport(arguments.out, "coverage", changedTotal != 0U ? dotOf(*patch) : kGreen, headline, reason, body, legend, noGo ? "failure" : "success", description, figures, base, pages.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::format("[latest main report]({}/metrics/coverage/)", pages)});
}

// instructions of each listed benchmark under callgrind (the whole run: its iterations dominate the set-up) and the
// shipped binaries' sizes; hosted runners grant no hardware counters, and instruction counts do not vary with the machine
void hotPaths(const Arguments& arguments) {
    const Base                     base   = baseline("hot-paths");
    const std::vector<std::string> listed = wordsOf(readFile(kRepository / "devtools" / "ci" / "hot-path-benchmarks.txt"));
    if (listed.empty()) {
        std::println(stderr, "devtools/ci/hot-path-benchmarks.txt is missing or names no benchmark");
        std::exit(2);
    }
    std::vector<fs::path> benchmarks;
    for (const auto& entry : fs::recursive_directory_iterator(arguments.build)) {
        if (entry.is_regular_file() && std::ranges::contains(listed, entry.path().filename().string()) && (entry.status().permissions() & fs::perms::owner_exec) != fs::perms::none && isOwnBuildFile(entry.path())) {
            benchmarks.push_back(entry.path());
        }
    }
    std::ranges::sort(benchmarks, {}, [](const fs::path& path) { return path.filename(); });
    for (const std::string& name : listed) {
        if (std::ranges::none_of(benchmarks, [&name](const fs::path& path) { return path.filename() == name; })) {
            std::println(stderr, "{} is listed but not built in {}", name, arguments.build.string());
            std::exit(2);
        }
    }
    fs::create_directories(arguments.out);
    Figures figures;
    for (const fs::path& benchmark : benchmarks) {
        const fs::path out = arguments.out / (benchmark.filename().string() + ".callgrind");
        run({"valgrind", "--tool=callgrind", "--callgrind-out-file=" + out.string(), benchmark.string()});
        const std::vector<std::string> profile = linesOf(readFile(out));
        const auto                     total   = std::ranges::find_if(profile, [](const std::string& line) { return line.starts_with("summary: ") || line.starts_with("totals: "); });
        if (total == profile.end()) {
            std::println(stderr, "callgrind counted no instructions of {} (is valgrind installed?)", benchmark.string());
            std::exit(2);
        }
        figures.emplace_back(benchmark.filename().string(), numberIn(total->substr(total->find(' ') + 1)));
    }
    for (const std::string& binary : kSettings.hotPathBinaries) {
        if (fs::exists(arguments.build / binary)) {
            figures.emplace_back(fs::path{binary}.filename().string() + " bytes", static_cast<double>(fs::file_size(arguments.build / binary)));
        }
    }
    const double                                 limit = kSettings.hotPathLimit;
    const double                                 noise = kSettings.hotPathNoise;
    std::map<std::string, std::optional<double>> changes;
    for (const auto& [name, value] : figures) {
        const std::optional<double> recorded = numberIn(valueOf(base.row, name));
        changes[name]                        = recorded && *recorded != 0.0 && value ? std::optional<double>{100.0 * (*value - *recorded) / *recorded} : std::nullopt;
    }
    const auto dotOf = [&](std::optional<double> change) { return !change || std::abs(*change) < noise ? std::string_view{} : *change > limit ? kRed : *change > 0.0 ? kYellow : kGreen; };
    const auto row   = [&](const std::string& name, double value) {
        const std::optional<double> change = changes.at(name);
        return std::format("| {} | {} | {} | {} |\n", dotOf(change), name, !change ? std::string{"n/a"} : std::abs(*change) < noise ? std::string{"±0"} : std::format("{:+.2f} %", *change), withThousands(std::llround(value)));
    };
    const std::string        tableHead = "| | figure | Δ | now |\n|---|---|---:|---:|\n";
    std::vector<std::string> moved;
    std::size_t              above  = 0;
    std::size_t              rising = 0;
    for (const auto& [name, value] : figures) {
        const std::string_view dot = dotOf(changes.at(name));
        if (!dot.empty()) {
            moved.push_back(name);
        }
        above += dot == kRed ? 1U : 0U;
        rising += dot == kYellow ? 1U : 0U;
    }
    std::ranges::stable_sort(moved, std::greater{}, [&](const std::string& name) { return std::abs(*changes.at(name)); });
    const auto  valueOfFigure = [&figures](const std::string& name) { return std::ranges::find(figures, name, &Figures::value_type::first)->second.value_or(0.0); };
    std::string body;
    if (!moved.empty()) {
        body += tableHead;
        for (const std::string& name : moved) {
            body += row(name, valueOfFigure(name));
        }
        body += "\n";
    }
    std::optional<std::string> charted;
    for (const std::string& name : moved) {
        if (!charted && name.starts_with("bm_")) {
            charted = name;
        }
    }
    for (const auto& [name, value] : figures) {
        if (!charted && name.starts_with("bm_")) {
            charted = name;
        }
    }
    if (charted) {
        body += chart(std::format("{} instructions on main", *charted), base.rows, *charted, valueOfFigure(*charted), kSettings.chartPoints);
    }
    std::string all = tableHead;
    for (const auto& [name, value] : figures) {
        all += row(name, value.value_or(0.0));
    }
    body += details("All figures", all);
    // a pull request builds WASM-Minimal, whose size does not compare: main's WASM-Release files, as wasm-size recorded them
    if (const std::vector<Row> wasm = history("wasm-size"); !wasm.empty()) {
        std::string files;
        for (const auto& [column, value] : wasm.back()) {
            if (column.ends_with(" bytes") && !value.empty()) {
                files += std::format("- `{}`: {}\n", column.substr(0, column.size() - std::string_view{" bytes"}.size()), withThousands(std::llround(numberIn(value).value_or(0.0))));
            }
        }
        body += details(std::format("WASM application on main (`{}`, bytes)", valueOf(wasm.back(), "commit").substr(0, 8)), files);
    }
    body += details("Method", std::format("Instructions: each benchmark listed in `devtools/ci/hot-path-benchmarks.txt`, from the GCC 15 Release build, run once under `valgrind --tool=callgrind`, the whole process counted (its iterations dominate the set-up); reproducible to about 0.01 %, so a change below {} % reads ±0; wall time is not compared. "
                                          "Sizes: the release binaries as built. A rise above {} % is a no-go unless the commit message names it (CLAUDE.md, measured quality).",
                                  shortest(noise), shortest(limit)));
    std::string      headline;
    std::string_view dot;
    if (base.row.empty()) {
        headline = std::format("{} measured", plural(figures.size(), "figure"));
    } else {
        const std::string verdict = above != 0U ? std::format("{} up by more than {} %", above, shortest(limit)) : rising != 0U ? std::format("{} up by at most {} %", rising, shortest(limit)) : std::string{"no figure up"};
        const std::string largest = moved.empty() ? std::string{} : std::format(" · `{}` {:+.1f} %", moved.front(), *changes.at(moved.front()));
        headline                  = verdict + largest;
        dot                       = above != 0U ? kRed : rising != 0U ? kYellow : kGreen;
    }
    writeReport(arguments.out, "hot-paths", dot, headline, above != 0U ? "name it in the commit message" : "", body, std::format("{} down · {} up by at most {} % · {} up by more than {} % · ±0 within {} %", kGreen, kYellow, shortest(limit), kRed, shortest(limit), shortest(noise)), above != 0U ? "failure" : "success", headline, figures, base);
}

void wasmSize(const Arguments& arguments) {
    Figures     sizes;
    std::string body;
    double      loaded = 0.0;
    double      served = 0.0;
    for (const std::string& file : kSettings.wasmFiles) {
        for (const std::string& variant : {file, file + ".gz"}) {
            if (!fs::exists(arguments.build / variant)) {
                continue;
            }
            const auto bytes = static_cast<double>(fs::file_size(arguments.build / variant));
            sizes.emplace_back(fs::path{variant}.filename().string() + " bytes", bytes);
            body += std::format("- `{}`: {}\n", fs::path{variant}.filename().string(), withThousands(std::llround(bytes)));
            (variant == file ? loaded : served) += bytes;
        }
    }
    const std::string headline = std::format("{} bytes ({} as served, gzip-compressed)", withThousands(std::llround(loaded)), withThousands(std::llround(served)));
    writeReport(arguments.out, "wasm-size", "", headline, "", body, "", "success", "measured", sizes, Base{});
}

// adds this commit's row to `<bot>/history.csv` on the ci-metrics branch, in place of a row of the same commit and tool
// version (a rerun), with its `--report` as `<bot>/reports/<commit>.txt`, keeping the reports of the last rows only.
// Built with git plumbing from the branch's current head and pushed without force: a writer that lost the race to
// another bot fetches the new head and tries again.
void record(const Arguments& arguments) {
    const Table recorded = csvTable(readFile(arguments.figures));
    if (recorded.rows.empty()) {
        std::println(stderr, "no figures in {}", arguments.figures.string());
        std::exit(2);
    }
    const std::string bot     = recorded.rows.front().at("bot");
    const std::string branch  = kSettings.historyRef.substr(kSettings.historyRef.find('/') + 1);
    const std::string commit  = environmentOr("GITHUB_SHA", trimmed(checked({"git", "rev-parse", "HEAD"}).out));
    const std::string subject = checked({"git", "log", "-1", "--format=%s", commit}).out;
    std::smatch       pullRequest;
    const bool        fromPullRequest = std::regex_search(subject, pullRequest, std::regex{R"(\(#(\d+)\)\s*$)"});
    Row               row{{"commit", commit}, {"date", trimmed(checked({"git", "log", "-1", "--format=%cI", commit}).out)}, {"pr", fromPullRequest ? pullRequest[1].str() : std::string{}}, {"tool", recorded.rows.front().at("tool")}};
    for (const std::string& column : recorded.columns | std::views::drop(2)) {
        row[column] = recorded.rows.front().at(column);
    }
    const std::vector<std::string> environment{"GIT_AUTHOR_NAME=" + kSettings.author, "GIT_AUTHOR_EMAIL=41898282+github-actions[bot]@users.noreply.github.com", "GIT_COMMITTER_NAME=" + kSettings.author, "GIT_COMMITTER_EMAIL=41898282+github-actions[bot]@users.noreply.github.com"};
    std::mt19937                   jitter{std::random_device{}()};
    fs::create_directories(arguments.out);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const bool        fetched = run({"git", "fetch", "--no-tags", "origin", "refs/heads/" + branch}).status == 0;
        const std::string parent  = fetched ? trimmed(checked({"git", "rev-parse", "FETCH_HEAD"}).out) : std::string{};
        Table             table   = parent.empty() ? Table{} : csvTable(run({"git", "show", std::format("{}:{}/history.csv", parent, bot)}).out);
        const auto        rerun   = std::ranges::find_if(table.rows, [&](const Row& kept) { return valueOf(kept, "commit") == commit && valueOf(kept, "tool") == row.at("tool"); });
        if (rerun == table.rows.end()) {
            table.rows.push_back(row);
        } else {
            *rerun = row;
        }
        // commit order, also when an older commit is recorded late
        std::ranges::stable_sort(table.rows, {}, [](const Row& kept) { return epochSecondsOf(valueOf(kept, "date")).value_or(0LL); });
        // the history's own column order first, then the figures new to it in the order the bot wrote them
        std::vector<std::string> columns{"commit", "date", "pr", "tool"};
        for (const std::vector<std::string>& names : {table.columns, std::vector<std::string>(recorded.columns.begin() + 2, recorded.columns.end())}) {
            for (const std::string& column : names) {
                if (!std::ranges::contains(columns, column)) {
                    columns.push_back(column);
                }
            }
        }
        table.columns                    = columns;
        const fs::path           index   = arguments.out / "record.index";
        std::vector<std::string> indexed = environment;
        indexed.push_back("GIT_INDEX_FILE=" + index.string());
        fs::remove(index);
        if (!parent.empty()) {
            checked({"git", "read-tree", parent}, {}, indexed);
        }
        std::vector<std::pair<std::string, std::string>> files{{bot + "/history.csv", csvText(table)}};
        if (!arguments.report.empty()) {
            files.emplace_back(std::format("{}/reports/{}.txt", bot, commit), readFile(arguments.report));
            std::set<std::string> kept;
            for (const Row& keptRow : table.rows | std::views::drop(table.rows.size() > kSettings.keptReports ? table.rows.size() - kSettings.keptReports : 0U)) {
                kept.insert(std::format("{}/reports/{}.txt", bot, valueOf(keptRow, "commit")));
            }
            const std::vector<std::string> stored = parent.empty() ? std::vector<std::string>{} : wordsOf(run({"git", "ls-tree", "--name-only", std::format("{}:{}/reports", parent, bot)}).out);
            for (const std::string& name : stored) {
                if (!kept.contains(std::format("{}/reports/{}", bot, name))) {
                    checked({"git", "update-index", "--force-remove", std::format("{}/reports/{}", bot, name)}, {}, indexed);
                }
            }
        }
        for (const auto& [path, content] : files) {
            const std::string blob = trimmed(checked({"git", "hash-object", "-w", "--stdin"}, content).out);
            checked({"git", "update-index", "--add", "--cacheinfo", std::format("100644,{},{}", blob, path)}, {}, indexed);
        }
        const std::string        tree = trimmed(checked({"git", "write-tree"}, {}, indexed).out);
        std::vector<std::string> commitTree{"git", "commit-tree", tree};
        if (!parent.empty()) {
            commitTree.insert(commitTree.end(), {"-p", parent});
        }
        commitTree.insert(commitTree.end(), {"-m", std::format("{}: {}", bot, commit.substr(0, 7))});
        const std::string created = trimmed(checked(commitTree, {}, environment).out);
        if (run({"git", "push", "origin", std::format("{}:refs/heads/{}", created, branch)}).status == 0) {
            std::println("recorded {} for {} as {}", bot, commit.substr(0, 7), created.substr(0, 7));
            return;
        }
        const auto delay = std::chrono::milliseconds{2000 + attempt * 3000 + static_cast<int>(jitter() % 3000U)};
        std::println("{} moved on; trying again in {} s", branch, delay.count() / 1000);
        std::this_thread::sleep_for(delay);
    }
    std::println(stderr, "could not record {} for {}: {} kept moving", bot, commit.substr(0, 7), branch);
    std::exit(1);
}

constexpr std::string_view kUsage = R"(usage: qualitybot <formatting|naming|code-style|coverage|hot-paths|wasm-size> --out <dir> [--build <dir>] [--profiles <dir>]
       qualitybot record --out <dir> --figures <figures.csv> [--report <file>]
  --out       where comment.md, status.json and figures.csv go
  --build     the build directory (naming, code-style: Clang; coverage, hot-paths, wasm-size: their flavour's)
  --profiles  the directory of .profraw files the tests wrote (coverage)
  --figures   a bot's figures.csv to add to its history (record)
  --report    the bot's full report, kept beside the history for comparing (record))";

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> words(argv + 1, argv + argc);
    Arguments                           arguments;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto value = [&] { return i + 1 < words.size() ? fs::path{words[++i]} : fs::path{}; };
        if (words[i] == "--out") {
            arguments.out = value();
        } else if (words[i] == "--build") {
            arguments.build = value();
        } else if (words[i] == "--profiles") {
            arguments.profiles = value();
        } else if (words[i] == "--figures") {
            arguments.figures = value();
        } else if (words[i] == "--report") {
            arguments.report = value();
        } else if (arguments.bot.empty()) {
            arguments.bot = std::string{words[i]};
        }
    }
    const std::map<std::string_view, void (*)(const Arguments&)> kRun = {{"formatting", formatting}, {"naming", naming}, {"code-style", codeStyle}, {"coverage", coverage}, {"hot-paths", hotPaths}, {"wasm-size", wasmSize}, {"record", record}};
    const auto                                                   bot  = kRun.find(arguments.bot);
    if (bot == kRun.end() || arguments.out.empty()) {
        std::println(stderr, "{}", kUsage);
        return 2;
    }
    // the tools and settings are read beside this source, whose path the compiler wrote in: an absolute one
    if (!fs::exists(kRepository / "devtools" / "qualitybot.cpp")) {
        std::println(stderr, "qualitybot cannot find its repository from `{}`: compile it with an absolute path (\"$PWD/devtools/qualitybot.cpp\")", __FILE__);
        return 2;
    }
    bot->second(arguments);
    return 0;
}
