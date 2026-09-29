#include "DiagnosticsPanel.hpp"

#include "Fonts.hpp"
#include "ScopedWindow.hpp"

#include <format>
#include <string>

namespace gr::present {

namespace {
constexpr float kViewportWidthFraction  = 0.6f;
constexpr float kViewportHeightFraction = 0.5f;
constexpr float kPaddingLineHeights     = 0.8f;
} // namespace

void DiagnosticsPanel::draw(const Diagnostics& diagnostics, const Theme& theme, float interfacePixelsPerPoint) const {
    if (!open) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2         size{viewport->Size.x * kViewportWidthFraction, viewport->Size.y * kViewportHeightFraction};
    const ImVec2         origin{viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f, viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f};

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         panel("##diagnostics", nullptr, kWindowFlags);

    ImDrawList* canvas = ImGui::GetWindowDrawList();
    canvas->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, theme.menuBackground, 6.0f);
    canvas->AddRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, theme.track, 6.0f);

    const ScopedFont body(Fonts::kInterfacePoints * interfacePixelsPerPoint);
    const float      line = ImGui::GetTextLineHeight();
    float            y    = origin.y + line * kPaddingLineHeights;
    const float      x    = origin.x + line * kPaddingLineHeights;

    const std::string heading = diagnostics.empty() ? std::string{"no problems found"} : std::format("{} problem{} found", diagnostics.entries.size(), diagnostics.entries.size() == 1UZ ? "" : "s");
    canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, theme.text, heading.c_str());
    y += line * 1.8f;

    // a problem wraps at the panel's width: its message names the file and what is wrong, and neither may be cut
    const float wrapWidth = size.x - 2.0f * line * kPaddingLineHeights;
    for (const Diagnostic& entry : diagnostics.entries) {
        const std::string text = entry.message();
        const float       tall = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, wrapWidth, text.c_str()).y;
        if (y + tall > origin.y + size.y - line * kPaddingLineHeights) {
            canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, theme.track, "...");
            break;
        }
        canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{x, y}, entry.fatal ? theme.fill : theme.text, text.c_str(), nullptr, wrapWidth);
        y += tall + line * 0.3f;
    }
}

} // namespace gr::present
