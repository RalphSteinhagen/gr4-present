#include "Viewer.hpp"

#include "SwipeNavigation.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
#include <common/TouchHandler.hpp>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// Right, Space and Page Down advance; Left, Backspace and Page Up retreat; Home returns to the first view, and N
/// shows or hides the presenter notes. L turns the pointer effect on and off; B opens the break screen, + and - add
/// or take a minute from its countdown, and any navigation key closes it without moving. The navigation keys are
/// what a presentation remote sends, which is why they are not configurable.
void applyNavigationKeys(Viewer& viewer) {
    const bool forward  = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool backward = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    const bool home     = ImGui::IsKeyPressed(ImGuiKey_Home);
    if (ImGui::IsKeyPressed(ImGuiKey_P)) { // starts and stops the clips on this view; the space bar advances
        viewer.videos.toggle();
    }
#ifdef __EMSCRIPTEN__
    if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !viewer.presenter) { // a second window for the presenter: notes, clock, next
        openPresenterWindow();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false) && !phoneLink().empty()) { // the phone presenter view's address, as a code
        viewer.showPhoneLink = !viewer.showPhoneLink;
    }
#endif
    if (ImGui::IsKeyPressed(ImGuiKey_N)) { // the presenter panel, which no remote sends and so cannot be hit by accident
        viewer.notes.visible = !viewer.notes.visible;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        viewer.pointing = !viewer.pointing;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        viewer.onBreak     = !viewer.onBreak;
        viewer.breakEndsAt = ImGui::GetTime() + 60.0 * static_cast<double>(viewer.loader.manifest().breakMinutes);
    }
    if (viewer.onBreak) {
        const bool more = ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd);
        const bool less = ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
        viewer.breakEndsAt += more ? 60.0 : (less ? -60.0 : 0.0);
        if (forward || backward || home) {
            viewer.onBreak = false; // the audience is back; the slide is where it was left
        }
        return;
    }

    const Cursor before = viewer.navigator.cursor;
    if (home && !viewer.navigator.graph.views.empty()) {
        viewer.navigator.jumpTo(viewer.navigator.graph.views.front().id);
    } else if (forward) {
        viewer.navigator.next();
    } else if (backward) {
        viewer.navigator.previous();
    }
    settleCursor(viewer, before);
}

/**
 * Mouse wheel and two-finger pinch zoom into the slide, a drag pans it, and a double click or Escape brings it back.
 *
 * The slide wins over whatever is on it: a chart never sees the wheel, so the same gesture does the same thing
 * everywhere on a slide. One finger on a zoomed slide pans it instead of turning the page, which is why a flick
 * navigates only when the slide is not zoomed.
 */
