#ifndef GR4_PRESENT_FALLBACK_SCENE_HPP
#define GR4_PRESENT_FALLBACK_SCENE_HPP

#include "Texture.hpp"
#include "Theme.hpp"

#include <chrono>
#include <string>

namespace gr::present {

/**
 * Shown when no presentation could be read.
 *
 * Drawn from what is already in the binary -- the mark and the bundled face -- so that it cannot itself fail to
 * load, and so that nothing large has to be embedded for a screen that is usually never seen. It states the address
 * that did not answer, because "could not load" without the address is useless on someone else's machine.
 *
 * It will be replaced by a genuine embedded presentation once the package format carries enough to express it;
 * keeping the wording and layout close to ViewScene makes that swap uneventful.
 */
struct FallbackScene {
    std::string               address;
    std::string               reason;
    std::size_t               attempt = 0UZ;
    std::chrono::milliseconds untilRetry{};

    void draw(const Texture& mark, const Theme& theme) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_FALLBACK_SCENE_HPP
