#ifndef GR4_PRESENT_FALLBACK_SCENE_HPP
#define GR4_PRESENT_FALLBACK_SCENE_HPP

#include "Texture.hpp"
#include "Theme.hpp"

#include <chrono>
#include <string>

namespace gr::present {

/// shown when no presentation could be read, naming the address that did not answer
/// drawn from the embedded mark and face only, so it cannot itself fail to load
struct FallbackScene {
    std::string               address;
    std::string               reason;
    std::size_t               attempt = 0UZ;
    std::chrono::milliseconds untilRetry{};

    void draw(const Texture& mark, const Theme& theme) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_FALLBACK_SCENE_HPP
