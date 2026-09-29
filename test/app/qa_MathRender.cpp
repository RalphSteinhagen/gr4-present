#include <boost/ut.hpp>

#include "MathRender.hpp"

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr std::uint32_t kOpaqueWhite = 0xFFFFFFFFU;
constexpr std::uint32_t kOpaqueRed   = 0xFF0000FFU; // ImGui packs 0xAABBGGRR

/**
 * Every formula is rendered in `main`, and the suite below only reads what came out.
 *
 * boost.ut runs a suite from a static destructor, and MicroTeX keeps its macro tables in globals of its own. Those
 * are constructed before this file's, so they are destroyed after it -- which means a suite that called the engine
 * directly would be calling into tables that no longer exist. The GUI tests here already work this way for the same
 * kind of reason.
 */
// never destroyed on purpose: boost.ut runs a suite from a static destructor, and under libc++ a plain global here
// is torn down first, so the assertions would read freed memory. This costs a few kilobytes at exit.
auto& gRendered = *new std::map<std::string, std::expected<Formula, std::string>, std::less<>>{};

[[nodiscard]] const std::expected<Formula, std::string>& rendered(std::string_view name) { return gRendered.find(name)->second; }

auto& gOutlined = *new std::map<std::string, std::expected<VectorDrawing, std::string>, std::less<>>{}; // for the same reason

[[nodiscard]] std::uint8_t alphaAt(const RasterImage& image, std::uint32_t x, std::uint32_t y) { return image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4UZ + 3UZ]; }

[[nodiscard]] std::size_t inkedPixels(const RasterImage& image) {
    std::size_t inked = 0UZ;
    for (std::size_t pixel = 3UZ; pixel < image.rgba.size(); pixel += 4UZ) {
        inked += image.rgba[pixel] > 128U ? 1UZ : 0UZ;
    }
    return inked;
}

} // namespace

