#include <gr4-present/Markdown.hpp>
#include <gr4-present/Number.hpp>
#include <gr4-present/Slice.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>

#include <gnuradio-4.0/TriggerMatcher.hpp> // trim

namespace gr::present {

namespace {

constexpr std::string_view kDirectiveFence = ":::";
constexpr std::string_view kPause          = ". . .";
constexpr std::string_view kCodeFence      = "```";
constexpr std::string_view kMathFence      = "$$";
constexpr std::string_view kNotesDirective = "notes";

constexpr int kMaxHeading = 6;

using gr::trigger::detail::trim;

/// `{shrink=off centre}`: space-separated `key=value` or a bare flag, which is `key=on`
[[nodiscard]] std::vector<std::pair<std::string, std::string>> fenceOptionsOf(std::string_view braced) {
    const auto close = braced.find('}');
    if (!braced.starts_with('{') || close == std::string_view::npos) {
        return {}; // an unclosed brace is a typo, and half a setting is worse than none
    }
    std::vector<std::pair<std::string, std::string>> options;
    const std::string_view                           inside = braced.substr(1UZ, close - 1UZ);
    // words are split at spaces, except inside double quotes: `region="0.2 0.4 0.1 0.1"` is one value
    std::size_t at = 0UZ;
    while (at < inside.size()) {
        at = inside.find_first_not_of(' ', at);
        if (at == std::string_view::npos) {
            break;
        }
        std::size_t end    = at;
        bool        quoted = false;
        while (end < inside.size() && (quoted || inside[end] != ' ')) {
            quoted = inside[end] == '"' ? !quoted : quoted;
            ++end;
        }
        const std::string_view option = inside.substr(at, end - at);
        at                            = end;
        const auto       equals       = option.find('=');
        std::string_view value        = equals == std::string_view::npos ? std::string_view{"on"} : trim(option.substr(equals + 1UZ));
        if (value.size() >= 2UZ && value.front() == '"' && value.back() == '"') {
            value = value.substr(1UZ, value.size() - 2UZ);
        }
        std::string_view key = trim(option.substr(0UZ, equals));
        if (key.starts_with('.') && equals == std::string_view::npos) {
            key.remove_prefix(1UZ); // a Pandoc class, `.striped`, is the flag `striped`
        }
        options.emplace_back(std::string{key}, std::string{value});
    }
    return options;
}

[[nodiscard]] std::pair<std::string_view, std::string_view> withoutAnchor(std::string_view text) noexcept {
    if (!text.ends_with('}')) {
        return {text, {}};
    }
    const auto open = text.rfind("{#");
    if (open == std::string_view::npos) {
        return {text, {}};
    }
    return {trim(slice(text, 0UZ, open)), slice(text, open + 2UZ, text.size() - open - 3UZ)};
}

[[nodiscard]] bool isRule(std::string_view line) noexcept {
    const std::string_view body = trim(line);
    return body.size() >= 3UZ && (body.find_first_not_of('-') == std::string_view::npos || body.find_first_not_of('*') == std::string_view::npos);
}

void appendText(std::vector<InlineSpan>& spans, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!spans.empty() && spans.back().kind == InlineKind::text && spans.back().font.empty() && spans.back().size.empty()) {
        spans.back().text.append(text);
        return;
    }
    spans.push_back(InlineSpan{.kind = InlineKind::text, .text = std::string{text}, .target = {}});
}

struct Match {
    std::size_t consumed = 0UZ; // characters of the source the construct occupies; zero when it does not close
    InlineSpan  span;
};

/// `[text](target)` and `![alt](target)`
[[nodiscard]] Match matchReference(std::string_view text, bool image) {
    const std::size_t labelStart = image ? 2UZ : 1UZ;
    const auto        labelEnd   = text.find(']', labelStart);
    if (labelEnd == std::string_view::npos || labelEnd + 1UZ >= text.size() || text[labelEnd + 1UZ] != '(') {
        return {};
    }
    const auto targetEnd = text.find(')', labelEnd + 2UZ);
    if (targetEnd == std::string_view::npos) {
        return {};
    }
    const std::string_view label  = text.substr(labelStart, labelEnd - labelStart);
    const std::string_view target = text.substr(labelEnd + 2UZ, targetEnd - labelEnd - 2UZ);
    return Match{.consumed = targetEnd + 1UZ, .span = InlineSpan{.kind = image ? InlineKind::image : InlineKind::link, .text = std::string{label}, .target = std::string{target}}};
}

/// `[words]{font=hand size=80%}`, a bracketed span: the words, and the face and size they are set in
struct StyledSpan {
    std::size_t      consumed = 0UZ;
    std::string_view words;
    std::string      font;
    std::string      size;
};

[[nodiscard]] StyledSpan matchStyledSpan(std::string_view text) {
    // brackets nest, so `[a citation[^x] here]{...}` closes at the bracket that matches the first
    int         depth = 0;
    std::size_t close = std::string_view::npos;
    for (std::size_t at = 0UZ; at < text.size() && close == std::string_view::npos; ++at) {
        if (text[at] == '\\') {
            ++at;
        } else if (text[at] == '[') {
            ++depth;
        } else if (text[at] == ']' && --depth == 0) {
            close = at;
        }
    }
    if (close == std::string_view::npos || close + 1UZ >= text.size() || text[close + 1UZ] != '{') {
        return {};
    }
    const auto braceEnd = text.find('}', close + 1UZ);
    if (braceEnd == std::string_view::npos) {
        return {};
    }
    StyledSpan styled{.consumed = braceEnd + 1UZ, .words = text.substr(1UZ, close - 1UZ), .font = {}, .size = {}};
    for (auto& [key, value] : fenceOptionsOf(text.substr(close + 1UZ, braceEnd - close))) {
        if (key == "font") {
            styled.font = std::move(value);
        } else if (key == "size") {
            styled.size = std::move(value);
        }
    }
    // `[x]{#id}` and other braces after a bracket are not a style, and stay as they were written
    return styled.font.empty() && styled.size.empty() ? StyledSpan{} : styled;
}

/// `[^label]`, a footnote or citation reference; the label goes in the target, as a link's does
[[nodiscard]] Match matchFootnoteReference(std::string_view text) {
    if (!text.starts_with("[^")) {
        return {};
    }
    const auto close = text.find(']', 2UZ);
    if (close == std::string_view::npos || close == 2UZ) {
        return {};
    }
    const std::string_view label = text.substr(2UZ, close - 2UZ);
    return Match{.consumed = close + 1UZ, .span = InlineSpan{.kind = InlineKind::footnote, .text = std::string{label}, .target = std::string{label}}};
}

[[nodiscard]] Match matchDelimited(std::string_view text, std::string_view marker, InlineKind kind) {
    const auto close = text.find(marker, marker.size());
    if (close == std::string_view::npos || close == marker.size()) {
        return {};
    }
    return Match{.consumed = close + marker.size(), .span = InlineSpan{.kind = kind, .text = std::string{text.substr(marker.size(), close - marker.size())}, .target = {}}};
}

/// a code line that is only a comment holding `@step` or `@step N`: N, zero for the next step, nothing for any other
/// line
[[nodiscard]] std::optional<int> codeStepMarker(std::string_view line) noexcept {
    for (const std::string_view comment : {std::string_view{"//"}, std::string_view{"#"}, std::string_view{"--"}, std::string_view{";"}, std::string_view{"%%"}, std::string_view{"%"}}) {
        if (!line.starts_with(comment)) {
            continue;
        }
        std::string_view rest = trim(slice(line, comment.size()));
        if (!rest.starts_with("@step")) {
            return std::nullopt;
        }
        rest = trim(slice(rest, 5UZ));
        if (rest.empty()) {
            return 0;
        }
        return parseNumber<int>(rest);
    }
    return std::nullopt;
}

/// the length of a `<br>`, `<br/>` or `<br />` at the front of `text`, in either case; zero when there is none
[[nodiscard]] std::size_t lineBreakTag(std::string_view text) noexcept {
    for (const std::string_view tag : {std::string_view{"<br>"}, std::string_view{"<br/>"}, std::string_view{"<br />"}}) {
        if (text.size() >= tag.size() && std::ranges::equal(slice(text, 0UZ, tag.size()), tag, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; })) {
            return tag.size();
        }
    }
    return 0UZ;
}

