#include <boost/ut.hpp>

#include "Grid.hpp"
#include "RasterCamera.hpp"

#include <array>
#include <cmath>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] bool approx(float value, float wanted, float tolerance = 0.01f) noexcept { return std::abs(value - wanted) < tolerance; }

/// the area of `layout` with this id, or a zero area so a missing one fails an expectation
[[nodiscard]] Area areaOf(const Layout& layout, std::string_view id) {
    const Area* found = layout.find(id);
    return found == nullptr ? Area{} : *found;
}

const std::vector<std::string>& kEveryBoxFilled = *new std::vector<std::string>{};

} // namespace

// Ground truth: the notation by hand -- row entries share its width by weight (default 1), rows share the height left
const suite<"Grid"> gridTests = [] {
    "a row is a list of cells in brackets"_test = [] {
        const std::vector<GridRow> rows = gridRowsOf("[(left), (right)]");
        expect(eq(rows.size(), 1UZ));
        expect(eq(rows[0].cells.size(), 2UZ));
        expect(rows[0].cells[0].name == "left");
        expect(rows[0].cells[1].name == "right");
        expect(eq(rows[0].cells[0].share, 0.0f)) << "a cell that states no share divides what is left";
    };

    "the author's own example reads as three rows"_test = [] {
        const std::vector<GridRow> rows = gridRowsOf("[(pre-amble)], [(code_cpp), (code_python), (code_yaml)]:6, [(post)]");
        expect(eq(rows.size(), 3UZ));
        expect(rows[0].cells[0].name == "pre-amble") << "the outer bracket is a row, not part of the first name";
        expect(eq(rows[1].cells.size(), 3UZ));
        expect(rows[2].cells[0].name == "post") << "and the last row is not lost";
        expect(approx(rows[1].share, 6.0f)) << "the share after the bracket belongs to the row";
    };

    "brackets may be left off when the grid is one row"_test = [] {
        const bool same = gridRowsOf("(left), (right)") == gridRowsOf("[(left), (right)]");
        expect(same) << "so the common case reads without ceremony";
    };

    "a cell may state its share of the row"_test = [] {
        const std::vector<GridRow> rows = gridRowsOf("[(left, 0.6), (right, 0.4)]");
        expect(approx(rows[0].cells[0].share, 0.6f));
        expect(approx(rows[0].cells[1].share, 0.4f));
        expect(rows[0].cells[0].name == "left") << "the name is what comes before the comma";
    };

    "a malformed grid yields nothing rather than half a layout"_test = [] {
        expect(gridRowsOf("[(left), (right)").empty()) << "an unclosed row";
        expect(gridRowsOf("").empty());
    };

    "boxes take the share of the row they state"_test = [] {
        const Layout layout = gridLayoutOf(gridRowsOf("[(main, 0.75), (aside, 0.25)]"), 1000.0f, 600.0f, kEveryBoxFilled);
        const Area   main   = areaOf(layout, "main");
        const Area   aside  = areaOf(layout, "aside");
        expect(gt(main.width, 0.0f) && gt(aside.width, 0.0f));
        expect(approx(main.width / aside.width, 3.0f, 0.05f)) << "three quarters against one, whatever the margins are";
        expect(approx(main.y, aside.y)) << "and they sit on the same line";
        expect(lt(main.x, aside.x));
    };

    "rows share the height the title and footer leave"_test = [] {
        const Layout layout = gridLayoutOf(gridRowsOf("[(a)]:2, [(b)]:1"), 1000.0f, 600.0f, kEveryBoxFilled);
        const Area   a      = areaOf(layout, "a");
        const Area   b      = areaOf(layout, "b");
        expect(approx(a.height / b.height, 2.0f, 0.02f));
        expect(lt(a.y, b.y)) << "in the order they were written";
        expect(gt(b.y, areaOf(layout, "title").y)) << "and both below the title";
        expect(lt(b.y + b.height, areaOf(layout, "footer").y + 1.0f)) << "and above the footer";
    };

    // Ground truth: by hand -- every layout of the same rows fills the same height, so the self-sized row's height comes out of the others'
    "a self-sizing row takes its content height and the others divide the rest by share"_test = [] {
        constexpr std::string_view kRows  = "[(a)], [(b)]:2, [(c)]";
        const Layout               shared = gridLayoutOf(gridRowsOf(kRows), 1000.0f, 600.0f, kEveryBoxFilled);
        const float                usable = areaOf(shared, "a").height + areaOf(shared, "b").height + areaOf(shared, "c").height;
        const std::array<float, 3> wanted{50.0f, 0.0f, 0.0f};
        const Layout               sized = gridLayoutOf(gridRowsOf(kRows), 1000.0f, 600.0f, kEveryBoxFilled, wanted);
        const Area                 b     = areaOf(sized, "b");
        const Area                 c     = areaOf(sized, "c");
        expect(approx(areaOf(sized, "a").height, 50.0f)) << "the self-sizing row is as tall as its content";
        expect(approx(b.height, (usable - 50.0f) * 2.0f / 3.0f) && approx(c.height, (usable - 50.0f) / 3.0f)) << "the others share what it leaves, two to one";

        const std::array<float, 3> tooTall{10000.0f, 0.0f, 0.0f};
        expect(approx(areaOf(gridLayoutOf(gridRowsOf(kRows), 1000.0f, 600.0f, kEveryBoxFilled, tooTall), "a").height, 0.45f * usable)) << "content taller than the slide is capped at 45 % of the usable height";
    };

    "every grid carries the slide's chrome so a consumer cannot tell it from a master"_test = [] {
        const Layout layout = gridLayoutOf(gridRowsOf(kDefaultGrid), 1000.0f, 600.0f, kEveryBoxFilled);
        expect(layout.find("title") != nullptr);
        expect(layout.find("footer") != nullptr);
        expect(layout.find("content") != nullptr);
        expect(approx(layout.width, 1000.0f) && approx(layout.height, 600.0f));
    };

    "the title keeps its place whatever the grid below it is"_test = [] {
        const Area one = areaOf(gridLayoutOf(gridRowsOf(kDefaultGrid), 1000.0f, 600.0f, kEveryBoxFilled), "title");
        const Area two = areaOf(gridLayoutOf(gridRowsOf("[(a), (b)], [(c)]"), 1000.0f, 600.0f, kEveryBoxFilled), "title");
        expect(one == two) << "which is what stops a title changing size from slide to slide";
    };

    "a 36 pt title and its subtitle fit the band, and the first content starts just below it"_test = [] {
        // Ground truth: 36 pt title and 18 pt body at 1280 x 720 (96 px to the inch), subtitle at the title over 1.5
        constexpr float kTitlePixels    = 48.0f;
        constexpr float kSubtitlePixels = kTitlePixels / 1.5f;
        constexpr float kBodyPixels     = 24.0f;
        const Layout    layout          = gridLayoutOf(gridRowsOf("[(a)]"), 1280.0f, 720.0f, kEveryBoxFilled);
        const Area      title           = areaOf(layout, "title");
        const Area      first           = areaOf(layout, "a");
        // a `<br>` starts the subtitle, so the band must hold both lines
        expect(ge(title.height, kTitlePixels + kSubtitlePixels)) << "the band is " << title.height << " px for a 48 px line and a 32 px one";
        expect(lt(title.y, kBodyPixels * 0.5f)) << "and starts close to the top edge, at " << title.y;
        expect(ge(first.y, title.y + title.height)) << "the content starts below the band, at " << first.y;
        expect(lt(first.y - (title.y + title.height), kBodyPixels)) << "with no more than a small margin, " << first.y - (title.y + title.height);
    };

    "an unnamed box is addressed by its position in reading order"_test = [] {
        const Layout layout = gridLayoutOf(gridRowsOf("[(), ()], [()]"), 1000.0f, 600.0f, kEveryBoxFilled);
        expect(layout.find("1") != nullptr);
        expect(layout.find("2") != nullptr);
        expect(layout.find("3") != nullptr) << "counting across rows, not restarting on each";
        expect(approx(areaOf(layout, "3").width, areaOf(layout, "1").width * 2.0f, 30.0f)) << "the lone box spans the row";
    };

    "an empty box collapses and gives its share to the rest of the row"_test = [] {
        const std::vector<std::string>& onlyLeft = *new std::vector<std::string>{"left"};
        const Layout                    both     = gridLayoutOf(gridRowsOf("[(left), (right)]"), 1000.0f, 600.0f, kEveryBoxFilled);
        const Layout                    alone    = gridLayoutOf(gridRowsOf("[(left), (right)]"), 1000.0f, 600.0f, onlyLeft);
        expect(alone.find("right") == nullptr) << "the empty box is gone";
        expect(gt(areaOf(alone, "left").width, areaOf(both, "left").width * 1.8f)) << "and the survivor takes the room";
    };

    "a row with nothing in it gives its height to the rows that have something"_test = [] {
        const std::vector<std::string>& onlyTop = *new std::vector<std::string>{"a"};
        const Layout                    both    = gridLayoutOf(gridRowsOf("[(a)], [(b)]"), 1000.0f, 600.0f, kEveryBoxFilled);
        const Layout                    alone   = gridLayoutOf(gridRowsOf("[(a)], [(b)]"), 1000.0f, 600.0f, onlyTop);
        expect(alone.find("b") == nullptr);
        expect(gt(areaOf(alone, "a").height, areaOf(both, "a").height * 1.8f));
    };

    // Ground truth: the multiplication by hand -- 0.55 of a thousand is 550
    "boxes an author placed themselves are fractions of the slide"_test = [] {
        const std::vector<Area> declared = regionsOf(std::vector<std::pair<std::string, std::string>>{{"wide", "0.05 0.30 0.90 0.25"}, {"narrow", "0.55 0.60 0.40 0.30"}});
        const Layout            layout   = declaredLayoutOf(declared, 1000.0f, 600.0f);

        expect(approx(areaOf(layout, "wide").x, 50.0f));
        expect(approx(areaOf(layout, "wide").width, 900.0f));
        expect(approx(areaOf(layout, "narrow").x, 550.0f));
        expect(approx(areaOf(layout, "narrow").y, 360.0f));
        expect(approx(areaOf(layout, "narrow").height, 180.0f));
    };

    "a placed layout still gets the slide's chrome, and does not overwrite it"_test = [] {
        const std::vector<Area> plain = regionsOf(std::vector<std::pair<std::string, std::string>>{{"body", "0.1 0.3 0.8 0.5"}});
        const Layout            given = declaredLayoutOf(plain, 1000.0f, 600.0f);
        expect(given.find("title") != nullptr) << "a title nobody placed is put where every other slide has it";
        expect(given.find("footer") != nullptr);

        const std::vector<Area> own  = regionsOf(std::vector<std::pair<std::string, std::string>>{{"title", "0.0 0.0 0.5 0.1"}, {"body", "0.1 0.3 0.8 0.5"}});
        const Layout            mine = declaredLayoutOf(own, 1000.0f, 600.0f);
        expect(approx(areaOf(mine, "title").width, 500.0f)) << "and one the author placed is left where they put it";
        expect(eq(std::ranges::count(mine.areas, std::string{"title"}, &Area::id), 1L)) << "exactly once";
    };

    "a placed layout is generated, so it fills the viewport rather than letterboxing into it"_test = [] {
        const std::vector<Area> boxes = regionsOf(std::vector<std::pair<std::string, std::string>>{{"body", "0.1 0.3 0.8 0.5"}});
        expect(declaredLayoutOf(boxes, 1000.0f, 600.0f).generated);
        expect(gridLayoutOf(gridRowsOf(kDefaultGrid), 1000.0f, 600.0f, kEveryBoxFilled).generated);
    };

    // Ground truth: a drawing carries its own boxes, so a grid over one contradicts it; a photograph carries none
    "asking for a drawing and a grid at once is reported, over a photograph it is not"_test = [] {
        expect(contradictsMaster("figures/master.svg", "(left, right)", false)) << "a drawn master and a named grid";
        expect(contradictsMaster("figures/master.svg", "", true)) << "a drawn master and boxes placed by hand";
        expect(!contradictsMaster("figures/master.svg", "", false)) << "a drawn master on its own is the usual case";
        expect(!contradictsMaster("", "(left, right)", false)) << "and a grid on its own is the other usual case";
        expect(!contradictsMaster("media/Science_FAIR.webp", "(left, right)", false)) << "a photograph has no boxes to contradict";
    };

    // Ground truth: a cell must be at least twelve ems wide, else the row is laid out down the slide
    "a row too narrow to read is laid out down the slide instead of across it"_test = [] {
        const std::vector<GridRow> threeAcross = gridRowsOf("[(a), (b), (c)]:6");

        // 1280x720: about 370 px per cell against a body of 25 (14.8 ems); 600x800: about 155 px against 17 (9.1 ems)
        const std::vector<GridRow> wide   = stackedWhenNarrow(threeAcross, 1280.0f, 720.0f);
        const std::vector<GridRow> narrow = stackedWhenNarrow(threeAcross, 600.0f, 800.0f);

        expect(eq(wide.size(), 1UZ)) << "three columns across a landscape slide are readable and must stay a row";
        expect(eq(wide[0].cells.size(), 3UZ));

        expect(eq(narrow.size(), 3UZ)) << "three cells too narrow to read should become three rows, not " << narrow.size();
        for (const GridRow& row : narrow) {
            expect(eq(row.cells.size(), 1UZ)) << "a stacked row holds one cell";
        }
        expect(narrow[0].cells[0].name == "a") << "the cells keep the order the author wrote them in";
        expect(narrow[1].cells[0].name == "b");
        expect(narrow[2].cells[0].name == "c");
        expect(lt(std::abs(narrow[0].share - 6.0f), 0.01f)) << "a stacked row keeps the share its row stated, so the group grows as it stacks";

        const std::vector<GridRow> roomy = stackedWhenNarrow(gridRowsOf("[(a), (b)]"), 4000.0f, 1000.0f);
        expect(eq(roomy.size(), 1UZ)) << "two cells of a 4000 px slide have room and must not stack";
        expect(eq(roomy[0].cells.size(), 2UZ));
    };

    "a single box is never stacked, however narrow the slide"_test = [] {
        const std::vector<GridRow> one = stackedWhenNarrow(gridRowsOf("[(content)]"), 240.0f, 320.0f);
        expect(eq(one.size(), 1UZ));
        expect(eq(one[0].cells.size(), 1UZ)) << "there is nothing to stack it against";
    };

    "stacking a row puts its boxes one under the other, each the full width"_test = [] {
        const std::vector<GridRow> rows   = stackedWhenNarrow(gridRowsOf("[(a), (b), (c)]:6"), 600.0f, 800.0f);
        const Layout               layout = gridLayoutOf(rows, 600.0f, 800.0f, std::vector<std::string>{"a", "b", "c"});

        const Area* first  = layout.find("a");
        const Area* second = layout.find("b");
        const Area* third  = layout.find("c");
        expect(first != nullptr && second != nullptr && third != nullptr);
        if (first == nullptr || second == nullptr || third == nullptr) {
            return;
        }
        expect(lt(std::abs(first->x - second->x), 0.01f)) << "stacked boxes share a left edge";
        expect(lt(std::abs(first->width - second->width), 0.01f)) << "and a width";
        expect(gt(second->y, first->y)) << "and follow one another down the slide";
        expect(gt(third->y, second->y));
        expect(gt(first->width, 300.0f)) << "each box now has the slide's width rather than a third of it: " << first->width;
    };

    "a section layout is a title and nothing else"_test = [] {
        const Layout layout = gridLayoutOf(gridRowsOf(""), 1000.0f, 600.0f, kEveryBoxFilled);
        expect(layout.find("title") != nullptr);
        expect(eq(layout.areas.size(), 2UZ)) << "the title and the footer, and no content box at all";
    };

    "what a grid cannot read is named, and a well-formed one names nothing"_test = [] {
        const auto problemsOf = [](std::string_view spec) {
            std::vector<std::string> problems;
            static_cast<void>(gridRowsOf(spec, &problems));
            return problems;
        };
        expect(problemsOf("[(syntax, 0.42), (a), (b)]:5, [(c)]").empty()) << "the demo deck's grid";
        expect(eq(problemsOf("[(a), (b)").size(), 1UZ)) << "an unclosed row";
        expect(eq(problemsOf("[(a, 0.7), (b, 0.6)]").size(), 1UZ)) << "two cells claiming 1.3 of their row";
        expect(eq(problemsOf("[(a, wide)]").size(), 1UZ)) << "a fraction that is no number";
        expect(eq(problemsOf("[(a)]:tall").size(), 1UZ)) << "a weight that is no number";
        expect(eq(problemsOf("[(a)] stray").size(), 1UZ)) << "text that is neither a row nor a cell";
    };
};

int main() { return 0; }
