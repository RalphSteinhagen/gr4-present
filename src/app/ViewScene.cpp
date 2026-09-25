#include "ViewScene.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

namespace gr::present {

namespace {
constexpr float kArtworkWidthFraction = 0.5f; // of the viewport
constexpr float kArtworkToSubtitle    = 0.04f;
} // namespace

void ViewScene::draw(const Texture& artwork, const Theme& theme) const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         scene("##view", nullptr, flags);

    ImDrawList*  canvas      = ImGui::GetWindowDrawList();
    const ImVec2 artworkSize = artwork.id == 0 ? ImVec2{0.0f, 0.0f} : artwork.scaledToWidth(viewport->Size.x * kArtworkWidthFraction);

    const ScopedFont title(Fonts::titleSize(viewport->Size.y));
    const float      gap      = viewport->Size.y * kArtworkToSubtitle;
    const ImVec2     extent   = ImGui::CalcTextSize(heading.c_str());
    const float      blockTop = viewport->Pos.y + (viewport->Size.y - (artworkSize.y + gap + extent.y)) * 0.5f;
    const float      centreX  = viewport->Pos.x + viewport->Size.x * 0.5f;

    if (artwork.id != 0) {
        const ImVec2 artworkMin{centreX - artworkSize.x * 0.5f, blockTop};
        canvas->AddImage(artwork.id, artworkMin, ImVec2{artworkMin.x + artworkSize.x, artworkMin.y + artworkSize.y});
    }
    canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{centreX - extent.x * 0.5f, blockTop + artworkSize.y + gap}, theme.text, heading.c_str());

    if (!hint.empty()) {
        const ScopedFont status(Fonts::statusSize(viewport->Size.y));
        const ImVec2     hintExtent = ImGui::CalcTextSize(hint.c_str());
        canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{centreX - hintExtent.x * 0.5f, blockTop + artworkSize.y + gap + extent.y + gap * 0.5f}, theme.track, hint.c_str());
    }
}

} // namespace gr::present
