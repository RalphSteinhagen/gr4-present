#include "LoadingScreen.hpp"

#include "ScopedWindow.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

#include <imgui_internal.h>

namespace gr::present {

namespace {
constexpr float kBarWidth     = 260.0f;
constexpr float kBarHeight    = 3.0f;
constexpr float kBarSpacing   = 11.0f;
constexpr float kLogoToBars   = 48.0f;
constexpr float kBarsToStatus = 26.0f;
} // namespace

std::string_view defaultStatus(LoadStage stage) noexcept {
    switch (stage) {
    case LoadStage::application: return "loading application";
    case LoadStage::content: return "loading content";
    case LoadStage::initialisation: return "initialising";
    }
    return {};
}

void LoadingScreen::setProgress(LoadStage stage, float fraction) noexcept { progress[std::to_underlying(stage)] = std::clamp(fraction, 0.0f, 1.0f); }

bool LoadingScreen::finished() const noexcept {
    return std::ranges::all_of(progress, [](float fraction) { return fraction >= 1.0f; });
}

LoadStage LoadingScreen::activeStage() const noexcept {
    const auto pending = std::ranges::find_if(progress, [](float fraction) { return fraction < 1.0f; });
    return pending == progress.cend() ? LoadStage::initialisation : static_cast<LoadStage>(std::ranges::distance(progress.cbegin(), pending));
}

void LoadingScreen::draw(ImTextureID logo, ImVec2 logoSize, const Theme& theme, float opacity) const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         launchWindow("##launch", nullptr, kWindowFlags);

    ImDrawList* canvas = ImGui::GetWindowDrawList();
    if (opacity < 1.0f) { // over the deck: on top of its window, under the menu, which brings itself to the front
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImVec4 background = theme.backgroundColour();
        background.w *= opacity;
        const ImVec2 corner{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
        canvas->AddRectFilled(viewport->Pos, corner, ImGui::ColorConvertFloat4ToU32(background));
    }

    const float  blockHeight = logoSize.y + kLogoToBars + static_cast<float>(kLoadStageCount) * kBarHeight + static_cast<float>(kLoadStageCount - 1UZ) * kBarSpacing + kBarsToStatus + ImGui::GetTextLineHeight();
    const ImVec2 centre{viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f};
    float        cursorY = centre.y - blockHeight * 0.5f;

    const ImVec2 logoMin{centre.x - logoSize.x * 0.5f, cursorY};
    const ImVec2 logoMax{logoMin.x + logoSize.x, logoMin.y + logoSize.y};
    canvas->AddImage(logo, logoMin, logoMax, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(theme.logoTint, opacity));
    cursorY += logoSize.y + kLogoToBars;

    const float barLeft  = centre.x - kBarWidth * 0.5f;
    const float rounding = kBarHeight * 0.5f;
    for (const float fraction : progress) {
        const ImVec2 barMin{barLeft, cursorY};
        const ImVec2 barMax{barLeft + kBarWidth, cursorY + kBarHeight};
        canvas->AddRectFilled(barMin, barMax, dimmed(theme.track, opacity), rounding);
        if (fraction > 0.0f) {
            canvas->AddRectFilled(barMin, ImVec2{barLeft + kBarWidth * fraction, barMax.y}, dimmed(theme.fill, opacity), rounding);
        }
        cursorY += kBarHeight + kBarSpacing;
    }
    cursorY += kBarsToStatus - kBarSpacing;

    const std::string_view status = defaultStatus(activeStage());
    const ImVec2           extent = ImGui::CalcTextSize(status.data(), status.data() + status.size());
    canvas->AddText(ImVec2{centre.x - extent.x * 0.5f, cursorY}, dimmed(theme.text, opacity), status.data(), status.data() + status.size());
}

} // namespace gr::present
