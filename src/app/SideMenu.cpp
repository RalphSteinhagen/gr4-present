#include "SideMenu.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <imgui_internal.h>

namespace gr::present {

namespace {
constexpr float kProximityMargin = 0.035f; // pointer distance from the left edge that opens the menu: inside the slide's text margin, so a line's first word can be selected
constexpr float kRevealPerSecond = 6.0f;
constexpr float kItemPadding     = 0.012f;
constexpr float kLineSpacing     = 1.55f; // of the entry's own size, so ten entries can be counted at a glance
constexpr float kSmallestEntry   = 0.62f; // of the body size: below this a list stops being readable
constexpr float kWidthShare      = 0.17f; // of the slide: the centre entry's 15 % and its padding
constexpr float kNameShare       = 0.15f; // of the slide's width: the most a name may take before it is cut

} // namespace

float SideMenu::lensPoints(std::size_t distance) {
    const float turned = std::min(static_cast<float>(distance) / kLensReach, 1.0f) * std::numbers::pi_v<float> * 0.5f;
    return std::max(kLensSmallest, kLensSmallest + (kLensLargest - kLensSmallest) * std::cos(turned)); // cos(pi/2) is not quite 0
}

void SideMenu::draw(const Theme& theme) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    drawnEntries.clear();
    drawnPanel = {};

    // The utility entries are sized so that ten of them would fit the screen, then held at the body size so a short
    // deck does not get a comically large menu.
    const float body   = Fonts::slideBodySize(viewport->Size.x, viewport->Size.y);
    const float wanted = viewport->Size.y / (static_cast<float>(kEntriesInView) * kLineSpacing);
    const float entry  = std::clamp(wanted, body * kSmallestEntry, body);
    const float width  = std::clamp(viewport->Size.x * kWidthShare, entry * 8.0f, viewport->Size.x * 0.85f);
    const float margin = viewport->Size.x * kProximityMargin;

    const ImVec2 pointer = ImGui::GetIO().MousePos;
    const bool   nearby  = pointer.x >= viewport->Pos.x && pointer.x < viewport->Pos.x + std::max(margin, width * revealed) && pointer.y >= viewport->Pos.y && pointer.y < viewport->Pos.y + viewport->Size.y;

    const float target = nearby ? 1.0f : 0.0f;
    revealed           = std::clamp(revealed + (target - revealed) * std::min(1.0f, ImGui::GetIO().DeltaTime * kRevealPerSecond), 0.0f, 1.0f);
    if (revealed <= 0.001f) {
        return;
    }

    const float padding = viewport->Size.y * kItemPadding;
    drawnPanel          = Rectangle{.x = viewport->Pos.x - width * (1.0f - revealed), .y = viewport->Pos.y, .width = width, .height = viewport->Size.y};
    ImGui::SetNextWindowPos(ImVec2{drawnPanel.x, drawnPanel.y});
    ImGui::SetNextWindowSize(ImVec2{width, viewport->Size.y});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, revealed);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padding, padding});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.menuBackground);

    // NoDecoration would hide the scrollbar as well, and a deck with more slides than fit needs to say so: the
    // entries are sized so ten are in view, and a longer list scrolls rather than shrinking past readability.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    {
        const ScopedWindow menu("##side-menu", nullptr, flags);
        // in front of the slide, which is drawn in a window of its own and would otherwise cover the menu's words
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const ScopedFont face(entry);

        const auto row = [&theme, entry](const Item& item) {
            if (item.separatorBefore) {
                ImGui::Separator();
            }
            if (ImGui::Selectable(item.label.c_str())) {
                item.activate();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (!item.mnemonic.empty()) {
                // the key that does the same thing, set against the right edge so the labels still read as a column
                const ImVec2 at = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), entry * 0.8f, ImVec2{ImGui::GetItemRectMax().x - entry * 1.2f, at.y + entry * 0.1f}, dimmed(theme.text, 0.45f), item.mnemonic.c_str());
            }
        };

        // The slides are a cylindrical lens: the one shown sits in the middle at 18 pt, cut to 15 % of the slide's
        // width, and the others shrink with their distance from it to 8 pt, as names on a drum turning away from the
        // reader. The list does not scroll; the lens moves with the slide. The controls keep the foot of the menu.
        const float foot = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(utilities.size()) + padding * 2.0f;
        ImGui::BeginChild("##slides", ImVec2{0.0f, -foot}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 room    = ImGui::GetContentRegionAvail();
        const float  nameCap = std::min(viewport->Size.x * kNameShare, room.x);
        const auto   sizeAt  = [&](std::size_t index) {
            const std::size_t distance = index > current ? index - current : current - index;
            return Fonts::pointsToPixels(lensPoints(distance), viewport->Size.x, viewport->Size.y);
        };
        const auto placeAt = [&](std::size_t index, float top) {
            const float pixels = sizeAt(index);
            ImGui::PushFont(nullptr, pixels);
            ImGui::SetCursorPos(ImVec2{0.0f, top});
            const std::string name = Fonts::fitted(items[index].label, nameCap);
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushStyleColor(ImGuiCol_Text, index == current ? theme.fill : theme.text);
            if (ImGui::Selectable(name.c_str(), index == current, ImGuiSelectableFlags_None, ImVec2{room.x, 0.0f})) {
                items[index].activate();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            const ImVec2 low  = ImGui::GetItemRectMin();
            const ImVec2 high = ImGui::GetItemRectMax();
            drawnEntries.emplace_back(index, Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y});
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::PopFont();
            return pixels * 1.25f; // the line it took
        };
        if (!items.empty()) {
            const std::size_t centre = std::min(current, items.size() - 1UZ);
            const float       middle = room.y * 0.5f - sizeAt(centre) * 0.6f;
            placeAt(centre, middle);
            float below = middle + sizeAt(centre) * 1.25f;
            for (std::size_t index = centre + 1UZ; index < items.size() && below < room.y; ++index) {
                below += placeAt(index, below);
            }
            float above = middle;
            for (std::size_t index = centre; index-- > 0UZ && above > 0.0f;) {
                above -= sizeAt(index) * 1.25f;
                if (above < 0.0f) {
                    break;
                }
                placeAt(index, above);
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::Dummy(ImVec2{0.0f, padding * 0.5f});
        for (const Item& item : utilities) {
            row(item);
        }
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace gr::present
