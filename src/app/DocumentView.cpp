#include "DocumentView.hpp"

#include "Canvas.hpp"
#include "Fonts.hpp"
#include "PlotView.hpp"
#include "ScopedWindow.hpp"
#include "SvgImage.hpp"
#include "Transition.hpp"

#include <gr4-present/EffectSource.hpp>
#include <gr4-present/Number.hpp>
#include <gr4-present/Slice.hpp>

#include <imgui_internal.h> // ImTextCountUtf8BytesFromChar

#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat>
#include <format>
#include <numbers>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <variant>

namespace gr::present {

namespace {

/// a drawing becomes vectors on an exported page; anything else stays the picture it is
[[nodiscard]] RecordedImageKind imageKindOf(std::string_view reference) noexcept { return isSvgReference(reference) ? RecordedImageKind::figure : RecordedImageKind::picture; }

constexpr float kDarkVariantBelowBrightness = 0.5f;
constexpr float kSlideNumberHeightShare     = 0.45f;
constexpr float kUnfollowedLineOpacity      = 0.35f;
constexpr float kColumnMarginsTotalEms      = 6.0f;
constexpr float kColumnMarginMaxWidthShare  = 0.04f;
constexpr float kTitleAirEms                = 1.6f;
constexpr float kAnchorAirShare             = 0.06f;
constexpr float kFooterClearEms             = 2.1f;
constexpr float kFooterSmallestStatusShare  = 0.7f;
constexpr float kRiseEms                    = 0.6f;
constexpr float kSubtitleRatio              = 20.0f / 36.0f; // 20 pt under a 36 pt title
constexpr float kGapBelowTitleLines         = 0.79f;
// captions sit in a band at the foot, not a column that would cover what the camera frames
constexpr float kCaptionMaxHeightShare    = 0.30f;
constexpr float kCaptionMaxLines          = 7.0f;
constexpr float kParagraphGapLines        = 0.55f;
constexpr float kHeadingGapAbove          = 1.1f;
constexpr float kListIndentLines          = 1.6f;
constexpr float kRulePadding              = 0.8f;
constexpr float kRegionAspect             = 0.5625f; // 16:9, height over width
constexpr float kPlaceholderRounding      = 4.0f;
constexpr float kBarLines                 = 1.6f;
constexpr float kCodeFontScale            = 0.86f; // code lines are long and every glyph is full width
constexpr float kCodePaddingLines         = 0.6f;
constexpr float kCodeHangingIndentChars   = 2.0f;
constexpr float kCellFloorShare           = 0.40f;
constexpr float kStripeWashOpacity        = 0.20f;
constexpr float kHeaderWashOpacity        = 0.34f;
constexpr float kInfoBoxBackdropOpacity   = 0.70f;
constexpr float kInfoBoxFrameOpacity      = 0.60f;
constexpr float kInfoBoxFrameWidthPixels  = 1.5f;
constexpr float kInfoBoxPaddingEms        = 0.6f;
constexpr float kInfoBoxGapEms            = 0.8f;
constexpr float kInfoBoxMinWidthShare     = 0.22f;
constexpr float kInfoBoxMaxWidthShare     = 0.34f;
constexpr float kInfoBoxReadableEms       = 9.0f;
constexpr float kInfoBoxWideEms           = 22.0f;
constexpr ImU32 kStripeTint               = IM_COL32(0xC0, 0xC0, 0xC0, 0xFF);
constexpr float kTableColumnGapLines      = 1.0f;
constexpr float kTableRowGapLines         = 0.35f;
constexpr float kFooterTextHeightShare    = 0.75f;
constexpr float kFramePaddingEms          = 0.4f;
constexpr float kStatusBarLines           = 1.6f;
constexpr float kFigureMaxRoomShare       = 0.95f;
constexpr float kPlotMaxRoomShare         = 0.95f;
constexpr float kDisplayFormulaScale      = 1.35f;
constexpr float kFootnoteMarkerScale      = 0.7f;
constexpr float kFootnoteScale            = 0.62f;
constexpr float kReferenceScale           = 0.72f;
constexpr float kReferenceIndentLines     = 1.6f;
constexpr float kQrLines                  = 9.5f; // scannable from the back of a room
constexpr float kQrUrlScale               = 0.75f;
constexpr float kQrShareOfWidth           = 0.20f;
constexpr float kQrAddressSplitCodeWidths = 1.6f;
constexpr float kVideoBarHeightLines      = 0.18f;
constexpr float kVideoButtonSizeLines     = 0.9f;
constexpr float kVideoBarGapLines         = 0.25f;
constexpr float kVideoButtonGapLines      = 0.2f;
constexpr float kVideoFootAirLines        = 0.3f;
constexpr float kVideoControlsHeightLines = kVideoBarGapLines + kVideoBarHeightLines + kVideoButtonGapLines + kVideoButtonSizeLines + kVideoFootAirLines;

/// from the deck's ladder: the slide title is 36 pt, headings in the flow step down from 24 pt towards the body
[[nodiscard]] float headingSize(int level, float bodyPixels) noexcept {
    if (level <= 1) {
        return bodyPixels * Fonts::titleRatio();
    }
    const float heading = bodyPixels * Fonts::headingRatio();
    const float body    = bodyPixels;
    const float t       = std::clamp(static_cast<float>(level - 1) / 3.0f, 0.0f, 1.0f);
    return heading + (body * 1.15f - heading) * t;
}

/// a heading's line and padding in body heights, so a caption's title is never scaled down to fit
[[nodiscard]] float titleHeadingEms() noexcept { return Fonts::titleRatio() * 1.5f + 1.2f; }

/// `textFace` replaces the regular face for plain words (a table header is bold)
[[nodiscard]] ImFont* faceOf(const InlineSpan& span, const FaceSet& faces, ImFont* textFace) {
    const bool    plain  = span.kind == InlineKind::text || span.kind == InlineKind::image;
    const FaceSet chosen = span.font.empty() ? faces : Fonts::instance().faceSet(span.font).value_or(faces);
    return plain && span.font.empty() && textFace != nullptr ? textFace : chosen.forKind(span.kind);
}

/// a span's `size=` in pixels: points at the deck's reference, or a share of `around`
[[nodiscard]] float sizeOf(const InlineSpan& span, float around) {
    const std::optional<TypeSize> said = span.size.empty() ? std::nullopt : parseTypeSize(span.size);
    if (!said) {
        return around;
    }
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    return said->unit == TypeSize::Unit::relative ? around * said->value : Fonts::pointsToPixels(said->value, viewport.x, viewport.y);
}

/// applies a body size the author set; auto-shrink floors at the deck's floor, or at once if already below it
void sizedAsSaid(DocumentView::Placement& where, const std::optional<TypeSize>& size, bool slide = false) {
    if (!size) {
        return;
    }
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    if (slide && where.titlePixels <= 0.0f) {
        where.titlePixels = where.bodyPixels;
    }
    where.bodyPixels        = size->unit == TypeSize::Unit::relative ? where.bodyPixels * size->value : Fonts::pointsToPixels(size->value, viewport.x, viewport.y);
    const float floorPixels = Fonts::pointsToPixels(Fonts::instance().scale.floor, viewport.x, viewport.y);
    where.floorScale        = std::min(1.0f, floorPixels / where.bodyPixels);
}

/// turns vertices [from, end) of `list` by -`degrees` about `centre` (a turning camera turns the scene the other way)
void turnVertices(ImDrawList& list, int from, ImVec2 centre, float degrees) {
    if (degrees == 0.0f) {
        return;
    }
    const float radians = -degrees * std::numbers::pi_v<float> / 180.0f;
    const float cosine  = std::cos(radians);
    const float sine    = std::sin(radians);
    for (int index = from; index < list.VtxBuffer.Size; ++index) {
        ImVec2&     at = list.VtxBuffer[index].pos;
        const float dx = at.x - centre.x;
        const float dy = at.y - centre.y;
        at             = ImVec2{centre.x + dx * cosine - dy * sine, centre.y + dx * sine + dy * cosine};
    }
}

/// the picture just recorded turns as its vertices did, so the page matches the screen
void turnRecorded(PageRecording* recording, ImVec2 pivot, float degrees) {
    if (recording == nullptr || recording->primitives.empty() || degrees == 0.0f) {
        return;
    }
    if (auto* image = std::get_if<RecordedImage>(&recording->primitives.back())) {
        image->turnDegrees = degrees;
        image->pivotX      = pivot.x;
        image->pivotY      = pivot.y;
    }
}

/// what a block is across slides: its `{#id}` when it has one, else its kind and the words it shows
[[nodiscard]] std::string morphKeyOf(const Block& block) {
    if (!block.id.empty()) {
        return "#" + block.id;
    }
    std::string key = std::format("{}:", static_cast<int>(block.kind));
    for (const InlineSpan& span : block.spans) {
        key += span.text;
    }
    for (const std::string& line : block.lines) {
        key += line + "\n";
    }
    for (const InlineSpan& cell : block.cells | std::views::join) {
        key += cell.text + "|";
    }
    return key;
}

/// where a drawing starts in the draw list: its first vertex and a command of its own, so a wipe clips only it
struct DrawnFrom {
    int vertex  = 0;
    int command = 0;
};

[[nodiscard]] DrawnFrom startDrawing(ImDrawList& list) {
    list.AddDrawCmd();
    return DrawnFrom{.vertex = list.VtxBuffer.Size, .command = list.CmdBuffer.Size - 1};
}

/// moves what was drawn since `start` `progress` into place as `kind` says: fade, rise (`lift` px), grow, wipe
void arrive(ImDrawList& list, DrawnFrom start, std::string_view kind, std::string_view from, float progress, float lift, const DocumentView::ThroughEffect& through = {}, std::string_view key = {}) {
    const float eased = smoothstep(progress);
    ImVec2      low{FLT_MAX, FLT_MAX};
    ImVec2      high{-FLT_MAX, -FLT_MAX};
    for (const ImDrawVert& vertex : list.VtxBuffer | std::views::drop(start.vertex)) {
        low  = ImVec2{std::min(low.x, vertex.pos.x), std::min(low.y, vertex.pos.y)};
        high = ImVec2{std::max(high.x, vertex.pos.x), std::max(high.y, vertex.pos.y)};
    }
    if (low.x > high.x) {
        return;
    }
    if (!std::ranges::contains(kRevealNames, kind) && through && through(list, start.command, kind, key, eased, low, high)) {
        return;
    }
    if (kind == "wipe") {
        const ImVec2 size{high.x - low.x, high.y - low.y};
        for (ImDrawCmd& command : list.CmdBuffer | std::views::drop(start.command)) {
            ImVec4& clip = command.ClipRect;
            if (from == "right") {
                clip.x = std::max(clip.x, high.x - eased * size.x);
            } else if (from == "above") {
                clip.w = std::min(clip.w, low.y + eased * size.y);
            } else if (from == "below") {
                clip.y = std::max(clip.y, high.y - eased * size.y);
            } else {
                clip.z = std::min(clip.z, low.x + eased * size.x);
            }
        }
        list.AddDrawCmd(); // later drawing keeps its own clip rectangle
        return;
    }
    const float  away   = (1.0f - eased) * lift;
    const ImVec2 offset = kind != "rise" ? ImVec2{} : (from == "left" ? ImVec2{-away, 0.0f} : from == "right" ? ImVec2{away, 0.0f} : from == "above" ? ImVec2{0.0f, -away} : ImVec2{0.0f, away});
    const float  scale  = kind == "grow" ? 0.9f + 0.1f * eased : 1.0f;
    const ImVec2 centre{(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
    for (ImDrawVert& vertex : list.VtxBuffer | std::views::drop(start.vertex)) {
        vertex.pos = ImVec2{centre.x + (vertex.pos.x - centre.x) * scale + offset.x, centre.y + (vertex.pos.y - centre.y) * scale + offset.y};
        vertex.col = dimmed(vertex.col, eased);
    }
}

/// scales what was drawn since `start` (vertices, clips, export, text selection, links) by `factor` about `origin`
void scaleDrawn(ImDrawList& list, DrawnFrom start, PageRecording* recording, std::size_t recordedFrom, TextRuns& runs, std::size_t runsFrom, std::vector<DocumentView::LinkArea>& links, std::size_t linksFrom, ImVec2 origin, float factor) {
    const auto x      = [&](float value) { return origin.x + (value - origin.x) * factor; };
    const auto y      = [&](float value) { return origin.y + (value - origin.y) * factor; };
    const auto scaled = [&](const Rectangle& area) { return Rectangle{.x = x(area.x), .y = y(area.y), .width = area.width * factor, .height = area.height * factor}; };
    for (ImDrawVert& vertex : list.VtxBuffer | std::views::drop(start.vertex)) {
        vertex.pos = ImVec2{x(vertex.pos.x), y(vertex.pos.y)};
    }
    for (ImDrawCmd& command : list.CmdBuffer | std::views::drop(start.command)) {
        command.ClipRect = ImVec4{x(command.ClipRect.x), y(command.ClipRect.y), x(command.ClipRect.z), y(command.ClipRect.w)};
    }
    if (recording != nullptr) {
        for (RecordedPrimitive& primitive : recording->primitives | std::views::drop(recordedFrom)) {
            std::visit(
                [&](auto& recorded) {
                    using Kind = std::decay_t<decltype(recorded)>;
                    if constexpr (std::is_same_v<Kind, RecordedText>) {
                        const float     scaledSize = recorded.size * factor;
                        const float     scaledX    = x(recorded.x);
                        const float     scaledY    = y(recorded.y);
                        const Rectangle scaledClip = scaled(recorded.clip);

                        recorded = RecordedText{.text = std::move(recorded.text), .face = recorded.face, .size = scaledSize, .x = scaledX, .y = scaledY, .colour = recorded.colour, .clip = scaledClip, .deckFace = std::move(recorded.deckFace)};
                    } else if constexpr (std::is_same_v<Kind, RecordedShape>) {
                        for (std::size_t at = 0UZ; at + 1UZ < recorded.points.size(); at += 2UZ) {
                            recorded.points[at]       = x(recorded.points[at]);
                            recorded.points[at + 1UZ] = y(recorded.points[at + 1UZ]);
                        }
                        recorded.thickness *= factor;
                        recorded.rounding *= factor;
                        recorded.clip = scaled(recorded.clip);
                    } else if constexpr (std::is_same_v<Kind, RecordedImage>) {
                        recorded.at   = scaled(recorded.at);
                        recorded.clip = scaled(recorded.clip);
                        recorded.size *= factor;
                    } else {
                        recorded.at = scaled(recorded.at);
                    }
                },
                primitive);
        }
    }
    for (TextRun& run : runs.runs | std::views::drop(runsFrom)) {
        run.area = scaled(run.area);
    }
    for (DocumentView::LinkArea& link : links | std::views::drop(linksFrom)) {
        link.area = scaled(link.area);
    }
    list.AddDrawCmd(); // later drawing keeps its own clip rectangle, not the scaled one
}

[[nodiscard]] Theme fadedTo(const Theme& theme, float opacity) noexcept {
    if (opacity >= 1.0f) {
        return theme;
    }
    Theme faded          = theme;
    faded.text           = dimmed(theme.text, opacity);
    faded.track          = dimmed(theme.track, opacity);
    faded.fill           = dimmed(theme.fill, opacity);
    faded.logoTint       = dimmed(theme.logoTint, opacity);
    faded.menuBackground = dimmed(theme.menuBackground, opacity);
    return faded;
}

struct InlinePen {
    Canvas                               canvas;
    ImVec2                               origin;
    ImVec2                               pen;
    float                                right      = 0.0f;
    float                                lineHeight = 0.0f;
    FaceSet                              faces;                // what the slide or box being laid out is set in
    ImFont*                              face       = nullptr; // replaces the body face, e.g. bold in a table header
    const DocumentView::FormulaLookup*   formulas   = nullptr;
    const std::vector<std::string>*      numbering  = nullptr; // referenced footnote labels, in order of appearance
    std::string_view                     linkTarget = {};      // while a link's words are drawn: where it leads
    std::vector<DocumentView::LinkArea>* links      = nullptr; // where a click is caught; none when nobody asks
    float                                baseSize   = 0.0f;    // size of the words around a larger or smaller span
    float                                lineGrowth = 0.0f;    // how far a larger span on this line reaches below it

    void newline(std::string_view separator = " ") {
        pen.x = origin.x;
        pen.y += lineHeight + lineGrowth;
        lineGrowth = 0.0f;
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith(separator);
        }
    }

    void word(std::string_view text, ImU32 colour, ImFont* font, float size) {
        const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
        if (pen.x > origin.x && pen.x + extent.x > right) {
            newline();
        }
        // a span set at another size sits on the line's baseline rather than at its top
        const float lift = baseSize > 0.0f && size != baseSize ? ImGui::GetFont()->GetFontBaked(baseSize)->Ascent - font->GetFontBaked(size)->Ascent : 0.0f;
        lineGrowth       = std::max(lineGrowth, lift + extent.y - lineHeight);
        if (canvas) { // the measuring pass advances the pen without painting
            canvas.text(font, size, ImVec2{pen.x, pen.y + lift}, colour, text);
            if (!linkTarget.empty()) {
                const Rectangle area{.x = pen.x, .y = pen.y, .width = extent.x, .height = lineHeight};
                if (canvas.recording != nullptr) {
                    canvas.recording->primitives.emplace_back(RecordedLink{.target = std::string{linkTarget}, .at = area});
                }
                if (links != nullptr) {
                    links->push_back(DocumentView::LinkArea{.target = std::string{linkTarget}, .area = area});
                }
            }
        }
        pen.x += extent.x;
    }

    void run(std::string_view text, ImU32 colour, ImFont* font, float size) {
        std::size_t start = 0UZ;
        while (start < text.size()) {
            const auto space = text.find(' ', start);
            const auto end   = space == std::string_view::npos ? text.size() : space + 1UZ;
            word(text.substr(start, end - start), colour, font, size);
            start = end;
        }
    }

    void superscript(std::string_view text, ImU32 colour, float size) {
        ImFont* const font   = ImGui::GetFont();
        const float   raised = size * kFootnoteMarkerScale;
        const ImVec2  extent = font->CalcTextSizeA(raised, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
        if (pen.x > origin.x && pen.x + extent.x > right) {
            newline();
        }
        if (canvas) {
            canvas.text(font, raised, ImVec2{pen.x, pen.y - lineHeight * 0.1f}, colour, text);
        }
        pen.x += extent.x;
    }

    /// 1-based, in the order the view first refers to them; 0 when the label was never defined
    [[nodiscard]] int numberOf(std::string_view label) const {
        if (numbering == nullptr) {
            return 0;
        }
        const auto found = std::ranges::find(*numbering, label);
        return found == numbering->end() ? 0 : static_cast<int>(found - numbering->begin()) + 1;
    }

    /// blits a rasterised formula with its baseline on the text baseline, rather than its box on the line box
    void formula(const PlacedFormula& placed, std::string_view latex, ImU32 tint, float size) {
        const float width  = placed.texture.width;
        const float height = placed.texture.height;
        if (pen.x > origin.x && pen.x + width > right) {
            newline();
        }
        if (canvas) {
            const float ascent = ImGui::GetFont()->GetFontBaked(size)->Ascent;
            const float top    = pen.y + ascent - placed.baseline;
            canvas.image(placed.texture.id, ImVec2{pen.x, top}, ImVec2{pen.x + width, top + height}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, tint, RecordedImageKind::formula, latex, size, false);
        }
        pen.x += width;
    }

    void span(const InlineSpan& inlineSpan, const Theme& theme, float size) {
        baseSize             = size;
        ImFont* const font   = faceOf(inlineSpan, faces, face);
        const float   spanSz = sizeOf(inlineSpan, size);
        switch (inlineSpan.kind) {
        case InlineKind::strong:
        case InlineKind::emphasis: run(inlineSpan.text, theme.text, font, spanSz); return;
        case InlineKind::code: run(inlineSpan.text, theme.fill, font, spanSz); return;
        case InlineKind::math: {
            const PlacedFormula* rendered = formulas != nullptr && *formulas ? (*formulas)(inlineSpan.text, spanSz, false) : nullptr;
            if (rendered == nullptr || rendered->texture.id == 0) {
                run(inlineSpan.text, theme.fill, font, spanSz); // the source, so the author sees what failed
                return;
            }
            formula(*rendered, inlineSpan.text, theme.text, spanSz);
            return;
        }
        case InlineKind::footnote: {
            const int number = numberOf(inlineSpan.target);
            superscript(number > 0 ? std::to_string(number) : std::string{"?"}, theme.fill, size); // an undefined label stays visible
            return;
        }
        case InlineKind::lineBreak: newline("\n"); return;
        case InlineKind::link:
            linkTarget = inlineSpan.target;
            run(inlineSpan.text, theme.fill, font, spanSz);
            linkTarget = {};
            return;
        case InlineKind::image:
        case InlineKind::text: run(inlineSpan.text, theme.text, font, spanSz); return;
        }
    }
};

} // namespace

namespace {

[[nodiscard]] float naturalWidth(const std::vector<InlineSpan>& spans, float size, const FaceSet& faces, ImFont* face) {
    float width = 0.0f;
    for (const InlineSpan& span : spans) { // a formula is measured from its source, since its raster exists only while painting
        width += faceOf(span, faces, face)->CalcTextSizeA(sizeOf(span, size), FLT_MAX, 0.0f, span.text.data(), span.text.data() + span.text.size()).x;
    }
    return width;
}

[[nodiscard]] float widestWord(const std::vector<InlineSpan>& cell, float size, const FaceSet& faces, ImFont* face) {
    float widest = 0.0f;
    for (const InlineSpan& span : cell) {
        // measured in the face it is drawn in: `code` is set in the wider monospace face
        ImFont* const font     = faceOf(span, faces, face);
        const float   spanSize = sizeOf(span, size);
        for (const auto word : std::string_view{span.text} | std::views::split(' ')) {
            const std::string_view text{word};
            if (!text.empty()) {
                widest = std::max(widest, font->CalcTextSizeA(spanSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x);
            }
        }
    }
    return widest;
}

/// returns the table's height; a `striped` row is laid out twice (measure, then draw over the wash)
float drawTable(Canvas canvas, const Theme& theme, const FaceSet& faces, const Block& table, float left, float right, float top, float size, const DocumentView::FormulaLookup& formulas, bool striped) {
    const std::size_t columns = table.columns.size();
    const std::size_t rows    = table.rowCount();
    if (columns == 0UZ || rows == 0UZ) {
        return 0.0f;
    }

    ImFont* const bold       = faces.bold;
    const float   lineHeight = ImGui::GetTextLineHeight();
    const float   gap        = lineHeight * kTableColumnGapLines;

    std::vector<float> wanted(columns, 0.0f);
    for (std::size_t row = 0UZ; row < rows; ++row) {
        for (std::size_t column = 0UZ; column < columns; ++column) {
            wanted[column] = std::max(wanted[column], naturalWidth(table.cellAt(row, column), size, faces, row == 0UZ ? bold : nullptr));
        }
    }

    const float available = right - left - gap * static_cast<float>(columns - 1UZ);

    // columns within their fair share keep their ask, so one wide column cannot wrap the others
    const std::vector<float> width = columnWidthsSharing(wanted, available);
    const float              drawn = std::accumulate(width.begin(), width.end(), 0.0f) + gap * static_cast<float>(columns - 1UZ);

    float y = top;
    for (std::size_t row = 0UZ; row < rows; ++row) {
        const auto layRow = [&](Canvas into) {
            float x      = left;
            float bottom = y;
            for (std::size_t column = 0UZ; column < columns; ++column) {
                if (into.runs != nullptr) {
                    into.runs->breakWith(column == 0UZ ? "\n" : "\t");
                }
                const std::vector<InlineSpan>& cell      = table.cellAt(row, column);
                const float                    cellWidth = width[column];
                ImFont* const                  face      = row == 0UZ ? bold : ImGui::GetFont();

                // only a single word wider than its column (identifier, URL) shrinks its cell, as far as needed
                const float cellSize = std::max(size * std::min(1.0f, cellWidth / std::max(widestWord(cell, size, faces, face), 1.0f)), size * kCellFloorShare);
                const float slack    = std::max(0.0f, cellWidth - naturalWidth(cell, cellSize, faces, row == 0UZ ? bold : nullptr));
                // a cell that had to wrap has no slack, so it aligns left
                const float offset = table.columns[column] == Alignment::right ? slack : table.columns[column] == Alignment::centre ? slack * 0.5f : 0.0f;

                InlinePen pen{.canvas = into, .origin = ImVec2{x + offset, y}, .pen = ImVec2{x + offset, y}, .right = x + cellWidth, .lineHeight = lineHeight, .faces = faces, .face = row == 0UZ ? bold : nullptr, .formulas = &formulas};
                for (const InlineSpan& span : cell) {
                    pen.span(span, theme, cellSize);
                }
                bottom = std::max(bottom, pen.pen.y + lineHeight);
                x += cellWidth + gap;
            }
            return bottom;
        };

        if (striped && canvas && (row == 0UZ || row % 2UZ == 0UZ)) {
            const float measured = layRow(Canvas{});
            const float pad      = lineHeight * kTableRowGapLines * 0.5f;
            canvas.filledRect(ImVec2{left, y - pad}, ImVec2{left + drawn, measured + pad}, dimmed(kStripeTint, row == 0UZ ? kHeaderWashOpacity : kStripeWashOpacity));
        }
        const float bottom = layRow(canvas);
        y                  = bottom + lineHeight * kTableRowGapLines;
        if (row == 0UZ && canvas) {
            const float rule = y - lineHeight * kTableRowGapLines * 0.5f;
            canvas.line(ImVec2{left, rule}, ImVec2{left + drawn, rule}, dimmed(theme.text, 0.4f));
        }
    }
    return y - top;
}

/// returns the block's height; long lines wrap with a hanging indent; call without a canvas to size, then to paint
/// `lineStarts`, when given, gets the first vertex of each source line drawn and, last, the end of the final one
float drawCode(Canvas canvas, const Theme& theme, ImFont* font, const Block& block, float left, float right, float top, float size, float opacity, std::vector<int>* lineStarts = nullptr) {
    assert(font != nullptr);

    const ImVec2 emBox      = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "M");
    const float  lineHeight = emBox.y;
    const float  padding    = lineHeight * kCodePaddingLines;
    const float  textLeft   = left + padding;
    const float  textRight  = right - padding;
    const float  hanging    = textLeft + emBox.x * kCodeHangingIndentChars;

    float y = top + padding;
    for (const TokenLine& tokens : highlight(block.lines, languageOf(block.info))) {
        if (canvas && lineStarts != nullptr) {
            lineStarts->push_back(canvas.list->VtxBuffer.Size);
        }
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith("\n"); // a wrapped line copies as one source line
        }
        float x = textLeft;
        for (const Token& token : tokens) {
            const char* begin  = token.text.c_str();
            const char* end    = begin + token.text.size();
            const ImU32 colour = dimmed(codeColour(theme, token.kind), opacity);
            bool        fresh  = false;
            while (begin < end) {
                const char* remaining = begin;
                ImVec2      extent{};
                if (const float room = textRight - x; room > 0.0f) {
                    extent = font->CalcTextSizeA(size, room, 0.0f, begin, end, &remaining);
                }
                if (remaining == begin) {
                    if (!fresh) {
                        y += lineHeight;
                        x     = hanging;
                        fresh = true;
                        continue;
                    }
                    remaining = begin + ImTextCountUtf8BytesFromChar(begin, end); // a column narrower than one glyph: emit it anyway
                    extent    = font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, remaining);
                }
                if (canvas) {
                    canvas.text(font, size, ImVec2{x, y}, colour, std::string_view{begin, remaining});
                }
                x += extent.x;
                begin = remaining;
                fresh = false;
                if (begin < end) {
                    y += lineHeight;
                    x     = hanging;
                    fresh = true;
                }
            }
        }
        y += lineHeight;
    }
    if (canvas && lineStarts != nullptr) {
        lineStarts->push_back(canvas.list->VtxBuffer.Size);
    }
    return y + padding - top;
}

[[nodiscard]] DocumentView::Placement placementFor(const Layout* layout, const Texture* backdrop, std::string_view backdropSource, std::span<const std::string> backdropHidden, const ImGuiViewport& viewport, Canvas canvas, const Rectangle& cameraFrame, float opacity, std::span<const std::string> claimed, float rotation) {
    const float slideBody = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);
    // no master: type is sized from the viewport itself, so a phone reads the same either way up
    if (layout == nullptr || layout->width <= 0.0f || layout->height <= 0.0f) {
        const float width = DocumentView::columnWidth(viewport.Size.x, slideBody);
        // the height a pass is allowed, not the window's, or content would fit with its last line off screen
        const float contentLeft = viewport.Pos.x + (viewport.Size.x - width) * 0.5f;
        const float contentTop  = viewport.Pos.y + slideBody * kTitleTopEms;
        const float available   = std::max(viewport.Size.y - slideBody * (kTitleTopEms + kFooterClearEms), slideBody);
        return DocumentView::Placement{.contentLeft = contentLeft, .contentTop = contentTop, .contentWidth = width, .available = available, .bodyPixels = slideBody, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
    }

    // a `top`/`middle`/`bottom` master is composed to the screen: outer bands keep their shape, the middle stretches
    const Area* splitTopArea    = layout->generated ? nullptr : layout->find("top");
    const Area* splitBottomArea = layout->generated ? nullptr : layout->find("bottom");
    if (splitTopArea != nullptr && splitBottomArea != nullptr && cameraFrame.width <= 0.0f && layout->find("content") != nullptr) {
        const float factor       = viewport.Size.x / layout->width;
        const float splitTop     = splitTopArea->y + splitTopArea->height;
        const float splitBottom  = splitBottomArea->y;
        const float screenBottom = viewport.Pos.y + viewport.Size.y;
        const float middle       = viewport.Size.y - (splitTop + layout->height - splitBottom) * factor;
        if (middle > 0.0f && splitBottom > splitTop) {
            DocumentView::Placement split{.bodyPixels = slideBody, .titlePixels = slideBody, .inset = 0.6f, .factor = factor, .originX = viewport.Pos.x, .originY = viewport.Pos.y};
            split.splitTop     = splitTop;
            split.splitBottom  = splitBottom;
            split.splitHeight  = layout->height;
            split.middleFactor = middle / (splitBottom - splitTop);
            split.screenBottom = screenBottom;
            if (canvas && backdrop != nullptr && backdrop->id != 0) {
                const ImU32 tint  = dimmed(IM_COL32_WHITE, opacity);
                const float right = viewport.Pos.x + viewport.Size.x;
                const auto  band  = [&](float fromY, float toY) {
                    const ImVec2 low{viewport.Pos.x, split.mapY(fromY)};
                    const ImVec2 high{right, split.mapY(toY)};
                    const ImVec2 uvLow{0.0f, fromY / layout->height};
                    const ImVec2 uvHigh{1.0f, toY / layout->height};
                    canvas.image(backdrop->id, low, high, uvLow, uvHigh, tint, RecordedImageKind::master, backdropSource, 0.0f, false, backdropHidden);
                };
                band(0.0f, splitTop);
                band(splitTop, splitBottom);
                band(splitBottom, layout->height);
            }
            const float inset  = split.bodyPixels * 0.5f;
            const auto  mapped = [&split, inset](const Area* area) {
                if (area == nullptr) {
                    return Rectangle{};
                }
                const Rectangle box = split.map(*area);
                return Rectangle{.x = box.x + inset, .y = box.y + inset, .width = box.width - 2.0f * inset, .height = box.height - 2.0f * inset};
            };
            const Rectangle body = mapped(layout->find("content"));
            split.contentLeft    = body.x;
            split.contentTop     = body.y;
            split.contentWidth   = body.width;
            split.available      = body.height;
            split.title          = mapped(layout->find("title"));
            split.footer         = mapped(layout->find("footer"));
            split.slideNumber    = layout->find("slide_number") == nullptr ? Rectangle{} : split.map(*layout->find("slide_number")); // a mark, not a text box: no inset
            return split;
        }
    }

    // the camera's frame fills the view; no frame is the whole document, projected like a slide master
    const Rectangle framed = cameraFrame.width > 0.0f && cameraFrame.height > 0.0f ? cameraFrame : Rectangle{.x = 0.0f, .y = 0.0f, .width = layout->width, .height = layout->height};

    const Area* content = layout->find("content");
    if (content == nullptr && layout->generated) {
        // a grid without `content` puts unplaced prose in the first box nothing else claimed
        const auto isNamedSlot = [](const Area& area) { return area.id == "title" || area.id == "footer" || area.id == "slide_number"; };
        const auto isClaimed   = [claimed](const Area& area) { return std::ranges::find(claimed, area.id) != claimed.end(); };
        const auto first       = std::ranges::find_if(layout->areas, [&](const Area& area) { return !isNamedSlot(area) && area.width > 0.0f && !isClaimed(area); });
        content                = first == layout->areas.end() ? nullptr : &*first;
    }

    // a drawing with anchors but no box for words: heading band on top, prose band at the foot, both pinned to the view
    // bands are reserved before letterboxing from the type at full scale, which can only over-reserve
    // a grid whose boxes are all named slots is not a drawing: its flow is empty and it keeps its title and footer
    const bool  caption = !layout->generated && (content == nullptr || content->width <= 0.0f);
    const float band    = caption ? slideBody : 0.0f;
    const float above   = caption ? band * (titleHeadingEms() + kTitleAirEms * 0.5f) : 0.0f;
    const float below   = caption ? std::min(viewport.Size.y * kCaptionMaxHeightShare, band * kCaptionMaxLines) + band : 0.0f;
    const float room    = std::max(viewport.Size.y - above - below, 1.0f);

    // air around the anchor, since a frame on its outline cuts off strokes and labels that reach past it
    const bool      anchored   = caption && cameraFrame.width > 0.0f && cameraFrame.height > 0.0f;
    const float     airedShare = 1.0f + 2.0f * kAnchorAirShare;
    const Rectangle aired{.x = framed.x - framed.width * kAnchorAirShare, .y = framed.y - framed.height * kAnchorAirShare, .width = framed.width * airedShare, .height = framed.height * airedShare};
    const Rectangle frame = anchored ? aired : framed;

    // a partial frame covers the view (no letterbox bars), blending to the whole drawing so moves do not jump
    const float fitted   = std::min(viewport.Size.x / frame.width, room / frame.height);
    const float covering = std::max(viewport.Size.x / layout->width, room / layout->height);
    const float partial  = std::clamp((1.0f - std::min(frame.width / layout->width, frame.height / layout->height)) * 4.0f, 0.0f, 1.0f);
    const float factor   = fitted + std::max(covering - fitted, 0.0f) * partial;
    // the camera keeps to the drawing within the room between title and words; a turning drawing gives this up
    const float turned  = std::clamp(std::abs(rotation) / 90.0f, 0.0f, 1.0f);
    const auto  kept    = [turned](float wanted, float from, float extent, float drawn) { return std::lerp(drawn >= extent ? std::clamp(wanted, from + extent - drawn, from) : wanted, wanted, turned); };
    const float originX = kept(viewport.Pos.x + viewport.Size.x * 0.5f - (frame.x + frame.width * 0.5f) * factor, viewport.Pos.x, viewport.Size.x, layout->width * factor);
    const float originY = kept(viewport.Pos.y + above + room * 0.5f - (frame.y + frame.height * 0.5f) * factor, viewport.Pos.y + above, room, layout->height * factor);

    if (canvas && backdrop != nullptr && backdrop->id != 0) {
        const ImU32 tint = dimmed(IM_COL32_WHITE, opacity);
        // a drawing under a caption is clipped to its room, never behind the title or words
        const ImVec2         captionRoomLow{viewport.Pos.x, viewport.Pos.y + above};
        const ImVec2         captionRoomHigh{viewport.Pos.x + viewport.Size.x, viewport.Pos.y + above + room};
        const ScopedClipRect underCaption{caption ? canvas.list : nullptr, captionRoomLow, captionRoomHigh};
        const int            turnFrom = canvas.list->VtxBuffer.Size;
        const ImVec2         drawnLow{originX, originY};
        const ImVec2         drawnHigh{originX + layout->width * factor, originY + layout->height * factor};
        canvas.image(backdrop->id, drawnLow, drawnHigh, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, tint, RecordedImageKind::master, backdropSource, 0.0f, false, backdropHidden);
        if (caption) {
            // only a drawing under a caption turns; its words are pinned to the view
            const ImVec2 pivot{viewport.Pos.x + viewport.Size.x * 0.5f, viewport.Pos.y + above + room * 0.5f};
            turnVertices(*canvas.list, turnFrom, pivot, rotation);
            turnRecorded(canvas.recording, pivot, rotation);
        }
    }

    // a drawn master keeps its proportions; a generated layout sets type from the screen, so headings match
    const float masterBody = layout->generated ? slideBody : Fonts::bodySize(layout->height * factor);

    if (caption) {
        // sized from `band`: words pinned to the view keep the size they would have had if the picture filled it
        const float             width = DocumentView::columnWidth(viewport.Size.x, band);
        const float             left  = viewport.Pos.x + (viewport.Size.x - width) * 0.5f;
        const Rectangle         heading{.x = left, .y = viewport.Pos.y, .width = width, .height = band * titleHeadingEms()};
        const float             captionTop = viewport.Pos.y + viewport.Size.y - below;
        const BoxOptions        captionOptions{.bottom = true};
        DocumentView::Placement placement{.contentLeft = left, .contentTop = captionTop, .contentWidth = width, .available = below - band, .bodyPixels = band, .title = heading, .options = captionOptions};
        placement.factor  = factor;
        placement.originX = originX;
        placement.originY = originY;
        return placement;
    }
    // authors draw boxes where content sits, not where glyphs touch the edge; generated ones are on the margin
    const float inset  = masterBody * 0.5f;
    const float insetX = layout->generated ? 0.0f : inset;
    const float insetY = layout->generated ? 0.0f : inset;
    const auto  exact  = [originX, originY, factor](const Area* area) {
        if (area == nullptr) {
            return Rectangle{};
        }
        return Rectangle{.x = originX + area->x * factor, .y = originY + area->y * factor, .width = area->width * factor, .height = area->height * factor};
    };
    const auto mapped = [originX, originY, factor, insetY, insetX](const Area* area) {
        if (area == nullptr) {
            return Rectangle{};
        }
        return Rectangle{.x = originX + area->x * factor + insetX, .y = originY + area->y * factor + insetY, .width = area->width * factor - insetX * 2.0f, .height = area->height * factor - insetY * 2.0f};
    };
    const Rectangle         body = mapped(content);
    DocumentView::Placement placement{.contentLeft = body.x, .contentTop = body.y, .contentWidth = body.width, .available = body.height, .bodyPixels = masterBody, .titlePixels = slideBody, .inset = layout->generated ? 0.0f : 0.6f};
    placement.title       = mapped(layout->find("title"));
    placement.footer      = mapped(layout->find("footer"));
    placement.slideNumber = exact(layout->find("slide_number"));
    placement.factor      = factor;
    placement.originX     = originX;
    placement.originY     = originY;
    return placement;
}

/// shares a live region's box between charts and bars: `here` takes a strip, another name takes that layout area
[[nodiscard]] DocumentView::RegionBoxes regionBoxes(const Block& block, Rectangle box, const Layout* layout, const DocumentView::Placement& where, float bodyLine, std::vector<std::string>& withoutArea) {
    DocumentView::RegionBoxes boxes{.charts = box, .toolbar = std::nullopt, .status = std::nullopt};
    const float               strip    = bodyLine * kBarLines;
    const auto                placeBar = [&](std::string_view asked, bool above) -> std::optional<Rectangle> {
        if (asked.empty()) {
            return std::nullopt;
        }
        if (const Area* area = layout != nullptr && asked != "here" ? layout->find(asked) : nullptr; area != nullptr) {
            return where.map(*area);
        }
        if (asked != "here") {
            withoutArea.push_back(std::format("{}/{}", block.name(), asked));
        }
        boxes.charts.height -= strip;
        if (above) {
            boxes.charts.y += strip;
            return Rectangle{.x = box.x, .y = box.y, .width = box.width, .height = strip};
        }
        return Rectangle{.x = box.x, .y = box.y + box.height - strip, .width = box.width, .height = strip};
    };
    boxes.toolbar = placeBar(block.field("toolbar"), true);
    boxes.status  = placeBar(block.field("status"), false);
    return boxes;
}

} // namespace

std::vector<float> columnWidthsSharing(std::span<const float> wanted, float available) {
    const float        asked = std::accumulate(wanted.begin(), wanted.end(), 0.0f);
    std::vector<float> width(wanted.begin(), wanted.end());
    if (asked <= available) {
        return width;
    }
    const float fair   = available / static_cast<float>(wanted.size());
    float       modest = 0.0f;
    float       greedy = 0.0f;
    for (const float column : wanted) {
        (column <= fair ? modest : greedy) += column;
    }
    const float leftOver = std::max(0.0f, available - modest);
    for (std::size_t column = 0UZ; column < wanted.size(); ++column) {
        if (wanted[column] > fair) {
            width[column] = greedy > 0.0f ? wanted[column] / greedy * leftOver : fair;
        }
    }
    return width;
}

CameraGeometry DocumentView::cameraGeometry(float reservedTop, const Rectangle& wantedUv, const CameraMove* move, float reservedBottom) const {
    if (cameraImage == nullptr || cameraImage->id == 0 || cameraImage->width <= 0 || cameraImage->height <= 0) {
        return CameraGeometry{};
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          body     = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);

    // one composition for every scope: the picture gets the room the words leave
    const Rectangle box{.x = viewport.Pos.x, .y = viewport.Pos.y + reservedTop, .width = viewport.Size.x, .height = std::max(viewport.Size.y - reservedTop - reservedBottom - body * 1.2f, 1.0f)};

    const float     imageAspect  = static_cast<float>(cameraImage->width) / static_cast<float>(cameraImage->height);
    const Rectangle wanted       = wantedUv.width > 0.0f && wantedUv.height > 0.0f ? wantedUv : Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
    const float     screenAspect = box.width / box.height;

    if (move != nullptr) {
        // a move is drawn unclamped and may show a margin past the picture; only the existing texture is sampled
        const Rectangle frame = cameraFrameAt(move->from, move->via, move->to, move->progress, screenAspect, imageAspect);
        const float     left  = std::max(frame.x, 0.0f);
        const float     top   = std::max(frame.y, 0.0f);
        const float     right = std::min(frame.x + frame.width, 1.0f);
        const float     below = std::min(frame.y + frame.height, 1.0f);
        if (right <= left || below <= top) {
            return CameraGeometry{.box = box, .drawn = Rectangle{}, .uv = Rectangle{}, .frame = frame};
        }
        const auto      onScreenX = [&](float u) { return box.x + (u - frame.x) / frame.width * box.width; };
        const auto      onScreenY = [&](float v) { return box.y + (v - frame.y) / frame.height * box.height; };
        const Rectangle drawn{.x = onScreenX(left), .y = onScreenY(top), .width = onScreenX(right) - onScreenX(left), .height = onScreenY(below) - onScreenY(top)};
        const Rectangle uv{.x = left, .y = top, .width = right - left, .height = below - top};
        return CameraGeometry{.box = box, .drawn = drawn, .uv = uv, .frame = frame};
    }

    const Rectangle uv = fittedToAspect(wanted, screenAspect, imageAspect);

    // when the picture ran out (a very wide region on an upright phone) it is centred in the box, not stretched
    const float framedAspect = uv.width * imageAspect / uv.height;
    const float drawnWidth   = std::min(box.width, box.height * framedAspect);
    const float drawnHeight  = drawnWidth / framedAspect;

    return CameraGeometry{.box = box, .drawn = Rectangle{.x = box.x + (box.width - drawnWidth) * 0.5f, .y = box.y + (box.height - drawnHeight) * 0.5f, .width = drawnWidth, .height = drawnHeight}, .uv = uv, .frame = uv};
}

void DocumentView::drawInfoBox(const Theme& theme, const Rectangle& face, const Rectangle& screen) {
    if (infoBox.opacity <= 0.0f || infoBox.contents.blocks.empty() || face.width <= 0.0f) {
        return;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          body     = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);
    const float          pad      = body * kInfoBoxPaddingEms;
    const float          gap      = body * kInfoBoxGapEms;

    // beside the region as wide as the roomier side allows; on a portrait screen above or below, up to screen width
    const float readable = body * kInfoBoxReadableEms;
    const float beside   = std::max(face.x - screen.x, screen.x + screen.width - (face.x + face.width)) - 2.0f * gap;
    const float width    = beside >= readable ? std::clamp(beside, std::max(screen.width * kInfoBoxMinWidthShare, readable), std::max(screen.width * kInfoBoxMaxWidthShare, readable)) : std::min(screen.width - 2.0f * gap, body * kInfoBoxWideEms);
    const Theme shown    = fadedTo(theme, opacity * infoBox.opacity);
    const auto  drawBox  = [&](const Document& contents, std::string_view side) {
        // measured with its heading, which `heightOf` leaves out because a slide's heading lives in the title band
        const Placement measuring{.contentLeft = 0.0f, .contentTop = 0.0f, .contentWidth = width - 2.0f * pad, .available = screen.height, .bodyPixels = body, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
        const float     text = layoutPass(shown, contents, 1.0f, false, measuring, Selection::all).height;
        const Rectangle box  = besideRegion(face, screen, width, text + 2.0f * pad, side, gap);

        const Canvas canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        canvas.filledRect(lowCorner(box), highCorner(box), dimmed(theme.background, kInfoBoxBackdropOpacity * opacity * infoBox.opacity), kPlaceholderRounding);
        canvas.rect(lowCorner(box), highCorner(box), dimmed(theme.text, kInfoBoxFrameOpacity * opacity * infoBox.opacity), kPlaceholderRounding, 0, kInfoBoxFrameWidthPixels);
        const Placement inside{.contentLeft = box.x + pad, .contentTop = box.y + pad, .contentWidth = box.width - 2.0f * pad, .available = box.height - 2.0f * pad, .bodyPixels = body, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
        layoutPass(shown, contents, 1.0f, true, inside, Selection::all);
        return box;
    };
    const Rectangle box = drawBox(infoBox.contents, infoBox.side);
    if (!infoBox.aside.blocks.empty()) {
        // the side the caption did not take
        const std::string_view opposite = box.x + box.width <= face.x ? "right" : box.x >= face.x + face.width ? "left" : box.y + box.height <= face.y ? "below" : "above";
        std::ignore                     = drawBox(infoBox.aside, opposite);
    }

    infoBoxDrawn  = box;
    infoFaceDrawn = face;
}

Rectangle DocumentView::drawCameraImage(float opacityNow, const CameraGeometry& geometry, const Rectangle& clip) const {
    if (cameraImage == nullptr || geometry.drawn.width <= 0.0f) {
        return Rectangle{};
    }
    const Canvas         into{ImGui::GetWindowDrawList(), recording, &textRuns};
    const ImU32          tint = dimmed(IM_COL32_WHITE, opacityNow);
    const ScopedClipRect inFrame{into.list, lowCorner(clip), highCorner(clip)};
    const int            turnFrom = into.list->VtxBuffer.Size;
    into.image(cameraImage->id, lowCorner(geometry.drawn), highCorner(geometry.drawn), //
        lowCorner(geometry.uv), highCorner(geometry.uv), tint, RecordedImageKind::picture, cameraSource);
    if (cameraScope == CameraScope::image) {
        const ImVec2 pivot{geometry.box.x + geometry.box.width * 0.5f, geometry.box.y + geometry.box.height * 0.5f};
        turnVertices(*into.list, turnFrom, pivot, cameraRotation);
        turnRecorded(recording, pivot, cameraRotation);
    }
    return geometry.drawn;
}

float DocumentView::columnWidth(float viewportWidth, float bodyPixels) noexcept {
    // three ems a side would eat a quarter of a phone screen, hence the share cap
    const float margin = std::min(kColumnMarginsTotalEms * 0.5f * bodyPixels, kColumnMarginMaxWidthShare * viewportWidth);
    return std::max(viewportWidth - 2.0f * margin, bodyPixels);
}

BoxOptions boxOptionsOf(const Block& block) noexcept {
    const auto flag = [&block](std::string_view name, bool otherwise) {
        const std::string_view said = block.field(name);
        return said.empty() ? otherwise : (said != "off" && said != "false" && said != "no");
    };
    const std::optional<float> inSeconds    = parseSeconds(block.field("dur"));
    const float                afterSeconds = parseSeconds(block.field("after")).value_or(0.0f);
    std::string                overlay{block.field("overlay")};
    for (const auto& [key, value] : block.fields) {
        if (!overlay.empty() && key.starts_with("overlay.")) {
            overlay += std::format(" {}={}", slice(key, 8UZ), value);
        }
    }
    BoxOptions options;
    options.shrink       = flag("shrink", true);
    options.centre       = flag("centre", false);
    options.striped      = flag("striped", false);
    options.notes        = flag("notes", true);
    options.bottom       = flag("bottom", false);
    options.frame        = flag("frame", false);
    options.left         = flag("left", false);
    options.fit          = flag("fit", false);
    options.slideNotes   = block.field("notes") == "all";
    options.size         = parseTypeSize(block.field("size"));
    options.font         = std::string{block.field("font")};
    options.in           = std::string{block.field("in")};
    options.inSeconds    = inSeconds && *inSeconds > 0.0f ? inSeconds : std::nullopt;
    options.afterSeconds = std::max(afterSeconds, 0.0f);
    options.inFrom       = std::string{block.field("from")};
    options.overlay      = std::move(overlay);
    return options;
}

DocumentView::Placement DocumentView::bodyPlacement(bool paint) const {
    const ImGuiViewport*     viewport = ImGui::GetMainViewport();
    std::vector<std::string> claimed;
    for (const AreaDocument& slot : areaDocuments) {
        claimed.push_back(slot.id);
    }
    for (const auto& [id, unusedResource] : areaContent) {
        claimed.push_back(id);
    }
    return placementFor(layout, layoutBackdrop, layoutSource, layoutHidden, *viewport, Canvas{paint ? ImGui::GetWindowDrawList() : nullptr, recording, &textRuns}, cameraFrame, opacity, claimed, cameraRotation);
}

float DocumentView::heightOf(const Theme& theme, const Document& contents, float width, float bodyPixels, float room, const BoxOptions& options) {
    Placement box{.contentLeft = 0.0f, .contentTop = 0.0f, .contentWidth = width, .available = room, .bodyPixels = bodyPixels, .title = {}, .footer = {}, .options = options, .inset = 0.0f};
    sizedAsSaid(box, slideSize);
    sizedAsSaid(box, options.size);
    const LaidOut needed = layoutPass(theme, contents, 1.0f, false, box, Selection::exceptHeading);
    // a row is followed by a gutter, so the space its last block leaves is not part of what it needs
    return needed.heightToFit();
}

/// every named area of the layout as a thin frame with its name in a corner, for the author laying a slide out
void DocumentView::drawAreaOutlines(const Theme& theme, const Placement& where) const {
    ImDrawList& list  = *ImGui::GetWindowDrawList();
    const ImU32 frame = dimmed(theme.fill, 0.9f);
    const float size  = where.bodyPixels * 0.55f;
    for (const Area& area : layout->areas) {
        if (area.id.empty()) {
            continue;
        }
        const Rectangle box = where.map(area);
        list.AddRect(lowCorner(box), highCorner(box), frame, 0.0f, ImDrawFlags_None, 1.5f);
        const std::string label = area.kind.empty() ? area.id : area.id + " (" + area.kind + ")";
        // the corner where text least often is
        const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        list.AddText(ImGui::GetFont(), size, ImVec2{box.x + box.width - extent.x - size * 0.3f, box.y + box.height - extent.y - size * 0.15f}, frame, label.c_str());
    }
}

void DocumentView::draw(const Theme& theme) {
    drawnBlocks.clear();
    placedRegions.clear();
    clipped.clear();
    barsWithoutArea.clear();
    videoButtons.clear();
    linkAreas.clear();
    textRuns.clear();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         scene(windowName, nullptr, kWindowFlags);
    drawnInto = ImGui::GetWindowDrawList();
    if (beforeDrawing) {
        beforeDrawing(*drawnInto);
    }

    const Theme faded = fadedTo(theme, opacity);
    infoBoxDrawn      = Rectangle{};
    infoFaceDrawn     = Rectangle{};
    cameraFrameDrawn  = Rectangle{};

    // the words are measured before the picture is placed, so the picture takes only the room left over
    float wordsNeed = 0.0f;
    float notesNeed = 0.0f;
    if (cameraImage != nullptr) {
        // measured for both scopes, which start from the same composition; notes go beneath the picture as its caption
        Placement probe = bodyPlacement(false);
        sizedAsSaid(probe, slideSize, true);
        const float withNotes = layoutPass(faded, *document, 1.0f, false, probe, Selection::all).height;
        probe.options.notes   = false;
        const float without   = layoutPass(faded, *document, 1.0f, false, probe, Selection::all).height;
        notesNeed             = std::max(withNotes - without, 0.0f);
        wordsNeed             = std::min(without + probe.bodyPixels, viewport->Size.y * 0.45f);
    }

    Placement where = bodyPlacement(true); // draws the backdrop
    sizedAsSaid(where, slideSize, true);
    if (afterBackdrop) {
        afterBackdrop(*drawnInto);
    }
    const bool split = where.title.width > 0.0f;

    // the picture goes down after the backdrop and before the words, so the words read over it
    Rectangle picture;
    float     reservedTop = 0.0f;
    Rectangle infoFace;
    Rectangle infoScreen;
    Rectangle notesUnder;
    if (cameraImage != nullptr) {
        reservedTop = where.contentTop + wordsNeed - viewport->Pos.y;
        const Rectangle   screen{.x = viewport->Pos.x, .y = viewport->Pos.y, .width = viewport->Size.x, .height = viewport->Size.y};
        const CameraMove* moving = cameraMove.active() ? &cameraMove : nullptr;

        if (cameraScope == CameraScope::image) {
            const CameraGeometry framed = cameraGeometry(reservedTop, cameraUv, moving, notesNeed);
            picture                     = drawCameraImage(opacity, framed, framed.box);
            cameraFrameDrawn            = framed.frame;
            notesUnder                  = Rectangle{.x = picture.x, .y = picture.y + picture.height, .width = picture.width, .height = 0.0f};
            infoFace                    = framed.onScreen(infoBox.region);
            infoScreen                  = framed.box;
        } else {
            // `slide` magnifies the whole slide: the image scope's composition under one transform
            const CameraGeometry whole = cameraGeometry(reservedTop, Rectangle{}, nullptr, notesNeed);

            const auto glassFor = [&](const Rectangle& region) { return magnifierFor(whole, cameraGeometry(reservedTop, region, nullptr, notesNeed), region.width > 0.0f, screen); };

            Glass glass = glassFor(cameraUv);
            if (moving != nullptr) {
                // a move interpolates each stop's glass window as the image scope does; clamping each frame would jump
                const auto  windowOf = [&screen](const Glass& g) { return Rectangle{.x = (screen.x - g.offsetX) / g.factor, .y = (screen.y - g.offsetY) / g.factor, .width = screen.width / g.factor, .height = screen.height / g.factor}; };
                const Glass from     = glassFor(moving->from);
                const Glass to       = glassFor(moving->to);
                const Glass via      = moving->via.width > 0.0f ? glassFor(moving->via) : Glass{};
                Rectangle   window   = interpolateVia(windowOf(from), moving->via.width > 0.0f ? windowOf(via) : Rectangle{}, windowOf(to), moving->progress);
                if (window.width > kMaxPullBack * screen.width) {
                    window.x += 0.5f * (window.width - kMaxPullBack * screen.width);
                    window.y += 0.5f * (window.height - kMaxPullBack * screen.height);
                    window.width  = kMaxPullBack * screen.width;
                    window.height = kMaxPullBack * screen.height;
                }
                const float factor = screen.width / window.width;
                glass              = Glass{.factor = factor, .offsetX = screen.x - window.x * factor, .offsetY = screen.y - window.y * factor};
            }
            const bool identity = glass.factor == 1.0f && glass.offsetX == 0.0f && glass.offsetY == 0.0f;
            const auto moved    = [glass](float x, float y) { return ImVec2{x * glass.factor + glass.offsetX, y * glass.factor + glass.offsetY}; };

            const ImVec2         corner = moved(whole.drawn.x, whole.drawn.y);
            const CameraGeometry travelled{.box = screen, .drawn = Rectangle{.x = corner.x, .y = corner.y, .width = whole.drawn.width * glass.factor, .height = whole.drawn.height * glass.factor}, .uv = whole.uv, .frame = whole.frame};
            picture = drawCameraImage(opacity, travelled, identity ? whole.box : screen);
            // the part of the picture on screen, in picture fractions, as the image scope reports it
            const float shownX      = (screen.x - glass.offsetX) / glass.factor;
            const float shownY      = (screen.y - glass.offsetY) / glass.factor;
            const float shownU      = whole.uv.x + (shownX - whole.drawn.x) / whole.drawn.width * whole.uv.width;
            const float shownV      = whole.uv.y + (shownY - whole.drawn.y) / whole.drawn.height * whole.uv.height;
            const float shownWidth  = screen.width / glass.factor / whole.drawn.width * whole.uv.width;
            const float shownHeight = screen.height / glass.factor / whole.drawn.height * whole.uv.height;
            cameraFrameDrawn        = Rectangle{.x = shownU, .y = shownV, .width = shownWidth, .height = shownHeight};
            const ImVec2 below      = moved(whole.drawn.x, whole.drawn.y + whole.drawn.height);
            notesUnder              = Rectangle{.x = below.x, .y = below.y, .width = whole.drawn.width * glass.factor, .height = 0.0f};
            if (const Rectangle onWhole = whole.onScreen(infoBox.region); onWhole.width > 0.0f) {
                const ImVec2 faceCorner = moved(onWhole.x, onWhole.y);
                infoFace                = Rectangle{.x = faceCorner.x, .y = faceCorner.y, .width = onWhole.width * glass.factor, .height = onWhole.height * glass.factor};
                infoScreen              = screen;
            }

            where.available    = std::max(whole.drawn.y - where.contentTop, where.bodyPixels);
            const ImVec2 words = moved(where.contentLeft, where.contentTop);
            where.contentLeft  = words.x;
            where.contentTop   = words.y;
            where.contentWidth *= glass.factor;
            where.available *= glass.factor;
            where.bodyPixels *= glass.factor;
        }
    }

    if (picture.width > 0.0f && cameraScope == CameraScope::image) {
        where.available = std::max(picture.y - where.contentTop, where.bodyPixels);
    }

    if (split) {
        // bodyPixels must be set: every size is a multiple of it, and zero lets the atlas substitute a tiny title
        const bool  drawn     = layout != nullptr && !layout->generated;
        const float titleBody = where.titlePixels > 0.0f ? where.titlePixels : where.bodyPixels;
        Placement   titleArea{.contentLeft = where.title.x, .contentTop = where.title.y, .contentWidth = where.title.width, .available = where.title.height, .bodyPixels = titleBody, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
        // a generated band shrinks a wrapped title (a portrait phone), its subtitle not below the floor, then clips;
        // a shallow drawn box lends the title air instead
        const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        const float  floorPixels  = Fonts::pointsToPixels(Fonts::instance().scale.floor, viewportSize.x, viewportSize.y);
        titleArea.floorScale      = std::min(1.0f, floorPixels / (headingSize(1, titleBody) * kSubtitleRatio));
        const LaidOut titleNeeded = layoutPass(faded, *document, 1.0f, false, titleArea, Selection::headingOnly);
        const float   titleScale  = drawn ? 1.0f : fittingScale(faded, *document, titleArea, Selection::headingOnly, titleNeeded).first;
        const float   lent        = drawn ? std::max(titleNeeded.height - where.title.height, 0.0f) * 0.5f : 0.0f;
        titleArea.contentTop -= lent;
        titleArea.available += 2.0f * lent;
        const Canvas         canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        const ScopedClipRect inTitle{canvas.list, ImVec2{where.title.x, where.title.y - lent}, ImVec2{where.title.x + where.title.width, where.title.y + where.title.height + lent}};
        layoutPass(faded, *document, titleScale, true, titleArea, Selection::headingOnly);
    }

    const bool notesBeneath = notesNeed > 0.0f && picture.width > 0.0f;
    if (notesBeneath) {
        where.options.notes = false;
    }
    const Selection body              = split ? Selection::exceptHeading : Selection::all;
    const LaidOut   needed            = layoutPass(faded, *document, 1.0f, false, where, body);
    const auto [bodyScale, bodyDrawn] = fittingScale(faded, *document, where, body, needed);
    appliedScale                      = bodyScale;
    const float drawnHeight           = bodyDrawn.height;
    const bool  atFloor               = bodyDrawn.heightToFit() > where.available + kFitTolerancePixels;
    if (where.options.bottom) {
        where.contentTop += std::max(0.0f, where.available - drawnHeight);
    }
    // what still does not fit at the floor is cut off at the bottom of the body
    if (atFloor) {
        clipped.push_back(Overrun{.id = "content", .wanted = drawnHeight, .available = where.available, .shrank = true});
    }
    {
        const ImVec2         bodyLow{0.0f, where.contentTop};
        const ImVec2         bodyHigh{ImGui::GetIO().DisplaySize.x, where.contentTop + where.available};
        const ScopedClipRect cutAtFloor{atFloor ? ImGui::GetWindowDrawList() : nullptr, bodyLow, bodyHigh};
        layoutPass(faded, *document, appliedScale, true, where, body);
    }
    if (notesBeneath) {
        // notes sit at the picture's lower edge and keep to its width, as a caption does
        Placement under{.contentLeft = notesUnder.x, .contentTop = notesUnder.y, .contentWidth = notesUnder.width, .available = 0.0f, .bodyPixels = where.bodyPixels, .title = {}, .footer = {}, .options = where.options, .inset = 0.0f};
        under.options.notes = true;
        layoutPass(faded, *document, 1.0f, true, under, Selection::notesOnly);
    }

    // a `:::<name>` block runs the same layout pass as the main flow into the box of that name
    slideNotes.clear();
    for (const AreaDocument& entry : areaDocuments) {
        for (const Block& block : entry.contents.blocks | std::views::filter([this](const Block& block) { return block.step <= step; })) {
            appendCitedLabels(block, slideNotes);
        }
    }
    for (const AreaDocument& entry : areaDocuments) {
        const Area* area = layout == nullptr ? nullptr : layout->find(entry.id);
        if (area == nullptr) {
            continue;
        }
        const Rectangle framed  = where.map(*area);
        const float     padding = entry.options.frame ? where.bodyPixels * kFramePaddingEms : 0.0f;
        const Rectangle box{.x = framed.x + padding, .y = framed.y + padding, .width = framed.width - 2.0f * padding, .height = framed.height - 2.0f * padding};
        if (entry.options.frame) {
            ImDrawList& list = *ImGui::GetWindowDrawList();
            list.AddRectFilled(lowCorner(framed), highCorner(framed), faded.background, kPlaceholderRounding);
            list.AddRect(lowCorner(framed), highCorner(framed), dimmed(faded.text, 0.45f), kPlaceholderRounding, ImDrawFlags_None, 1.5f);
        }
        const Rectangle slotFooter = entry.options.slideNotes ? where.footer : Rectangle{};
        Placement       slot{.contentLeft = box.x, .contentTop = box.y, .contentWidth = box.width, .available = box.height, .bodyPixels = where.bodyPixels, .title = {}, .footer = slotFooter, .options = entry.options, .inset = 0.0f};
        sizedAsSaid(slot, entry.options.size);

        const LaidOut wanted   = layoutPass(faded, entry.contents, 1.0f, false, slot, Selection::all);
        const bool    overruns = wanted.heightToFit() > slot.available;
        // content scales down to fit unless the author fixed the size: then it is cut off and reported in `clipped`
        const auto [scale, fitted] = overruns && entry.options.shrink ? fittingScale(faded, entry.contents, slot, Selection::all, wanted) : std::pair{1.0f, wanted};
        if (overruns && !entry.options.shrink) {
            clipped.push_back(Overrun{.id = entry.id, .wanted = wanted.height, .available = slot.available, .shrank = false});
        }
        if (entry.options.centre) {
            slot.contentTop += std::max(0.0f, slot.available - wanted.height * scale) * 0.5f;
        } else if (entry.options.bottom) {
            slot.contentTop += std::max(0.0f, slot.available - wanted.height * scale);
        }
        // a box that may not shrink clips at once; one that may clips only at the floor; both are reported
        const bool           shrinks    = overruns && entry.options.shrink;
        const LaidOut        atFull     = LaidOut{.height = wanted.height * scale, .trailingGap = wanted.trailingGap};
        const LaidOut        boxDrawn   = shrinks ? fitted : atFull;
        const float          boxHeight  = boxDrawn.height;
        const bool           boxAtFloor = shrinks && boxDrawn.heightToFit() > slot.available + kFitTolerancePixels;
        const bool           boxFits    = boxAtFloor && entry.options.fit;
        const Canvas         into{ImGui::GetWindowDrawList(), recording, &textRuns};
        const bool           boxCut = !entry.options.shrink || (boxAtFloor && !boxFits);
        const ScopedClipRect inBox{boxCut ? into.list : nullptr, lowCorner(box), highCorner(box)};
        const float          boxSeconds  = entry.options.inSeconds.value_or(revealSeconds ? revealSeconds(entry.options.in) : kRevealSeconds);
        const float          boxProgress = (arrivedSeconds - entry.options.afterSeconds) / boxSeconds;
        const bool           boxArrives  = arriving && !entry.options.in.empty() && boxProgress < 1.0f;
        const bool           overlaid    = !entry.options.overlay.empty() && entry.options.overlay != "none";
        const DrawnFrom      overlayFrom = overlaid ? startDrawing(*into.list) : DrawnFrom{};
        const DrawnFrom      boxFrom     = boxArrives ? startDrawing(*into.list) : DrawnFrom{};
        const DrawnFrom      fitFrom     = boxFits ? startDrawing(*into.list) : DrawnFrom{};
        const auto           marks       = std::array{recording != nullptr ? recording->primitives.size() : 0UZ, textRuns.runs.size(), linkAreas.size()};
        layoutPass(faded, entry.contents, scale, true, slot, Selection::all);
        if (boxFits) {
            scaleDrawn(*into.list, fitFrom, recording, marks[0], textRuns, marks[1], linkAreas, marks[2], ImVec2{box.x, slot.contentTop}, slot.available / std::max(boxDrawn.heightToFit(), 1.0f));
        }
        if (boxArrives) {
            arrive(*into.list, boxFrom, entry.options.in, entry.options.inFrom, std::clamp(boxProgress, 0.0f, 1.0f), slot.bodyPixels * kRiseEms, throughEffect, std::format("box:{}", entry.id));
        }
        if (overlaid && throughEffect) {
            std::ignore = throughEffect(*into.list, overlayFrom.command, entry.options.overlay, std::format("overlay:{}", entry.id), 1.0f, lowCorner(box), highCorner(box));
        }
        if (boxAtFloor && !boxFits) {
            clipped.push_back(Overrun{.id = entry.id, .wanted = boxHeight, .available = slot.available, .shrank = true});
        }
    }
    if (outlineAreas && layout != nullptr) {
        drawAreaOutlines(faded, where);
    }

    // other named areas may hold an image or a drawing the flow never sees
    for (const auto& [id, files] : areaContent) {
        const SchemeFiles schemeFiles = schemeFilesOf(files);
        const Area*       area        = layout == nullptr ? nullptr : layout->find(id);
        // the variant for what the picture stands on: the master's brightness there, else the scheme's background
        const bool        darkBehind = area != nullptr && area->backdropLight >= 0.0f ? area->backdropLight < kDarkVariantBelowBrightness : faded.scheme == ColourScheme::dark;
        const std::string reference{darkBehind ? schemeFiles.dark : schemeFiles.light};
        const Texture*    art = imageFor ? imageFor(reference) : nullptr;
        if (area == nullptr) {
            continue;
        }
        const Rectangle box = where.map(*area);
        const Canvas    into{ImGui::GetWindowDrawList(), recording, &textRuns};
        const ImU32     tint = dimmed(IM_COL32_WHITE, opacity);
        if (art != nullptr && art->id != 0) {
            // fitted inside the box, keeping its aspect
            const float  factor = std::min(box.width / art->width, box.height / art->height);
            const ImVec2 size{art->width * factor, art->height * factor};
            const ImVec2 origin{box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f};
            into.image(art->id, origin, ImVec2{origin.x + size.x, origin.y + size.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, tint, imageKindOf(reference), reference);
        } else {
            into.rect(lowCorner(box), highCorner(box), dimmed(faded.text, 0.35f), kPlaceholderRounding);
            const std::string label = "missing: " + reference;
            into.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{box.x + 6.0f, box.y + 6.0f}, dimmed(faded.text, 0.6f), label.c_str());
        }
    }

    if (where.slideNumber.width > 0.0f && !footerNumber.empty()) {
        // a master's number box centres the number, sized by the box
        const float      numberPixels = where.slideNumber.height * kSlideNumberHeightShare;
        const ScopedFont numberFont(numberPixels);
        const ImVec2     size = ImGui::CalcTextSize(footerNumber.c_str());
        const Canvas     canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        canvas.text(ImGui::GetFont(), numberPixels, ImVec2{where.slideNumber.x + (where.slideNumber.width - size.x) * 0.5f, where.slideNumber.y + (where.slideNumber.height - size.y) * 0.5f}, dimmed(faded.text, 0.8f), footerNumber.c_str());
    }
    if (where.footer.width > 0.0f) {
        // the number sits at the right edge; the footer line shrinks to what is left, then is cut
        // status size follows the window height, which exceeds the band on an upright phone: cap a grid's at 3/4
        const bool       gridBand     = layout == nullptr || layout->generated;
        const float      statusPixels = gridBand ? std::min(Fonts::statusSize(viewport->Size.y), where.footer.height * kFooterTextHeightShare) : Fonts::statusSize(viewport->Size.y);
        const ScopedFont status(statusPixels);
        const bool       numberHere = !footerNumber.empty() && where.slideNumber.width <= 0.0f;
        const float      numberWide = numberHere ? ImGui::CalcTextSize(footerNumber.c_str()).x : 0.0f;
        const float      room       = where.footer.width - numberWide - (numberHere ? statusPixels : 0.0f);
        const Canvas     canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        if (numberHere) {
            canvas.text(ImGui::GetFont(), statusPixels, ImVec2{where.footer.x + where.footer.width - numberWide, where.footer.y}, dimmed(faded.text, 0.55f), footerNumber.c_str());
        }
        if (!footerText.empty() && room > 0.0f) {
            const float       textWide = ImGui::CalcTextSize(footerText.c_str()).x;
            const ScopedFont  fitting(textWide > room ? std::max(statusPixels * room / textWide, statusPixels * kFooterSmallestStatusShare) : statusPixels);
            const std::string line = Fonts::fitted(footerText, room);
            canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{where.footer.x, where.footer.y + (statusPixels - ImGui::GetFontSize())}, dimmed(faded.text, 0.55f), line.c_str());
        }
    }

    // last, so a stop's words sit over everything else
    drawInfoBox(theme, infoFace, infoScreen);
    if (afterDrawing) {
        afterDrawing(*drawnInto);
    }
}

auto DocumentView::fittingScale(const Theme& theme, const Document& contents, const Placement& where, Selection selection, LaidOut wanted) -> std::pair<float, LaidOut> {
    // the space after the last block need not fit, or a box sized to its words would shrink them for nothing
    if (wanted.heightToFit() <= where.available) {
        return {1.0f, wanted};
    }
    // text wraps at the box's width at every scale, so only height decides; the estimate is a lower bound
    const auto measure = [&](float scale) {
        const LaidOut laidOut = layoutPass(theme, contents, scale, false, where, selection);
        return std::pair{laidOut.heightToFit() <= where.available + kFitTolerancePixels, laidOut};
    };
    const float floor = where.floorScale > 0.0f ? where.floorScale : Fonts::instance().scale.floor / Fonts::instance().scale.body;
    return largestFitting<LaidOut>(floor, std::max(where.available / wanted.height, floor), measure);
}

/// what one layout pass shares between the blocks it lays out
struct DocumentView::Pass {
    const Document&                 document;
    const Theme&                    theme;
    const Placement&                where;
    const Canvas&                   canvas;
    const FaceSet&                  faces;
    Selection                       selection;
    float                           scale;
    float                           left;
    float                           right;
    float                           width;
    float                           top;
    float                           bodyLine;
    float                           ceiling; // the height a picture may take, its share of the box
    const std::vector<std::string>& numbering;
    float                           cursor;
    float                           trailingGap; // space after the last block laid out so far
};

std::vector<std::string> DocumentView::citedLabels(const Document& contents, Selection selection, const Placement& where) const {
    std::vector<std::string> referenced;
    for (const Block& block : contents.blocks) {
        if (block.step > step) {
            continue;
        }
        const auto visible = [selection, &block] {
            const bool isHeading = block.kind == BlockKind::heading;
            return !((selection == Selection::headingOnly && !isHeading) || ((selection == Selection::exceptHeading || selection == Selection::notesOnly) && isHeading));
        };
        if (!visible()) {
            continue;
        }
        appendCitedLabels(block, referenced);
    }

    if (where.options.slideNotes) {
        for (const std::string& label : slideNotes) {
            if (std::ranges::find(referenced, label) == referenced.end()) {
                referenced.push_back(label);
            }
        }
    }
    return referenced;
}

void DocumentView::layoutVideo(Pass& pass, const Block& block) {
    const std::string_view source   = block.field("src");
    const bool             autoplay = block.field("autoplay") != "false";
    const bool             loop     = block.field("loop") != "false";
    const bool             sound    = block.field("audio") == "true"; // silent unless the author asks
    const PlayingVideo*    clip     = source.empty() || !videoFor ? nullptr : videoFor(source, autoplay, loop, sound);

    if (clip != nullptr && clip->texture.id != 0) {
        // a lone clip takes the room, centred; otherwise capped like other pictures or no scale could fit the slide
        const float barsAndAir = pass.bodyLine * kVideoControlsHeightLines;
        const bool  alone      = std::ranges::count_if(pass.document.blocks, [this, &pass](const Block& other) { return other.step <= step && (other.kind != BlockKind::heading || pass.selection == Selection::all); }) <= 1;
        const float room       = std::max(alone ? pass.where.available * pass.scale - (pass.cursor - pass.top) - barsAndAir : std::min(pass.ceiling, pass.where.available * pass.scale - (pass.cursor - pass.top) - barsAndAir), pass.bodyLine);
        ImVec2      size       = clip->texture.scaledToWidth(pass.right - pass.left);
        if (size.y > room) {
            size = ImVec2{size.x * room / size.y, room};
        }
        const float frameLeft = pass.left + (pass.right - pass.left - size.x) * 0.5f;
        if (alone) {
            pass.cursor += std::max(room - size.y, 0.0f) * 0.5f;
        }
        if (pass.canvas) {
            pass.canvas.image(clip->texture.id, ImVec2{frameLeft, pass.cursor}, ImVec2{frameLeft + size.x, pass.cursor + size.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), RecordedImageKind::video, source);

            // marks are drawn as shapes: a triangle and bars are not in the subset face
            const float barTop = pass.cursor + size.y + pass.bodyLine * kVideoBarGapLines;
            const float height = pass.bodyLine * kVideoBarHeightLines;
            pass.canvas.filledRect(ImVec2{frameLeft, barTop}, ImVec2{frameLeft + size.x, barTop + height}, dimmed(pass.theme.track, 0.9f));
            const float through = clip->length > 0.0 ? std::clamp(static_cast<float>(clip->position / clip->length), 0.0f, 1.0f) : 0.0f;
            pass.canvas.filledRect(ImVec2{frameLeft, barTop}, ImVec2{frameLeft + size.x * through, barTop + height}, pass.theme.fill);

            const float buttonSide = pass.bodyLine * kVideoButtonSizeLines;
            const float buttonTop  = barTop + height + pass.bodyLine * kVideoButtonGapLines;
            const ImU32 markColour = dimmed(pass.theme.text, 0.75f);
            const auto  place      = [&](float x, VideoAction action, bool active) {
                const Rectangle area{.x = x, .y = buttonTop, .width = buttonSide, .height = buttonSide};
                videoButtons.push_back(VideoButton{.source = std::string{source}, .action = action, .area = area, .active = active});
                return ImVec2{x, buttonTop};
            };
            const float markSide = buttonSide * 0.7f;
            const auto  markAt   = [&](ImVec2 corner) { return ImVec2{corner.x + buttonSide * 0.15f, corner.y + buttonSide * 0.15f}; };

            const ImVec2 playAt = markAt(place(frameLeft, VideoAction::playPause, clip->playing));
            if (clip->playing) {
                pass.canvas.filledRect(ImVec2{playAt.x, playAt.y}, ImVec2{playAt.x + markSide * 0.35f, playAt.y + markSide}, markColour);
                pass.canvas.filledRect(ImVec2{playAt.x + markSide * 0.65f, playAt.y}, ImVec2{playAt.x + markSide, playAt.y + markSide}, markColour);
            } else {
                pass.canvas.filledTriangle(ImVec2{playAt.x, playAt.y}, ImVec2{playAt.x, playAt.y + markSide}, ImVec2{playAt.x + markSide, playAt.y + markSide * 0.5f}, markColour);
            }

            const ImVec2 rewindAt = markAt(place(frameLeft + buttonSide * 1.2f, VideoAction::rewind, false));
            pass.canvas.filledRect(ImVec2{rewindAt.x, rewindAt.y}, ImVec2{rewindAt.x + markSide * 0.2f, rewindAt.y + markSide}, markColour);
            pass.canvas.filledTriangle(ImVec2{rewindAt.x + markSide, rewindAt.y}, ImVec2{rewindAt.x + markSide, rewindAt.y + markSide}, ImVec2{rewindAt.x + markSide * 0.2f, rewindAt.y + markSide * 0.5f}, markColour);

            if (clip->hasSound) {
                const ImVec2 soundAt = markAt(place(frameLeft + size.x - buttonSide, VideoAction::muteToggle, clip->muted));
                const float  mid     = soundAt.y + markSide * 0.5f;
                pass.canvas.filledRect(ImVec2{soundAt.x, mid - markSide * 0.2f}, ImVec2{soundAt.x + markSide * 0.25f, mid + markSide * 0.2f}, markColour);
                pass.canvas.filledTriangle(ImVec2{soundAt.x + markSide * 0.25f, mid - markSide * 0.2f}, ImVec2{soundAt.x + markSide * 0.25f, mid + markSide * 0.2f}, ImVec2{soundAt.x + markSide * 0.65f, mid + markSide * 0.5f}, markColour);
                pass.canvas.filledTriangle(ImVec2{soundAt.x + markSide * 0.25f, mid - markSide * 0.2f}, ImVec2{soundAt.x + markSide * 0.65f, mid - markSide * 0.5f}, ImVec2{soundAt.x + markSide * 0.65f, mid + markSide * 0.5f}, markColour);
                if (clip->muted) {
                    pass.canvas.line(ImVec2{soundAt.x + markSide * 0.75f, mid - markSide * 0.3f}, ImVec2{soundAt.x + markSide, mid + markSide * 0.3f}, markColour, pass.bodyLine * 0.08f);
                    pass.canvas.line(ImVec2{soundAt.x + markSide * 0.75f, mid + markSide * 0.3f}, ImVec2{soundAt.x + markSide, mid - markSide * 0.3f}, markColour, pass.bodyLine * 0.08f);
                } else {
                    pass.canvas.line(ImVec2{soundAt.x + markSide * 0.8f, mid - markSide * 0.25f}, ImVec2{soundAt.x + markSide * 0.8f, mid + markSide * 0.25f}, markColour, pass.bodyLine * 0.08f);
                    pass.canvas.line(ImVec2{soundAt.x + markSide * 1.0f, mid - markSide * 0.4f}, ImVec2{soundAt.x + markSide * 1.0f, mid + markSide * 0.4f}, markColour, pass.bodyLine * 0.08f);
                }
            }
        }
        pass.cursor += size.y + barsAndAir;
    } else {
        if (pass.canvas) {
            const std::string message = clip == nullptr ? "a video directive needs a `src:`" : (clip->problem.empty() ? "loading " + std::string{source} : clip->problem);
            pass.canvas.rect(ImVec2{pass.left, pass.cursor}, ImVec2{pass.right, pass.cursor + (pass.right - pass.left) * kRegionAspect}, dimmed(pass.theme.text, 0.35f), kPlaceholderRounding);
            pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left + pass.bodyLine * 0.5f, pass.cursor + pass.bodyLine * 0.5f}, dimmed(pass.theme.text, 0.7f), message.c_str());
        }
        pass.cursor += (pass.right - pass.left) * kRegionAspect + (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
    }
}

void DocumentView::layoutQr(Pass& pass, const Block& block) {
    const std::string_view url  = block.field("url");
    const float            side = std::min(pass.bodyLine * kQrLines, pass.right - pass.left);
    const PlacedQrCode*    code = url.empty() || !qrFor ? nullptr : qrFor(url, side);
    if (code != nullptr && code->texture.id != 0) {
        if (pass.canvas) {
            pass.canvas.image(code->texture.id, ImVec2{pass.left, pass.cursor}, ImVec2{pass.left + side, pass.cursor + side}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), RecordedImageKind::qrCode, url);
        }
        const ImVec2 labelStart{pass.left + side + pass.bodyLine, pass.cursor + pass.bodyLine * 0.5f};
        InlinePen    pen{.canvas = pass.canvas, .origin = labelStart, .pen = labelStart, .right = pass.right, .lineHeight = pass.bodyLine, .faces = pass.faces, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        if (const std::string_view label = block.field("label"); !label.empty()) {
            pen.run(std::string{label}, pass.theme.text, ImGui::GetFont(), ImGui::GetFontSize());
            pen.newline();
        }
        pen.run(std::string{url}, dimmed(pass.theme.fill, 0.9f), pass.faces.mono, ImGui::GetFontSize() * kQrUrlScale);
        pass.cursor += side + (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
    } else if (pass.canvas) {
        const std::string message = code == nullptr ? "a qr directive needs a `url:`" : code->problem;
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left, pass.cursor}, dimmed(pass.theme.text, 0.7f), message.c_str());
        pass.cursor += pass.bodyLine + (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
    } else {
        pass.cursor += pass.bodyLine + (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
    }
}

void DocumentView::layoutReferences(Pass& pass, const Block& block) {
    // the page a room photographs: QR codes in a right-hand column; in a browser they encode the opening address
    float listRight = pass.right;
    if (const std::string_view written = block.field("qr"); !written.empty() && qrFor) {
        const std::string                                               url       = deckAddress.empty() ? std::string{written} : deckAddress;
        const std::string_view                                          label     = block.field("label");
        const std::string                                               joiner    = url.contains('?') ? "&" : "?";
        const float                                                     labelSize = pass.bodyLine * kReferenceScale;
        const float                                                     labelLine = labelSize * 1.5f;
        ImFont*                                                         mono      = pass.faces.mono;
        const std::array<std::pair<std::string, std::string_view>, 3UZ> codes{{{url, label.empty() ? std::string_view{"this deck"} : label}, {url + joiner + "export=slides", "PDF, slides"}, {url + joiner + "export=steps", "PDF, every step"}}};
        const float                                                     room           = ImGui::GetIO().DisplaySize.y - pass.cursor - pass.bodyLine;
        const float                                                     itemLabelSpace = 2.6f * labelLine;
        const float                                                     widthShare     = ImGui::GetIO().DisplaySize.x * kQrShareOfWidth;
        const float                                                     roomShare      = (room - static_cast<float>(codes.size()) * itemLabelSpace) / static_cast<float>(codes.size());
        const float                                                     side           = std::max(std::min(widthShare, roomShare), pass.bodyLine);
        const float                                                     box            = pass.right - side;
        float                                                           codeTop        = pass.cursor;
        for (const auto& [target, title] : codes) {
            const PlacedQrCode* code = qrFor(target, side);
            if (pass.canvas && code != nullptr && code->texture.id != 0) {
                const float titleWide = ImGui::GetFont()->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, title.data(), title.data() + title.size()).x;
                pass.canvas.text(ImGui::GetFont(), labelSize, ImVec2{box + std::max(side - titleWide, 0.0f) * 0.5f, codeTop}, dimmed(pass.theme.text, 0.8f), std::string{title}.c_str());
                const float codeY = codeTop + labelLine;
                pass.canvas.image(code->texture.id, ImVec2{box, codeY}, ImVec2{box + side, codeY + side}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), RecordedImageKind::qrCode, target);
                // a click opens what scanning opens, on screen and on paper
                const Rectangle codeArea{.x = box, .y = codeY, .width = side, .height = side};
                linkAreas.push_back(LinkArea{.target = target, .area = codeArea});
                if (pass.canvas.recording != nullptr) {
                    pass.canvas.recording->primitives.emplace_back(RecordedLink{.target = target, .at = codeArea});
                }
                // the scheme is not shown; host and path identify the address
                std::string_view shown = target;
                for (const std::string_view scheme : {std::string_view{"https://"}, std::string_view{"http://"}}) {
                    if (shown.starts_with(scheme)) {
                        shown.remove_prefix(scheme.size());
                    }
                }
                // an address too wide for the code breaks before its query, each line fitted
                const auto widthAt = [&](std::string_view text) {
                    if (text.empty()) {
                        return 0.0f;
                    }
                    return mono->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x; // ImGui reads a data-less view as NUL-terminated
                };
                const auto                              query = shown.find('?');
                const bool                              split = widthAt(shown) > side * kQrAddressSplitCodeWidths && query != std::string_view::npos;
                const std::array<std::string_view, 2UZ> lines{split ? shown.substr(0UZ, query) : shown, split ? shown.substr(query) : std::string_view{}};
                const float                             widest  = std::max(widthAt(lines[0]), widthAt(lines[1]));
                const float                             fitted  = widest > side ? labelSize * side / widest : labelSize;
                float                                   lineTop = codeY + side + labelSize * 0.25f;
                for (const std::string_view line : lines | std::views::filter([](std::string_view text) { return !text.empty(); })) {
                    const ImVec2 under{box + std::max(side - widthAt(line) * fitted / labelSize, 0.0f) * 0.5f, lineTop};
                    InlinePen    pen{.canvas = pass.canvas, .origin = under, .pen = under, .right = box + side + 1.0f, .lineHeight = labelLine, .faces = pass.faces, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
                    pen.linkTarget = target;
                    pen.word(line, dimmed(pass.theme.fill, 0.95f), mono, fitted);
                    lineTop += fitted * 1.2f;
                }
            }
            codeTop += side + itemLabelSpace;
        }
        listRight = box - pass.bodyLine; // gutter: references stay clear of the codes
    }
    // citations come first and keep their marker numbers (IEEE), so `[3]` on a slide is `[3]` here
    const float smaller = ImGui::GetFontSize() * kReferenceScale;
    for (std::size_t index = 0UZ; index < citations.size(); ++index) {
        if (pass.canvas.runs != nullptr) {
            pass.canvas.runs->breakWith("\n");
        }
        const std::vector<InlineSpan>* text = document->footnote(citations[index]);
        if (text == nullptr) {
            continue; // cited but never defined; the slide drew a `?`
        }
        const ImVec2 hangingOrigin{pass.left + pass.bodyLine * kReferenceIndentLines, pass.cursor};
        const ImVec2 referenceStart{pass.left, pass.cursor};
        InlinePen    pen{.canvas = pass.canvas, .origin = hangingOrigin, .pen = referenceStart, .right = listRight, .lineHeight = pass.bodyLine, .faces = pass.faces, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        pen.run("[" + std::to_string(index + 1UZ) + "]  ", dimmed(pass.theme.fill, 0.9f), ImGui::GetFont(), smaller);
        for (const InlineSpan& span : *text) {
            pen.span(span, pass.theme, smaller);
        }
        pass.cursor = pen.pen.y + pass.bodyLine * 1.1f;
    }

    if (!citations.empty() && !references.empty()) {
        pass.cursor += (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
    }
    for (const auto& [label, target] : references) {
        if (pass.canvas.runs != nullptr) {
            pass.canvas.runs->breakWith("\n");
        }
        const ImVec2 referenceStart{pass.left, pass.cursor};
        InlinePen    pen{.canvas = pass.canvas, .origin = referenceStart, .pen = referenceStart, .right = listRight, .lineHeight = pass.bodyLine, .faces = pass.faces, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        pen.run(label + "  ", pass.theme.text, ImGui::GetFont(), smaller);
        pen.linkTarget = target;
        pen.run(target, dimmed(pass.theme.fill, 0.9f), pass.faces.mono, smaller * 0.9f);
        pass.cursor = pen.pen.y + pass.bodyLine * 1.1f;
    }
    if (citations.empty() && references.empty() && pass.canvas) {
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left, pass.cursor}, dimmed(pass.theme.text, 0.6f), "this deck cites and links to nothing");
    }
}

void DocumentView::layoutRegion(Pass& pass, const Block& block) {
    // a region may name an area of the master, which then places it
    if (layout != nullptr) {
        std::string_view named = block.field("region");
        if (named.starts_with('#')) {
            named.remove_prefix(1UZ);
        }
        if (const Area* area = layout->find(named); area != nullptr) {
            const Rectangle   placed = pass.where.map(*area);
            const std::string region{block.name()};
            // only while painting: a measuring pass must not run a frame of someone else's widget
            const bool filled = pass.canvas && regionFor && regionFor(region, regionBoxes(block, placed, layout, pass.where, pass.bodyLine, barsWithoutArea));
            if (pass.canvas && !filled) {
                pass.canvas.rect(lowCorner(placed), highCorner(placed), dimmed(pass.theme.fill, 0.8f), kPlaceholderRounding);
                pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{placed.x + pass.bodyLine * 0.4f, placed.y + pass.bodyLine * 0.4f}, dimmed(pass.theme.text, 0.7f), region.c_str());
            }
            if (pass.canvas) {
                placedRegions.push_back(PlacedRegion{.id = std::string{block.name()}, .min = lowCorner(placed), .max = highCorner(placed)});
            }
            return; // outside the flow, so the cursor does not move
        }
    }
    // a status bar is one line; anything else is a chart as wide as the column
    const float            height = block.field("widget") == "status" ? pass.bodyLine * kStatusBarLines : pass.width * kRegionAspect;
    const ImVec2           min{pass.left, pass.cursor};
    const ImVec2           max{pass.right, pass.cursor + height};
    const std::string_view regionId = block.name();
    const Rectangle        regionArea{.x = pass.left, .y = pass.cursor, .width = pass.right - pass.left, .height = height};
    const bool             filled = pass.canvas && regionFor && regionFor(regionId, regionBoxes(block, regionArea, layout, pass.where, pass.bodyLine, barsWithoutArea));
    if (pass.canvas && !filled) {
        pass.canvas.rect(min, max, dimmed(pass.theme.fill, 0.8f), kPlaceholderRounding);
        const std::string label = block.id.empty() ? std::string{block.info} : block.info + " \xe2\x80\xa2 " + block.id;
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left + pass.bodyLine * 0.5f, pass.cursor + pass.bodyLine * 0.5f}, dimmed(pass.theme.text, 0.7f), label.c_str());
    }
    if (pass.canvas) {
        placedRegions.push_back(PlacedRegion{.id = std::string{block.name()}, .min = min, .max = max});
    }
    pass.cursor += height + (pass.trailingGap = pass.bodyLine * kParagraphGapLines);
}

