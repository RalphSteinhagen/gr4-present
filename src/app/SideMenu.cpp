#include "SideMenu.hpp"

#include "Fonts.hpp"
#include "ScopedWindow.hpp"

#include <algorithm>
#include <span>

#include <imgui_internal.h>

namespace gr::present {

namespace {
constexpr float kProximityMarginWidthShare = 0.035f; // inside the slide's text margin, so a line's first word stays selectable
constexpr float kRevealPerSecond           = 6.0f;
constexpr float kItemPadding               = 0.012f;

[[nodiscard]] Rectangle drawnRectangle() {
    const ImVec2 low  = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    return Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
}

} // namespace

SideMenu::ListSizes SideMenu::listSizes(std::size_t entries, float roomPoints) noexcept {
    // n - 1 names at size s and the shown one at the controls' size take ((n - 1) s + 15) lines of type
    const float typePoints    = roomPoints / kLineSpacingEntrySizes;
    const float others        = entries == 0UZ ? 0.0f : static_cast<float>(entries - 1UZ);
    const float fittingPoints = others > 0.0f ? (typePoints - Fonts::kInterfacePoints) / others : kOtherPoints;
    const float otherPoints   = std::min(kOtherPoints, fittingPoints);
    if (otherPoints >= kSmallestOtherPoints) {
        return ListSizes{.otherPoints = otherPoints, .scrolls = false};
    }
    return ListSizes{.otherPoints = kSmallestOtherPoints, .scrolls = true};
}

void SideMenu::draw(const Theme& theme, float interfacePixelsPerPoint) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    drawnEntries.clear();
    drawnFiles.clear();
    drawnFollowEdits = {};
    drawnPanel       = {};

    const float pixelsPerPoint = interfacePixelsPerPoint;
    const float utilityPixels  = Fonts::kInterfacePoints * pixelsPerPoint;
    const float widthShare     = viewport->Size.x < viewport->Size.y ? kPortraitWidthShare : kLandscapeWidthShare;
    const float width          = std::min(kPanelPoints * pixelsPerPoint, viewport->Size.x * widthShare);
    const float margin         = viewport->Size.x * kProximityMarginWidthShare;

    // exactly the window's origin is taken as no pointer: a browser's mouse enter after a key press reports (0, 0)
    const ImGuiIO& io       = ImGui::GetIO();
    const ImVec2   pointer  = io.MousePos;
    const bool     atOrigin = pointer.x == viewport->Pos.x && pointer.y == viewport->Pos.y;
    const bool     nearby   = !atOrigin && pointer.x >= viewport->Pos.x && pointer.x < viewport->Pos.x + std::max(margin, width * revealed) && pointer.y >= viewport->Pos.y && pointer.y < viewport->Pos.y + viewport->Size.y;

    const float target = nearby ? 1.0f : 0.0f;
    revealed           = std::clamp(revealed + (target - revealed) * std::min(1.0f, io.DeltaTime * kRevealPerSecond), 0.0f, 1.0f);
    if (revealed <= 0.001f) {
        return;
    }

    const float padding = viewport->Size.y * kItemPadding;
    drawnPanel          = Rectangle{.x = viewport->Pos.x - width * (1.0f - revealed), .y = viewport->Pos.y, .width = width, .height = viewport->Size.y};
    ImGui::SetNextWindowPos(ImVec2{drawnPanel.x, drawnPanel.y});
    ImGui::SetNextWindowSize(ImVec2{width, viewport->Size.y});
    const ScopedStyleValue fading{ImGuiStyleVar_Alpha, revealed};
    const ScopedStyleVar   padded{ImGuiStyleVar_WindowPadding, ImVec2{padding, padding}};
    const ScopedStyleColor background{ImGuiCol_WindowBg, theme.menuBackground};

