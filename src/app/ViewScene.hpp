#ifndef GR4_PRESENT_VIEW_SCENE_HPP
#define GR4_PRESENT_VIEW_SCENE_HPP

#include "Texture.hpp"
#include "Theme.hpp"

#include <string>

namespace gr::present {

/// draws one view of a presentation: its artwork, its heading, and a note about anything that could not be loaded
struct ViewScene {
    std::string heading;
    std::string hint; // shown small and dim beneath the heading; empty draws nothing

    void draw(const Texture& artwork, const Theme& theme) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_VIEW_SCENE_HPP
