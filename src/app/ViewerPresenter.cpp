#include "Viewer.hpp"

#include <imgui_internal.h>

#include <imgui.h>

#include <SDL3/SDL.h>

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

static void drawMiniature(Viewer& viewer, const Cursor& at, const Rectangle& box, const char* window);
static void drawPhoneCodeInNotes(Viewer& viewer, bool firstView);

/// the presenter's panel: this view's notes, where the talk is up to, and which view comes next
/// the phone presenter view's address as a QR code in the middle of the screen, for the presenter's phone to read
void drawPhoneLink(Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    if (!viewer.showPhoneLink) {
        return;
    }
    const std::string    address  = phoneLink();
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          side     = std::min(viewport.Size.x, viewport.Size.y) * 0.5f;
    const PlacedQrCode&  code     = viewer.qrCodes.get(address, side);
    if (code.texture.id == 0) {
        return;
    }
    const ImVec2 corner{viewport.Pos.x + (viewport.Size.x - side) * 0.5f, viewport.Pos.y + (viewport.Size.y - side) * 0.5f};
    ImDrawList*  canvas = ImGui::GetForegroundDrawList();
    canvas->AddRectFilled(ImVec2{corner.x - 16.0f, corner.y - 16.0f}, ImVec2{corner.x + side + 16.0f, corner.y + side + 16.0f}, IM_COL32(255, 255, 255, 255), 8.0f);
    canvas->AddImage(code.texture.id, corner, ImVec2{corner.x + side, corner.y + side});
#else
    (void)viewer;
#endif
}

/// On the first slide's notes, and only there: the phone presenter view's address as a QR code at the right of the
/// panel, so the presenter connects a phone before the talk without a second screen or a typed link.
/**
 * What the room sees, shrunk into one of the phone remote's miniatures.
 *
 * Laid out as the audience's screen lays it out -- 16:9, not the phone's own shape -- by drawing it with the viewport
 * taken to be that screen, in a window of its own, then moving and scaling what was drawn into `box`, as the zoom
 * moves a slide. A presenter's window runs no graphs, so a live region shows its recording.
 */
static void drawMiniature(Viewer& viewer, const Cursor& at, const Rectangle& box, const char* window) {
    constexpr ImVec2 kRoom{1280.0f, 720.0f};
    ImGuiViewport*   viewport      = ImGui::GetMainViewport();
    const ImVec2     phoneSize     = viewport->Size;
    const ImVec2     phoneWork     = viewport->WorkSize;
    viewport->Size                 = kRoom;
    viewport->WorkSize             = kRoom;
    const char* own                = viewer.documentView.windowName;
    viewer.documentView.windowName = window;
    const SectionVisual visual     = visualFor(viewer, at);
    drawSection(viewer, visual, at.step, 1.0f, visual.image.empty() ? visual.frame : visual.region);
    viewer.documentView.windowName = own;
    if (ImGuiWindow* const drawn = ImGui::FindWindowByName(window); drawn != nullptr) {
        ImGui::BringWindowToDisplayFront(drawn); // above the notes panel, whose box it fills
    }
    viewport->Size     = phoneSize;
    viewport->WorkSize = phoneWork;

    ImDrawList* const list = viewer.documentView.drawnInto;
    if (list == nullptr || box.width <= 0.0f) {
        return;
    }
    const float scale  = box.width / kRoom.x;
    const auto  placed = [&](float x, float y) { return ImVec2{box.x + (x - viewport->Pos.x) * scale, box.y + (y - viewport->Pos.y) * scale}; };
    for (ImDrawVert& vertex : list->VtxBuffer) {
        vertex.pos = placed(vertex.pos.x, vertex.pos.y);
    }
    for (ImDrawCmd& command : list->CmdBuffer) {
        const ImVec2 low  = placed(command.ClipRect.x, command.ClipRect.y);
        const ImVec2 high = placed(command.ClipRect.z, command.ClipRect.w);
        command.ClipRect  = ImVec4{std::max(low.x, box.x), std::max(low.y, box.y), std::min(high.x, box.x + box.width), std::min(high.y, box.y + box.height)};
    }
}

