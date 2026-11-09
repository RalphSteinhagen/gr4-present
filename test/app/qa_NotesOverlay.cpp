#include <boost/ut.hpp>

#include "NotesOverlay.hpp"

#include <cmath>
#include <cstddef>
#include <functional>

using namespace boost::ut;
using namespace gr::present;

// Ground truth: notes start at 24 pt, shrink to fit like a slide's text box, never below 10 pt; a model face, every glyph
// half as wide as the size and every line as tall, `glyphs` of them wrapped at `width` pixels.
namespace {

constexpr float kWidth    = 600.0f;
constexpr float kRoom     = 200.0f;
constexpr float kLargest  = 24.0f;
constexpr float kSmallest = 10.0f;

[[nodiscard]] auto modelText(std::size_t glyphs) {
    return [glyphs](float size) {
        const float perLine = std::floor(kWidth / (0.5f * size));
        return std::ceil(static_cast<float>(glyphs) / perLine) * size;
    };
}

[[nodiscard]] float fitted(const std::function<float(float)>& tallAt) { return NotesOverlay::fittingPixels(NotesOverlay::kLargestPoints, NotesOverlay::kSmallestPoints, kRoom, tallAt); }

} // namespace

const suite<"NotesOverlay"> notesOverlayTests = [] {
    "notes that fit at the largest size are set there"_test = [] {
        // 100 glyphs at 24 px: 50 per line, two lines of 24 px
        expect(eq(fitted(modelText(100UZ)), kLargest));
    };

    "longer notes shrink to the largest size that still fits"_test = [] {
        const auto  tallAt = modelText(1000UZ); // 20 lines at 24 px, 480 px against 200
        const float size   = fitted(tallAt);
        expect(size > kSmallest && size < kLargest) << size;
        expect(le(tallAt(size), kRoom)) << "they fit";
        expect(gt(tallAt(size + 0.5f), kRoom)) << "and half a pixel larger they would not";
    };

    "notes too long even at 10 pt are set at 10 pt"_test = [] { expect(eq(fitted(modelText(20'000UZ)), kSmallest)); };

    "more notes are never set larger"_test = [] {
        float previous = kLargest;
        for (std::size_t glyphs = 100UZ; glyphs <= 6'400UZ; glyphs *= 2UZ) {
            const float size = fitted(modelText(glyphs));
            expect(le(size, previous)) << glyphs << " glyphs";
            previous = size;
        }
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
