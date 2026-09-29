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

/// rasterises a formula once and keeps it, white and tinted at blit time, so one entry serves every colour
struct FormulaCache {
    /// formulas actually rasterised; climbs every frame when the cache misses
    std::size_t rasterised = 0UZ;

    /// `pixels` is the text size, rounded into the key: a formula is rasterised at the size it is drawn
    [[nodiscard]] const PlacedFormula& get(std::string_view latex, float pixels, bool display);

private:
    std::map<std::tuple<std::string, int, bool>, PlacedFormula, std::less<>> _rendered;
};

} // namespace gr::present

#endif // GR4_PRESENT_FORMULA_CACHE_HPP