[[nodiscard]] std::vector<InlineSpan> parseInlines(std::string_view text) {
    std::vector<InlineSpan> spans;
    std::size_t             plain = 0UZ;

    constexpr std::string_view kEscapable = "*_`$[]!\\";

    for (std::size_t index = 0UZ; index < text.size();) {
        const std::string_view rest = text.substr(index);
        if (rest.size() > 1UZ && rest.front() == '\\' && kEscapable.contains(rest[1UZ])) {
            appendText(spans, text.substr(plain, index - plain));
            appendText(spans, rest.substr(1UZ, 1UZ));
            index += 2UZ;
            plain = index;
            continue;
        }
        if (const std::size_t tag = lineBreakTag(rest); tag > 0UZ) {
            appendText(spans, text.substr(plain, index - plain));
            spans.push_back(InlineSpan{.kind = InlineKind::lineBreak, .text = {}, .target = {}});
            index += tag;
            plain = index;
            continue;
        }
        if (rest.starts_with('[') && !rest.starts_with("[^")) {
            if (const StyledSpan styled = matchStyledSpan(rest); styled.consumed > 0UZ) {
                appendText(spans, text.substr(plain, index - plain));
                for (InlineSpan inner : parseInlines(styled.words)) {
                    inner.font = inner.font.empty() ? styled.font : inner.font;
                    inner.size = inner.size.empty() ? styled.size : inner.size;
                    spans.push_back(std::move(inner));
                }
                index += styled.consumed;
                plain = index;
                continue;
            }
        }
        const Match match = rest.starts_with("[^")   ? matchFootnoteReference(rest)                    //
                            : rest.starts_with("![") ? matchReference(rest, true)                      //
                            : rest.starts_with('[')  ? matchReference(rest, false)                     //
                            : rest.starts_with("**") ? matchDelimited(rest, "**", InlineKind::strong)  //
                            : rest.starts_with('*')  ? matchDelimited(rest, "*", InlineKind::emphasis) //
                            : rest.starts_with('`')  ? matchDelimited(rest, "`", InlineKind::code)     //
                            : rest.starts_with('$')  ? matchDelimited(rest, "$", InlineKind::math)     //
                                                     : Match{};
        if (match.consumed == 0UZ) {
            ++index;
            continue;
        }
        appendText(spans, text.substr(plain, index - plain));
        spans.push_back(match.span);
        index += match.consumed;
        plain = index;
    }
    appendText(spans, text.substr(plain));
    return spans;
}

