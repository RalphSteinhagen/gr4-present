#ifndef GR4_PRESENT_MARKDOWN_HPP
#define GR4_PRESENT_MARKDOWN_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

enum class InlineKind { text, emphasis, strong, code, link, image, math, footnote, lineBreak }; // `lineBreak`: a written `<br>`

struct InlineSpan {
    InlineKind  kind = InlineKind::text;
    std::string text;      // the visible text; for an image, its alternative text
    std::string target;    // link or image reference, resolved against the package root by the caller
    std::string font = {}; // `[words]{font=hand}`: a role or a face the deck names; empty is the surrounding face
    std::string size = {}; // `[words]{size=80%}`, as written; empty is the surrounding size

    bool operator==(const InlineSpan&) const = default;
};

/// how a table column's cells sit in the width the column was given
enum class Alignment : std::uint8_t { left, centre, right };

enum class BlockKind { paragraph, heading, listItem, codeBlock, rule, directive, table, plot, formula };

/// how revealed blocks arrive: the `in=`, `from=` and `dur=` of a `:::step`
struct Reveal {
    std::string          kind    = {}; // fade, rise, wipe, grow or an effect's name; empty is at once
    std::optional<float> seconds = {}; // `dur=`, positive; absent is as long as its way of arriving takes
    std::string          from    = {}; // `from=left|right|above|below`: where a rise comes from, or a wipe starts; empty is below, or left

    bool operator==(const Reveal&) const = default;
};

/// one block of a presentation document; a reveal step belongs to the block, and a `:::step` fence numbers every
/// block after it
struct Block {
    BlockKind   kind    = BlockKind::paragraph;
    int         level   = 0;     // heading level 1-6, or list nesting depth from 0
    bool        ordered = false; // a numbered rather than a bulleted list item
    int         step    = 0;     // the reveal step this block first appears at
    std::string id      = {};    // stable anchor: an explicit {#id}, else derived from a heading's text
    std::string info    = {};    // fenced-code language, or the directive's name: gr4, layout, ...

    std::vector<InlineSpan>  spans = {}; // paragraph, heading and list-item content
    std::vector<std::string> lines = {}; // fenced-code content, verbatim

    std::vector<std::pair<std::string, std::string>> fields = {}; // a directive's `key: value` lines

    std::vector<Alignment>               columns = {}; // table: one entry per column, taken from the delimiter row
    std::vector<std::vector<InlineSpan>> cells   = {}; // table: row-major and rectangular, the header row first

    bool             closed    = true; // a directive whose `:::` never came, and which therefore took every line after it
    std::vector<int> lineSteps = {};   // a code block revealed in steps: the step each of its `lines` first shows at; empty shows all

    Reveal reveal = {}; // `:::step {with in=...}`: arrives with the step before, in its own way; an empty kind is the step's

    [[nodiscard]] std::string_view field(std::string_view name) const noexcept;
    /// the name a region is known by: its anchor, else its directive's name
    [[nodiscard]] std::string_view name() const noexcept { return id.empty() ? std::string_view{info} : std::string_view{id}; }

    [[nodiscard]] std::size_t rowCount() const noexcept { return columns.empty() ? 0UZ : cells.size() / columns.size(); }

    /// row 0 is the header; a row shorter than the header was padded when the table was parsed, so this never fails
    [[nodiscard]] const std::vector<InlineSpan>& cellAt(std::size_t row, std::size_t column) const noexcept { return cells[row * columns.size() + column]; }

    bool operator==(const Block&) const = default;
};

/// the document's own metadata, from YAML front matter when `---` is the very first line; elsewhere `---` stays a rule
struct FrontMatter {
    std::string title;
    std::string author;
    std::string email;
    std::string numbering; // none, number or number-of-total; empty means number

    bool operator==(const FrontMatter&) const = default;
};

struct Document {
    std::vector<Block> blocks;
    FrontMatter        front;

    /// `[^label]: text` definitions, in the order they were written; a reference carries the label in its target
    std::vector<std::pair<std::string, std::vector<InlineSpan>>> footnotes;

