#include "LoadingScreen.hpp"

#include "ImGuiScoped.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

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

std::string_view LoadingScreen::statusText() const noexcept { return statusOverride ? std::string_view{*statusOverride} : defaultStatus(activeStage()); }

void LoadingScreen::draw(ImTextureID logo, ImVec2 logoSize, const Theme& theme) const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         launchWindow("##launch", nullptr, flags);

    ImDrawList* canvas = ImGui::GetWindowDrawList();

    const float  blockHeight = logoSize.y + kLogoToBars + static_cast<float>(kLoadStageCount) * kBarHeight + static_cast<float>(kLoadStageCount - 1UZ) * kBarSpacing + kBarsToStatus + ImGui::GetTextLineHeight();
    const ImVec2 centre{viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f};
    float        cursorY = centre.y - blockHeight * 0.5f;

    const ImVec2 logoMin{centre.x - logoSize.x * 0.5f, cursorY};
    canvas->AddImage(logo, logoMin, ImVec2{logoMin.x + logoSize.x, logoMin.y + logoSize.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, theme.logoTint);
    cursorY += logoSize.y + kLogoToBars;

    const float barLeft  = centre.x - kBarWidth * 0.5f;
    const float rounding = kBarHeight * 0.5f;
    for (const float fraction : progress) {
        const ImVec2 barMin{barLeft, cursorY};
        const ImVec2 barMax{barLeft + kBarWidth, cursorY + kBarHeight};
        canvas->AddRectFilled(barMin, barMax, theme.track, rounding);
        if (fraction > 0.0f) {
            canvas->AddRectFilled(barMin, ImVec2{barLeft + kBarWidth * fraction, barMax.y}, theme.fill, rounding);
        }
        cursorY += kBarHeight + kBarSpacing;
    }
    cursorY += kBarsToStatus - kBarSpacing;

    const std::string_view status = statusText();
    const ImVec2           extent = ImGui::CalcTextSize(status.data(), status.data() + status.size());
    canvas->AddText(ImVec2{centre.x - extent.x * 0.5f, cursorY}, theme.text, status.data(), status.data() + status.size());
}

} // namespace gr::present