static void drawPhoneCodeInNotes(Viewer& viewer, bool firstView) {
    if (!viewer.notes.visible || !firstView || viewer.presenter) {
        return;
    }
    const std::string address = phoneLink();
    if (address.empty()) {
        return;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          side     = viewport.Size.y * 0.24f; // inside the panel at the foot of the screen
    const PlacedQrCode&  code     = viewer.qrCodes.get(address, side);
    if (code.texture.id == 0) {
        return;
    }
    const float  right = viewer.notes.buttonsLeft > 0.0f ? viewer.notes.buttonsLeft : viewport.Pos.x + viewport.Size.x - 16.0f;
    const ImVec2 corner{right - side - 8.0f, viewport.Pos.y + viewport.Size.y - side - 24.0f};
    ImDrawList*  canvas = ImGui::GetForegroundDrawList();
    canvas->AddRectFilled(ImVec2{corner.x - 8.0f, corner.y - 8.0f}, ImVec2{corner.x + side + 8.0f, corner.y + side + 8.0f}, IM_COL32(255, 255, 255, 255), 4.0f);
    canvas->AddImage(code.texture.id, corner, ImVec2{corner.x + side, corner.y + side});
    // the address it holds, so a mistyped host is seen before the phone is pointed at it
    canvas->AddText(ImVec2{corner.x, corner.y - ImGui::GetFontSize() - 10.0f}, dimmed(viewer.theme.text, 0.8f), "phone presenter view");
    viewer.phoneCodeShown = true;
}

void drawNotes(Viewer& viewer) {
    viewer.phoneCodeShown      = false;
    const SectionVisual visual = visualFor(viewer, viewer.navigator.cursor);
    const std::string   notes  = visual.section == nullptr ? std::string{} : notesOf(visual.section->document);

    const auto& views = viewer.navigator.graph.views;
    const auto  here  = std::ranges::find_if(views, [&viewer](const View& view) { return view.id == viewer.navigator.cursor.viewId; });

    const std::string where = here == views.end() ? std::string{} : std::format("{} / {}", static_cast<std::size_t>(here - views.begin()) + 1UZ, views.size());
    // the id, which is what the menu shows too: a view is known by its anchor throughout
    const std::string next = here == views.end() || here + 1 == views.end() ? std::string{} : (here + 1)->id;

    std::string status = where;
    if (viewer.presenter) {
        const auto        elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - viewer.openedAt).count();
        const std::time_t now     = std::time(nullptr);
        std::tm           local{};
        localtime_r(&now, &local);
        if (here != views.end() && here->stepCount > 1UZ) {
            status += std::format("  \u00b7  step {}/{}", viewer.navigator.cursor.step + 1UZ, here->stepCount);
        }
        status += std::format("  \u00b7  {}:{:02} elapsed  \u00b7  {:02}:{:02}", elapsed / 60, elapsed % 60, local.tm_hour, local.tm_min);
    }
    const ImVec2 screen              = ImGui::GetMainViewport()->Size;
    viewer.notes.compact             = viewer.presenter && std::min(screen.x, screen.y) < kCompactPresenterSide;
    const NotesOverlay::Action asked = viewer.notes.draw(viewer.theme, notes, status, next);
    // One tap on a phone can arrive as a touch and again as the mouse events the browser makes of it, and moved the
    // deck two steps; the same button pressed again this soon is the same press.
    constexpr double kSamePress = 0.3; // seconds
    const double     now        = ImGui::GetTime();
    if (asked != NotesOverlay::Action::none && (asked != viewer.lastNotesAction || now - viewer.lastNotesPress >= kSamePress)) {
        viewer.lastNotesPress  = now;
        viewer.lastNotesAction = asked;
        const Cursor before    = viewer.navigator.cursor;
        if (asked == NotesOverlay::Action::nextStep) {
            viewer.navigator.next();
        } else if (asked == NotesOverlay::Action::nextSlide) {
            viewer.navigator.nextView();
        } else {
            viewer.navigator.previous();
        }
        settleCursor(viewer, before);
    }
    if (viewer.notes.compact && viewer.notes.visible) { // the phone remote: the slide now shown, and what the next press shows
        drawMiniature(viewer, viewer.navigator.cursor, viewer.notes.miniatures[0], "##miniature-now");
        drawMiniature(viewer, viewer.navigator.nextCursor(), viewer.notes.miniatures[1], "##miniature-next");
    }
    drawPhoneCodeInNotes(viewer, here == views.begin());
}

