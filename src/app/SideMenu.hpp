#ifndef GR4_PRESENT_SIDE_MENU_HPP
#define GR4_PRESENT_SIDE_MENU_HPP

#include "Theme.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gr::present {

/**
 * Controls parked off the left edge so a presentation view is unobstructed, sliding out as the pointer nears them.
 *
 * Proximity rather than hover, because a strip wide enough to hover reliably is already wide enough to distract. On a
 * touch screen there is no pointer at all, so a tap inside the same margin opens it.
 *
 * The slides are a lens centred on the one shown and the controls stay at the foot: a deck of forty sections once
 * put `speaker notes` below the fold, where a presenter looking for it during a talk will not find it.
 */
struct SideMenu {
    struct Item {
        std::string           label;
        std::function<void()> activate;
        bool                  separatorBefore = false;
        std::string           mnemonic; // the key that does the same thing, shown at the right of the entry
    };

    std::vector<Item> items;           // the slides, as a lens centred on the one shown
    std::size_t       current = 0UZ;   // which of `items` is the slide being shown
    std::vector<Item> utilities;       // what the menu can do, kept in view at the foot whatever the list is doing
    float             revealed = 0.0f; // 0 hidden, 1 fully out; animated towards the target each frame

    std::vector<std::pair<std::size_t, Rectangle>> drawnEntries = {}; // the slide entries drawn last frame, by index into `items`
    Rectangle                                      drawnPanel   = {}; // the panel drawn last frame, empty while hidden

    void draw(const Theme& theme);

    static constexpr float kLensLargest  = 18.0f; // points, the slide being shown
    static constexpr float kLensSmallest = 8.0f;  // points, every name further than the lens reaches
    static constexpr float kLensReach    = 3.5f;  // entries either side of the centre over which a name shrinks

    /// at least this many utility entries fit the menu's foot without it growing
    static constexpr std::size_t kEntriesInView = 10UZ;

    /// the size of a slide's name `distance` entries away from the one shown, on a drum turning away from the reader
    [[nodiscard]] static float lensPoints(std::size_t distance);
};

} // namespace gr::present

#endif // GR4_PRESENT_SIDE_MENU_HPP
