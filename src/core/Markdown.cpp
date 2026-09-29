#include <gr4-present/Markdown.hpp>
#include <gr4-present/Number.hpp>

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

/**
 * The directives that configure a slide, as against the ones that hold content.
 *
 * A grid's boxes are named by its author, so the parser cannot have a list of them; it can have a list of the
 * names that mean something to the viewer, and treat everything else as a box. `:::left` is then prose for the
 * box called `left`, and its lines are kept as written rather than read as settings -- a sentence with a colon
 * in it is a sentence, which is the same reason `:::notes` keeps its own.
 */
constexpr int kMaxHeading = 6;

using gr::trigger::detail::trim;

/**
 * The options a fence carries in braces: `{shrink=off centre}`.
 *
 * Space separated, either `key=value` or a bare flag, which is the same as `key=on`. They join the block's
 * fields, so a box reads them the way it reads everything else it was told.
 */
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

/// splits `text {#anchor}` into its text and anchor
[[nodiscard]] std::pair<std::string_view, std::string_view> withoutAnchor(std::string_view text) noexcept {
    if (!text.ends_with('}')) {
        return {text, {}};
    }
    const auto open = text.rfind("{#");
    if (open == std::string_view::npos) {
        return {text, {}};
    }
    return {trim(text.substr(0UZ, open)), text.substr(open + 2UZ, text.size() - open - 3UZ)};
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
    return Match{.consumed = targetEnd + 1UZ, .span = InlineSpan{.kind = image ? InlineKind::image : InlineKind::link, .text = std::string{text.substr(labelStart, labelEnd - labelStart)}, .target = std::string{text.substr(labelEnd + 2UZ, targetEnd - labelEnd - 2UZ)}}};
}

/// `[words]{font=hand size=80%}`, a bracketed span: the words, and the face and size they are set in
struct StyledSpan {
    std::size_t      consumed = 0UZ; // zero when this is no styled span
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

/// a run delimited by `marker`, e.g. `**strong**`
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

/// a code line that is only a comment holding `@step` or `@step N`: N, zero for the next step, nothing for any other line
[[nodiscard]] std::optional<int> codeStepMarker(std::string_view line) noexcept {
    for (const std::string_view comment : {std::string_view{"//"}, std::string_view{"#"}, std::string_view{"--"}, std::string_view{";"}, std::string_view{"%"}}) {
        if (!line.starts_with(comment)) {
            continue;
        }
        std::string_view rest = trim(line.substr(comment.size()));
        if (!rest.starts_with("@step")) {
            return std::nullopt;
        }
        rest = trim(rest.substr(5UZ));
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
        if (text.size() >= tag.size() && std::ranges::equal(text.substr(0UZ, tag.size()), tag, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; })) {
            return tag.size();
        }
    }
    return 0UZ;
}

[[nodiscard]] std::vector<InlineSpan> parseInlines(std::string_view text) {
    std::vector<InlineSpan> spans;
    std::size_t             plain = 0UZ;

    // a backslash writes the character after it, rather than that character meaning what it usually means
    constexpr std::string_view kEscapable = "*_`$[]!\\";

    for (std::size_t index = 0UZ; index < text.size();) {
        const std::string_view rest = text.substr(index);
        if (rest.size() > 1UZ && rest.front() == '\\' && kEscapable.contains(rest[1UZ])) {
            // without this the backslash was kept and the star eaten, which is neither reading of what was written
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
                    inner.font = inner.font.empty() ? styled.font : inner.font; // a span inside keeps its own
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
    const std::string_view body   = line.substr(indent);
    if (body.starts_with("- ") || body.starts_with("* ")) {
        return ListMarker{.found = true, .ordered = false, .depth = indent / 2UZ, .content = body.substr(2UZ)};
    }
    const auto dot = body.find(". ");
    if (dot != std::string_view::npos && dot > 0UZ && body.substr(0UZ, dot).find_first_not_of("0123456789") == std::string_view::npos) {
        return ListMarker{.found = true, .ordered = true, .depth = indent / 2UZ, .content = body.substr(dot + 2UZ)};
    }
    return {};
}

} // namespace

bool isConfigurationDirective(std::string_view name) noexcept { return name == "layout" || name == "regions" || name == "place" || name == "grid" || name == "view" || name == "video" || name == "qr" || name == "references" || name == "gr4"; }

bool isLayoutSetting(std::string_view key) noexcept {
    constexpr std::array<std::string_view, 12> kSettings{"anchor", "transition", "region", "via", "camera", "grid", "duration", "advance", "rotate", "font", "size", "outline"};
    return std::ranges::contains(kSettings, key);
}

SchemeFiles schemeFilesOf(std::string_view value) noexcept {
    const std::size_t bar = value.find('|');
    if (bar == std::string_view::npos) {
        return {.light = trim(value), .dark = trim(value)};
    }
    return {.light = trim(value.substr(0UZ, bar)), .dark = trim(value.substr(bar + 1UZ))};
}

std::string_view Block::field(std::string_view name) const noexcept {
    const auto entry = std::ranges::find(fields, name, &std::pair<std::string, std::string>::first);
    return entry == fields.end() ? std::string_view{} : std::string_view{entry->second};
}

const Document::StepReveal* Document::revealOf(int step) const noexcept {
    const auto found = std::ranges::find(stepReveals, step, &StepReveal::step);
    return found == stepReveals.end() ? nullptr : &*found;
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

    // every view carries the whole list: a footnote is defined once, wherever the author put it, and a view draws
    // only the ones it actually refers to. What the document says about itself travels the same way, because the
    // footer of every slide is made of it and a slide is drawn from its own section alone.
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
        // only the delays of steps this section owns, renumbered the way its blocks were
        const int last = base + section.document.stepCount();
        for (const auto& [number, seconds] : document.stepDelays) {
            if (number > base && number < last) {
                section.document.stepDelays.emplace_back(number - base, seconds);
            }
        }
        for (const Document::StepReveal& reveal : document.stepReveals) {
            if (reveal.step > base && reveal.step < last) {
                section.document.stepReveals.push_back(Document::StepReveal{.step = reveal.step - base, .kind = reveal.kind, .seconds = reveal.seconds});
            }
        }
    }
    return sections;
}

