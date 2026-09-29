#include "Morph.hpp"

#include <boost/ut.hpp>

#include <vector>

using namespace boost::ut;
using gr::present::DrawnPiece;
using gr::present::morphPositions;

namespace {

/// the four corners of a box, as a draw list would hold a filled rectangle
void addBox(std::vector<ImDrawVert>& vertices, float x0, float y0, float x1, float y1) {
    for (const ImVec2 at : {ImVec2{x0, y0}, ImVec2{x1, y0}, ImVec2{x1, y1}, ImVec2{x0, y1}}) {
        vertices.push_back(ImDrawVert{.pos = at, .uv = {}, .col = 0U});
    }
}

[[nodiscard]] bool near(ImVec2 at, ImVec2 expected) { return std::abs(at.x - expected.x) < 1e-3f && std::abs(at.y - expected.y) < 1e-3f; }

/// a code block on each slide: its panel, then one line inside it; the leaving slide's vertices first, as both are drawn
/// into the one list. Leaving: panel (0,0)-(100,40), line (10,10)-(50,20). Arriving: panel (0,100)-(100,160), the
/// same line (10,140)-(50,150) when `sameLine`, a different one at that place otherwise.
struct TwoSlides {
    std::vector<ImDrawVert> vertices;
    std::vector<DrawnPiece> leaving;
    std::vector<DrawnPiece> arriving;
};

[[nodiscard]] TwoSlides codeBlockMoving(bool sameLine) {
    TwoSlides slides;
    addBox(slides.vertices, 0.0f, 0.0f, 100.0f, 40.0f);
    addBox(slides.vertices, 10.0f, 10.0f, 50.0f, 20.0f);
    addBox(slides.vertices, 0.0f, 100.0f, 100.0f, 160.0f);
    addBox(slides.vertices, 10.0f, 140.0f, 50.0f, 150.0f);
    slides.leaving  = {{.key = "#code/x = 1;", .vertexFrom = 4, .vertexTo = 8, .line = true}, {.key = "#code", .vertexFrom = 0, .vertexTo = 8}};
    slides.arriving = {{.key = sameLine ? "#code/x = 1;" : "#code/y = 2;", .vertexFrom = 12, .vertexTo = 16, .line = true}, {.key = "#code", .vertexFrom = 8, .vertexTo = 16}};
    return slides;
}

} // namespace

const suite<"Morph"> morphTests = [] {
    // Ground truth by hand, halfway (eased 0.5): the line's boxes (10,10,40x10) and (10,140,40x10) meet at (10,75,40x10);
    // the panels' boxes (0,0,100x40) and (0,100,100x60) at (0,50,100x50), so a panel corner (100,40) of the leaving
    // slide goes to (100,100). The line is not moved a second time with its panel.
    "a line both slides keep is halfway between its places, and its panel moves on its own"_test = [] {
        TwoSlides slides = codeBlockMoving(true);
        morphPositions(slides.vertices, slides.leaving, slides.arriving, 0.5f);
        expect(near(slides.vertices[4].pos, {10.0f, 75.0f}) && near(slides.vertices[6].pos, {50.0f, 85.0f})) << "the leaving copy of the line";
        expect(near(slides.vertices[12].pos, {10.0f, 75.0f}) && near(slides.vertices[14].pos, {50.0f, 85.0f})) << "the arriving copy, at the same place";
        expect(near(slides.vertices[0].pos, {0.0f, 50.0f}) && near(slides.vertices[2].pos, {100.0f, 100.0f})) << "the leaving panel";
        expect(near(slides.vertices[8].pos, {0.0f, 50.0f}) && near(slides.vertices[10].pos, {100.0f, 100.0f})) << "the arriving panel";
    };

    // A changed line has no partner and travels inside its panel: the leaving line (10,10)-(50,20) in the panel
    // (0,0,100x40) mapped onto (0,50,100x50) lands at (10,62.5)-(50,75).
    "a changed line moves with its panel"_test = [] {
        TwoSlides slides = codeBlockMoving(false);
        morphPositions(slides.vertices, slides.leaving, slides.arriving, 0.5f);
        expect(near(slides.vertices[4].pos, {10.0f, 62.5f}) && near(slides.vertices[6].pos, {50.0f, 75.0f}));
    };

    "at the start and the end of the move each slide is where it was drawn"_test = [] {
        TwoSlides start = codeBlockMoving(true);
        morphPositions(start.vertices, start.leaving, start.arriving, 0.0f);
        expect(near(start.vertices[4].pos, {10.0f, 10.0f}) && near(start.vertices[2].pos, {100.0f, 40.0f})) << "eased 0 leaves the leaving slide in place";
        TwoSlides end = codeBlockMoving(true);
        morphPositions(end.vertices, end.leaving, end.arriving, 1.0f);
        expect(near(end.vertices[4].pos, {10.0f, 140.0f}) && near(end.vertices[2].pos, {100.0f, 160.0f})) << "eased 1 puts it where the arriving one is";
        expect(near(end.vertices[12].pos, {10.0f, 140.0f})) << "and the arriving slide stays where it was drawn";
    };
};

int main() { return 0; }