void applyZoomInput(Viewer& viewer) {
    ImGuiIO&      io       = ImGui::GetIO();
    const ImVec2& size     = ImGui::GetMainViewport()->Size;
    bool          twoTouch = false;

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
    using Touch = DigitizerUi::TouchHandler<>;
    if (Touch::nFingers == 2UZ) {
        twoTouch              = true;
        const ImVec2 a        = Touch::fingerPos[0];
        const ImVec2 b        = Touch::fingerPos[1];
        const float  distance = std::hypot(a.x - b.x, a.y - b.y);
        const ImVec2 centre   = ImVec2{0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
        if (viewer.pinching && viewer.pinchDistance > 1.0f) {
            viewer.zoom.zoomAt(centre.x, centre.y, distance / viewer.pinchDistance, size.x, size.y);
            viewer.zoom.panBy(centre.x - viewer.pinchCentre.x, centre.y - viewer.pinchCentre.y, size.x, size.y);
        }
        viewer.pinching      = true;
        viewer.pinchDistance = distance;
        viewer.pinchCentre   = centre;
    } else if (Touch::nFingers == 0UZ) {
        viewer.pinching = false;
    }
#endif

    if (io.WantCaptureMouse) {
        return; // the side menu and the notes scroll and click on their own
    }
    if (io.MouseWheel != 0.0f && !twoTouch) {
        constexpr float kFactorPerNotch = 1.15f;
        viewer.zoom.zoomAt(io.MousePos.x, io.MousePos.y, std::pow(kFactorPerNotch, io.MouseWheel), size.x, size.y);
    }
    io.MouseWheel  = 0.0f;
    io.MouseWheelH = 0.0f;

    const bool escape = escapeReachedThePage() || ImGui::IsKeyPressed(ImGuiKey_Escape, false); // read every frame, or a stale one resets a later zoom
    if (viewer.zoom.active() && (io.MouseDoubleClicked[0] || escape)) {
        viewer.zoom.reset();
    }
    if (viewer.zoom.active() && !twoTouch && io.MouseDown[0] && !viewer.selection.anchor.has_value()) {
        viewer.zoom.panBy(io.MouseDelta.x, io.MouseDelta.y, size.x, size.y);
    }
}

/// a click that lands on a button under a clip does what the button says; a drag that ended there does not
void applyVideoButtons(Viewer& viewer) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !io.MouseReleased[0] || io.MouseDragMaxDistanceSqr[0] > kDragThresholdSquared) {
        return;
    }
    const float x = viewer.zoom.slideX(io.MousePos.x);
    const float y = viewer.zoom.slideY(io.MousePos.y);
    for (const DocumentView::VideoButton& button : viewer.documentView.videoButtons) {
        const Rectangle& area = button.area;
        if (x < area.x || x > area.x + area.width || y < area.y || y > area.y + area.height) {
            continue;
        }
        switch (button.action) {
        case DocumentView::VideoAction::playPause: viewer.videos.toggle(button.source); break;
        case DocumentView::VideoAction::rewind: viewer.videos.restart(button.source); break;
        case DocumentView::VideoAction::muteToggle: viewer.videos.toggleMute(button.source); break;
        }
        return;
    }
}

/// a click on a link follows it: `#view` inside the deck; a deck's own PDF exported where it runs natively, since
/// the deck shown there is the one asked for; anything else opened by the system
void applyLinkClicks(Viewer& viewer) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return;
    }
    const float x       = viewer.zoom.slideX(io.MousePos.x);
    const float y       = viewer.zoom.slideY(io.MousePos.y);
    const auto  under   = [x, y](const Rectangle& area) { return x >= area.x && x <= area.x + area.width && y >= area.y && y <= area.y + area.height; };
    const auto  clicked = std::ranges::find_if(viewer.documentView.linkAreas, under, &DocumentView::LinkArea::area);
    // what can be clicked says so the way a web page does, so a link in a slide does not have to be found by trying
    if (clicked != viewer.documentView.linkAreas.end() || std::ranges::any_of(viewer.documentView.videoButtons, under, &DocumentView::VideoButton::area)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (!io.MouseReleased[0] || io.MouseDragMaxDistanceSqr[0] > kDragThresholdSquared) {
        return;
    }
    // A fingertip is wider than a line of small text, so a tap takes the nearest link within half a touch target of it
    // (44 px, as Apple's guidelines and WCAG 2.5.5 size one); a pointer takes only the link under it.
    constexpr float kTouchTargetPixels = 44.0f;
    const auto      distanceTo         = [x, y](const DocumentView::LinkArea& link) {
        const Rectangle& area = link.area;
        return std::hypot(std::max({area.x - x, 0.0f, x - area.x - area.width}), std::max({area.y - y, 0.0f, y - area.y - area.height}));
    };
    const auto nearest = std::ranges::min_element(viewer.documentView.linkAreas, {}, distanceTo);
    const auto taken   = io.MouseSource == ImGuiMouseSource_TouchScreen && nearest != viewer.documentView.linkAreas.end() && distanceTo(*nearest) <= kTouchTargetPixels * 0.5f ? nearest : clicked;
    if (taken == viewer.documentView.linkAreas.end()) {
        return;
    }
    const std::string target = taken->target; // the areas are rebuilt by whatever the click starts
    if (target.starts_with('#')) {
        const Cursor before = viewer.navigator.cursor;
        const bool   jumped = viewer.navigator.jumpTo(std::string_view{target}.substr(1UZ));
        if (jumped) {
            settleCursor(viewer, before);
        }
        return;
    }
#ifndef __EMSCRIPTEN__
    if (const auto exportAt = target.find("export="); exportAt != std::string::npos) {
        startExport(viewer, std::string_view{target}.substr(exportAt + 7UZ, 5UZ) == "steps" ? "steps" : "slides", false);
        return;
    }
#endif
    SDL_OpenURL(target.c_str());
}

