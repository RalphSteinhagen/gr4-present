#ifndef GR4_PRESENT_SIDE_MENU_HPP
#define GR4_PRESENT_SIDE_MENU_HPP

#include "Theme.hpp"

#include <functional>
#include <string>
#include <vector>

namespace gr::present {

/**
 * Controls parked off the left edge so a presentation view is unobstructed, sliding out as the pointer nears them.
 *
 * Proximity rather than hover, because a strip wide enough to hover reliably is already wide enough to distract. On a
 * touch screen there is no pointer at all, so a tap inside the same margin opens it.
 */
struct SideMenu {
    struct Item {
        std::string           label;
        std::function<void()> activate;
        bool                  separatorBefore = false;
    };

    std::vector<Item> items;
    float             revealed = 0.0f; // 0 hidden, 1 fully out; animated towards the target each frame

    void draw(const Theme& theme);
};

} // namespace gr::present

#endif // GR4_PRESENT_SIDE_MENU_HPP
