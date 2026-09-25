#include "FallbackScene.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

#include <format>
#include <string_view>

namespace gr::present {

namespace {
constexpr float kMarkWidthFraction = 0.18f;
constexpr float kGapFraction       = 0.035f;
constexpr float kTextWidthFraction = 0.80f;

[[nodiscard]] float lineHeight(float pixels) { return pixels; }

/// keeps a long address inside the viewport instead of letting it run off both edges
[[nodiscard]] std::string elided(std::string_view text, float available) {
    if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <= available) {
        return std::string{text};
    }
    std::string shortened{text};
    while (shortened.size() > 4UZ && ImGui::CalcTextSize(shortened.c_str()).x > available) {
        shortened.erase(shortened.size() / 2UZ, 1UZ);
    }
    return shortened.insert(shortened.size() / 2UZ, "...");
}

void drawCentred(ImDrawList* canvas, std::string_view text, float centreX, float top, ImU32 colour) {
    const ImVec2 extent = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    canvas->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{centreX - extent.x * 0.5f, top}, colour, text.data(), text.data() + text.size());
}
} // namespace

void FallbackScene::draw(const Texture& mark, const Theme& theme) const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         scene("##fallback", nullptr, flags);

    ImDrawList*  canvas    = ImGui::GetWindowDrawList();
    const ImVec2 markSize  = mark.id == 0 ? ImVec2{0.0f, 0.0f} : mark.scaledToWidth(viewport->Size.x * kMarkWidthFraction);
    const float  gap       = viewport->Size.y * kGapFraction;
    const float  centreX   = viewport->Pos.x + viewport->Size.x * 0.5f;
    const float  available = viewport->Size.x * kTextWidthFraction;

    // the address goes on its own line at body size: a URL at headline size overflows any viewport
    const std::string scanning = untilRetry.count() > 0 ? std::format("scanning ... (attempt {}, next in {}s)", attempt, (untilRetry.count() + 999) / 1000) : std::format("scanning ... (attempt {})", attempt);

    float blockHeight = markSize.y + gap;
    blockHeight += lineHeight(Fonts::titleSize(viewport->Size.y)) + gap * 0.5f;
    blockHeight += lineHeight(Fonts::bodySize(viewport->Size.y)) + gap;
    blockHeight += lineHeight(Fonts::statusSize(viewport->Size.y));
    if (!reason.empty()) {
        blockHeight += lineHeight(Fonts::statusSize(viewport->Size.y)) * 1.4f;
    }

    float cursorY = viewport->Pos.y + (viewport->Size.y - blockHeight) * 0.5f;
    if (mark.id != 0) {
        canvas->AddImage(mark.id, ImVec2{centreX - markSize.x * 0.5f, cursorY}, ImVec2{centreX + markSize.x * 0.5f, cursorY + markSize.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, theme.logoTint);
    }
    cursorY += markSize.y + gap;

    {
        const ScopedFont title(Fonts::titleSize(viewport->Size.y));
        drawCentred(canvas, "no signal", centreX, cursorY, theme.text);
        cursorY += ImGui::GetFontSize() + gap * 0.5f;
    }
    {
        const ScopedFont body(Fonts::bodySize(viewport->Size.y));
        drawCentred(canvas, elided(address, available), centreX, cursorY, theme.track);
        cursorY += ImGui::GetFontSize() + gap;
    }
    {
        const ScopedFont status(Fonts::statusSize(viewport->Size.y));
        drawCentred(canvas, scanning, centreX, cursorY, theme.track);
        if (!reason.empty()) {
            drawCentred(canvas, elided(reason, available), centreX, cursorY + ImGui::GetFontSize() * 1.4f, theme.track);
        }
    }
}

} // namespace gr::present
