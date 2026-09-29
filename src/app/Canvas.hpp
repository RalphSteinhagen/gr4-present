#ifndef GR4_PRESENT_CANVAS_HPP
#define GR4_PRESENT_CANVAS_HPP

#include "TextRuns.hpp"

#include <gr4-present/export/PageRecording.hpp>

#include <imgui.h>

#include <span>
#include <string_view>

namespace gr::present {

/**
 * The draw list a slide is drawn into, and, while a page is being exported, the recording that is handed the same
 * primitives: text as text and pictures by what they show. Without a recording it draws exactly what the draw list
 * would have drawn.
 */
struct Canvas {
    ImDrawList*    list      = nullptr;
    PageRecording* recording = nullptr;
    TextRuns*      runs      = nullptr; // what a reader can select: the words, and a formula as its LaTeX

    explicit operator bool() const noexcept { return list != nullptr; }

    void text(ImFont* font, float size, ImVec2 at, ImU32 colour, std::string_view text) const;
    void line(ImVec2 from, ImVec2 to, ImU32 colour, float thickness = 1.0f) const;
    void polyline(std::span<const ImVec2> points, ImU32 colour, ImDrawFlags flags, float thickness) const;
    void rect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding = 0.0f, ImDrawFlags flags = 0, float thickness = 1.0f) const;
    void filledRect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding = 0.0f) const;
    void filledTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 colour) const;
    void filledCircle(ImVec2 centre, float radius, ImU32 colour) const;
    /// `source` says what the texture shows, which is what a page draws in its place
    void image(ImTextureID texture, ImVec2 low, ImVec2 high, ImVec2 uvLow, ImVec2 uvHigh, ImU32 tint, RecordedImageKind kind, std::string_view source, float size = 0.0f, bool display = false, std::span<const std::string> hidden = {}) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_CANVAS_HPP
