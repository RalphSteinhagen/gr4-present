#include "FormulaCache.hpp"

#include "MathRender.hpp"

#include <cmath>
#include <utility>

namespace gr::present {

const PlacedFormula& FormulaCache::get(std::string_view latex, float pixels, bool display) {
    // rounded, so a viewport resized by a fraction of a pixel does not rasterise everything again
    auto key = std::tuple<std::string, int, bool>{std::string{latex}, static_cast<int>(std::lround(pixels)), display};
    if (const auto found = _rendered.find(key); found != _rendered.end()) {
        return found->second;
    }

    ++rasterised;
    PlacedFormula placed;
    // white, so the blit can tint it to whatever the theme asks for
    if (auto formula = renderFormula(latex, static_cast<float>(std::get<1>(key)), 0xFFFFFFFFU, display); formula.has_value()) {
        placed.texture  = Texture::loadRgba(formula->image.rgba, static_cast<int>(formula->image.width), static_cast<int>(formula->image.height));
        placed.baseline = formula->baseline;
    } else {
        placed.problem = std::move(formula.error());
    }
    return _rendered.emplace(std::move(key), std::move(placed)).first->second;
}

} // namespace gr::present