#ifdef __EMSCRIPTEN__
// SDL3 has no clipboard in a browser. The Clipboard API wants a secure context, which a deck served over plain http
// on a LAN address is not, so a hidden textarea and `execCommand` stand in there; the canvas gets the keys back.
// clang-format off
EM_JS(void, copyToClipboard, (const char* text), {
    const value    = UTF8ToString(text);
    const fallback = () => {
        const area = document.createElement("textarea");
        area.value = value;
        area.style.position = "fixed";
        area.style.opacity  = "0";
        document.body.appendChild(area);
        area.select();
        try { document.execCommand("copy"); } catch (error) {}
        area.remove();
        if (Module.canvas) { Module.canvas.focus(); }
    };
    if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(value).catch(fallback);
    } else {
        fallback();
    }
});
// clang-format on
#endif

/// A mouse or pen drag that starts on drawn text selects it, and Ctrl+C (Cmd+C on a Mac) copies it. A drag that
/// starts elsewhere pans as it always has, a press that does not move is a click and clears the selection, and a
/// finger never selects: on a touch screen a drag turns the page.
void applyTextSelection(Viewer& viewer) {
    const ImGuiIO& io        = ImGui::GetIO();
    TextSelection& selection = viewer.selection;
    if (!(selection.on == viewer.navigator.cursor)) {
        selection = TextSelection{.on = viewer.navigator.cursor};
    }
    // the slide's words are where the zoom put them; the notes' are where they are on the screen
    const auto runsOf  = [&viewer](bool inNotes) { return std::span<const TextRun>{inNotes ? viewer.notes.runs.runs : viewer.documentView.textRuns.runs}; };
    const auto pointer = [&viewer, &io](bool inNotes) { return inNotes ? io.MousePos : ImVec2{viewer.zoom.slideX(io.MousePos.x), viewer.zoom.slideY(io.MousePos.y)}; };
    if (io.MouseClicked[0]) {
        const bool   byTouch = io.MouseSource == ImGuiMouseSource_TouchScreen;
        const ImVec2 inNotes = pointer(true);
        const ImVec2 onSlide = pointer(false);
        // the notes are an ImGui window, which claims the pointer, so a press on their words is looked for first
        const auto noted   = byTouch ? std::nullopt : runAt(runsOf(true), inNotes.x, inNotes.y);
        const auto written = byTouch || noted.has_value() || io.WantCaptureMouse ? std::nullopt : runAt(runsOf(false), onSlide.x, onSlide.y);
        selection          = TextSelection{.anchor = noted.or_else([&] { return written; }), .focus = noted.or_else([&] { return written; }), .on = viewer.navigator.cursor, .inNotes = noted.has_value()};
    }
    const std::span<const TextRun> runs = runsOf(selection.inNotes);
    if (runs.empty() || (selection.anchor.has_value() && *selection.anchor >= runs.size())) {
        selection = TextSelection{.on = viewer.navigator.cursor};
        return;
    }
    if (selection.anchor.has_value() && io.MouseDown[0] && io.MouseDragMaxDistanceSqr[0] > kDragThresholdSquared) {
        const ImVec2 at    = pointer(selection.inNotes);
        selection.dragging = true;
        selection.focus    = runAt(runs, at.x, at.y).or_else([&] { return runNearest(runs, at.x, at.y); });
    }
    if (io.MouseReleased[0] && selection.anchor.has_value() && !selection.dragging) {
        selection = TextSelection{.on = viewer.navigator.cursor}; // a click on a word selects nothing, and leaves a link to be followed
    }
    if (!selection.dragging || !selection.anchor.has_value() || !selection.focus.has_value()) {
        return;
    }
    const std::size_t first = std::min(*selection.anchor, *selection.focus);
    const std::size_t last  = std::min(std::max(*selection.anchor, *selection.focus), runs.size() - 1UZ);
    const ImU32       wash  = (viewer.theme.fill & 0x00FFFFFFU) | (0x4DU << 24U); // the accent at 30 %, so the words still read through
    ImDrawList*       into  = selection.inNotes ? ImGui::GetForegroundDrawList() : viewer.documentView.drawnInto;
    for (std::size_t index = first; index <= last; ++index) {
        const Rectangle& area = runs[index].area;
        into->AddRectFilled(ImVec2{area.x, area.y}, ImVec2{area.x + area.width, area.y + area.height}, wash);
    }
    // Ctrl+C, and Cmd+C on a Mac: ImGui only swaps the two where `__APPLE__` is defined, which a browser build never is
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) || ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_C)) {
        const std::string copied = copiedText(runs, *selection.anchor, *selection.focus);
#ifdef __EMSCRIPTEN__
        copyToClipboard(copied.c_str());
#else
        SDL_SetClipboardText(copied.c_str());
#endif
    }
}

