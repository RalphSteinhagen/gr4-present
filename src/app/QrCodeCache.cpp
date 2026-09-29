#include "QrCode.hpp"

#include <cmath>
#include <utility>

namespace gr::present {

const PlacedQrCode& QrCache::get(std::string_view text, float pixels) {
    auto key = std::pair<std::string, int>{std::string{text}, static_cast<int>(std::lround(pixels))};
    if (const auto found = _encoded.find(key); found != _encoded.end()) {
        return found->second;
    }

    PlacedQrCode placed;
    if (auto image = renderQr(key.first, key.second); image.has_value()) {
        placed.texture = Texture::loadRgba(image->rgba, static_cast<int>(image->width), static_cast<int>(image->height));
    } else {
        placed.problem = std::move(image.error());
    }
    return _encoded.emplace(std::move(key), std::move(placed)).first->second;
}

} // namespace gr::present