/// the notes pinned above the footer so citations sit in one place; the reported height is the stacked one
void DocumentView::layoutFootnotes(Pass& pass, std::span<const std::string> referenced) {
    if (referenced.empty() || !pass.where.options.notes) {
        return;
    }
    const float noteSize = pass.where.bodyPixels * kFootnoteScale * pass.scale;
    const auto  notes    = [&](float from, Canvas into) {
        float y = from + pass.bodyLine * kParagraphGapLines;
        if (into) {
            into.line(ImVec2{pass.left, y}, ImVec2{pass.left + (pass.right - pass.left) * 0.3f, y}, dimmed(pass.theme.text, 0.3f));
        }
        y += pass.bodyLine * 0.5f;

        const ScopedFont noteFace(noteSize, pass.faces.regular);
        const float      noteLine = ImGui::GetTextLineHeight();
        for (std::size_t index = 0UZ; index < referenced.size(); ++index) {
            if (into.runs != nullptr) {
                into.runs->breakWith("\n");
            }
            InlinePen  pen{.canvas = into, .origin = ImVec2{pass.left, y}, .pen = ImVec2{pass.left, y}, .right = pass.right, .lineHeight = noteLine, .faces = pass.faces, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
            const auto known  = std::ranges::find(pass.numbering, referenced[index]);
            const auto number = known == pass.numbering.end() ? index : static_cast<std::size_t>(known - pass.numbering.begin());
            pen.superscript(std::to_string(number + 1UZ), dimmed(pass.theme.fill, 0.9f), noteSize);
            pen.word(" ", dimmed(pass.theme.text, 0.7f), ImGui::GetFont(), noteSize);
            if (const std::vector<InlineSpan>* text = pass.document.footnote(referenced[index]); text != nullptr) {
                for (const InlineSpan& span : *text) {
                    pen.span(span, pass.theme, noteSize);
                }
            } else {
                pen.run("no note is defined for [^" + referenced[index] + "]", dimmed(pass.theme.fill, 0.9f), ImGui::GetFont(), noteSize);
            }
            y = pen.pen.y + noteLine * 1.25f;
        }
        return y;
    };

    const float stacked = notes(pass.cursor, Canvas{});
    if (pass.canvas) {
        const float foot = pass.where.footer.width > 0.0f ? pass.where.footer.y - pass.where.bodyPixels * pass.scale : pass.where.contentTop + pass.where.available;
        notes(std::max(pass.cursor, foot - (stacked - pass.cursor)), pass.canvas);
    }
    pass.cursor      = stacked;
    pass.trailingGap = 0.0f;
}

auto DocumentView::layoutPass(const Theme& theme, const Document& contents, float scale, bool paint, const Placement& where, Selection selection) -> LaidOut {

    const Canvas canvas{paint ? ImGui::GetWindowDrawList() : nullptr, recording, &textRuns};
    // the box keeps its width at every scale; type and pictures are at most proportional to it, so a single pass fits
    const float width = where.contentWidth;
    const float left  = where.contentLeft;
    const float right = left + width;
    const float top   = where.contentTop + where.bodyPixels * where.inset * scale;

    const Fonts&     fonts = Fonts::instance();
    const FaceSet    faces = fonts.faceSet(where.options.font.empty() ? slideFont : where.options.font).value_or(fonts.roles());
    const ScopedFont body(where.bodyPixels * scale, faces.regular);
    const float      bodyLine = ImGui::GetTextLineHeight();

    // `referenced` is what this view cites; `numbering` fixes marker numbers: deck-wide (IEEE) order when known
    const std::vector<std::string>  referenced = citedLabels(contents, selection, where);
    const std::vector<std::string>& numbering  = citations.empty() ? referenced : citations;

    // pictures share the room (one fills the box, two split it); without a ceiling the slide could never fit
    const auto  isFigure = [](const Block& block) { return (block.kind == BlockKind::paragraph && block.spans.size() == 1UZ && block.spans.front().kind == InlineKind::image) || (block.kind == BlockKind::directive && block.info == "video"); };
    const auto  figures  = static_cast<float>(std::ranges::count_if(contents.blocks, [this, &isFigure](const Block& block) { return block.step <= step && isFigure(block); }));
    const float ceiling  = where.available * kFigureMaxRoomShare * scale / std::max(figures, 1.0f);
    Pass        pass{
               .document    = contents,
               .theme       = theme,
               .where       = where,
               .canvas      = canvas,
               .faces       = faces,
               .selection   = selection,
               .scale       = scale,
               .left        = left,
               .right       = right,
               .width       = width,
               .top         = top,
               .bodyLine    = bodyLine,
               .ceiling     = ceiling,
               .numbering   = numbering,
               .cursor      = top,
               .trailingGap = 0.0f,
    };
    float& cursor = pass.cursor;

    std::array<int, 8UZ> ordinals{}; // one running number per nesting level

    const auto gapAfter = [&pass](float gap) { return pass.trailingGap = gap; };
    for (const Block& block : contents.blocks) {
        if (block.step > step) {
            continue;
        }
        const bool isHeading = block.kind == BlockKind::heading;
        if ((selection == Selection::headingOnly && !isHeading) || (selection == Selection::exceptHeading && isHeading) || selection == Selection::notesOnly) {
            continue;
        }
        pass.trailingGap = 0.0f;
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith("\n");
        }
        const int drawnFrom = canvas ? canvas.list->VtxBuffer.Size : 0;
        if (block.kind != BlockKind::listItem) {
            ordinals.fill(0);
        } else {
            std::ranges::fill(ordinals | std::views::drop(static_cast<std::size_t>(block.level) + 1UZ), 0);
        }

        const Reveal           atOnce       = {};
        const Reveal*          written      = block.reveal.kind.empty() ? contents.revealOf(block.step) : &block.reveal;
        const Reveal&          reveal       = written != nullptr ? *written : atOnce;
        const std::string_view revealKind   = reveal.kind;
        const float            revealTime   = reveal.seconds.value_or(revealSeconds ? revealSeconds(revealKind) : kRevealSeconds);
        const float            revealed     = stepSeconds < 0.0f ? 1.0f : std::clamp(stepSeconds / std::max(revealTime, 0.01f), 0.0f, 1.0f);
        const bool             blockArrive  = canvas && step > 0 && block.step == step && !revealKind.empty() && revealed < 1.0f;
        const std::string_view revealFrom   = reveal.from;
        const DrawnFrom        arrivalStart = blockArrive ? startDrawing(*canvas.list) : DrawnFrom{};
        ImDrawList* const      arrivalList  = blockArrive ? canvas.list : nullptr;
        const std::string      arrivalKey   = blockArrive ? std::format("block:{}", &block - contents.blocks.data()) : std::string{};

        switch (block.kind) {
        case BlockKind::rule: {
            cursor += bodyLine * kRulePadding;
            if (canvas) {
                canvas.line(ImVec2{left, cursor}, ImVec2{right, cursor}, dimmed(theme.text, 0.25f));
            }
            cursor += bodyLine * kRulePadding;
            break;
        }
        case BlockKind::heading: {
            const float                     size        = headingSize(block.level, where.bodyPixels) * scale;
            ImFont* const                   headingFace = block.level == 1 ? faces.title : faces.regular;
            std::optional<const ScopedFont> heading{std::in_place, size, headingFace};
            // third-level headings and below read as a bold label
            ImFont* const labelFace   = block.level >= 3 ? faces.bold : nullptr;
            const float   headingLine = ImGui::GetTextLineHeight();
            // no space above a heading in its own title box or first in its box
            cursor += selection == Selection::headingOnly || block.level == 1 || cursor <= top ? 0.0f : headingLine * kHeadingGapAbove;
            const ImVec2 headingStart{left, cursor};
            InlinePen    pen{.canvas = canvas, .origin = headingStart, .pen = headingStart, .right = right, .lineHeight = headingLine, .faces = faces, .face = labelFace, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            bool         subtitled = false;
            for (const InlineSpan& inlineSpan : block.spans) {
                // in a title, what follows a `<br>` is a smaller sub-title on its own line
                if (inlineSpan.kind == InlineKind::lineBreak && block.level == 1 && !subtitled) {
                    subtitled = true;
                    pen.newline("\n");
                    heading.reset();
                    heading.emplace(size * kSubtitleRatio, faces.regular);
                    pen.lineHeight = ImGui::GetTextLineHeight();
                    continue;
                }
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + ImGui::GetTextLineHeight() + gapAfter(ImGui::GetTextLineHeight() * (block.level == 1 && selection != Selection::headingOnly ? kGapBelowTitleLines : kParagraphGapLines));
            break;
        }
        case BlockKind::codeBlock: {
            const float codeSize = ImGui::GetFontSize() * kCodeFontScale;
            const float height   = drawCode(Canvas{}, theme, faces.mono, block, left, right, cursor, codeSize, opacity);
            if (canvas) {
                // the panel goes first: a draw list paints in order and it sits behind the text
                canvas.filledRect(ImVec2{left, cursor}, ImVec2{right, cursor + height}, dimmed(codePanel(theme), opacity), kPlaceholderRounding);
                // lines of later steps stay blank so nothing moves as they arrive; under `{follow}` all show
                const bool follow = !block.field("follow").empty();
                Block      shown  = block;
                for (std::size_t line = 0UZ; !follow && line < shown.lineSteps.size() && line < shown.lines.size(); ++line) {
                    if (shown.lineSteps[line] > step) {
                        shown.lines[line].clear();
                    }
                }
                // each line is a morph piece keyed by its unindented text within the block's `{#id}` or among all code
                std::vector<int> lineStarts;
                drawCode(canvas, theme, faces.mono, shown, left, right, cursor, codeSize, opacity, &lineStarts);
                if (follow && std::ranges::contains(block.lineSteps, step)) {
                    for (std::size_t line = 0UZ; line + 1UZ < lineStarts.size() && line < block.lineSteps.size(); ++line) {
                        if (block.lineSteps[line] == step) {
                            continue;
                        }
                        for (ImDrawVert& vertex : std::span{canvas.list->VtxBuffer.Data + lineStarts[line], static_cast<std::size_t>(lineStarts[line + 1UZ] - lineStarts[line])}) {
                            vertex.col = dimmed(vertex.col, kUnfollowedLineOpacity);
                        }
                    }
                }
                const std::string blockKey = block.id.empty() ? std::string{"code"} : "#" + block.id;
                for (std::size_t line = 0UZ; line + 1UZ < lineStarts.size() && line < shown.lines.size(); ++line) {
                    const std::string_view text   = shown.lines[line];
                    const std::size_t      indent = text.find_first_not_of(" \t");
                    if (indent != std::string_view::npos && lineStarts[line + 1UZ] > lineStarts[line]) {
                        drawnBlocks.push_back(DrawnPiece{.key = blockKey + "/" + std::string{text.substr(indent)}, .vertexFrom = lineStarts[line], .vertexTo = lineStarts[line + 1UZ], .line = true});
                    }
                }
            }
            cursor += height + gapAfter(bodyLine * kParagraphGapLines);
            break;
        }
        case BlockKind::formula: {
            std::string latex;
            for (const std::string& piece : block.lines) {
                if (!latex.empty()) {
                    latex.push_back(' ');
                }
                latex.append(piece);
            }
            const float          size     = ImGui::GetFontSize() * kDisplayFormulaScale;
            const PlacedFormula* rendered = formulaFor ? formulaFor(latex, size, true) : nullptr;
            if (rendered != nullptr && rendered->texture.id != 0) {
                // centred unless its box asks for the left edge
                const float drawnWidth  = rendered->texture.width;
                const float drawnHeight = rendered->texture.height;
                const float factor      = drawnWidth > right - left ? (right - left) / drawnWidth : 1.0f;
                const float origin      = where.options.left ? left : left + (right - left - drawnWidth * factor) * 0.5f;
                if (canvas) {
                    canvas.image(rendered->texture.id, ImVec2{origin, cursor}, ImVec2{origin + drawnWidth * factor, cursor + drawnHeight * factor}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, theme.text, RecordedImageKind::formula, latex, size, true);
                }
                cursor += drawnHeight * factor + gapAfter(bodyLine * kParagraphGapLines);
            } else {
                if (canvas) {
                    const std::string message = rendered == nullptr ? latex : rendered->problem;
                    canvas.text(faces.mono, ImGui::GetFontSize(), ImVec2{left, cursor}, dimmed(theme.text, 0.7f), message.c_str());
                }
                cursor += bodyLine + gapAfter(bodyLine * kParagraphGapLines);
            }
            break;
        }
        case BlockKind::plot: {
            // a plot flattens past its share of the room at the drawn scale instead of narrowing
            const float wide = right - left;
            // a plot followed only by plots shares the room that is left; with words after it keeps its aspect
            const auto  isPlot     = [this](const Block& candidate) { return candidate.step <= step && candidate.kind == BlockKind::plot; };
            const auto  here       = std::ranges::find_if(contents.blocks, [&block](const Block& candidate) { return &candidate == &block; });
            const auto  trailing   = std::ranges::find_if(here, contents.blocks.end(), [&](const Block& candidate) { return candidate.step <= step && candidate.kind != BlockKind::plot; });
            const float plotsLeft  = static_cast<float>(std::ranges::count_if(here, contents.blocks.end(), isPlot));
            const float roomLeft   = where.available * kPlotMaxRoomShare * scale - (cursor - top);
            const float aspectTall = std::min(wide * kPlotAspect, where.available * kPlotMaxRoomShare * scale);
            const float tall       = trailing == contents.blocks.end() ? std::max(aspectTall, roomLeft / plotsLeft) : aspectTall;
            // its height follows from the room alone, so only a painting pass reads the data and draws
            if (canvas) {
                // not cached: decks hold few small plots and a cache would need invalidating on package reload
                Plot plot = parsePlot(block.lines);
                if (!plot.source.empty() && plot.series.empty() && plot.problem.empty()) {
                    const std::string_view csv = dataFor ? dataFor(plot.source) : std::string_view{};
                    if (csv.empty()) {
                        plot.problem = "cannot read " + plot.source;
                    } else {
                        readCsv(plot, csv);
                    }
                }
                drawPlot(canvas, theme, plot, Rectangle{.x = left, .y = cursor, .width = wide, .height = tall}, Fonts::chartLabelPixels(ImGui::GetFontSize(), ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y));
            }
            cursor += tall + gapAfter(bodyLine * kParagraphGapLines);
            break;
        }
        case BlockKind::table: {
            const float height = drawTable(canvas, theme, faces, block, left, right, cursor, ImGui::GetFontSize(), formulaFor, where.options.striped);
            cursor += height + gapAfter(bodyLine * kParagraphGapLines);
            break;
        }
        case BlockKind::listItem: {
            const float       indent = left + bodyLine * kListIndentLines * static_cast<float>(block.level + 1);
            const std::string bullet = block.ordered ? std::to_string(++ordinals[static_cast<std::size_t>(block.level)]) + "." : std::string{"\xe2\x80\xa2"};
            if (canvas) {
                // a bullet is not copied; an ordered item's number is the author's, so it copies
                const Canvas marker{canvas.list, canvas.recording, block.ordered ? canvas.runs : nullptr};
                marker.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{indent - bodyLine, cursor}, dimmed(theme.text, 0.7f), block.ordered ? bullet + " " : bullet);
            }
            InlinePen pen{.canvas = canvas, .origin = ImVec2{indent, cursor}, .pen = ImVec2{indent, cursor}, .right = right, .lineHeight = bodyLine, .faces = faces, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            for (const InlineSpan& inlineSpan : block.spans) {
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + bodyLine + gapAfter(bodyLine * kParagraphGapLines * 0.4f);
            break;
        }
        case BlockKind::directive: {
            if (block.info == "video") {
                layoutVideo(pass, block);
            } else if (block.info == "qr") {
                layoutQr(pass, block);
            } else if (block.info == "references") {
                layoutReferences(pass, block);
            } else if (block.info == "gr4" || block.info == "shader" || block.info == "stage") {
                layoutRegion(pass, block);
            }
            break;
        }
        case BlockKind::paragraph: {
            const bool isImageOnly = block.spans.size() == 1UZ && block.spans.front().kind == InlineKind::image;
            if (isImageOnly) {
                const InlineSpan& image   = block.spans.front();
                const Texture*    texture = imageFor ? imageFor(image.target) : nullptr;
                if (texture != nullptr && texture->id != 0) {
                    // past the figure ceiling it keeps its shape and narrows, centred: a stretch is worse than a margin
                    ImVec2 size = texture->scaledToWidth(width);
                    if (size.y > ceiling) {
                        size = ImVec2{size.x * ceiling / size.y, ceiling};
                    }
                    const float origin = left + (width - size.x) * 0.5f;
                    if (canvas) {
                        canvas.image(texture->id, ImVec2{origin, cursor}, ImVec2{origin + size.x, cursor + size.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), imageKindOf(image.target), image.target);
                        for (const SvgLink& link : linksFor ? linksFor(image.target) : std::span<const SvgLink>{}) {
                            const Rectangle area{.x = origin + link.area.x * size.x, .y = cursor + link.area.y * size.y, .width = link.area.width * size.x, .height = link.area.height * size.y};
                            linkAreas.push_back(LinkArea{.target = link.target, .area = area});
                            if (canvas.recording != nullptr) {
                                canvas.recording->primitives.emplace_back(RecordedLink{.target = link.target, .at = area});
                            }
                        }
                    }
                    cursor += size.y + gapAfter(bodyLine * kParagraphGapLines);
                } else {
                    const float height = width * kRegionAspect * 0.6f;
                    if (canvas) {
                        canvas.rect(ImVec2{left, cursor}, ImVec2{right, cursor + height}, dimmed(theme.text, 0.35f), kPlaceholderRounding);
                    }
                    const std::string label = "missing image: " + image.target;
                    if (canvas) {
                        canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{left + bodyLine * 0.5f, cursor + bodyLine * 0.5f}, dimmed(theme.text, 0.6f), label.c_str());
                    }
                    cursor += height + gapAfter(bodyLine * kParagraphGapLines);
                }
                break;
            }
            InlinePen pen{.canvas = canvas, .origin = ImVec2{left, cursor}, .pen = ImVec2{left, cursor}, .right = right, .lineHeight = bodyLine, .faces = faces, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            for (const InlineSpan& inlineSpan : block.spans) {
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + bodyLine + gapAfter(bodyLine * kParagraphGapLines);
            break;
        }
        }
        if (arrivalList != nullptr) {
            arrive(*arrivalList, arrivalStart, revealKind, revealFrom, revealed, bodyLine * kRiseEms, throughEffect, arrivalKey);
        }
        if (canvas && canvas.list->VtxBuffer.Size > drawnFrom) {
            drawnBlocks.push_back(DrawnPiece{.key = morphKeyOf(block), .vertexFrom = drawnFrom, .vertexTo = canvas.list->VtxBuffer.Size});
        }
    }

    layoutFootnotes(pass, referenced);
    return LaidOut{.height = cursor - where.contentTop + where.bodyPixels * 2.0f * where.inset * scale, .trailingGap = pass.trailingGap};
}

} // namespace gr::present