// Ground truth is TeX's own typesetting rules and the shapes of the glyphs, never a stored picture: a digit with a
// counter has a hole in it, a fraction is taller than its numerator, and display style sets a sum's limits above and
// below rather than beside it. Each says what the output must be, independently of how it is produced.
const suite<"MathRender"> mathRenderTests = [] {
    "a formula rasterises to something with ink in it"_test = [] {
        const auto& formula = rendered("pythagoras");
        expect(formula.has_value()) << (formula.has_value() ? std::string{} : formula.error());
        if (!formula.has_value()) {
            return;
        }
        expect(gt(formula->image.width, 0U));
        expect(gt(formula->image.height, 0U));
        expect(eq(formula->image.rgba.size(), static_cast<std::size_t>(formula->image.width) * formula->image.height * 4UZ));
        expect(gt(inkedPixels(formula->image), 100UZ)) << "the bitmap came back blank";
        expect(gt(formula->image.width, formula->image.height)) << "this equation is wider than it is tall";
        expect(gt(formula->baseline, 0.0f));
        expect(lt(formula->baseline, static_cast<float>(formula->image.height)));
    };

    "a glyph with a counter keeps its hole"_test = [] {
        // the whole reason the outlines go to plutovg rather than to a draw list: a draw list fills one simple
        // polygon, so the inside of a zero would come out solid
        const auto& zero = rendered("zero");
        expect(zero.has_value()) << (zero.has_value() ? std::string{} : zero.error());
        if (!zero.has_value()) {
            return;
        }
        const RasterImage&  image   = zero->image;
        const std::uint32_t middleY = image.height / 2U;
        expect(lt(alphaAt(image, image.width / 2U, middleY), std::uint8_t{64})) << "the centre of a zero is filled in, so holes are being ignored";

        std::size_t crossings = 0UZ;
        bool        inside    = false;
        for (std::uint32_t x = 0U; x < image.width; ++x) {
            const bool ink = alphaAt(image, x, middleY) > 128U;
            crossings += ink && !inside ? 1UZ : 0UZ;
            inside = ink;
        }
        expect(eq(crossings, 2UZ)) << "a horizontal cut through the middle of a zero crosses its stroke exactly twice";
    };

    "a fraction is taller than the things it is made of"_test = [] {
        const auto& plain    = rendered("one");
        const auto& fraction = rendered("half");
        expect(plain.has_value() && fraction.has_value());
        if (!plain.has_value() || !fraction.has_value()) {
            return;
        }
        // TeX sets a numerator and a denominator at script size, about 0.7 of text size, and separates them by the
        // fraction rule with a prescribed gap on each side, so the stack is a good half again taller than one digit
        expect(gt(fraction->image.height, plain->image.height * 3U / 2U)) << "the fraction was not stacked";
    };

    "display style sets a sum's limits above and below it"_test = [] {
        const auto& text    = rendered("sum-text");
        const auto& display = rendered("sum-display");
        expect(text.has_value() && display.has_value());
        if (!text.has_value() || !display.has_value()) {
            return;
        }
        // TeX's rule, not this renderer's: the same formula becomes taller and narrower when its limits move from
        // beside the operator to above and below it
        expect(gt(display->image.height, text->image.height)) << "display style did not grow the operator";
        expect(lt(display->image.width, text->image.width)) << "display style did not stack the limits";
    };

    "the colour asked for is the colour drawn"_test = [] {
        const auto& red = rendered("red-a");
        expect(red.has_value());
        if (!red.has_value()) {
            return;
        }
        bool sawRed = false;
        for (std::size_t pixel = 0UZ; pixel + 3UZ < red->image.rgba.size(); pixel += 4UZ) {
            sawRed = sawRed || (red->image.rgba[pixel + 3UZ] > 200U && red->image.rgba[pixel] > 200U && red->image.rgba[pixel + 1UZ] < 60U && red->image.rgba[pixel + 2UZ] < 60U);
        }
        expect(sawRed) << "the glyph was not drawn in the colour that was asked for";
    };

    "a formula that yields nothing comes back as a message"_test = [] {
        const auto& broken = rendered("broken"); // an environment that is opened and never closed
        expect(!broken.has_value()) << "a formula with no extent must not pretend to have rendered";
        if (!broken.has_value()) {
            expect(!broken.error().empty()) << "the author needs to be told what went wrong";
        }
        expect(!rendered("empty").has_value());
    };

    "a formula whose command needs a font that is not loaded is caught, not fatal"_test = [] {
        // `\mathcal` wants a calligraphic family, and only the maths font is loaded, so the engine throws rather
        // than returning. Catching it is what keeps one bad command on one slide from taking the viewer with it --
        // which is exactly what it did until the Emscripten link was told to generate the catch.
        const auto& thrown = rendered("throws");
        expect(!thrown.has_value()) << "an unsupported command must not pretend to have rendered";
        if (!thrown.has_value()) {
            expect(!thrown.error().empty()) << "the author needs to be told what went wrong";
        }
    };

    "an unknown command is set as text rather than refused"_test = [] {
        // worth recording rather than assuming: the engine does not reject a command it does not know, it typesets
        // the name, so a misspelling shows up on the slide as words instead of as a diagnostic
        const auto& unknown = rendered("unknown-command");
        expect(unknown.has_value());
        if (unknown.has_value()) {
            expect(gt(inkedPixels(unknown->image), 100UZ));
        }
    };

    // The outlines an exported page draws must be the formula the screen shows. The bitmap is the second, independent
    // rendering: plutovg filled the same engine output, so the outlines' extent and the bitmap's ink must agree to
    // within the anti-aliasing at the edges.
    "a formula's outlines cover the same place as its bitmap"_test = [] {
        for (const std::string_view name : {"pythagoras", "sum-display", "red-a"}) {
            const auto& outlined = gOutlined.find(name)->second;
            const auto& bitmap   = rendered(name);
            expect(outlined.has_value() && bitmap.has_value()) << name;
            if (!outlined.has_value() || !bitmap.has_value()) {
                continue;
            }
            expect(eq(outlined->width, static_cast<float>(bitmap->image.width)) && eq(outlined->height, static_cast<float>(bitmap->image.height))) << name << ": the outlines are in the bitmap's pixels";
            float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
            for (const VectorPath& path : outlined->paths) {
                for (std::size_t at = 0UZ; at + 1UZ < path.points.size(); at += 2UZ) {
                    left   = std::min(left, path.points[at]);
                    right  = std::max(right, path.points[at]);
                    top    = std::min(top, path.points[at + 1UZ]);
                    bottom = std::max(bottom, path.points[at + 1UZ]);
                }
            }
            std::uint32_t inkLeft = bitmap->image.width, inkTop = bitmap->image.height, inkRight = 0U, inkBottom = 0U;
            for (std::uint32_t y = 0U; y < bitmap->image.height; ++y) {
                for (std::uint32_t x = 0U; x < bitmap->image.width; ++x) {
                    if (alphaAt(bitmap->image, x, y) > 128U) {
                        inkLeft   = std::min(inkLeft, x);
                        inkRight  = std::max(inkRight, x + 1U);
                        inkTop    = std::min(inkTop, y);
                        inkBottom = std::max(inkBottom, y + 1U);
                    }
                }
            }
            constexpr float kEdge = 2.0f; // pixels: an outline's control points may sit just outside the ink they bound
            expect(std::abs(left - static_cast<float>(inkLeft)) <= kEdge && std::abs(right - static_cast<float>(inkRight)) <= kEdge) << name << ": horizontally " << left << ".." << right << " against ink " << inkLeft << ".." << inkRight;
            expect(std::abs(top - static_cast<float>(inkTop)) <= kEdge && std::abs(bottom - static_cast<float>(inkBottom)) <= kEdge) << name << ": vertically " << top << ".." << bottom << " against ink " << inkTop << ".." << inkBottom;
        }
    };

    "an outlined formula keeps the colour asked for, and a broken one says why"_test = [] {
        const auto& red = gOutlined.find("red-a")->second;
        expect(red.has_value() && !red->paths.empty() && std::ranges::all_of(red->paths, [](const VectorPath& path) { return path.fill == kOpaqueRed; })) << "every outline of the red A is filled red";
        const auto& broken = gOutlined.find("broken")->second;
        expect(!broken.has_value() && !broken.error().empty()) << "an environment opened and never closed has nothing to outline either";
    };
};