/**
 * Reads and removes the front matter, when the very first line is `---`.
 *
 * Only the first line: `---` means a rule above a paragraph and the line between a plot's header and its rows
 * everywhere else, and a document that begins with a rule is a document, not a document with settings.
 */
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
            value = value.substr(1UZ, value.size() - 2UZ); // quoted as YAML would quote it, read as YAML would read it
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
    return run >= opened && trim(body.substr(run)).empty();
}

/// the source as lines without their `%%` and `<!-- -->` comments, and which lines held nothing else. Code blocks and
/// inline code keep theirs, where they are text; the line count stays, so a warning names the line the author sees.
struct CommentFreeLines {
    std::vector<std::string> text;
    std::vector<bool>        onlyComment;
};

[[nodiscard]] CommentFreeLines withoutComments(std::string_view source) {
    CommentFreeLines result;
    std::size_t      openFence     = 0UZ; // the backticks of the code block the lines are in; 0 outside one
    bool             insideComment = false;
    for (const auto lineRange : source | std::views::split('\n')) {
        std::string_view line{lineRange};
        if (line.ends_with('\r')) {
            line.remove_suffix(1UZ);
        }
        if (openFence > 0UZ || (!insideComment && fenceLength(trim(line)) > 0UZ)) {
            openFence = openFence > 0UZ ? (closesFence(trim(line), openFence) ? 0UZ : openFence) : fenceLength(trim(line));
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

Document parseMarkdown(std::string_view source) {
    Document document;
    document.front           = frontMatterOf(source);
    int         step         = 0;
    int         openSteps    = 0;
    bool        insideCode   = false;
    std::size_t openFence    = 0UZ; // the backticks the open code block began with; only as many or more close it
    int         sectionStart = 0;   // the step counter where the current slide began, for `@step N`
    // `:::step {with}` adds to the step before rather than starting one: the blocks from here to the next step arrive
    // together with that step's, each set how its own `in=` says
    std::optional<std::size_t> withFrom;
    std::string                withKind;
    std::string                withDirection;
    float                      withSeconds = 0.4f;
    const auto                 closeWith   = [&] {
        if (withFrom) {
            for (Block& joined : document.blocks | std::views::drop(*withFrom)) {
                joined.revealKind    = withKind;
                joined.revealSeconds = withSeconds;
                joined.revealFrom    = withDirection;
            }
            withFrom.reset();
        }
    };
    bool        insideFormula   = false;
    bool        insideDirective = false;
    bool        insideBoxCode   = false;
    std::size_t boxFence        = 0UZ; // as `openFence`, for a code block inside a box
    std::size_t boxDepth        = 0UZ; // fences opened inside a content box, so its own closing fence is known
    std::size_t openDirective   = 0UZ; // the block the open `:::` fence made, to mark it if the fence is never closed
    std::string paragraph;

    /// whether the lines being read are a content box's prose, whose fences are its own and not the slide's
    const auto insideBox = [&] { return insideDirective && !document.blocks.empty() && document.blocks.back().kind == BlockKind::directive && !isConfigurationDirective(document.blocks.back().info); };

    const auto flushParagraph = [&] {
        const std::string_view body = trim(paragraph);
        if (!body.empty()) {
            const auto [text, anchor] = withoutAnchor(body);
            document.blocks.push_back(Block{.kind = BlockKind::paragraph, .level = 0, .ordered = false, .step = step, .id = std::string{anchor}, .info = {}, .spans = parseInlines(text), .lines = {}, .fields = {}, .columns = {}, .cells = {}});
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
        if (insideCode) {
            Block& code = document.blocks.back();
            if (closesFence(body, openFence)) {
                insideCode = false;
            } else if (const std::optional<int> marked = codeStepMarker(body); marked.has_value() && code.kind == BlockKind::codeBlock) {
                // `// @step` and its kin start the lines a further reveal step shows; the marker itself is not code
                step = *marked > 0 ? std::max(step + 1, sectionStart + *marked) : step + 1;
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
        if (insideBox() && (insideBoxCode || fenceLength(body) > 0UZ)) {
            if (!insideBoxCode) {
                insideBoxCode = true;
                boxFence      = fenceLength(body);
            } else if (closesFence(body, boxFence)) {
                insideBoxCode = false;
            }
            document.blocks.back().lines.emplace_back(line);
            continue;
        }

        // Pandoc's slide pause, a paragraph of three spaced dots: the same as a bare `:::step`; in a box it is the
        // box's own and read when the box is
        if (body == kPause && !insideDirective) {
            flushParagraph();
            closeWith();
            ++step;
            continue;
        }

        if (body.starts_with(kDirectiveFence)) {
            flushParagraph();
            // Pandoc's rule: three or more colons are a fence, and one without a name closes the innermost open box
            // whatever its length, so `::::` around a box with `:::` inside reads as nested rather than as a box named `:`
            std::string_view name = trim(body.substr(std::min(body.find_first_not_of(':'), body.size())));
            name                  = trim(name.substr(0UZ, name.find_last_not_of(':') + 1UZ)); // `::: Warning ::::::`, as Pandoc allows

            // A fence inside a content box belongs to that box. Its lines are its own prose and are parsed again
            // on their own, so a `:::step` written inside one is that box's business, not the slide's: it neither
            // advances the slide's counter nor takes the box's closing fence for its own.
            if (insideBox()) {
                if (name.empty() && boxDepth == 0UZ) {
                    insideDirective = false;
                    continue; // the box's own closing fence
                }
                if (name.empty()) {
                    --boxDepth;
                } else {
                    ++boxDepth;
                }
                document.blocks.back().lines.emplace_back(line);
                continue;
            }

            // `:::left {shrink=off}` -- the braces say how the box behaves, and are no part of its name
            const auto brace   = name.find('{');
            const auto options = brace == std::string_view::npos ? std::vector<std::pair<std::string, std::string>>{} : fenceOptionsOf(name.substr(brace));
            if (brace != std::string_view::npos) {
                name = trim(name.substr(0UZ, brace));
            }
            if (name.empty()) { // the closing fence
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
                const auto option = [&options](std::string_view key) { return std::ranges::find(options, key, &std::pair<std::string, std::string>::first); };
                if (option("with") != options.end() && step > sectionStart) {
                    withFrom      = document.blocks.size();
                    withKind      = option("in") != options.end() ? option("in")->second : std::string{};
                    withDirection = option("from") != options.end() ? option("from")->second : std::string{};
                    withSeconds   = 0.4f;
                    if (const auto dur = option("dur"); dur != options.end()) {
                        withSeconds = parseSeconds(dur->second).value_or(withSeconds);
                    }
                    if (option("after") != options.end()) {
                        document.warnings.push_back(std::format("line {}: a step `with` the one before has no `after=` of its own", index + 1UZ));
                    }
                    continue;
                }
                ++step;
                const auto after = std::ranges::find(options, "after", &std::pair<std::string, std::string>::first);
                if (after != options.end()) {
                    if (const std::optional<float> seconds = parseSeconds(after->second); seconds) {
                        document.stepDelays.emplace_back(step, *seconds);
                    }
                }
                if (const auto in = std::ranges::find(options, "in", &std::pair<std::string, std::string>::first); in != options.end()) {
                    Document::StepReveal reveal{.step = step, .kind = in->second, .seconds = 0.4f, .from = {}};
                    if (const auto from = std::ranges::find(options, "from", &std::pair<std::string, std::string>::first); from != options.end()) {
                        reveal.from = from->second;
                    }
                    if (const auto dur = std::ranges::find(options, "dur", &std::pair<std::string, std::string>::first); dur != options.end()) {
                        if (const std::optional<float> seconds = parseSeconds(dur->second); seconds && *seconds > 0.0f) {
                            reveal.seconds = *seconds;
                        }
                    }
                    document.stepReveals.push_back(reveal);
                }
                continue;
            }
            if (name == "stop") {
                ++step; // a camera stop is a reveal step that also carries the words shown while it is current
            }
            insideDirective = true;
            boxDepth        = 0UZ;
            // Any other directive keeps its name in `info` and its `key: value` lines in `fields`. A content box
            // has no such lines -- they are its prose -- so its fields are its options and nothing else. A
            // configuring directive keeps none: every unreserved key it carries names an area to fill, and an
            // option would be read as one.
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
            document.blocks.push_back(Block{.kind = BlockKind::directive, .level = 0, .ordered = false, .step = step, .id = std::move(id), .info = std::string{name}, .spans = {}, .lines = {}, .fields = std::move(fields), .columns = {}, .cells = {}});
            openDirective = document.blocks.size() - 1UZ;
            continue;
        }

        // `:::notes` carries prose for the presenter, not settings, so its lines are kept as written -- a sentence
        // with a colon in it is a sentence
        if (insideDirective && !document.blocks.empty() && document.blocks.back().kind == BlockKind::directive && !isConfigurationDirective(document.blocks.back().info)) {
            // a content box, or the presenter's notes: either way the lines are its own and not the slide's flow
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

        // `$$ ... $$`, on one line or over several: a display formula, as against the `$ ... $` of running text
        if (body.starts_with(kMathFence)) {
            flushParagraph();
            Block formula{.kind = BlockKind::formula, .level = 0, .ordered = false, .step = step, .id = {}, .info = {}, .spans = {}, .lines = {}, .fields = {}, .columns = {}, .cells = {}};
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
            insideCode = true;
            openFence  = fence;
            // a ```plot block is data rather than source, so it is its own kind; its lines are collected the same way
            // the same braces a `:::` fence takes: the language is the name, and what follows in braces is how the
            // block behaves. Without this the language read as `cpp {striped}` and nothing was highlighted.
            std::string_view language = trim(body.substr(fence));
            const auto       brace    = language.find('{');
            const auto       options  = brace == std::string_view::npos ? std::vector<std::pair<std::string, std::string>>{} : fenceOptionsOf(language.substr(brace));
            if (brace != std::string_view::npos) {
                language = trim(language.substr(0UZ, brace));
            }
            const std::string info{language};
            document.blocks.push_back(Block{.kind = info == "plot" ? BlockKind::plot : BlockKind::codeBlock, .level = 0, .ordered = false, .step = step, .id = {}, .info = info, .spans = {}, .lines = {}, .fields = options, .columns = {}, .cells = {}});
            continue;
        }

        // `[^label]: text` defines a footnote or a citation; it is not a block of its own and never draws in place
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
            document.blocks.push_back(Block{.kind = BlockKind::rule, .level = 0, .ordered = false, .step = step, .id = {}, .info = {}, .spans = {}, .lines = {}, .fields = {}, .columns = {}, .cells = {}});
            continue;
        }
        if (body.starts_with('#')) {
            flushParagraph();
            const auto hashes         = std::min(body.find_first_not_of('#'), static_cast<std::size_t>(kMaxHeading));
            const auto [text, anchor] = withoutAnchor(trim(body.substr(hashes)));
            if (hashes == 1UZ) {
                closeWith(); // a slide's blocks never arrive with the last step of the slide before
                sectionStart = step;
            }
            document.blocks.push_back(Block{.kind = BlockKind::heading, .level = static_cast<int>(hashes), .ordered = false, .step = step, .id = anchor.empty() ? slugOf(text) : std::string{anchor}, .info = {}, .spans = parseInlines(text), .lines = {}, .fields = {}, .columns = {}, .cells = {}});
            continue;
        }
        if (const ListMarker marker = listMarkerOf(line); marker.found) {
            flushParagraph();
            const auto [text, anchor] = withoutAnchor(trim(marker.content));
            document.blocks.push_back(Block{.kind = BlockKind::listItem, .level = static_cast<int>(marker.depth), .ordered = marker.ordered, .step = step, .id = std::string{anchor}, .info = {}, .spans = parseInlines(text), .lines = {}, .fields = {}, .columns = {}, .cells = {}});
            continue;
        }

        if (body.contains('|') && index + 1UZ < lines.size() && isDelimiterRow(trim(lines[index + 1UZ]))) {
            const std::vector<std::string> header     = splitCells(body);
            const std::vector<std::string> delimiters = splitCells(trim(lines[index + 1UZ]));
            if (header.size() == delimiters.size()) { // GFM: a delimiter row of a different width is not a table
                flushParagraph();
                Block table{.kind = BlockKind::table, .level = 0, .ordered = false, .step = step, .id = {}, .info = {}, .spans = {}, .lines = {}, .fields = {}, .columns = {}, .cells = {}};
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
                ++index; // the delimiter row itself
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
    if (insideDirective && openDirective < document.blocks.size()) {
        document.blocks[openDirective].closed = false;
    }
    return document;
}

} // namespace gr::present
