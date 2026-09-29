#include "Canvas.hpp"

#include "Fonts.hpp"

#include <imgui_internal.h> // ImTextCharFromUtf8

#include <initializer_list>
#include <string>
#include <utility>

namespace gr::present {

namespace {

[[nodiscard]] Rectangle clipOf(const ImDrawList& list) {
    const ImVec2 low  = list.GetClipRectMin();
    const ImVec2 high = list.GetClipRectMax();
    return Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
}

[[nodiscard]] RecordedFace faceOf(const ImFont* font) {
    const Fonts& fonts = Fonts::instance();
    if (font == fonts.bold) {
        return RecordedFace::bold;
    }
    if (font == fonts.italic) {
        return RecordedFace::italic;
    }
    if (font == fonts.boldItalic) {
        return RecordedFace::boldItalic;
    }
    if (font == fonts.mono) {
        return RecordedFace::mono;
    }
    if (font == fonts.icons) {
        return RecordedFace::icons;
    }
    return font == fonts.face || font == nullptr ? RecordedFace::body : RecordedFace::other;
}

[[nodiscard]] RecordedFace recordedFaceOf(BuiltinFace builtin) noexcept {
    switch (builtin) {
    case BuiltinFace::body: return RecordedFace::body;
    case BuiltinFace::bold: return RecordedFace::bold;
    case BuiltinFace::italic: return RecordedFace::italic;
    case BuiltinFace::boldItalic: return RecordedFace::boldItalic;
    case BuiltinFace::mono: return RecordedFace::mono;
    }
    return RecordedFace::body;
}

/**
 * A run in a deck's face, recorded as ImGui drew it: in parts, wherever a glyph came from another source than the
 * face itself -- the built-in face merged behind it, or the icons -- so the PDF sets each part in a face that has it.
 */
void recordDeckRun(PageRecording& recording, const Fonts::DeckFace& deckFace, float size, ImVec2 at, ImU32 colour, std::string_view text, const Rectangle& clip) {
    ImFontBaked* const baked      = deckFace.font->GetFontBaked(size);
    const char*        partStart  = text.data();
    unsigned int       partSource = 0U;
    const char* const  end        = text.data() + text.size();
    const auto         flush      = [&](const char* partEnd) {
        if (partEnd == partStart) {
            return;
        }
        const float  offset = deckFace.font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), partStart).x;
        RecordedText part{.text = std::string{partStart, partEnd}, .face = partSource == 2U ? RecordedFace::icons : recordedFaceOf(deckFace.fallback), .size = size, .x = at.x + offset, .y = at.y, .colour = colour, .clip = clip, .deckFace = partSource == 0U ? deckFace.name : std::string{}};
        recording.primitives.emplace_back(std::move(part));
        partStart = partEnd;
    };
    for (const char* at8 = text.data(); at8 < end;) {
        unsigned int       codepoint = 0U;
        const int          length    = ImTextCharFromUtf8(&codepoint, at8, end);
        const ImFontGlyph* glyph     = baked->FindGlyph(static_cast<ImWchar>(codepoint));
        const unsigned int source    = glyph == nullptr ? 0U : glyph->SourceIdx;
        if (source != partSource) {
            flush(at8);
            partSource = source;
        }
        at8 += length;
    }
    flush(end);
}

[[nodiscard]] std::vector<float> pointsOf(std::initializer_list<ImVec2> corners) {
    std::vector<float> points;
    points.reserve(corners.size() * 2UZ);
    for (const ImVec2 corner : corners) {
        points.push_back(corner.x);
        points.push_back(corner.y);
    }
    return points;
}

} // namespace

void Canvas::text(ImFont* font, float size, ImVec2 at, ImU32 colour, std::string_view text) const {
    list->AddText(font, size, at, colour, text.data(), text.data() + text.size());
    Fonts::instance().noteFallbacks(font, size, text);
    if (runs != nullptr && !text.empty()) {
        const float wide = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
        runs->add(text, Rectangle{.x = at.x, .y = at.y, .width = wide, .height = size});
    }
    if (recording == nullptr) {
        return;
    }
    if (const Fonts::DeckFace* deckFace = Fonts::instance().deckFace(font); deckFace != nullptr) {
        recordDeckRun(*recording, *deckFace, size, at, colour, text, clipOf(*list));
        return;
    }
    recording->primitives.emplace_back(RecordedText{.text = std::string{text}, .face = faceOf(font), .size = size, .x = at.x, .y = at.y, .colour = colour, .clip = clipOf(*list)});
}

