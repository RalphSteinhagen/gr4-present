#include "SideMenu.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

#include <algorithm>

namespace gr::present {

namespace {
constexpr float kWidthFraction   = 0.16f; // of the viewport width
constexpr float kProximityMargin = 0.06f; // pointer distance from the left edge that opens the menu
constexpr float kRevealPerSecond = 6.0f;
constexpr float kItemPadding     = 0.012f;
} // namespace

void SideMenu::draw(const Theme& theme) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float          width    = viewport->Size.x * kWidthFraction;
    const float          margin   = viewport->Size.x * kProximityMargin;

    const ImVec2 pointer = ImGui::GetIO().MousePos;
    const bool   nearby  = pointer.x >= viewport->Pos.x && pointer.x < viewport->Pos.x + std::max(margin, width * revealed) && pointer.y >= viewport->Pos.y && pointer.y < viewport->Pos.y + viewport->Size.y;

    const float target = nearby ? 1.0f : 0.0f;
    revealed           = std::clamp(revealed + (target - revealed) * std::min(1.0f, ImGui::GetIO().DeltaTime * kRevealPerSecond), 0.0f, 1.0f);
    if (revealed <= 0.001f) {
        return;
    }

    const float padding = viewport->Size.y * kItemPadding;
    ImGui::SetNextWindowPos(ImVec2{viewport->Pos.x - width * (1.0f - revealed), viewport->Pos.y});
    ImGui::SetNextWindowSize(ImVec2{width, viewport->Size.y});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, revealed);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{padding, padding});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.menuBackground);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    {
        const ScopedWindow menu("##side-menu", nullptr, flags);
        const ScopedFont   body(Fonts::bodySize(viewport->Size.y));
        for (const Item& item : items) {
            if (item.separatorBefore) {
                ImGui::Dummy(ImVec2{0.0f, padding});
                ImGui::Separator();
                ImGui::Dummy(ImVec2{0.0f, padding});
            }
            if (ImGui::Selectable(item.label.c_str())) {
                item.activate();
            }
        }
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace gr::present
