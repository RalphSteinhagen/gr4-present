#ifndef GR4_PRESENT_MATH_RENDER_HPP
#define GR4_PRESENT_MATH_RENDER_HPP

#include "SvgImage.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace gr::present {

struct Formula {
    RasterImage image;
    /// measured down from the top of the bitmap, so an inline formula sits on the text baseline rather than on the
    /// bottom of its own box
    float baseline = 0.0f;
};

/**
 * Rasterises a LaTeX formula.
 *
 * MicroTeX is compiled with `GLYPH_RENDER_TYPE=1`, so it hands the backend glyph outlines rather than glyph ids and
 * no typeface is ever loaded: the vendored `FiraMath-Regular.clm2` carries both the OpenType MATH metrics and the
 * outlines. The outlines are filled with plutovg, which lunasvg already brings to both backends, because a draw
 * list cannot fill a path that has holes and every glyph with a counter -- `0`, `e`, `@` -- has one.
 *
 * The result is a bitmap, and a formula does not change between frames, so the caller uploads it once and keeps it
 * exactly as it keeps a figure.
 *
 * `display` selects TeX's display style, which is what `$$...$$` means: larger operators, and limits above and
 * below rather than beside. A formula the engine cannot parse yields the message it raised, for the slide to show:
 * a broken formula is the author's mistake and hiding it helps nobody.
 */
[[nodiscard]] std::expected<Formula, std::string> renderFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display);

/// the same formula as outlines in the bitmap's pixels, for a page that draws it as vectors rather than as the bitmap
[[nodiscard]] std::expected<VectorDrawing, std::string> outlineFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display);

} // namespace gr::present

#endif // GR4_PRESENT_MATH_RENDER_HPP