    /// `:::step {after=2}`: a step that reveals itself that many seconds after the one before it, keyed by step number
    std::vector<std::pair<int, float>> stepDelays;

    /// `:::step {in=fade dur=0.6}`: how the blocks a step reveals arrive, keyed by step number; absent is at once
    struct StepReveal {
        int    step   = 0;
        Reveal reveal = {};

        bool operator==(const StepReveal&) const = default;
    };
    std::vector<StepReveal> stepReveals;

    /// what the parser read past rather than understood, "line 12: ...", for the problems list
    std::vector<std::string> warnings = {};

    [[nodiscard]] int                            stepCount() const noexcept;           // one more than the highest step any block carries
    [[nodiscard]] float                          delayBefore(int step) const noexcept; // seconds after which `step` reveals itself; negative waits for a key
    [[nodiscard]] const Reveal*                  revealOf(int step) const noexcept;    // how `step` arrives; null is at once
    [[nodiscard]] const Block*                   withId(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<InlineSpan>* footnote(std::string_view label) const noexcept;

    bool operator==(const Document&) const = default;
};

/// a view of the presentation: the blocks under one heading, keyed by that heading's stable anchor
struct Section {
    std::string id;
    Document    document;

    bool operator==(const Section&) const = default;
};

/// parses the CommonMark subset the presentation format uses, plus `:::step` and `:::gr4` fenced directives
[[nodiscard]] Document parseMarkdown(std::string_view source);

/// splits a document at headings of `level` or above; blocks before the first heading form a leading section
[[nodiscard]] std::vector<Section> sectionsOf(const Document& document, int level = 1);

/// the prose a `:::<name>` box holds, parsed as a document of its own; the slide's footnotes travel with it
[[nodiscard]] Document boxDocumentOf(std::span<const std::string> lines, const Document& slide);

/// appends each footnote label a block cites, prose then table cells, that `labels` does not hold yet
void appendCitedLabels(const Block& block, std::vector<std::string>& labels);

/// what a deck cites, numbered in reading order across all its sections (IEEE), each once and first occurrence first;
/// a `:::<name>` box counts where it stands in the file; a `#view` link turns to a slide and is no reference
struct Citations {
    std::vector<std::string>                         footnotes;  // labels
    std::vector<std::pair<std::string, std::string>> references; // label, target
};
[[nodiscard]] Citations citationsOf(std::span<const Section> sections);

/// whether a directive's name configures the slide; any other name is a content box whose lines are kept as written
/// the names in a comma-separated field (`next: architecture, demo`, `transitions: fade, fire`), in the author's
/// order; blank entries are dropped
[[nodiscard]] std::vector<std::string> parseNameList(std::string_view list);

/// the value of the first `key` in a list of `key: value` pairs; absent when no pair has that key
[[nodiscard]] std::optional<std::string_view> valueFor(std::span<const std::pair<std::string, std::string>> pairs, std::string_view key) noexcept;

[[nodiscard]] bool isConfigurationDirective(std::string_view name) noexcept;

/// a `:::<name>` directive the viewer gives no meaning of its own, so it is a box that holds prose
[[nodiscard]] bool isBox(const Block& block) noexcept;

/// whether a key of `:::layout` is a setting of the slide; any other key names an area of the master
[[nodiscard]] bool isLayoutSetting(std::string_view key) noexcept;

/// the file an area shows per colour scheme: `light.svg | dark.svg`, or one name for both
struct SchemeFiles {
    std::string_view light;
    std::string_view dark;

    bool operator==(const SchemeFiles&) const = default;
};

[[nodiscard]] SchemeFiles schemeFilesOf(std::string_view value) noexcept;

/// the anchor a heading gets when it carries no explicit `{#id}`: lowercase, spaces and punctuation to hyphens
[[nodiscard]] std::string slugOf(std::string_view heading);

/// the presenter notes of a document, joined from every `:::notes` block it holds; empty when it has none
[[nodiscard]] std::string notesOf(const Document& document);

} // namespace gr::present

#endif // GR4_PRESENT_MARKDOWN_HPP
