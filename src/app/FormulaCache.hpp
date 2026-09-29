#ifndef GR4_PRESENT_FORMULA_CACHE_HPP
#define GR4_PRESENT_FORMULA_CACHE_HPP

#include "Texture.hpp"

#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace gr::present {

struct PlacedFormula {
    Texture     texture;
    float       baseline = 0.0f; // in texels, measured down from the top of the bitmap
    std::string problem;         // when set, the formula did not render and this is what to show instead
};

/**
 * Rasterises a formula once and keeps it.
 *
 * A formula does not change between frames, so this is the same bargain `FigureCache` makes. The bitmap is drawn
 * white and tinted when it is blitted, which keeps one entry per formula rather than one per colour and lets a
 * cross-fade change its opacity without re-rendering anything.
 */
struct FormulaCache {
    /// how many formulas were actually rasterised; a cache that misses every frame makes this climb without bound
    std::size_t rasterised = 0UZ;

    /// `pixels` is the text size the formula is set at; it is part of the key, because a formula is rasterised at
    /// the size it will be drawn rather than scaled from one master
    [[nodiscard]] const PlacedFormula& get(std::string_view latex, float pixels, bool display);

private:
    std::map<std::tuple<std::string, int, bool>, PlacedFormula, std::less<>> _rendered;
};

} // namespace gr::present

#endif // GR4_PRESENT_FORMULA_CACHE_HPP
