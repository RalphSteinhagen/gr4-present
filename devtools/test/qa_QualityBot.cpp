#include <boost/ut.hpp>

#include "QualityBot.hpp"

using namespace boost::ut;
using namespace devtools::bot;

const suite<"QualityBot"> tests = [] {
    "a quoted CSV field keeps its comma, quote and line break, and a table reads back as written"_test = [] {
        const Table       written{.columns = {"commit", "note", "total"}, .rows = {Row{{"commit", "a1"}, {"note", "say \"hi\", then\nleave"}, {"total", "76.9"}}, Row{{"commit", "b2"}, {"total", ""}}}};
        const std::string text = csvText(written);
        expect(eq(text, std::string{"commit,note,total\na1,\"say \"\"hi\"\", then\nleave\",76.9\nb2,,\n"}));
        const Table read = csvTable(text);
        expect(read.columns == written.columns);
        expect(eq(read.rows.size(), 2UZ));
        expect(eq(read.rows[0].at("note"), std::string{"say \"hi\", then\nleave"}));
        expect(eq(read.rows[1].at("note"), std::string{})) << "a missing value is empty, never zero";
    };

    "a number keeps its value through percent signs and thousands separators, and text is no number"_test = [] {
        expect(eq(numberIn("76.9 %").value_or(-1.0), 76.9));
        expect(eq(numberIn("1,234,567").value_or(-1.0), 1234567.0));
        expect(!numberIn("").has_value());
        expect(!numberIn("n/a").has_value());
        expect(!numberIn("12abc").has_value());
    };

    "thousands are grouped by three from the right"_test = [] {
        expect(eq(withThousands(0), std::string{"0"}));
        expect(eq(withThousands(999), std::string{"999"}));
        expect(eq(withThousands(1000), std::string{"1,000"}));
        expect(eq(withThousands(71839253671), std::string{"71,839,253,671"}));
        expect(eq(withThousands(-1234), std::string{"-1,234"}));
    };

    "a change within half a tenth of a point reads ±0.0, a missing side n/a"_test = [] {
        expect(eq(signedChange(76.94, 76.9), std::string{"±0.0"}));
        expect(eq(signedChange(77.0, 76.9), std::string{"+0.1"}));
        expect(eq(signedChange(75.0, 76.9), std::string{"-1.9"}));
        expect(eq(signedChange(std::nullopt, 76.9), std::string{"n/a"}));
    };

    "a cut never splits a UTF-8 character"_test = [] {
        const std::string text = "ab🟢"; // 🟢 is four bytes
        expect(eq(cutUtf8(text, 3), std::string{"ab"}));
        expect(eq(cutUtf8(text, 5), std::string{"ab"}));
        expect(eq(cutUtf8(text, 6), text));
    };

    "a JSON string escapes quotes, backslashes and control characters"_test = [] { expect(eq(jsonQuoted("a\"b\\c\nd\x01"), std::string{R"("a\"b\\c\nd\u0001")"})); };

    "the changed lines of a diff are the added hunks' lines, a deleted file contributes none"_test = [] {
        const std::string diff    = "diff --git a/x.cpp b/x.cpp\n--- a/x.cpp\n+++ b/x.cpp\n@@ -3,0 +4,2 @@\n+a\n+b\n@@ -9 +11 @@\n-c\n+d\n"
                                    "diff --git a/gone.cpp b/gone.cpp\n--- a/gone.cpp\n+++ /dev/null\n@@ -1,3 +0,0 @@\n";
        const auto        changed = changedLinesIn(diff);
        expect(eq(changed.size(), 1UZ));
        expect(changed.at("x.cpp") == std::set<unsigned>{4U, 5U, 11U});
    };

    "line numbers read as ranges"_test = [] {
        expect(eq(rangesOf({3U, 5U, 6U, 7U, 9U}), std::string{"L3, L5–L7, L9"}));
        expect(eq(rangesOf({}), std::string{}));
    };

    "the base is the recorded commit, else its nearest recorded ancestor, and never figures of other tools"_test = [] {
        const std::vector<Row> history{Row{{"commit", "c1aaaaaaaa"}, {"tool", "t1"}, {"total", "70"}}, Row{{"commit", "c2bbbbbbbb"}, {"tool", "t1"}, {"total", "71"}}};
        const Base             exact = baseFrom("c2bbbbbbbb", {"c2bbbbbbbb", "c1aaaaaaaa"}, history, "t1");
        expect(eq(exact.row.at("total"), std::string{"71"}));
        expect(eq(exact.rows.size(), 2UZ));
        expect(exact.note.empty());
        const Base nearest = baseFrom("c3cccccccc", {"c3cccccccc", "c1aaaaaaaa"}, history, "t1");
        expect(eq(nearest.row.at("total"), std::string{"70"}));
        expect(eq(nearest.rows.size(), 1UZ)) << "the chart ends at the stand-in";
        expect(eq(nearest.note, std::string{"not recorded; compared with `c1aaaaaa`"}));
        const Base otherTools = baseFrom("c2bbbbbbbb", {"c2bbbbbbbb"}, history, "t2");
        expect(otherTools.row.empty());
        expect(eq(otherTools.note, std::string{"tools changed since `c2bbbbbb`: not comparable"}));
        const Base none = baseFrom("c9", {"c9"}, history, "t1");
        expect(eq(none.note, std::string{"not recorded yet"}));
    };

    "a report's figures are its two-space lines, not the places indented beneath them"_test = [] {
        const std::string report  = "(3) reading volume\n  repeated expressions: 2837 (written three times or more)\n    14 | blocks/basic/ConverterBlocks.hpp: 0.5 * x | line 3\n  SLOC: 71,150\n";
        const auto        figures = figureLines(report);
        expect(eq(figures.size(), 2UZ));
        expect(eq(figures[0].first, std::string{"repeated expressions"}));
        expect(eq(figures[0].second, std::string{"2837"}));
        expect(eq(figures[1].second, std::string{"71,150"}));
    };

    "an ISO date with an offset is the same instant as its UTC spelling"_test = [] {
        expect(eq(epochSecondsOf("1970-01-01T00:00:00+00:00").value_or(-1), 0LL));
        expect(eq(epochSecondsOf("2026-10-03T08:25:32+02:00").value_or(-1), epochSecondsOf("2026-10-03T06:25:32+00:00").value_or(-2)));
        expect(!epochSecondsOf("yesterday").has_value());
    };

    "a wide, collapsed chart draws main's recorded values and this run last, and nothing without a value"_test = [] {
        const std::vector<Row> rows{Row{{"commit", "abcdef123"}, {"total", "70.5"}}, Row{{"commit", "bcdefa234"}, {"total", ""}}};
        const std::string      drawn = chart("t", rows, "total", 71.0, 30);
        expect(drawn.contains(R"(x-axis ["abcdef1", "this"])"));
        expect(drawn.contains("line [70.5, 71]"));
        expect(drawn.starts_with("<details><summary>📈 t</summary>")) << "collapsed below the table";
        expect(drawn.contains("    width: 1200\n    height: 400\n")) << "3:1, wider than 16:9";
        expect(chart("t", {}, "total", std::nullopt, 30).empty());
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