void Canvas::line(ImVec2 from, ImVec2 to, ImU32 colour, float thickness) const {
    list->AddLine(from, to, colour, thickness);
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::line, .points = pointsOf({from, to}), .colour = colour, .thickness = thickness, .clip = clipOf(*list)});
    }
}

void Canvas::polyline(std::span<const ImVec2> points, ImU32 colour, ImDrawFlags flags, float thickness) const {
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, flags, thickness);
    if (recording != nullptr) {
        RecordedShape shape{.kind = (flags & ImDrawFlags_Closed) != 0 ? RecordedShapeKind::closedPolyline : RecordedShapeKind::polyline, .points = {}, .colour = colour, .thickness = thickness, .clip = clipOf(*list)};
        for (const ImVec2 point : points) {
            shape.points.push_back(point.x);
            shape.points.push_back(point.y);
        }
        recording->primitives.emplace_back(std::move(shape));
    }
}

void Canvas::rect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding, ImDrawFlags flags, float thickness) const {
    list->AddRect(low, high, colour, rounding, flags, thickness);
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::rect, .points = pointsOf({low, high}), .colour = colour, .thickness = thickness, .rounding = rounding, .clip = clipOf(*list)});
    }
}

void Canvas::filledRect(ImVec2 low, ImVec2 high, ImU32 colour, float rounding) const {
    list->AddRectFilled(low, high, colour, rounding);
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::filledRect, .points = pointsOf({low, high}), .colour = colour, .thickness = 0.0f, .rounding = rounding, .clip = clipOf(*list)});
    }
}

void Canvas::filledTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 colour) const {
    list->AddTriangleFilled(a, b, c, colour);
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::filledTriangle, .points = pointsOf({a, b, c}), .colour = colour, .thickness = 0.0f, .clip = clipOf(*list)});
    }
}

void Canvas::filledCircle(ImVec2 centre, float radius, ImU32 colour) const {
    list->AddCircleFilled(centre, radius, colour);
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedShape{.kind = RecordedShapeKind::filledCircle, .points = pointsOf({centre, ImVec2{centre.x + radius, centre.y}}), .colour = colour, .thickness = 0.0f, .clip = clipOf(*list)});
    }
}

void Canvas::image(ImTextureID texture, ImVec2 low, ImVec2 high, ImVec2 uvLow, ImVec2 uvHigh, ImU32 tint, RecordedImageKind kind, std::string_view source, float size, bool display, std::span<const std::string> hidden) const {
    list->AddImage(texture, low, high, uvLow, uvHigh, tint);
    // a drawing's labels are where it put them, through whatever part of it the camera shows
    if (runs != nullptr && runs->labelsOf && (kind == RecordedImageKind::figure || kind == RecordedImageKind::master) && uvHigh.x > uvLow.x && uvHigh.y > uvLow.y) {
        const ImVec2 scale{(high.x - low.x) / (uvHigh.x - uvLow.x), (high.y - low.y) / (uvHigh.y - uvLow.y)};
        for (const DrawnLabel& label : runs->labelsOf(source, hidden)) {
            if (label.area.x < uvLow.x || label.area.y < uvLow.y || label.area.x + label.area.width > uvHigh.x || label.area.y + label.area.height > uvHigh.y) {
                continue; // outside the part on screen, so not a word anyone can see to select
            }
            runs->breakWith("\n");
            runs->add(label.text, Rectangle{.x = low.x + (label.area.x - uvLow.x) * scale.x, .y = low.y + (label.area.y - uvLow.y) * scale.y, .width = label.area.width * scale.x, .height = label.area.height * scale.y});
        }
        runs->breakWith("\n");
    }
    if (runs != nullptr && kind == RecordedImageKind::formula) {
        const std::string_view fence = display ? "$$" : "$";
        runs->add(std::string{fence} + std::string{source} + std::string{fence}, Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y});
    }
    if (recording != nullptr) {
        recording->primitives.emplace_back(RecordedImage{.kind = kind, .source = std::string{source}, .at = Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y}, .uv = Rectangle{.x = uvLow.x, .y = uvLow.y, .width = uvHigh.x - uvLow.x, .height = uvHigh.y - uvLow.y}, .tint = tint, .clip = clipOf(*list), .size = size, .display = display, .hidden = {hidden.begin(), hidden.end()}});
    }
}

} // namespace gr::present
