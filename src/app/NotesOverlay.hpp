#ifndef GR4_PRESENT_NOTES_OVERLAY_HPP
#define GR4_PRESENT_NOTES_OVERLAY_HPP

#include "TextRuns.hpp"
#include "Theme.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <array>

#include <string_view>

namespace gr::present {

/// the presenter's panel: notes for the view on screen, the position in the talk and what comes next
/// drawn over the foot of the slide on a laptop; `compact` is a phone as the remote, notes above one row of buttons
struct NotesOverlay {
    bool visible = false;
    bool compact = false; // a phone's presenter view: the notes fill the screen above one row of buttons

    /// what the three buttons at the panel's right edge ask for, top to bottom: the next step, the next slide, back
    enum class Action { none, nextStep, nextSlide, back };

    /// `position` is what the status line says, such as "3 / 12"; `nextTitle` is empty on the last view
    Action draw(const Theme& theme, std::string_view notes, std::string_view position, std::string_view nextTitle, float interfacePixelsPerPoint);

    std::array<Rectangle, 3> buttons     = {};      // where the three buttons were drawn last frame, in the order above
    float                    buttonsLeft = 0.0f;    // the left edge of their column, so whatever else sits in the panel keeps clear
    Rectangle                notesArea;             // where the notes were set last frame, when compact
    std::array<Rectangle, 2> miniatures     = {};   // where the slide now shown and the next press's go, when compact; screen pixels
    float                    notesPixels    = 0.0f; // the size they were set at, after shrinking to fit
    float                    notesScroll    = 0.0f; // how far they are scrolled, and how far they can be
    float                    notesScrollMax = 0.0f;
    TextRuns                 runs; // the notes' lines as drawn last frame, in screen pixels, for a reader to select
};

} // namespace gr::present

#endif // GR4_PRESENT_NOTES_OVERLAY_HPP
