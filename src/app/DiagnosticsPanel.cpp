#include "DiagnosticsPanel.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

#include <format>
#include <string>

namespace gr::present {

namespace {
constexpr float kWidthFraction  = 0.6f; // of the viewport
constexpr float kHeightFraction = 0.5f;
constexpr float kPadding        = 0.8f; // of a line height
} // namespace

void DiagnosticsPanel::draw(const Diagnostics& diagnostics, const Theme& theme) const {
    if (!open) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2         size{viewport->Size.x * kWidthFraction, viewport->Size.y * kHeightFraction};
    const ImVec2         origin{viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f, viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f};

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         panel("##diagnostics", nullptr, flags);

    ImDrawList* canvas = ImGui::GetWindowDrawList();
    canvas->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, theme.menuBackground, 6.0f);
    canvas->AddRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, theme.track, 6.0f);

    const ScopedFont body(Fonts::statusSize(viewport->Size.y));
    const float      line = ImGui::GetTextLineHeight();
    float            y    = origin.y + line * kPadding;
    const float      x    = origin.x + line * kPadding;

    const std::string heading = diagnostics.empty() ? std::string{"no problems found"} : std::format("{} problem{} found", diagnostics.entries.size(), diagnostics.entries.size() == 1UZ ? "" : "s");
    canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, theme.text, heading.c_str());
    y += line * 1.8f;

    for (const Diagnostic& entry : diagnostics.entries) {
        if (y + line > origin.y + size.y - line * kPadding) {
            canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, theme.track, "...");
            break;
        }
        // a fatal problem is why nothing is showing, so it is not dimmed to the same level as a missing figure
        const std::string text = entry.message();
        canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, entry.fatal ? theme.fill : theme.text, text.c_str());
        y += line * 1.3f;
    }
}

} // namespace gr::present
