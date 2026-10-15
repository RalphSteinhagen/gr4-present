#ifndef GR4_PRESENT_SIDE_MENU_HPP
#define GR4_PRESENT_SIDE_MENU_HPP

#include "Theme.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace gr::present {

/// the presenter's controls, in interface points (the device's own scale, not the slide's), parked off the left edge, sliding out as the pointer nears; a tap inside the same margin opens it by touch
/// proximity rather than hover, as a strip wide enough to hover reliably would distract
/// the slides are listed in one size with the one shown in bold, slightly larger; a list too long to fit at a readable size
/// scrolls instead and keeps the shown one in view; the controls stay at the foot, never below the fold, and the deck's
/// files (Load, follow edits, Save) at the top, drawn as the controls are
struct SideMenu {
    struct Item {
        std::string           label;
        std::function<void()> activate;
        bool                  separatorBefore = false;
        std::string           mnemonic; // the key that does the same thing, shown at the right of the entry
    };

    std::vector<Item> items;                                                 // the slides
    std::size_t       current = 0UZ;                                         // which of `items` is the slide being shown
    std::vector<Item> utilities;                                             // what the menu can do, kept in view at the foot whatever the list is doing
    std::vector<Item> files       = {};                                      // Load, then Save, above the slides
    bool*             followEdits = nullptr;                                 // a toggle between Load and Save while a deck that can be followed is shown
    float             revealed    = 0.0f;                                    // 0 hidden, 1 fully out; animated towards the target each frame
    std::size_t       scrolledTo  = std::numeric_limits<std::size_t>::max(); // the shown slide the list last scrolled to; a new one is scrolled into view

    std::vector<std::pair<std::size_t, Rectangle>> drawnEntries     = {}; // the slide entries drawn last frame, by index into `items`
    std::vector<std::pair<std::size_t, Rectangle>> drawnFiles       = {}; // Load and Save drawn last frame, by index into `files`
    Rectangle                                      drawnFollowEdits = {}; // the toggle drawn last frame, empty without one
    Rectangle                                      drawnPanel       = {}; // the panel drawn last frame, empty while hidden

    void draw(const Theme& theme, float interfacePixelsPerPoint);

    struct ListSizes {
        float otherPoints = 0.0f;  // every slide but the shown one, which is set bold at the controls' size
        bool  scrolls     = false; // the names do not all fit at the smallest size
    };

    static constexpr float kOtherPoints           = 16.0f;  // a slide's name when every one fits at it
    static constexpr float kSmallestOtherPoints   = 12.5f;  // below this the list scrolls rather than shrinks
    static constexpr float kPanelPoints           = 225.0f; // the panel's width, capped by the window's share below
    static constexpr float kPortraitWidthShare    = 0.40f;  // of a window taller than wide (a phone held upright)
    static constexpr float kLandscapeWidthShare   = 0.25f;  // of any other window
    static constexpr float kLineSpacingEntrySizes = 1.25f;  // a line's height, in sizes of its type

    /// the size that fits the names other than the shown one into `roomPoints` of height beside it: 16 pt when all fit,
    /// else one smaller size, else 12.5 pt and a scrolling list; points are the device's interface points
    [[nodiscard]] static ListSizes listSizes(std::size_t entries, float roomPoints) noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_SIDE_MENU_HPP