/// splits a table row on unescaped pipes, dropping the optional outer ones; `\|` becomes a literal pipe in a cell
[[nodiscard]] std::vector<std::string> splitCells(std::string_view row) {
    row = trim(row);
    if (row.starts_with('|')) {
        row.remove_prefix(1UZ);
    }
    if (row.ends_with('|') && !row.ends_with("\\|")) {
        row.remove_suffix(1UZ);
    }

    std::vector<std::string> cells;
    std::string              current;
    for (std::size_t at = 0UZ; at < row.size(); ++at) {
        if (row[at] == '\\' && at + 1UZ < row.size() && row[at + 1UZ] == '|') {
            current.push_back('|');
            ++at;
        } else if (row[at] == '|') {
            cells.push_back(std::string{trim(current)});
            current.clear();
        } else {
            current.push_back(row[at]);
        }
    }
    cells.push_back(std::string{trim(current)});
    return cells;
}

/// `---`, `:--`, `--:` or `:-:` in every cell: the row that turns the line above it into a table header
[[nodiscard]] bool isDelimiterRow(std::string_view line) noexcept {
    if (!line.contains('-') || !line.contains('|')) {
        return false;
    }
    for (const std::string& cell : splitCells(line)) {
        std::string_view rest = cell;
        if (rest.starts_with(':')) {
            rest.remove_prefix(1UZ);
        }
        if (rest.ends_with(':')) {
            rest.remove_suffix(1UZ);
        }
        if (rest.empty() || rest.find_first_not_of('-') != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Alignment alignmentOf(std::string_view cell) noexcept {
    const bool left  = cell.starts_with(':');
    const bool right = cell.ends_with(':');
    return left && right ? Alignment::centre : right ? Alignment::right : Alignment::left;
}

struct ListMarker {
    bool             found   = false;
    bool             ordered = false;
    std::size_t      depth   = 0UZ;
    std::string_view content;
};

[[nodiscard]] ListMarker listMarkerOf(std::string_view line) noexcept {
    const std::size_t      indent = std::min(line.find_first_not_of(" \t"), line.size());
    const std::string_view body   = slice(line, indent);
    if (body.starts_with("- ") || body.starts_with("* ")) {
        return ListMarker{.found = true, .ordered = false, .depth = indent / 2UZ, .content = slice(body, 2UZ)};
    }
    const auto dot = body.find(". ");
    if (dot != std::string_view::npos && dot > 0UZ && slice(body, 0UZ, dot).find_first_not_of("0123456789") == std::string_view::npos) {
        return ListMarker{.found = true, .ordered = true, .depth = indent / 2UZ, .content = slice(body, dot + 2UZ)};
    }
    return {};
}

} // namespace

bool isConfigurationDirective(std::string_view name) noexcept {
    constexpr std::array<std::string_view, 11> kConfigurationDirectives{"layout", "regions", "place", "grid", "view", "video", "qr", "references", "gr4", "shader", "stage"};
    return std::ranges::contains(kConfigurationDirectives, name);
}

std::vector<std::string> parseNameList(std::string_view list) {
    std::vector<std::string> names;
    for (const auto part : list | std::views::split(',')) {
        const std::string_view entry{part};
        const auto             first = entry.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            continue;
        }
        const auto last = entry.find_last_not_of(" \t");
        names.emplace_back(entry.substr(first, last - first + 1UZ));
    }
    return names;
}

bool isBox(const Block& block) noexcept { return block.kind == BlockKind::directive && !isConfigurationDirective(block.info); }

bool isLayoutSetting(std::string_view key) noexcept {
    constexpr std::array<std::string_view, 14> kSettings{"anchor", "transition", "region", "via", "camera", "grid", "duration", "advance", "rotate", "font", "size", "outline", "background", "overlay"};
    return std::ranges::contains(kSettings, key);
}

SchemeFiles schemeFilesOf(std::string_view value) noexcept {
    const std::size_t bar = value.find('|');
    if (bar == std::string_view::npos) {
        return {.light = trim(value), .dark = trim(value)};
    }
    return {.light = trim(slice(value, 0UZ, bar)), .dark = trim(slice(value, bar + 1UZ))};
}

std::optional<std::string_view> valueFor(std::span<const std::pair<std::string, std::string>> pairs, std::string_view key) noexcept {
    const auto entry = std::ranges::find(pairs, key, &std::pair<std::string, std::string>::first);
    return entry == pairs.end() ? std::nullopt : std::optional<std::string_view>{entry->second};
}

std::string_view Block::field(std::string_view name) const noexcept { return valueFor(fields, name).value_or(std::string_view{}); }

const Reveal* Document::revealOf(int step) const noexcept {
    const auto found = std::ranges::find(stepReveals, step, &StepReveal::step);
    return found == stepReveals.end() ? nullptr : &found->reveal;
}

float Document::delayBefore(int step) const noexcept {
    const auto found = std::ranges::find(stepDelays, step, &std::pair<int, float>::first);
    return found == stepDelays.end() ? -1.0f : found->second;
}

int Document::stepCount() const noexcept {
    int highest = -1;
    for (const Block& block : blocks) {
        highest = std::max(highest, block.step);
        for (const int lineStep : block.lineSteps) {
            highest = std::max(highest, lineStep);
        }
    }
    return highest + 1 < 1 ? 1 : highest + 1;
}

const Block* Document::withId(std::string_view id) const noexcept {
    const auto block = std::ranges::find(blocks, id, &Block::id);
    return block == blocks.end() ? nullptr : &*block;
}

const std::vector<InlineSpan>* Document::footnote(std::string_view label) const noexcept {
    const auto found = std::ranges::find(footnotes, label, &std::pair<std::string, std::vector<InlineSpan>>::first);
    return found == footnotes.end() ? nullptr : &found->second;
}
std::string slugOf(std::string_view heading) {
    std::string slug;
    slug.reserve(heading.size());
    for (const char c : heading) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!slug.empty() && slug.back() != '-') {
            slug.push_back('-');
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

std::string notesOf(const Document& document) {
    std::string notes;
    for (const Block& block : document.blocks) {
        if (block.kind != BlockKind::directive || block.info != kNotesDirective) {
            continue;
        }
        for (const std::string& line : block.lines) {
            notes.append(line).push_back('\n');
        }
    }
    while (!notes.empty() && std::isspace(static_cast<unsigned char>(notes.back())) != 0) {
        notes.pop_back();
    }
    return notes;
}
Document boxDocumentOf(std::span<const std::string> lines, const Document& slide) {
    std::string prose;
    for (const std::string& line : lines) {
        prose += line;
        prose += '\n';
    }
    Document contents  = parseMarkdown(prose);
    contents.footnotes = slide.footnotes;
    return contents;
}

std::vector<Section> sectionsOf(const Document& document, int level) {
    std::vector<Section> sections;
    const auto           startsSection = [level](const Block& block) { return block.kind == BlockKind::heading && block.level <= level; };

    for (const Block& block : document.blocks) {
        if (sections.empty() || startsSection(block)) {
            sections.push_back(Section{.id = startsSection(block) ? block.id : std::string{}, .document = {}});
        }
        sections.back().document.blocks.push_back(block);
    }

    // every view carries all footnotes and the front matter, as a slide is drawn from its own section alone
    for (Section& section : sections) {
        section.document.footnotes = document.footnotes;
        section.document.front     = document.front;
    }

    // steps are numbered across the whole document, so rebase each section's onto zero for its own (view, step) cursor
    for (Section& section : sections) {
        const auto lowest = std::ranges::min_element(section.document.blocks, {}, &Block::step);
        if (lowest == section.document.blocks.end()) {
            continue;
        }
        const int base = lowest->step;
        for (Block& block : section.document.blocks) {
            block.step -= base;
            for (int& lineStep : block.lineSteps) {
                lineStep -= base;
            }
        }
        const int last = base + section.document.stepCount();
        for (const auto& [number, seconds] : document.stepDelays) {
            if (number > base && number < last) {
                section.document.stepDelays.emplace_back(number - base, seconds);
            }
        }
        for (const Document::StepReveal& stepReveal : document.stepReveals) {
            if (stepReveal.step > base && stepReveal.step < last) {
                section.document.stepReveals.push_back(Document::StepReveal{.step = stepReveal.step - base, .reveal = stepReveal.reveal});
            }
        }
    }
    return sections;
}

void appendCitedLabels(const Block& block, std::vector<std::string>& labels) {
    const auto note = [&labels](const std::vector<InlineSpan>& spans) {
        for (const InlineSpan& span : spans) {
            if (span.kind == InlineKind::footnote && !std::ranges::contains(labels, span.target)) {
                labels.push_back(span.target);
            }
        }
    };
    note(block.spans);
    std::ranges::for_each(block.cells, note);
}

Citations citationsOf(std::span<const Section> sections) {
    Citations  cited;
    const auto noteLinks = [&cited](const std::vector<InlineSpan>& spans) {
        for (const InlineSpan& span : spans) {
            if (span.kind == InlineKind::link && !span.target.starts_with('#') && !std::ranges::contains(cited.references, span.target, &std::pair<std::string, std::string>::second)) {
                cited.references.emplace_back(span.text, span.target);
            }
        }
    };
    const auto gather = [&](const Block& block) {
        appendCitedLabels(block, cited.footnotes);
        noteLinks(block.spans);
        std::ranges::for_each(block.cells, noteLinks);
    };
    for (const Section& section : sections) {
        for (const Block& block : section.document.blocks) {
            gather(block);
            if (isBox(block) && !block.lines.empty()) {
                std::ranges::for_each(boxDocumentOf(block.lines, section.document).blocks, gather);
            }
        }
    }
    return cited;
}

/// reads and removes the front matter, only when the very first line is `---`
[[nodiscard]] FrontMatter frontMatterOf(std::string_view& source) {
    FrontMatter front;
    if (!source.starts_with("---")) {
        return front;
    }
    const auto firstBreak = source.find('\n');
    if (firstBreak == std::string_view::npos || trim(source.substr(0UZ, firstBreak)) != "---") {
        return front;
    }
    const auto closing = source.find("\n---", firstBreak);
    if (closing == std::string_view::npos) {
        return front; // an unclosed block is not front matter; the document keeps every line of itself
    }

    for (std::size_t at = firstBreak + 1UZ; at < closing;) {
        const auto       lineEnd = std::min(source.find('\n', at), closing);
        std::string_view line    = trim(source.substr(at, lineEnd - at));
        at                       = lineEnd + 1UZ;
        const auto colon         = line.find(':');
        if (line.empty() || line.starts_with('#') || colon == std::string_view::npos) {
            continue;
        }
        const std::string_view key   = trim(line.substr(0UZ, colon));
        std::string_view       value = trim(line.substr(colon + 1UZ));
        if (value.size() >= 2UZ && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1UZ, value.size() - 2UZ);
        }
        if (key == "title") {
            front.title = value;
        } else if (key == "author") {
            front.author = value;
        } else if (key == "email") {
            front.email = value;
        } else if (key == "numbering") {
            front.numbering = value;
        }
    }

    const auto after = source.find('\n', closing + 1UZ);
    source           = after == std::string_view::npos ? std::string_view{} : source.substr(after + 1UZ);
    return front;
}

namespace {

/// the run of backticks a fenced code block opens with, as CommonMark has it: three or more, else 0
[[nodiscard]] std::size_t fenceLength(std::string_view body) noexcept {
    const std::size_t run = std::min(body.find_first_not_of('`'), body.size());
    return run >= kCodeFence.size() ? run : 0UZ;
}

/// whether `body` closes a code block a fence of `opened` backticks began: a run at least as long, and nothing after
[[nodiscard]] bool closesFence(std::string_view body, std::size_t opened) noexcept {
    const std::size_t run = fenceLength(body);
    return run >= opened && trim(slice(body, run)).empty();
}

/// the lines without `%%` and `<!-- -->` comments, and which held nothing else; code keeps its own, the count stays
struct CommentFreeLines {
    std::vector<std::string> text;
    std::vector<bool>        onlyComment;
};

[[nodiscard]] CommentFreeLines withoutComments(std::string_view source) {
    CommentFreeLines result;
    std::size_t      codeFenceLength = 0UZ;
    bool             insideComment   = false;
    for (const auto lineRange : source | std::views::split('\n')) {
        std::string_view line{lineRange};
        if (line.ends_with('\r')) {
            line.remove_suffix(1UZ);
        }
        if (codeFenceLength > 0UZ || (!insideComment && fenceLength(trim(line)) > 0UZ)) {
            codeFenceLength = codeFenceLength > 0UZ ? (closesFence(trim(line), codeFenceLength) ? 0UZ : codeFenceLength) : fenceLength(trim(line));
            result.text.emplace_back(line);
            result.onlyComment.push_back(false);
            continue;
        }
        std::string kept;
        bool        hadComment = insideComment;
        for (std::size_t at = 0UZ; at < line.size();) {
            if (insideComment) {
                const std::size_t end = line.find("-->", at);
                at                    = end == std::string_view::npos ? line.size() : end + 3UZ;
                insideComment         = end == std::string_view::npos;
            } else if (line[at] == '`') { // inline code is text: its run of backticks closes it, comment markers or not
                const std::size_t run   = line.find_first_not_of('`', at) == std::string_view::npos ? line.size() - at : line.find_first_not_of('`', at) - at;
                const std::size_t close = line.find(line.substr(at, run), at + run);
                const std::size_t end   = close == std::string_view::npos ? at + run : close + run;
                kept.append(line.substr(at, end - at));
                at = end;
            } else if (line.substr(at).starts_with("<!--")) {
                insideComment = hadComment = true;
                at += 4UZ;
            } else if (line.substr(at).starts_with("%%")) {
                hadComment = true;
                break;
            } else {
                kept += line[at++];
            }
        }
        result.onlyComment.push_back(hadComment && trim(kept).empty());
        result.text.push_back(std::move(kept));
    }
    return result;
}

} // namespace

/// the `in=`, `from=` and `dur=` a `:::step` writes; a duration is a positive number of seconds, else the way's own decides
[[nodiscard]] Reveal writtenReveal(std::span<const std::pair<std::string, std::string>> options) {
    const std::optional<float> seconds  = parseSeconds(valueFor(options, "dur").value_or(std::string_view{}));
    const std::string_view     kind     = valueFor(options, "in").value_or(std::string_view{});
    const std::string_view     from     = valueFor(options, "from").value_or(std::string_view{});
    const bool                 positive = seconds && *seconds > 0.0f;
    return Reveal{.kind = std::string{kind}, .seconds = positive ? seconds : std::nullopt, .from = std::string{from}};
}

/// one pass over the deck's lines, each claimed by the first context that matches, in this order:
/// a comment-only line (skipped), the rest of a ```` ``` {area=…} ```` block, an open code fence (closed by one at least as long),
/// code inside a box (verbatim), Pandoc's pause, a `:::` fence (named: opens a box, step or stop; nameless: closes the innermost),
/// a box's lines (verbatim, for `boxDocumentOf`), a configuring directive's `key: value` fields, a `$$` formula,
/// then footnote, rule, heading, list item, table, paragraph; `step` only grows
Document parseMarkdown(std::string_view source) {
    Document document;
    document.front               = frontMatterOf(source);
    int         step             = 0;
    int         openSteps        = 0;
    std::size_t codeFenceLength  = 0UZ; // non-zero inside a fenced code or plot block
    std::size_t areaFenceLength  = 0UZ;
    int         sectionStartStep = 0;
    // `:::step {with}` joins the step before: blocks up to the next step arrive with it, each by its own `in=`
    std::optional<std::size_t> withFrom;
    Reveal                     withReveal;
    const auto                 closeWith = [&] {
        if (withFrom) {
            for (Block& joined : document.blocks | std::views::drop(*withFrom)) {
                joined.reveal = withReveal;
            }
            withFrom.reset();
        }
    };
    bool        insideFormula      = false;
    bool        insideDirective    = false;
    std::size_t boxCodeFenceLength = 0UZ; // non-zero inside a code fence within a box
    std::size_t nestedFenceDepth   = 0UZ;
    std::size_t openDirectiveIndex = 0UZ;
    std::string paragraph;

    const auto insideBox = [&] { return insideDirective && !document.blocks.empty() && isBox(document.blocks.back()); };

    const auto flushParagraph = [&] {
        const std::string_view body = trim(paragraph);
        if (!body.empty()) {
            const auto [text, anchor] = withoutAnchor(body);
            document.blocks.push_back(Block{.kind = BlockKind::paragraph, .step = step, .id = std::string{anchor}, .spans = parseInlines(text)});
        }
        paragraph.clear();
    };

    // materialised rather than streamed: a GFM table is recognised only by the line that follows its header
    const CommentFreeLines              uncommented = withoutComments(source);
    const std::vector<std::string_view> lines(uncommented.text.begin(), uncommented.text.end());

    for (std::size_t index = 0UZ; index < lines.size(); ++index) {
        if (uncommented.onlyComment[index]) {
            continue; // a note to the author, which neither ends a paragraph nor starts one
        }
        const std::string_view line = lines[index];
        const std::string_view body = trim(line);
        if (areaFenceLength > 0UZ) {
            document.blocks.back().lines.emplace_back(line);
            areaFenceLength = closesFence(body, areaFenceLength) ? 0UZ : areaFenceLength;
            continue;
        }
        if (codeFenceLength > 0UZ) {
            Block& code = document.blocks.back();
            if (closesFence(body, codeFenceLength)) {
                codeFenceLength = 0UZ;
            } else if (const std::optional<int> marked = codeStepMarker(body); marked.has_value() && code.kind == BlockKind::codeBlock) {
                // `// @step` and its kin start the lines a further reveal step shows; the marker itself is not code
                step = *marked > 0 ? std::max(step + 1, sectionStartStep + *marked) : step + 1;
                code.lineSteps.resize(code.lines.size(), code.step);
            } else {
                code.lines.emplace_back(line);
                if (!code.lineSteps.empty() || step != code.step) {
                    code.lineSteps.resize(code.lines.size() - 1UZ, code.step);
                    code.lineSteps.push_back(step);
                }
            }
            continue;
        }

        // a code block inside a box is the box's verbatim, so a `:::` it quotes opens and closes nothing
        if (insideBox() && (boxCodeFenceLength > 0UZ || fenceLength(body) > 0UZ)) {
            if (boxCodeFenceLength == 0UZ) {
                boxCodeFenceLength = fenceLength(body);
            } else if (closesFence(body, boxCodeFenceLength)) {
                boxCodeFenceLength = 0UZ;
            }
            document.blocks.back().lines.emplace_back(line);
            continue;
        }

        // Pandoc's slide pause (three spaced dots) equals a bare `:::step`; inside a box it belongs to the box
        if (body == kPause && !insideDirective) {
            flushParagraph();
            closeWith();
            ++step;
            continue;
        }

        if (body.starts_with(kDirectiveFence)) {
            flushParagraph();
            // Pandoc: three or more colons fence; a nameless one closes the innermost box, so `::::` around `:::` nests
            std::string_view name = trim(body.substr(std::min(body.find_first_not_of(':'), body.size())));
            name                  = trim(name.substr(0UZ, name.find_last_not_of(':') + 1UZ)); // `::: Warning ::::::`, as Pandoc allows

            // a fence inside a content box belongs to that box, whose lines are parsed again on their own
            if (insideBox()) {
                if (name.empty() && nestedFenceDepth == 0UZ) {
                    insideDirective = false;
                    continue;
                }
                if (name.empty()) {
                    --nestedFenceDepth;
                } else {
                    ++nestedFenceDepth;
                }
                document.blocks.back().lines.emplace_back(line);
                continue;
            }

            // the braces say how the box behaves and are no part of its name
            const auto brace   = name.find('{');
            const auto options = brace == std::string_view::npos ? std::vector<std::pair<std::string, std::string>>{} : fenceOptionsOf(name.substr(brace));
            if (brace != std::string_view::npos) {
                name = trim(name.substr(0UZ, brace));
            }
            if (name.empty()) {
                if (!insideDirective && openSteps == 0) {
                    document.warnings.push_back(std::format("line {}: a `{}` that closes nothing", index + 1UZ, body));
                }
                insideDirective = false;
                if (openSteps > 0) {
                    --openSteps;
                }
                continue;
            }
            if (name == "step") {
                closeWith();
                ++openSteps;
                if (valueFor(options, "with") && step > sectionStartStep) {
                    withFrom   = document.blocks.size();
                    withReveal = writtenReveal(options);
                    if (valueFor(options, "after")) {
                        document.warnings.push_back(std::format("line {}: a step `with` the one before has no `after=` of its own", index + 1UZ));
                    }
                    continue;
                }
                ++step;
                if (const auto after = valueFor(options, "after")) {
                    if (const std::optional<float> seconds = parseSeconds(*after); seconds) {
                        document.stepDelays.emplace_back(step, *seconds);
                    }
                }
                if (valueFor(options, "in")) {
                    document.stepReveals.push_back(Document::StepReveal{.step = step, .reveal = writtenReveal(options)});
                }
                continue;
            }
            if (name == "stop") {
                ++step; // a camera stop is a reveal step that also carries the words shown while it is current
            }
            insideDirective  = true;
            nestedFenceDepth = 0UZ;
            // a content box keeps its options as fields; a configuring directive keeps none, its keys name areas
            const bool box = !isConfigurationDirective(name);
            if (!box && !options.empty()) {
                document.warnings.push_back(std::format("line {}: `:::{}` takes its settings as `key: value` lines, so the braces are ignored", index + 1UZ, name));
            }
            std::string id     = {};
            auto        fields = box ? options : std::vector<std::pair<std::string, std::string>>{};
            if (const auto anchor = std::ranges::find_if(fields, [](const auto& option) { return option.first.starts_with('#'); }); anchor != fields.end()) {
                id = anchor->first.substr(1UZ); // Pandoc's `{#id}` names the box rather than setting an option
                fields.erase(anchor);
            }
            document.blocks.push_back(Block{.kind = BlockKind::directive, .step = step, .id = std::move(id), .info = std::string{name}, .fields = std::move(fields)});
            openDirectiveIndex = document.blocks.size() - 1UZ;
            continue;
        }

        // content boxes and `:::notes` keep their lines as written: a sentence with a colon is a sentence
        if (insideBox()) {
            document.blocks.back().lines.emplace_back(line);
            continue;
        }

        // only while the fence is open: prose after it often contains a colon, and that is not a field
        if (insideDirective && !document.blocks.empty() && document.blocks.back().kind == BlockKind::directive && body.contains(':')) {
            const auto colon  = body.find(':');
            Block&     region = document.blocks.back();
            const auto key    = trim(body.substr(0UZ, colon));
            const auto value  = trim(body.substr(colon + 1UZ));
            region.fields.emplace_back(std::string{key}, std::string{value});
            if (key == "id") {
                region.id = std::string{value};
            }
            continue;
        }

        if (insideFormula) {
            if (body.starts_with(kMathFence)) {
                insideFormula = false;
            } else {
                document.blocks.back().lines.emplace_back(line);
            }
            continue;
        }

        if (body.starts_with(kMathFence)) {
            flushParagraph();
            Block formula{.kind = BlockKind::formula, .step = step};
            if (const std::string_view inner = trim(body.substr(kMathFence.size())); inner.ends_with(kMathFence) && inner.size() >= kMathFence.size()) {
                formula.lines.emplace_back(trim(inner.substr(0UZ, inner.size() - kMathFence.size())));
            } else {
                if (!inner.empty()) {
                    formula.lines.emplace_back(inner);
                }
                insideFormula = true;
            }
            document.blocks.push_back(std::move(formula));
            continue;
        }

        if (const std::size_t fence = fenceLength(body); fence > 0UZ) {
            flushParagraph();
            codeFenceLength = fence;
            // takes the braces a `:::` fence takes, so `cpp {striped}` still highlights as cpp
            std::string_view language = trim(body.substr(fence));
            const auto       brace    = language.find('{');
            const auto       options  = brace == std::string_view::npos ? std::vector<std::pair<std::string, std::string>>{} : fenceOptionsOf(language.substr(brace));
            if (brace != std::string_view::npos) {
                language = trim(language.substr(0UZ, brace));
            }
            // ```cpp {area=left size=75%} is the box `:::left {size=75%}` holding that code block, written once
            if (const auto area = std::ranges::find(options, "area", &std::pair<std::string, std::string>::first); area != options.end() && !area->second.empty()) {
                codeFenceLength    = 0UZ;
                areaFenceLength    = fence;
                auto        fields = options;
                std::string opening{line};
                const auto  token = opening.find(std::format("area={}", area->second));
                opening.erase(token, token == std::string::npos ? 0UZ : std::format("area={}", area->second).size());
                fields.erase(fields.begin() + (area - options.begin()));
                document.blocks.push_back(Block{.kind = BlockKind::directive, .step = step, .info = area->second, .lines = {std::move(opening)}, .fields = std::move(fields)});
                continue;
            }
            const std::string info{language};
            document.blocks.push_back(Block{.kind = info == "plot" ? BlockKind::plot : BlockKind::codeBlock, .step = step, .info = info, .fields = options});
            continue;
        }

        // `[^label]: text` defines a footnote or citation and never draws in place
        if (body.starts_with("[^")) {
            if (const auto close = body.find("]:"); close != std::string_view::npos && close > 2UZ) {
                flushParagraph();
                document.footnotes.emplace_back(std::string{body.substr(2UZ, close - 2UZ)}, parseInlines(trim(body.substr(close + 2UZ))));
                continue;
            }
        }

        if (body.empty()) {
            flushParagraph();
            continue;
        }
        if (isRule(body)) {
            flushParagraph();
            document.blocks.push_back(Block{.kind = BlockKind::rule, .step = step});
            continue;
        }
        if (body.starts_with('#')) {
            flushParagraph();
            const auto hashes         = std::min(body.find_first_not_of('#'), static_cast<std::size_t>(kMaxHeading));
            const auto [text, anchor] = withoutAnchor(trim(body.substr(hashes)));
            if (hashes == 1UZ) {
                closeWith(); // a slide's blocks never arrive with the last step of the slide before
                sectionStartStep = step;
            }
            document.blocks.push_back(Block{.kind = BlockKind::heading, .level = static_cast<int>(hashes), .step = step, .id = anchor.empty() ? slugOf(text) : std::string{anchor}, .spans = parseInlines(text)});
            continue;
        }
        if (const ListMarker marker = listMarkerOf(line); marker.found) {
            flushParagraph();
            const auto [text, anchor] = withoutAnchor(trim(marker.content));
            document.blocks.push_back(Block{.kind = BlockKind::listItem, .level = static_cast<int>(marker.depth), .ordered = marker.ordered, .step = step, .id = std::string{anchor}, .spans = parseInlines(text)});
            continue;
        }

        if (body.contains('|') && index + 1UZ < lines.size() && isDelimiterRow(trim(lines[index + 1UZ]))) {
            const std::vector<std::string> header     = splitCells(body);
            const std::vector<std::string> delimiters = splitCells(trim(lines[index + 1UZ]));
            if (header.size() == delimiters.size()) { // GFM: a delimiter row of a different width is not a table
                flushParagraph();
                Block table{.kind = BlockKind::table, .step = step};
                for (const std::string& delimiter : delimiters) {
                    table.columns.push_back(alignmentOf(delimiter));
                }
                const auto appendRow = [&table](const std::vector<std::string>& row) {
                    // a short row is padded and anything past the header is dropped, so the table stays rectangular
                    for (std::size_t column = 0UZ; column < table.columns.size(); ++column) {
                        table.cells.push_back(column < row.size() ? parseInlines(row[column]) : std::vector<InlineSpan>{});
                    }
                };
                appendRow(header);
                ++index;
                while (index + 1UZ < lines.size()) {
                    const std::string_view next = trim(lines[index + 1UZ]);
                    if (next.empty() || !next.contains('|')) {
                        break;
                    }
                    appendRow(splitCells(next));
                    ++index;
                }
                document.blocks.push_back(std::move(table));
                continue;
            }
        }
        if (!paragraph.empty()) {
            paragraph.push_back(' ');
        }
        paragraph.append(body);
    }
    flushParagraph();
    closeWith();
    if (insideDirective && openDirectiveIndex < document.blocks.size()) {
        document.blocks[openDirectiveIndex].closed = false;
    }
    return document;
}

} // namespace gr::present