/// the words a section's own heading shows, which is what a presenter looks for in a list of slides
[[nodiscard]] std::string titleOf(const Viewer& viewer, std::string_view viewId) {
    const auto section = std::ranges::find_if(viewer.sections, [viewId](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == viewId; });
    if (section == viewer.sections.end()) {
        return std::string{viewId};
    }
    const auto heading = std::ranges::find_if(section->document.blocks, [](const Block& block) { return block.kind == BlockKind::heading; });
    if (heading == section->document.blocks.end()) {
        return std::string{viewId};
    }
    std::string title;
    for (const InlineSpan& span : heading->spans | std::views::take_while([](const InlineSpan& candidate) { return candidate.kind != InlineKind::lineBreak; })) {
        title += span.text; // the title without its sub-title, which is what a slide is known by
    }
    return title.empty() ? std::string{viewId} : title;
}

void buildSideMenu(Viewer& viewer) {
    viewer.menu.items.clear();
    for (const View& candidate : viewer.navigator.graph.views) {
        viewer.menu.items.push_back({.label = titleOf(viewer, candidate.id), // cut to the width it is drawn at
            .activate =
                [&viewer, id = candidate.id] {
                    const Cursor before = viewer.navigator.cursor;
                    viewer.navigator.jumpTo(id);
                    settleCursor(viewer, before);
                },
            .separatorBefore = false,
            .mnemonic        = {}});
    }
    // What the menu can do, rather than where it can go: kept at the foot of the menu and out of the scrolling
    // list, because a deck of this length put `speaker notes` below the fold of it.
    viewer.menu.utilities.clear();
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kChevronLeft} + "  previous",
        .activate =
            [&viewer] {
                const Cursor before = viewer.navigator.cursor;
                viewer.navigator.previous();
                settleCursor(viewer, before);
            },
        .separatorBefore = false,
        .mnemonic        = {}});
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kChevronRight} + "  next",
        .activate =
            [&viewer] {
                const Cursor before = viewer.navigator.cursor;
                viewer.navigator.next();
                settleCursor(viewer, before);
            },
        .separatorBefore = false,
        .mnemonic        = {}});
    viewer.menu.utilities.push_back({.label = viewer.diagnostics.empty() ? std::string{"no problems"} : std::string{Fonts::kWarning} + std::format("  problems ({})", viewer.diagnostics.entries.size()), .activate = [&viewer] { viewer.diagnosticsPanel.open = !viewer.diagnosticsPanel.open; }, .separatorBefore = false, .mnemonic = {}});
    // the presenter's notes, for a presenter holding a tablet and no keyboard. The key has always done this; the
    // entry gives the same thing to somebody who has no key to press, and names the key for somebody who has.
#ifdef __EMSCRIPTEN__
    if (!viewer.presenter) {
        viewer.menu.utilities.push_back({.label = std::string{"presenter window"}, .activate = [] { openPresenterWindow(); }, .separatorBefore = false, .mnemonic = "S"});
    }
#endif
    viewer.menu.utilities.push_back({.label = std::string{"speaker notes"}, .activate = [&viewer] { viewer.notes.visible = !viewer.notes.visible; }, .separatorBefore = false, .mnemonic = "N"});
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kExpand} + "  toggle full screen", .activate = [&viewer] { viewer.windowMode.toggle(); }, .separatorBefore = false, .mnemonic = {}});
#ifdef GR4_PRESENT_HAS_EXPORT
    for (const std::string_view mode : {std::string_view{"slides"}, std::string_view{"steps"}}) {
        const std::string label = mode == "slides" ? "PDF, slides" : "PDF, every step";
#ifdef __EMSCRIPTEN__
        // a tab of its own walks the deck, so this one stays where the presenter left it
        viewer.menu.utilities.push_back({.label = label, .activate = [mode] { SDL_OpenURL(std::format("?export={}", mode).c_str()); }, .separatorBefore = mode == "slides", .mnemonic = {}});
#else
        viewer.menu.utilities.push_back({.label = label, .activate = [&viewer, mode] { startExport(viewer, mode, false); }, .separatorBefore = mode == "slides", .mnemonic = {}});
#endif
    }
#endif
}

} // namespace gr::present
