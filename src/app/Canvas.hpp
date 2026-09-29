#ifndef GR4_PRESENT_CANVAS_HPP
#define GR4_PRESENT_CANVAS_HPP

#include "TextRuns.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <imgui.h>

#include <span>
#include <string_view>

namespace gr::present {

[[nodiscard]] inline ImVec2 lowCorner(const Rectangle& area) noexcept { return ImVec2{area.x, area.y}; }
[[nodiscard]] inline ImVec2 highCorner(const Rectangle& area) noexcept { return ImVec2{area.x + area.width, area.y + area.height}; }

/// the draw list a slide is drawn into; while a page is exported, the recording receives the same primitives
/// (text as text, pictures by what they show)
struct Canvas {
    ImDrawList*    list      = nullptr;
    PageRecording* recording = nullptr;
    TextRuns*      runs      = nullptr; // selectable text: words, and a formula as its LaTeX

    explicit operator bool() const noexcept { return list != nullptr; }

    void text(ImFont* font, float size, ImVec2 at, ImU32 colour, std::string_view text) const;
    void line(ImVec2 from, ImVec2 to, ImU32 colour, float thickness = 1.0f) const;
    void polyline(std::span<const ImVec2> points, ImU32 colour, ImDrawFlags flags, float thickness) const;
    void rect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding = 0.0f, ImDrawFlags flags = 0, float thickness = 1.0f) const;
    void filledRect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding = 0.0f) const;
    void filledTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 colour) const;
    void filledCircle(ImVec2 centre, float radius, ImU32 colour) const;
    /// `source` says what the texture shows, which a page draws in its place
    void image(ImTextureID texture, ImVec2 low, ImVec2 high, ImVec2 uvLow, ImVec2 uvHigh, ImU32 tint, RecordedImageKind kind, std::string_view source, float size = 0.0f, bool display = false, std::span<const std::string> hidden = {}) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_CANVAS_HPP