int main() {
    gRendered.emplace("pythagoras", renderFormula("x^2 + y^2 = z^2", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("zero", renderFormula("0", 96.0f, kOpaqueWhite, false));
    gRendered.emplace("one", renderFormula("1", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("half", renderFormula("\\frac{1}{2}", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("sum-text", renderFormula("\\sum_{i=1}^{n} i", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("sum-display", renderFormula("\\sum_{i=1}^{n} i", 48.0f, kOpaqueWhite, true));
    gRendered.emplace("red-a", renderFormula("A", 64.0f, kOpaqueRed, false));
    gRendered.emplace("broken", renderFormula("\\begin{matrix}", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("unknown-command", renderFormula("\\unknownmacro", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("throws", renderFormula("\\mathcal{F}", 48.0f, kOpaqueWhite, false));
    gRendered.emplace("empty", renderFormula("", 48.0f, kOpaqueWhite, false));
    gOutlined.emplace("pythagoras", outlineFormula("x^2 + y^2 = z^2", 48.0f, kOpaqueWhite, false));
    gOutlined.emplace("sum-display", outlineFormula("\\sum_{i=1}^{n} i", 48.0f, kOpaqueWhite, true));
    gOutlined.emplace("red-a", outlineFormula("A", 64.0f, kOpaqueRed, false));
    gOutlined.emplace("broken", outlineFormula("\\begin{matrix}", 48.0f, kOpaqueWhite, false));
    return 0;
}
