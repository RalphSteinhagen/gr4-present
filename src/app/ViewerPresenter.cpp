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

/// the phone presenter view's address as a QR code in the middle of the screen
void drawPhoneLink(Viewer& viewer) {
#ifdef __EMSCRIPTEN__
    if (!viewer.input.showPhoneLink) {
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

/// what the room sees, drawn at the audience's 16:9 with the viewport swapped, then moved and scaled into `box`
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
    drawSection(viewer, visual, at.step, 1.0f, visual.cameraFrame());
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

/// on the first slide's notes only: the phone presenter view's address as a QR code
static void drawPhoneCodeInNotes(Viewer& viewer, bool firstView) {
    if (!viewer.notes.visible || !firstView || viewer.presenter) {
        return;
    }
    const std::string address = phoneLink();
    if (address.empty()) {
        return;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          side     = viewport.Size.y * 0.24f;
    const PlacedQrCode&  code     = viewer.qrCodes.get(address, side);
    if (code.texture.id == 0) {
        return;
    }
    const float  right = viewer.notes.buttonsLeft > 0.0f ? viewer.notes.buttonsLeft : viewport.Pos.x + viewport.Size.x - 16.0f;
    const ImVec2 corner{right - side - 8.0f, viewport.Pos.y + viewport.Size.y - side - 24.0f};
    ImDrawList*  canvas = ImGui::GetForegroundDrawList();
    canvas->AddRectFilled(ImVec2{corner.x - 8.0f, corner.y - 8.0f}, ImVec2{corner.x + side + 8.0f, corner.y + side + 8.0f}, IM_COL32(255, 255, 255, 255), 4.0f);
    canvas->AddImage(code.texture.id, corner, ImVec2{corner.x + side, corner.y + side});
    canvas->AddText(ImVec2{corner.x, corner.y - ImGui::GetFontSize() - 10.0f}, dimmed(viewer.theme.text, 0.8f), "phone presenter view");
    viewer.presenterTools.phoneCodeShown = true;
}

void drawNotes(Viewer& viewer, float interfacePixelsPerPoint) {
    viewer.presenterTools.phoneCodeShown = false;
    const SectionVisual visual           = visualFor(viewer, viewer.navigator.cursor);
    const std::string   notes            = visual.section == nullptr ? std::string{} : notesOf(visual.section->document);

    const auto& views = viewer.navigator.graph.views;
    const auto  here  = std::ranges::find_if(views, [&viewer](const View& view) { return view.id == viewer.navigator.cursor.viewId; });

    const std::string where = here == views.end() ? std::string{} : std::format("{} / {}", static_cast<std::size_t>(here - views.begin()) + 1UZ, views.size());
    // the id, as the menu shows it
    const std::string next = here == views.end() || here + 1 == views.end() ? std::string{} : (here + 1)->id;

    std::string status = where;
    if (viewer.presenter) {
        const auto        elapsed = std::chrono::duration_cast<std::chrono::seconds>(viewer.frameStart - viewer.presenterTools.openedAt).count();
        const std::time_t now     = viewer.frameWallClock;
        std::tm           local{};
        localtime_r(&now, &local);
        if (here != views.end() && here->stepCount > 1UZ) {
            status += std::format("  \u00b7  step {}/{}", viewer.navigator.cursor.step + 1UZ, here->stepCount);
        }
        status += std::format("  \u00b7  {}:{:02} elapsed  \u00b7  {:02}:{:02}", elapsed / 60, elapsed % 60, local.tm_hour, local.tm_min);
    }
    const ImVec2 screen              = ImGui::GetMainViewport()->Size;
    viewer.notes.compact             = viewer.presenter && std::min(screen.x, screen.y) < kCompactPresenterSide;
    const NotesOverlay::Action asked = viewer.notes.draw(viewer.theme, notes, status, next, interfacePixelsPerPoint);
    // one tap on a phone can arrive as a touch and again as the browser's mouse events: the same press
    constexpr double kSamePressSeconds = 0.3;
    const double     now               = ImGui::GetTime();
    if (asked != NotesOverlay::Action::none && (asked != viewer.presenterTools.lastNotesAction || !viewer.presenterTools.lastNotesPress || now - *viewer.presenterTools.lastNotesPress >= kSamePressSeconds)) {
        viewer.presenterTools.lastNotesPress  = now;
        viewer.presenterTools.lastNotesAction = asked;
        const Cursor before                   = viewer.navigator.cursor;
        if (asked == NotesOverlay::Action::nextStep) {
            viewer.navigator.next();
        } else if (asked == NotesOverlay::Action::nextSlide) {
            viewer.navigator.nextView();
        } else {
            viewer.navigator.previous();
        }
        settleCursor(viewer, before);
    }
    if (viewer.notes.compact && viewer.notes.visible) {
        drawMiniature(viewer, viewer.navigator.cursor, viewer.notes.miniatures[0], "##miniature-now");
        drawMiniature(viewer, viewer.navigator.nextCursor(), viewer.notes.miniatures[1], "##miniature-next");
    }
    drawPhoneCodeInNotes(viewer, here == views.begin());
}

/// the words of a section's heading, without its sub-title
[[nodiscard]] std::string titleOf(const Viewer& viewer, std::string_view viewId) {
    const auto section = sectionOf(viewer.sections, viewId);
    if (section == nullptr) {
        return std::string{viewId};
    }
    const auto heading = std::ranges::find_if(section->document.blocks, [](const Block& block) { return block.kind == BlockKind::heading; });
    if (heading == section->document.blocks.end()) {
        return std::string{viewId};
    }
    std::string title;
    for (const InlineSpan& span : heading->spans | std::views::take_while([](const InlineSpan& candidate) { return candidate.kind != InlineKind::lineBreak; })) {
        title += span.text;
    }
    return title.empty() ? std::string{viewId} : title;
}

void buildSideMenu(Viewer& viewer) {
    viewer.menu.items.clear();
    for (const View& candidate : viewer.navigator.graph.views) {
        viewer.menu.items.push_back({.label = titleOf(viewer, candidate.id),
            .activate =
                [&viewer, id = candidate.id] {
                    const Cursor before = viewer.navigator.cursor;
                    viewer.navigator.jumpTo(id);
                    settleCursor(viewer, before);
                },
            .separatorBefore = false,
            .mnemonic        = {}});
    }
    // the deck's files, at the top; the presenter's window follows the audience's deck, so it has none
    viewer.menu.files.clear();
    if (!viewer.presenter) {
        viewer.menu.files.push_back({.label = std::string{Fonts::kFolder} + "  Load", .activate = [&viewer] { chooseDeck(viewer); }, .separatorBefore = false, .mnemonic = "Ctrl+O"});
        viewer.menu.files.push_back({.label = std::string{Fonts::kSave} + "  Save", .activate = [&viewer] { saveDeck(viewer); }, .separatorBefore = false, .mnemonic = "Ctrl+S"});
    }
    const bool followable   = !viewer.presenter && followableDeck(viewer); // the presenter's window follows the audience's
    viewer.menu.followEdits = followable ? &viewer.deckFiles.following : nullptr;

    // what the menu can do, rather than where it can go: kept at its foot, out of the scrolling list
    viewer.menu.utilities.clear();
    const std::string diagnosticsLabel  = viewer.diagnostics.empty() ? std::string{"no problems"} : std::string{Fonts::kWarning} + std::format("  problems ({})", viewer.diagnostics.entries.size());
    const auto        toggleDiagnostics = [&viewer] { viewer.diagnosticsPanel.open = !viewer.diagnosticsPanel.open; };
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kExpand} + "  full screen", .activate = [&viewer] { viewer.windowMode.toggle(); }, .separatorBefore = false, .mnemonic = "F"});
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kPointer} + "  pointer", .activate = [&viewer] { viewer.input.pointing = !viewer.input.pointing; }, .separatorBefore = false, .mnemonic = "L"});
    viewer.menu.utilities.push_back({.label = std::string{Fonts::kNotes} + "  notes", .activate = [&viewer] { viewer.notes.visible = !viewer.notes.visible; }, .separatorBefore = false, .mnemonic = "N"});
