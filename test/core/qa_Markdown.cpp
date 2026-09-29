#include <boost/ut.hpp>

#include <gr4-present/Markdown.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <string_view>

using namespace gr::present;
using namespace boost::ut;

namespace {

[[nodiscard]] std::string plainText(const Block& block) {
    std::string text;
    for (const InlineSpan& span : block.spans) {
        text.append(span.text);
    }
    return text;
}

[[nodiscard]] std::string cellText(const Block& table, std::size_t row, std::size_t column) {
    std::string text;
    for (const InlineSpan& span : table.cellAt(row, column)) {
        text.append(span.text);
    }
    return text;
}
} // namespace

// Ground truth is the CommonMark specification's own examples for each construct, transcribed here, plus the two
// fenced directives this format adds. The parser is never consulted about what it ought to produce.
const suite<"Markdown"> markdownTests = [] {
    "an ATX heading carries its level and a slug derived from its text"_test = [] {
        const Document document = parseMarkdown("### Frequency domain\n");
        expect(eq(document.blocks.size(), 1UZ));
        expect(document.blocks.front().kind == BlockKind::heading);
        expect(eq(document.blocks.front().level, 3));
        expect(eq(plainText(document.blocks.front()), std::string{"Frequency domain"}));
        expect(eq(document.blocks.front().id, std::string{"frequency-domain"}));
    };

    "an explicit anchor overrides the derived slug"_test = [] {
        const Document document = parseMarkdown("# Overview {#intro}\n");
        expect(eq(document.blocks.front().id, std::string{"intro"}));
        expect(eq(plainText(document.blocks.front()), std::string{"Overview"}));
        expect(document.withId("intro") != nullptr);
        expect(document.withId("overview") == nullptr);
    };

    "consecutive lines form one paragraph and a blank line ends it"_test = [] {
        const Document document = parseMarkdown("first line\nsecond line\n\na second paragraph\n");
        expect(eq(document.blocks.size(), 2UZ));
        expect(eq(plainText(document.blocks.front()), std::string{"first line second line"}));
        expect(eq(plainText(document.blocks.back()), std::string{"a second paragraph"}));
    };

    "emphasis, strong emphasis and code spans are separated from their surrounding text"_test = [] {
        const Document document = parseMarkdown("plain *emphasised* and **strong** and `literal` text\n");
        const auto&    spans    = document.blocks.front().spans;
        expect(eq(spans.size(), 7UZ)) << "expected text, em, text, strong, text, code, text";
        expect(spans[0].kind == InlineKind::text);
        expect(eq(spans[0].text, std::string{"plain "}));
        expect(spans[1].kind == InlineKind::emphasis);
        expect(eq(spans[1].text, std::string{"emphasised"}));
        expect(spans[3].kind == InlineKind::strong);
        expect(eq(spans[3].text, std::string{"strong"}));
        expect(spans[5].kind == InlineKind::code);
        expect(eq(spans[5].text, std::string{"literal"}));
    };

    // Ground truth: Pandoc's bracketed span, `[words]{key=value}`, with the attributes this viewer reads
    "a bracketed span sets its words in a face and a size, and the text around it keeps its own"_test = [] {
        const Document                document = parseMarkdown("say [hello there]{font=hand size=80%} to all\n");
        const std::vector<InlineSpan> expected{
            InlineSpan{.kind = InlineKind::text, .text = "say ", .target = {}},
            InlineSpan{.kind = InlineKind::text, .text = "hello there", .target = {}, .font = "hand", .size = "80%"},
            InlineSpan{.kind = InlineKind::text, .text = " to all", .target = {}},
        };
        expect(document.blocks.front().spans == expected);
    };

    "a span's words keep their emphasis, code and citations, each in the span's face"_test = [] {
        const Document                document = parseMarkdown("[a **bold** `x`[^cite]]{font=mono}\n");
        const std::vector<InlineSpan> expected{
            InlineSpan{.kind = InlineKind::text, .text = "a ", .target = {}, .font = "mono"},
            InlineSpan{.kind = InlineKind::strong, .text = "bold", .target = {}, .font = "mono"},
            InlineSpan{.kind = InlineKind::text, .text = " ", .target = {}, .font = "mono"},
            InlineSpan{.kind = InlineKind::code, .text = "x", .target = {}, .font = "mono"},
            InlineSpan{.kind = InlineKind::footnote, .text = "cite", .target = "cite", .font = "mono"},
        };
        expect(document.blocks.front().spans == expected);
    };

    "a span inside a span keeps its own face and takes the outer one's size"_test = [] {
        const Document                document = parseMarkdown("[a [b]{font=hand}]{font=mono size=12pt}\n");
        const std::vector<InlineSpan> expected{
            InlineSpan{.kind = InlineKind::text, .text = "a ", .target = {}, .font = "mono", .size = "12pt"},
            InlineSpan{.kind = InlineKind::text, .text = "b", .target = {}, .font = "hand", .size = "12pt"},
        };
        expect(document.blocks.front().spans == expected);
    };

    "links, citations, plain brackets and other braces are not spans"_test = [] {
        const auto spansOf = [](std::string_view source) { return parseMarkdown(std::string{source} + "\n").blocks.front().spans; };
        const auto link    = spansOf("[site](https://example.org){font=hand}");
        expect(eq(link.size(), 2UZ) && link[0].kind == InlineKind::link && link[0].font.empty()) << "a link is a link, and the braces after it text";
        const auto citation = spansOf("[^cooley]{size=80%}");
        expect(eq(citation.size(), 2UZ) && citation[0].kind == InlineKind::footnote);
        for (const std::string_view literal : {"[x]{#anchor}", "[x] {font=hand}", "[x]", "[x]{font=hand", "\\[x]{font=hand}"}) {
            const auto spans = spansOf(literal);
            expect(std::ranges::all_of(spans, [](const InlineSpan& span) { return span.font.empty() && span.size.empty(); })) << literal << " was taken as a span";
        }
    };

    "an unclosed delimiter stays literal text"_test = [] {
        const Document document = parseMarkdown("a * b c\n");
        expect(eq(document.blocks.front().spans.size(), 1UZ));
        expect(document.blocks.front().spans.front().kind == InlineKind::text);
        expect(eq(plainText(document.blocks.front()), std::string{"a * b c"}));
    };

    "links and images keep their target separate from their text"_test = [] {
        const Document document = parseMarkdown("see [the manual](docs/manual.md) and ![a diagram](figures/system.svg)\n");
        const auto&    spans    = document.blocks.front().spans;
        const auto     link     = spans[1];
        expect(link.kind == InlineKind::link);
        expect(eq(link.text, std::string{"the manual"}));
        expect(eq(link.target, std::string{"docs/manual.md"}));
        const auto image = spans[3];
        expect(image.kind == InlineKind::image);
        expect(eq(image.text, std::string{"a diagram"}));
        expect(eq(image.target, std::string{"figures/system.svg"}));
    };

    "bulleted and numbered items are distinguished and nesting sets the level"_test = [] {
        const Document document = parseMarkdown("- first\n- second\n  - nested\n1. one\n2. two\n");
        expect(eq(document.blocks.size(), 5UZ));
        expect(document.blocks[0].kind == BlockKind::listItem);
        expect(!document.blocks[0].ordered);
        expect(eq(document.blocks[0].level, 0));
        expect(eq(document.blocks[2].level, 1)) << "two spaces of indent is one level";
        expect(document.blocks[3].ordered);
        expect(eq(plainText(document.blocks[3]), std::string{"one"}));
    };

    "a fenced code block keeps its language and its content verbatim"_test = [] {
        const Document document = parseMarkdown("```cpp\nint main() {\n    return 0; // *not* emphasis\n}\n```\n\nafter\n");
        expect(eq(document.blocks.size(), 2UZ)) << "the closing fence must not open a second block";
        const Block& code = document.blocks.front();
        expect(code.kind == BlockKind::codeBlock);
        expect(eq(code.info, std::string{"cpp"}));
        expect(eq(code.lines.size(), 3UZ));
        expect(eq(code.lines[1], std::string{"    return 0; // *not* emphasis"})) << "indentation and markup survive unchanged";
        expect(eq(plainText(document.blocks.back()), std::string{"after"}));
    };

    "a thematic break is its own block"_test = [] {
        const Document document = parseMarkdown("above\n\n---\n\nbelow\n");
        expect(eq(document.blocks.size(), 3UZ));
        expect(document.blocks[1].kind == BlockKind::rule);
    };

    "a step directive advances the reveal step of every block it contains"_test = [] {
        const Document document = parseMarkdown("always visible\n\n:::step\nrevealed second\n:::\n\nalso always visible\n");
        expect(eq(document.blocks.size(), 3UZ));
        expect(eq(document.blocks[0].step, 0));
        expect(eq(document.blocks[1].step, 1));
        expect(eq(document.blocks[2].step, 1)) << "the counter does not rewind when a step closes";
        expect(eq(document.stepCount(), 2));
    };

    "successive step directives number consecutively"_test = [] {
        const Document document = parseMarkdown(":::step\none\n:::\n:::step\ntwo\n:::\n:::step\nthree\n:::\n");
        expect(eq(document.stepCount(), 4));
        expect(eq(document.blocks[0].step, 1));
        expect(eq(document.blocks[2].step, 3));
    };

    "a timed step names its delay and a manual step waits for a key"_test = [] {
        const Document document = parseMarkdown(":::step\nby key\n:::\n:::step {after=2}\ntimed\n:::\n:::step {after=0.5}\nquick\n:::\n:::step\nby key again\n:::\n");
        expect(eq(document.stepCount(), 5));
        expect(lt(document.delayBefore(1), 0.0f)) << "no delay waits for a key";
        expect(eq(document.delayBefore(2), 2.0f));
        expect(eq(document.delayBefore(3), 0.5f));
        expect(lt(document.delayBefore(4), 0.0f));
        expect(lt(document.delayBefore(0), 0.0f));
    };

    "a camera stop is a reveal step that keeps its own words and options"_test = [] {
        const Document document = parseMarkdown("intro\n\n:::stop {region=\"0.25 0.5 0.1 0.2\" box=left duration=2}\n**Bragg**\nNobel Prize(s): 1915\n:::\n\n:::stop\n:::\n");
        expect(eq(document.stepCount(), 3)) << "two stops after the opening view";
        expect(eq(document.blocks.size(), 3UZ));
        const Block& first = document.blocks[1];
        expect(first.kind == BlockKind::directive && first.info == "stop");
        expect(eq(first.step, 1));
        expect(eq(first.field("region"), std::string_view{"0.25 0.5 0.1 0.2"})) << "a quoted value keeps its spaces and loses its quotes";
        expect(eq(first.field("box"), std::string_view{"left"}));
        expect(eq(first.field("duration"), std::string_view{"2"}));
        expect(eq(first.lines.size(), 2UZ));
        expect(eq(first.lines[0], std::string{"**Bragg**"}));
        const Block& last = document.blocks[2];
        expect(eq(last.step, 2));
        expect(last.field("region").empty() && last.lines.empty()) << "an empty stop goes back to the whole picture";
    };

    "an unquoted fence option still ends at the next space"_test = [] {
        const Document document = parseMarkdown(":::left {shrink=off size=14pt}\ntext\n:::\n");
        expect(eq(document.blocks.front().field("shrink"), std::string_view{"off"}));
        expect(eq(document.blocks.front().field("size"), std::string_view{"14pt"}));
    };

    "a box whose fence is never closed is marked, and one that is closed is not"_test = [] {
        const Document open = parseMarkdown(":::left\nsome prose\n\n# Next slide\n\nmore\n");
        expect(eq(open.blocks.size(), 1UZ)) << "an open box takes every line after it, the heading included";
        expect(!open.blocks.front().closed);
        const Document shut = parseMarkdown(":::left\nsome prose\n:::\n\n# Next slide\n");
        expect(shut.blocks.front().closed);
        const Document layout = parseMarkdown(":::layout\nsource: a.svg\n\nprose without a colon\n");
        expect(!layout.blocks.front().closed) << "a configuring directive left open is marked as well";
    };

    "a written <br> breaks the line, in any of its spellings, but not inside code"_test = [] {
        const Document document = parseMarkdown("one<br>two<BR/>three<br />four `a<br>b`\n");
        const auto&    spans    = document.blocks.front().spans;
        expect(eq(std::ranges::count(spans, InlineKind::lineBreak, &InlineSpan::kind), 3L));
        expect(eq(spans.front().text, std::string{"one"}));
        expect(spans.back().kind == InlineKind::code && spans.back().text == "a<br>b") << "code keeps what it says";
        const Document title = parseMarkdown("# Title<br>Sub-title\n");
        expect(eq(std::ranges::count(title.blocks.front().spans, InlineKind::lineBreak, &InlineSpan::kind), 1L));
    };

    "an @step comment in code starts the lines a further step reveals, and is not itself code"_test = [] {
        const Document document = parseMarkdown("```cpp\nint a = 1;\n// @step\nint b = 2;\nint c = 3;\n# @step 3\nint d = 4;\n```\n\nafter\n");
        const Block&   code     = document.blocks.front();
        expect(eq(code.lines.size(), 4UZ)) << "the markers are dropped";
        expect(eq(code.lines[1], std::string{"int b = 2;"}));
        expect(code.lineSteps == std::vector<int>{0, 1, 1, 3}) << "@step is the next step, @step 3 the slide's third";
        expect(eq(document.stepCount(), 4));
        expect(eq(document.blocks.back().step, 3)) << "what follows the block appears with its last lines";
        expect(parseMarkdown("```cpp\n#include <x>\n```\n").blocks.front().lineSteps.empty()) << "a preprocessor line is not a marker";
    };

    "@step numbers count from the slide the code is on"_test = [] {
        const auto sections = sectionsOf(parseMarkdown("# one\n\n:::step\nx\n:::\n\n# two\n\n```py\na = 1\n# @step 2\nb = 2\n```\n"));
        expect(eq(sections.size(), 2UZ));
        if (sections.size() == 2UZ) {
            const Block& code = sections[1].document.blocks[1];
            expect(code.lineSteps == std::vector<int>{0, 2});
            expect(eq(sections[1].document.stepCount(), 3));
        }
    };

    "a document without directives has exactly one step"_test = [] {
        expect(eq(parseMarkdown("# title\n\nbody\n").stepCount(), 1));
        expect(eq(parseMarkdown("").stepCount(), 1));
    };

    "a live-region directive collects its fields and is addressable by id"_test = [] {
        const Document document = parseMarkdown(":::gr4\nid: spectrum\nworkflow: workflows/demo.grc\nwidget: FFT Spectrum\n:::\n");
        expect(eq(document.blocks.size(), 1UZ));
        const Block& region = document.blocks.front();
        expect(region.kind == BlockKind::directive);
        expect(eq(region.info, std::string{"gr4"}));
        expect(eq(region.field("workflow"), std::string_view{"workflows/demo.grc"}));
        expect(eq(region.field("widget"), std::string_view{"FFT Spectrum"}));
        expect(region.field("category").empty()) << "a field that was not given reads empty";
        expect(document.withId("spectrum") == &region);
    };

    "prose after a directive is prose, even when it contains a colon"_test = [] {
        const Document document = parseMarkdown(":::gr4\nworkflow: demo.grc\n:::\n\nThe field is named `region:`, so the author decides where it goes.\n");
        expect(eq(document.blocks.size(), 2UZ));
        const Block& region = document.blocks.front();
        expect(region.kind == BlockKind::directive);
        expect(eq(region.fields.size(), 1UZ)) << "the sentence after the closing fence must not become a field";
        expect(eq(region.field("workflow"), std::string_view{"demo.grc"}));
        expect(document.blocks.back().kind == BlockKind::paragraph);
    };

    "a live region inside a step carries that step"_test = [] {
        const Document document = parseMarkdown(":::step\n:::gr4\nworkflow: demo.grc\n:::\n:::\n");
        expect(eq(document.blocks.front().step, 1));
    };

    "the specification's own composition example parses into its parts"_test = [] {
        const Document document = parseMarkdown("# FFT demonstration\n\nThe graph below is running inside GNU Radio 4.0.\n\n![architecture](figures/filter.svg)\n\n:::gr4\nworkflow: workflows/filter.grc\nwidget: FFT Spectrum\n:::\n");
        expect(eq(document.blocks.size(), 4UZ));
        expect(document.blocks[0].kind == BlockKind::heading);
        expect(document.blocks[1].kind == BlockKind::paragraph);
        expect(document.blocks[2].spans.front().kind == InlineKind::image);
        expect(document.blocks[3].kind == BlockKind::directive);
        expect(eq(document.blocks[3].field("widget"), std::string_view{"FFT Spectrum"}));
    };

    "carriage returns and a missing trailing newline are tolerated"_test = [] {
        const Document windows = parseMarkdown("# title\r\n\r\nbody\r\n");
        expect(eq(windows.blocks.size(), 2UZ));
        expect(eq(plainText(windows.blocks.back()), std::string{"body"}));
        expect(eq(parseMarkdown("# title").blocks.size(), 1UZ));
    };

    "an empty document has no blocks"_test = [] {
        expect(parseMarkdown("").blocks.empty());
        expect(parseMarkdown("\n\n   \n").blocks.empty());
    };

    // The table cases below are the GFM specification's own examples, by its numbering, transcribed unchanged.
    "a table takes its columns from the header and its rows from the lines below"_test = [] {
        // GFM example 198
        const Document document = parseMarkdown("| foo | bar |\n| --- | --- |\n| baz | bim |\n");
        expect(eq(document.blocks.size(), 1UZ));
        const Block& table = document.blocks.front();
        expect(table.kind == BlockKind::table);
        expect(eq(table.columns.size(), 2UZ));
        expect(eq(table.rowCount(), 2UZ)) << "the header counts as a row";
        expect(eq(cellText(table, 0UZ, 0UZ), std::string{"foo"}));
        expect(eq(cellText(table, 0UZ, 1UZ), std::string{"bar"}));
        expect(eq(cellText(table, 1UZ, 0UZ), std::string{"baz"}));
        expect(eq(cellText(table, 1UZ, 1UZ), std::string{"bim"}));
    };

    "the delimiter row sets each column's alignment, and the outer pipes are optional"_test = [] {
        // GFM example 199: no leading or trailing pipe on the delimiter or the body row
        const Document document = parseMarkdown("| abc | defghi |\n:-: | -----------:\nbar | baz\n");
        const Block&   table    = document.blocks.front();
        expect(table.kind == BlockKind::table);
        expect(table.columns.at(0UZ) == Alignment::centre);
        expect(table.columns.at(1UZ) == Alignment::right);
        expect(eq(cellText(table, 1UZ, 0UZ), std::string{"bar"}));
        expect(eq(cellText(table, 1UZ, 1UZ), std::string{"baz"}));

        const Document simple = parseMarkdown("| a | b |\n| --- | --- |\n| 1 | 2 |\n");
        expect(simple.blocks.front().columns.at(0UZ) == Alignment::left) << "no colon means left";
    };

    "an escaped pipe is content, not a cell boundary"_test = [] {
        // GFM example 200
        const Document document = parseMarkdown("| f\\|oo  |\n| ------ |\n| b `\\|` az |\n");
        const Block&   table    = document.blocks.front();
        expect(eq(table.columns.size(), 1UZ)) << "the escaped pipe must not open a second column";
        expect(eq(cellText(table, 0UZ, 0UZ), std::string{"f|oo"})) << "the backslash is consumed, the pipe is kept";
    };

    "a delimiter row of a different width is not a table at all"_test = [] {
        // GFM example 203
        const Document document = parseMarkdown("| abc | def |\n| --- |\n| bar |\n");
        for (const Block& block : document.blocks) {
            expect(block.kind != BlockKind::table) << "two header cells against one delimiter cell is prose";
        }
    };

    "rows are padded and truncated to the header's width"_test = [] {
        // GFM example 204
        const Document document = parseMarkdown("| abc | def |\n| --- | --- |\n| bar |\n| bar | baz | boo |\n");
        const Block&   table    = document.blocks.front();
        expect(eq(table.rowCount(), 3UZ));
        expect(cellText(table, 1UZ, 1UZ).empty()) << "a missing cell is empty, not absent";
        expect(eq(cellText(table, 2UZ, 1UZ), std::string{"baz"}));
        expect(eq(table.cells.size(), 6UZ)) << "the third cell of the last row is dropped, so the table stays rectangular";
    };

    "a table may have no body rows at all"_test = [] {
        // GFM example 205
        const Document document = parseMarkdown("| abc | def |\n| --- | --- |\n");
        expect(document.blocks.front().kind == BlockKind::table);
        expect(eq(document.blocks.front().rowCount(), 1UZ));
    };

    "a blank line ends a table and what follows is prose again"_test = [] {
        const Document document = parseMarkdown("| a | b |\n| - | - |\n| 1 | 2 |\n\nafter the table\n");
        expect(eq(document.blocks.size(), 2UZ));
        expect(document.blocks.front().kind == BlockKind::table);
        expect(document.blocks.back().kind == BlockKind::paragraph);
        expect(eq(plainText(document.blocks.back()), std::string{"after the table"}));
    };

    "a cell keeps its inline markup"_test = [] {
        const Document document = parseMarkdown("| name | note |\n| --- | --- |\n| **bold** | see [docs](a.md) |\n");
        const Block&   table    = document.blocks.front();
        expect(table.cellAt(1UZ, 0UZ).front().kind == InlineKind::strong);
        const std::vector<InlineSpan>& note = table.cellAt(1UZ, 1UZ);
        const auto                     link = std::ranges::find_if(note, [](const InlineSpan& span) { return span.kind == InlineKind::link; });
        expect(link != note.end()) << "a link inside a cell was flattened to text";
        if (link != note.end()) {
            expect(eq(link->target, std::string{"a.md"}));
        }
    };

    "leading and trailing whitespace are both trimmed from a line"_test = [] {
        // a length counted from the start of the text rather than from the first non-space leaves the tail on
        const Document document = parseMarkdown("   indented and trailing   \n");
        expect(eq(plainText(document.blocks.front()), std::string{"indented and trailing"}));
    };

    "a dollar pair marks maths inside running text"_test = [] {
        const Document document = parseMarkdown("the identity $e^{i\\pi} + 1 = 0$ closes the circle\n");
        const auto&    spans    = document.blocks.front().spans;
        expect(eq(spans.size(), 3UZ));
        expect(spans[1].kind == InlineKind::math);
        expect(eq(spans[1].text, std::string{"e^{i\\pi} + 1 = 0"})) << "the LaTeX is kept exactly, backslashes and all";
        expect(eq(spans[0].text, std::string{"the identity "}));
        expect(eq(spans[2].text, std::string{" closes the circle"}));
    };

    "a lone dollar is not maths"_test = [] {
        const Document document = parseMarkdown("it costs $5 to enter\n");
        expect(eq(document.blocks.front().spans.size(), 1UZ));
        expect(document.blocks.front().spans.front().kind == InlineKind::text);
        expect(eq(plainText(document.blocks.front()), std::string{"it costs $5 to enter"}));
    };

    "a doubled dollar makes a display formula on one line or on several"_test = [] {
        const Document oneLine = parseMarkdown("before\n\n$$E = mc^2$$\n\nafter\n");
        expect(eq(oneLine.blocks.size(), 3UZ));
        expect(oneLine.blocks[1].kind == BlockKind::formula);
        expect(eq(oneLine.blocks[1].lines.size(), 1UZ));
        expect(eq(oneLine.blocks[1].lines.front(), std::string{"E = mc^2"}));
        expect(oneLine.blocks[2].kind == BlockKind::paragraph) << "the closing fence must not swallow what follows";

        const Document fenced = parseMarkdown("$$\n\\int_0^\\pi \\sin x \\, dx = 2\n$$\n\nafter\n");
        expect(eq(fenced.blocks.size(), 2UZ));
        expect(fenced.blocks.front().kind == BlockKind::formula);
        expect(eq(fenced.blocks.front().lines.size(), 1UZ));
        expect(eq(fenced.blocks.front().lines.front(), std::string{"\\int_0^\\pi \\sin x \\, dx = 2"}));
        expect(eq(plainText(fenced.blocks.back()), std::string{"after"}));
    };

    "a display formula inside a step carries that step"_test = [] {
        const Document document = parseMarkdown(":::step\n$$a^2$$\n:::\n");
        expect(document.blocks.front().kind == BlockKind::formula);
        expect(eq(document.blocks.front().step, 1));
    };

    "a plot block is data rather than source"_test = [] {
        const Document document = parseMarkdown("```plot\ntype: line\n---\nt,a\n0,1\n```\n\nafter\n");
        expect(eq(document.blocks.size(), 2UZ));
        expect(document.blocks.front().kind == BlockKind::plot);
        expect(eq(document.blocks.front().info, std::string{"plot"}));
        expect(eq(document.blocks.front().lines.size(), 4UZ)) << "the header and the rows are kept verbatim";
        expect(eq(document.blocks.front().lines.front(), std::string{"type: line"}));
        expect(parseMarkdown("```cpp\nint x;\n```\n").blocks.front().kind == BlockKind::codeBlock) << "any other language is still source";
    };

    "notes are prose, and a colon in a sentence is not a field"_test = [] {
        const Document document = parseMarkdown("# A view\n\n:::notes\nRemember: the corner is at 100 Hz.\nPause here.\n:::\n\nOn the slide.\n");
        const Block*   notes    = nullptr;
        for (const Block& block : document.blocks) {
            if (block.kind == BlockKind::directive && block.info == "notes") {
                notes = &block;
            }
        }
        expect(notes != nullptr) << "the notes directive was not kept";
        if (notes == nullptr) {
            return;
        }
        expect(notes->fields.empty()) << "a sentence with a colon became a setting";
        expect(eq(notes->lines.size(), 2UZ));
        expect(eq(notes->lines.front(), std::string{"Remember: the corner is at 100 Hz."}));
        expect(eq(notesOf(document), std::string{"Remember: the corner is at 100 Hz.\nPause here."}));
    };

    "every notes block in a view is collected, and a view without one has none"_test = [] {
        const Document two = parseMarkdown(":::notes\nfirst\n:::\n\nbody\n\n:::notes\nsecond\n:::\n");
        expect(eq(notesOf(two), std::string{"first\nsecond"}));
        expect(notesOf(parseMarkdown("# nothing to say\n")).empty());
    };

    "notes never reach the slide's own blocks"_test = [] {
        // the renderer draws only `gr4` directives, so notes staying a directive is what keeps them off the slide
        const Document document = parseMarkdown(":::notes\nsecret\n:::\n\nvisible\n");
        for (const Block& block : document.blocks) {
            expect(block.kind != BlockKind::paragraph || plainText(block) == "visible") << "the notes leaked into the slide";
        }
    };

    "a footnote reference carries its label and its definition is not a block"_test = [] {
        const Document document = parseMarkdown("Windowing reduces leakage[^harris].\n\n[^harris]: F. J. Harris, \"On the use of windows\", Proc. IEEE, vol. 66, no. 1, 1978.\n");
        expect(eq(document.blocks.size(), 1UZ)) << "the definition must not draw where it was written";
        const auto& spans = document.blocks.front().spans;
        expect(eq(spans.size(), 3UZ)) << "text, the reference, then the full stop";
        expect(spans[1].kind == InlineKind::footnote);
        expect(eq(spans[1].target, std::string{"harris"}));
        expect(eq(spans[2].text, std::string{"."})) << "what followed the reference is still there";

        expect(eq(document.footnotes.size(), 1UZ));
        const std::vector<InlineSpan>* text = document.footnote("harris");
        expect(text != nullptr);
        if (text != nullptr) {
            expect(!text->empty());
            expect(text->front().text.starts_with("F. J. Harris"));
        }
        expect(document.footnote("absent") == nullptr);
    };

    "a bracket that is not a footnote is still a link"_test = [] {
        const Document document = parseMarkdown("see [the manual](docs/manual.md) and [^note]\n\n[^note]: an aside\n");
        const auto&    spans    = document.blocks.front().spans;
        expect(spans[1].kind == InlineKind::link);
        expect(eq(spans[1].target, std::string{"docs/manual.md"}));
        expect(spans[3].kind == InlineKind::footnote);
    };

    "footnotes are numbered by the author's label, not by position"_test = [] {
        // labels are names so that adding a footnote in the middle does not renumber the source
        const Document document = parseMarkdown("a[^second] b[^first]\n\n[^first]: one\n[^second]: two\n");
        expect(eq(document.footnotes.size(), 2UZ));
        expect(eq(document.footnotes.front().first, std::string{"first"})) << "definitions keep the order they were written in";
        expect(document.footnote("second") != nullptr);
    };

    "every view can reach every footnote"_test = [] {
        const Document             document = parseMarkdown("# One {#one}\n\ntext[^a]\n\n# Two {#two}\n\nmore[^a]\n\n[^a]: the same note\n");
        const std::vector<Section> views    = sectionsOf(document);
        expect(eq(views.size(), 2UZ));
        for (const Section& view : views) {
            expect(view.document.footnote("a") != nullptr) << "view " << view.id << " cannot reach the note it refers to";
        }
    };

    "front matter at the top of the file says what the document is"_test = [] {
        const Document document = parseMarkdown("---\ntitle: A talk\nauthor: John Doe\nemail: JD@heaven.gov\nnumbering: number-of-total\n---\n\n# One {#one}\n\nbody\n");
        expect(eq(document.front.title, std::string{"A talk"}));
        expect(eq(document.front.author, std::string{"John Doe"}));
        expect(eq(document.front.email, std::string{"JD@heaven.gov"}));
        expect(eq(document.front.numbering, std::string{"number-of-total"}));
        expect(eq(document.blocks.size(), 2UZ)) << "the front matter is the document's, not a block of it";
        expect(document.blocks.front().kind == BlockKind::heading);
    };

    "a document that declares nothing about itself has empty front matter"_test = [] {
        const Document document = parseMarkdown("# One {#one}\n\nbody\n");
        expect(document.front == FrontMatter{});
    };

    "three dashes below the first line are still a rule"_test = [] {
        const Document document = parseMarkdown("some words\n\n---\n\ntitle: not a field\n");
        expect(std::ranges::any_of(document.blocks, [](const Block& block) { return block.kind == BlockKind::rule; })) << "the rule was eaten as front matter";
        expect(document.front == FrontMatter{});
    };

    "every view carries what the document says about itself"_test = [] {
        const Document             document = parseMarkdown("---\ntitle: A talk\n---\n\n# One {#one}\n\na\n\n# Two {#two}\n\nb\n");
        const std::vector<Section> views    = sectionsOf(document);
        expect(eq(views.size(), 2UZ));
        for (const Section& view : views) {
            expect(eq(view.document.front.title, std::string{"A talk"})) << "view " << view.id << " cannot say whose talk it is";
        }
    };

    "a fence carries its box's options in braces, and they are no part of its name"_test = [] {
        const Document document = parseMarkdown(":::left {shrink=off centre}\nprose\n:::\n");
        expect(eq(document.blocks.size(), 1UZ));
        const Block& box = document.blocks.front();
        expect(eq(box.info, std::string{"left"})) << "the braces were read as part of the name";
        expect(eq(box.field("shrink"), std::string_view{"off"}));
        expect(eq(box.field("centre"), std::string_view{"on"})) << "a flag written on its own is on";
        expect(eq(box.lines.size(), 1UZ)) << "the box's prose is still its own";
    };

    "a fence inside a content box belongs to that box"_test = [] {
        // A box's lines are read again on their own, so a reveal written inside one is that box's business. The
        // step used to escape the box, advance the slide's counter, and eat the box's own closing fence with it.
        const Document document = parseMarkdown(":::left\nbefore\n:::step\nafter\n:::\n:::\nafterwards\n");
        expect(eq(document.blocks.size(), 2UZ));
        const Block& box = document.blocks.front();
        expect(eq(box.info, std::string{"left"}));
        expect(eq(box.lines.size(), 4UZ)) << "the box did not keep the fence written inside it";
        expect(document.blocks.back().kind == BlockKind::paragraph);
        expect(eq(document.blocks.back().step, 0)) << "a step inside a box staged the slide around it";
    };

    "a fence quoted in a box's code block is code, and the box still closes"_test = [] {
        // a slide that shows its own syntax quotes `:::` lines in a fenced block; they open and close nothing
        const Document slide = parseMarkdown(":::left\n```markdown\n:::step {in=fade}\nshown\n```\n:::\nafterwards\n");
        expect(eq(slide.blocks.size(), 2UZ)) << "the quoted fence swallowed the box's closing fence";
        const Block& box = slide.blocks.front();
        expect(eq(box.lines.size(), 4UZ));
        expect(slide.blocks.back().kind == BlockKind::paragraph);
        const Document read = boxDocumentOf(box.lines, slide);
        expect(eq(read.stepCount(), 1)) << "a quoted step staged the box";
        expect(eq(read.blocks.size(), 1UZ));
        expect(read.blocks.front().kind == BlockKind::codeBlock);
        expect(eq(read.blocks.front().lines.size(), 2UZ)) << "the quoted lines are not the code block's";
    };

    "a box's own prose can stage itself"_test = [] {
        const Document slide = parseMarkdown(":::left\nbefore\n:::step\nafter\n:::\n:::\n");
        const Document box   = boxDocumentOf(slide.blocks.front().lines, slide);
        expect(eq(box.stepCount(), 2)) << "the reveal the box holds is not its own";
    };

    // How `:::step` read before the Pandoc alignment, recorded as it was so the alignment changes none of it
    "a step at slide level is a separator, closed or not, and the words after its closer stay in it"_test = [] {
        const Document open = parseMarkdown("a\n\n:::step\n\nb\n");
        expect(eq(open.blocks.size(), 2UZ) && eq(open.blocks[0].step, 0) && eq(open.blocks[1].step, 1));
        const Document closed = parseMarkdown("a\n\n:::step\nb\n:::\nc\n");
        expect(eq(closed.blocks.size(), 3UZ)) << "a, b and c";
        expect(closed.blocks.size() == 3UZ && eq(closed.blocks[1].step, 1) && eq(closed.blocks[2].step, 1)) << "the closer ends nothing";
    };

    "a step inside a box is closed inside it, and the box goes on after it"_test = [] {
        const Document direct = parseMarkdown(":::box\nx\n:::step\ny\n:::\n:::\nz\n");
        expect(eq(direct.blocks.size(), 2UZ) && direct.blocks.front().closed);
        expect(direct.blocks.front().lines == std::vector<std::string>{"x", ":::step", "y", ":::"});
        expect(direct.blocks.size() == 2UZ && eq(direct.blocks.back().step, 0)) << "the slide's own words are not staged by the box's step";
        const Document goesOn = parseMarkdown(":::box\nx\n:::step\ny\n:::\nw\n:::\nz\n");
        expect(goesOn.blocks.front().lines == std::vector<std::string>{"x", ":::step", "y", ":::", "w"}) << "box text after the step's closer stays in the box";
    };

    "a step left open inside a box takes the box's closer, and the box the rest of the slide"_test = [] {
        const Document swallowed = parseMarkdown(":::box\nx\n:::step\ny\n:::\nz\n");
        expect(eq(swallowed.blocks.size(), 1UZ) && !swallowed.blocks.front().closed) << "the box is marked as never closed, which the problems list reports";
    };

    // Ground truth: Pandoc's fenced divs -- a fence is three or more colons, one without attributes closes the
    // innermost open div whatever its length -- and its slide pause, a paragraph of three dots separated by spaces
    "a longer fence opens a box like a short one, and the box nests a shorter one"_test = [] {
        const Document slide = parseMarkdown("::::outer\n:::inner\nwords\n:::\n::::\nafter\n");
        expect(eq(slide.blocks.size(), 2UZ)) << "the outer box and the paragraph after it";
        expect(eq(slide.blocks.front().info, std::string{"outer"}) && slide.blocks.front().closed);
        expect(slide.blocks.front().lines == std::vector<std::string>{":::inner", "words", ":::"});
        expect(slide.blocks.back().kind == BlockKind::paragraph && eq(slide.blocks.back().step, 0));
    };

    "colons after a fence's name are decoration, as Pandoc's manual shows"_test = [] {
        const Document slide = parseMarkdown("::: Warning ::::::\nThis is a warning.\n\n::: Danger\nwithin\n:::\n::::::::::::::::::\n");
        expect(eq(slide.blocks.size(), 1UZ) && eq(slide.blocks.front().info, std::string{"Warning"}) && slide.blocks.front().closed);
    };

    "three spaced dots pause the slide, and a box"_test = [] {
        const Document slide = parseMarkdown("a\n\n. . .\n\nb\n");
        expect(eq(slide.blocks.size(), 2UZ) && eq(slide.blocks.back().step, 1)) << "the pause is no paragraph of its own";
        const Document boxed = parseMarkdown(":::box\nx\n\n. . .\n\ny\n:::\n");
        expect(eq(boxDocumentOf(boxed.blocks.front().lines, boxed).stepCount(), 2));
        const Document code = parseMarkdown("```\n. . .\n```\n");
        expect(eq(code.stepCount(), 1)) << "three dots in code are code";
    };

    "a fence's Pandoc attributes: a class is a flag, an id names the box"_test = [] {
        const Document slide = parseMarkdown(":::left {#first .striped size=80%}\nwords\n:::\n");
        const Block&   box   = slide.blocks.front();
        expect(eq(box.id, std::string{"first"}));
        expect(eq(box.field("striped"), std::string_view{"on"})) << "`.striped` is `striped`";
        expect(eq(box.field("size"), std::string_view{"80%"}));
    };

    "a fence that closes nothing and braces a configuring directive ignores are reported, a step's closer is not"_test = [] {
        expect(eq(parseMarkdown("a\n:::\n").warnings.size(), 1UZ)) << "a stray closing fence";
        expect(eq(parseMarkdown(":::layout {font=hand}\ngrid: (a)\n:::\n").warnings.size(), 1UZ)) << "braces on a configuring directive";
        expect(parseMarkdown("a\n\n:::step\nb\n:::\n").warnings.empty()) << "a slide-level step closed the Pandoc way";
        expect(parseMarkdown(":::left {font=hand}\nwords\n:::\n").warnings.empty()) << "a content box takes braces";
    };

    // Ground truth: PowerPoint's "With Previous", as the deck's author asked for it -- one step for the cursor,
    // each part arriving in its own way
    "a step `with` the one before shares its number, and carries its own effect"_test = [] {
        const Document slide   = parseMarkdown("# S\n\n:::step {in=rise}\n\nclaim\n\n:::step {in=fade with dur=1}\n\nfigure\n\n:::step\n\nnext\n");
        const auto     blockOf = [&slide](std::string_view words) { return std::ranges::find_if(slide.blocks, [words](const Block& block) { return !block.spans.empty() && block.spans.front().text == words; }); };
        expect(blockOf("claim") != slide.blocks.end() && blockOf("figure") != slide.blocks.end() && blockOf("next") != slide.blocks.end());
        expect(eq(blockOf("figure")->step, blockOf("claim")->step)) << "the figure arrives with the claim";
        expect(eq(blockOf("figure")->revealKind, std::string{"fade"}) && blockOf("claim")->revealKind.empty());
        expect(std::abs(blockOf("figure")->revealSeconds - 1.0f) < 1e-6f);
        expect(eq(blockOf("next")->step, blockOf("claim")->step + 1) && blockOf("next")->revealKind.empty()) << "and the step after is a step again";
        expect(eq(slide.stepCount(), 3)) << "the slide as it opens, the claim with its figure, and the next";
    };

    "a step `with` nothing before it on its slide is a step of its own"_test = [] {
        const Document slide = parseMarkdown("# S\n\nfirst\n\n:::step {with}\n\nsecond\n");
        expect(eq(slide.stepCount(), 2));
    };

    "front matter keeps what is inside quotes, not the quotes"_test = [] {
        const Document document = parseMarkdown("---\ntitle: \"Spectra: a primer\"\nauthor: 'Issue #42'\n---\n# One\n");
        expect(eq(document.front.title, std::string{"Spectra: a primer"}));
        expect(eq(document.front.author, std::string{"Issue #42"}));
    };

    "a box can cite what the slide defined"_test = [] {
        const Document slide = parseMarkdown("# One {#one}\n\n:::left\ncites[^a]\n:::\n\n[^a]: the note\n");
        const Block&   box   = *std::ranges::find(slide.blocks, std::string{"left"}, &Block::info);
        const Document read  = boxDocumentOf(box.lines, slide);
        expect(read.footnote("a") != nullptr) << "a box cannot reach the citation its slide defines";
    };

    "a backslash writes the character after it"_test = [] {
        const Document document = parseMarkdown("a \\*not emphasis\\* b\n");
        expect(eq(document.blocks.size(), 1UZ));
        std::string text;
        for (const InlineSpan& span : document.blocks.front().spans) {
            text.append(span.text);
        }
        expect(eq(text, std::string{"a *not emphasis* b"})) << "got: " << text;
        expect(std::ranges::none_of(document.blocks.front().spans, [](const InlineSpan& span) { return span.kind == InlineKind::emphasis; }));
    };

    "a code fence carries options in braces, like every other fence"_test = [] {
        const Document document = parseMarkdown("```cpp {wrap=off}\nint main() {}\n```\n");
        expect(eq(document.blocks.size(), 1UZ));
        const Block& code = document.blocks.front();
        expect(code.kind == BlockKind::codeBlock);
        expect(eq(code.info, std::string{"cpp"})) << "the language read as '" << code.info << "', so nothing would be highlighted";
        expect(eq(code.field("wrap"), std::string_view{"off"}));
    };

    "a directive that configures the slide takes no options"_test = [] {
        // every unreserved key of a layout directive names an area to fill, so an option there would name one too
        const Document document = parseMarkdown(":::layout {striped}\ngrid: [(a), (b)]\n:::\n");
        expect(eq(document.blocks.size(), 1UZ));
        expect(eq(document.blocks.front().info, std::string{"layout"}));
        expect(document.blocks.front().field("striped").empty());
        expect(eq(document.blocks.front().field("grid"), std::string_view{"[(a), (b)]"}));
    };

    // Comments as Pandoc and Obsidian write them: `<!-- -->` anywhere and across lines, `%%` to the end of a line.
    // Neither is drawn; code and inline code keep theirs, because there they are text.
    "a comment is not part of the slide, and a line holding only one does not end a paragraph"_test = [] {
        const Document document = parseMarkdown("first line %% why this wording\n%% a note of its own\nsecond <!-- inline --> line\n");
        expect(fatal(eq(document.blocks.size(), 1UZ)));
        expect(eq(plainText(document.blocks.front()), std::string{"first line second  line"}));
    };

    "an HTML comment across lines hides everything inside it"_test = [] {
        const Document document = parseMarkdown("# Kept\n\n<!--\n# Hidden\n\nhidden too\n-->\n\n# Also kept\n");
        expect(fatal(eq(document.blocks.size(), 2UZ)));
        expect(eq(plainText(document.blocks[0]), std::string{"Kept"}));
        expect(eq(plainText(document.blocks[1]), std::string{"Also kept"}));
    };

    "code and inline code keep their comment markers"_test = [] {
        const Document document = parseMarkdown("```python\nx = 1 %% 2  <!-- not a comment -->\n```\n\nwrite `a %% b` here\n");
        expect(fatal(eq(document.blocks.size(), 2UZ)));
        expect(eq(document.blocks[0].lines.size(), 1UZ) && document.blocks[0].lines.front() == "x = 1 %% 2  <!-- not a comment -->");
        expect(plainText(document.blocks[1]).contains("a %% b"));
    };

    // CommonMark: a code block closes only on a fence at least as long as the one that opened it, so Markdown that
    // shows a fenced block is itself fenced with four backticks
    "a longer fence holds a shorter one as its text, in the flow and in a box"_test = [] {
        const Document flow = parseMarkdown("````markdown\n```plot\ntype: bar\n```\n````\n\nafter\n");
        expect(fatal(eq(flow.blocks.size(), 2UZ)));
        expect(flow.blocks[0].kind == BlockKind::codeBlock && eq(flow.blocks[0].info, std::string{"markdown"}));
        expect(flow.blocks[0].lines == std::vector<std::string>{"```plot", "type: bar", "```"}) << "the inner fence is text";
        expect(eq(plainText(flow.blocks[1]), std::string{"after"}));
        const Document boxed = parseMarkdown(":::left\n````markdown\n```plot\n:::\n```\n````\n:::\n\nafter\n");
        expect(fatal(eq(boxed.blocks.size(), 2UZ))) << "the box closes on its own fence, not on the `:::` quoted inside";
        expect(eq(plainText(boxed.blocks[1]), std::string{"after"}));
    };

    "a warning names the line the author wrote, comments included"_test = [] {
        const Document document = parseMarkdown("<!--\nthree lines\nof comment\n-->\nwords\n\n:::\n");
        expect(fatal(eq(document.warnings.size(), 1UZ)));
        expect(document.warnings.front().starts_with("line 7:")) << document.warnings.front();
    };

    "slugs collapse punctuation and trim separators"_test = [] {
        expect(eq(slugOf("Frequency Domain!"), std::string{"frequency-domain"}));
        expect(eq(slugOf("  spaced  out  "), std::string{"spaced-out"}));
        expect(eq(slugOf("GR4/OpenDigitizer"), std::string{"gr4-opendigitizer"}));
        expect(slugOf("!!!").empty());
    };

    "an area names one file for both colour schemes, or the light one and the dark one either side of a bar"_test = [] {
        expect(schemeFilesOf("figures/logo.svg") == SchemeFiles{.light = "figures/logo.svg", .dark = "figures/logo.svg"});
        expect(schemeFilesOf("figures/logo.svg | figures/logo-dark.svg") == SchemeFiles{.light = "figures/logo.svg", .dark = "figures/logo-dark.svg"});
        expect(schemeFilesOf("a.svg|b.svg") == SchemeFiles{.light = "a.svg", .dark = "b.svg"}) << "the blanks round the bar are optional";
    };
};

int main() { return 0; }
