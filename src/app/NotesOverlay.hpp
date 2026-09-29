#ifndef GR4_PRESENT_NOTES_OVERLAY_HPP
#define GR4_PRESENT_NOTES_OVERLAY_HPP

#include "TextRuns.hpp"
#include "Theme.hpp"

#include <gr4-present/RegionGeometry.hpp>

#include <array>

#include <string_view>

namespace gr::present {

/**
 * The presenter's own panel: the notes for the view on screen, where the talk is up to, and what comes next.
 *
 * On a laptop it is drawn over the foot of the slide, so one window that can show or hide its notes works the same on
 * a projector and in a tab. On a phone in presenter mode (`compact`) the phone is the remote, not the screen the room
 * sees: the notes take the screen and the buttons sit in one row beneath them, where a thumb rests.
 */
struct NotesOverlay {
    bool visible = false;
    bool compact = false; // a phone's presenter view: the notes fill the screen above one row of buttons

    /// what the three buttons at the panel's right edge ask for, top to bottom: the next step, the next slide, back
    enum class Action { none, nextStep, nextSlide, back };

    /// `position` is what the status line says, such as "3 / 12"; `nextTitle` is empty on the last view
    Action draw(const Theme& theme, std::string_view notes, std::string_view position, std::string_view nextTitle);

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