/// shows what the slide drew magnified; the menu, the notes and the diagnostics are drawn on top, untouched
void applyZoomToDrawData(Viewer& viewer) {
    if (viewer.zoom.active()) {
        const ZoomPan&    zoom    = viewer.zoom;
        const ImVec2&     display = ImGui::GetIO().DisplaySize;
        const ImDrawData* data    = ImGui::GetDrawData();
        for (ImDrawList* list : data->CmdLists) {
            if (std::ranges::find(viewer.zoomedLists, list) == viewer.zoomedLists.end()) {
                continue;
            }
            for (ImDrawVert& vertex : list->VtxBuffer) {
                vertex.pos = ImVec2{zoom.screenX(vertex.pos.x), zoom.screenY(vertex.pos.y)};
            }
            for (ImDrawCmd& command : list->CmdBuffer) {
                command.ClipRect = ImVec4{std::max(zoom.screenX(command.ClipRect.x), 0.0f), std::max(zoom.screenY(command.ClipRect.y), 0.0f), std::min(zoom.screenX(command.ClipRect.z), display.x), std::min(zoom.screenY(command.ClipRect.w), display.y)};
            }
        }
    }
    viewer.zoomedLists.clear();
}

#ifdef GR4_PRESENT_HAS_OPENDIGITIZER
/**
 * Turns a finger's flick into navigation, using OpenDigitizer's finger tracking.
 *
 * `TouchHandler` already keeps where each finger went down and came up and when, and already synthesises the mouse
 * events ImGui needs so the side menu still works under a thumb. What it does not know is what a flick means to a
 * presentation, which is the one thing added here.
 *
 * Adapted from OpenDigitizer, `src/ui/common/TouchHandler.hpp`.
 */
void applyTouchNavigation(Viewer& viewer) {
    using Touch = DigitizerUi::TouchHandler<>;
    if (!Touch::fingerUp || Touch::nFingers != 0UZ) {
        return; // still on the glass, or nothing has been lifted since the last frame
    }
    // the finger that was just lifted is the one with an up position; a flick is one finger, so index zero
    const ImVec2     from  = Touch::fingerPosDown[0];
    const Rectangle& panel = viewer.menu.drawnPanel;
    if (panel.width > 0.0f && from.x < panel.x + panel.width) {
        return; // a flick that began on the side menu picks an entry; it must not also step the deck
    }
    // A live chart takes the pointer too, but on a phone it covers most of its slide: a flick across it turns the
    // slide, and a slow drag, which is no flick, still pans the chart.
    if (viewer.zoom.active() || viewer.pinching) {
        return; // on a magnified slide a finger pans it, and lifting the last finger of a pinch is not a page turn
    }

    const ImVec2 to = Touch::fingerPosUp[0];
    if (to.x < 0.0f || to.y < 0.0f) {
        return;
    }
    const auto  held = std::chrono::duration_cast<std::chrono::milliseconds>(Touch::fingerUpTimeStamp[0] - Touch::fingerDownTimeStamp[0]);
    const Flick flick{.fromX = from.x, .fromY = from.y, .toX = to.x, .toY = to.y, .duration = held};

    const Cursor      before = viewer.navigator.cursor;
    const SwipeAction action = swipeActionOf(flick, ImGui::GetMainViewport()->Size.x);
    if (action == SwipeAction::next) {
        viewer.navigator.next();
    } else if (action == SwipeAction::previous) {
        viewer.navigator.previous();
    }
    settleCursor(viewer, before);
}
#endif

} // namespace gr::present