#ifdef __EMSCRIPTEN__
    if (!viewer.presenter) {
        viewer.menu.utilities.push_back({.label = std::string{Fonts::kPresenter} + "  speaker", .activate = [] { openPresenterWindow(); }, .separatorBefore = false, .mnemonic = "S"});
    }
#endif
#ifdef GR4_PRESENT_HAS_EXPORT
    for (const std::string_view mode : {std::string_view{"slides"}, std::string_view{"steps"}}) {
        const std::string label = std::string{Fonts::kPdf} + (mode == "slides" ? "  PDF, slides" : "  PDF, every step");
#ifdef __EMSCRIPTEN__
        // a tab of its own walks the deck, so this one stays where the presenter left it
        viewer.menu.utilities.push_back({.label = label, .activate = [mode] { openExportTab(std::string{mode}.c_str()); }, .separatorBefore = mode == "slides", .mnemonic = {}});
#else
        viewer.menu.utilities.push_back({.label = label, .activate = [&viewer, mode] { startExport(viewer, mode, false); }, .separatorBefore = mode == "slides", .mnemonic = {}});
#endif
    }
#endif
    viewer.menu.utilities.push_back({.label = diagnosticsLabel, .activate = toggleDiagnostics, .separatorBefore = true, .mnemonic = {}});
}

} // namespace gr::present
