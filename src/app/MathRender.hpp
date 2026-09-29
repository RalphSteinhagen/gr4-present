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
    /// measured down from the top of the bitmap, so an inline formula sits on the text baseline
    float baseline = 0.0f;
};

/// rasterises a LaTeX formula with MicroTeX (`GLYPH_RENDER_TYPE=1`: outlines from the vendored FiraMath clm2)
/// outlines are filled with plutovg because a draw list cannot fill paths with holes (`0`, `e`, `@`)
/// `display` selects TeX's display style (`$$...$$`); a formula the engine cannot parse yields its message
[[nodiscard]] std::expected<Formula, std::string> renderFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display);

/// the same formula as outlines in the bitmap's pixels, for a page that draws vectors
[[nodiscard]] std::expected<VectorDrawing, std::string> outlineFormula(std::string_view latex, float pixels, std::uint32_t rgba, bool display);

} // namespace gr::present

#endif // GR4_PRESENT_MATH_RENDER_HPP