    // not NoDecoration, which would also hide the scrollbar a long list needs
    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    {
        const ScopedWindow menu("##side-menu", nullptr, kWindowFlags);
        // the slide is drawn in a window of its own and would otherwise cover the menu
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        const float columnGap = ImGui::GetStyle().ItemSpacing.x;
        // one control, drawn as every entry above and below the slides is: a narrow panel (a phone held upright) cuts the
        // label with an ellipsis, as it does a slide's name; the key sits in a right-aligned column, as legible as the label
        const auto control = [&](const std::string& label, const std::string& key, bool selected) {
            const float keyWidth   = key.empty() ? 0.0f : ImGui::CalcTextSize(key.c_str()).x;
            const float labelWidth = ImGui::GetContentRegionAvail().x - (keyWidth > 0.0f ? keyWidth + columnGap : 0.0f);
            const bool  pressed    = ImGui::Selectable(Fonts::fitted(label, labelWidth).c_str(), selected);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (keyWidth > 0.0f) {
                const ImVec2 keyAt{ImGui::GetItemRectMax().x - keyWidth, ImGui::GetItemRectMin().y};
                ImGui::GetWindowDrawList()->AddText(keyAt, dimmed(theme.text, 0.8f), key.c_str());
            }
            return pressed;
        };
        if (!files.empty()) { // the deck's files: Load, follow edits, then Save
            const ScopedFont face(utilityPixels);
            const auto       file = [&](std::size_t index) {
                const ScopedIndexId perEntry{static_cast<int>(index) + 1000};
                if (control(files[index].label, files[index].mnemonic, false)) {
                    files[index].activate();
                }
                drawnFiles.emplace_back(index, drawnRectangle());
            };
            file(0UZ);
            if (followEdits != nullptr) {
                if (control(std::string{*followEdits ? Fonts::kChecked : Fonts::kUnchecked} + "  follow edits", {}, false)) {
                    *followEdits = !*followEdits;
                }
                drawnFollowEdits = drawnRectangle();
            }
            for (std::size_t index = 1UZ; index < files.size(); ++index) {
                file(index);
            }
            ImGui::Separator();
        }

        // what is drawn below the list: its end, a separator, the air, a separator before each group, then the rows
        const float foot = [&] {
            const ScopedFont  face(utilityPixels);
            const float       spacing    = ImGui::GetStyle().ItemSpacing.y;
            const std::size_t separators = 1UZ + static_cast<std::size_t>(std::ranges::count_if(utilities, &Item::separatorBefore));
            return spacing * 2.0f + padding * 0.5f + spacing * static_cast<float>(separators) + ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(utilities.size());
        }();
        const float            slidesHeight = ImGui::GetContentRegionAvail().y - foot;
        const ListSizes        sizes        = listSizes(items.size(), slidesHeight / pixelsPerPoint);
        const std::size_t      shown        = items.empty() ? 0UZ : std::min(current, items.size() - 1UZ);
        const ImGuiWindowFlags listFlags    = sizes.scrolls ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        {
            const ScopedChild slides{"##slides", ImVec2{0.0f, slidesHeight}, ImGuiChildFlags_None, listFlags};
            // a line is its type and a quarter of the common size, the spacing listSizes counts on
            const ScopedStyleVar spaced{ImGuiStyleVar_ItemSpacing, ImVec2{ImGui::GetStyle().ItemSpacing.x, (kLineSpacingEntrySizes - 1.0f) * sizes.otherPoints * pixelsPerPoint}};
            const float          nameCap = ImGui::GetContentRegionAvail().x;
            if (!sizes.scrolls) { // a short list sits in the middle of the free height
                const float listHeight = items.empty() ? 0.0f : kLineSpacingEntrySizes * (static_cast<float>(items.size() - 1UZ) * sizes.otherPoints + Fonts::kInterfacePoints) * pixelsPerPoint;
                ImGui::SetCursorPosY(std::max(0.0f, (slidesHeight - listHeight) * 0.5f));
            }
            for (std::size_t index = 0UZ; index < items.size(); ++index) {
                const bool             isShown = index == shown;
                const ScopedFont       face(isShown ? utilityPixels : sizes.otherPoints * pixelsPerPoint, isShown ? Fonts::instance().bold : nullptr);
                const std::string      name = Fonts::fitted(items[index].label, nameCap);
                const ScopedIndexId    perEntry{static_cast<int>(index)};
                const ScopedStyleColor marked{ImGuiCol_Text, isShown ? theme.fill : theme.text};
                if (ImGui::Selectable(name.c_str(), isShown)) {
                    items[index].activate();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                if (isShown && sizes.scrolls && scrolledTo != shown) {
                    ImGui::SetScrollHereY(0.5f);
                    scrolledTo = shown;
                }
                drawnEntries.emplace_back(index, drawnRectangle());
            }
        }
        ImGui::Separator();
        ImGui::Dummy(ImVec2{0.0f, padding * 0.5f});

        const ScopedFont face(utilityPixels);
        for (const Item& item : utilities) {
            if (item.separatorBefore) {
                ImGui::Separator();
            }
            if (control(item.label, item.mnemonic, false)) {
                item.activate();
            }
        }
    }
}

} // namespace gr::present
