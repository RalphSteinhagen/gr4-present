#include "DocumentView.hpp"

#include "Canvas.hpp"
#include "Fonts.hpp"
#include "ImGuiScoped.hpp"
#include "PlotView.hpp"
#include "SvgImage.hpp"
#include "Transition.hpp"

#include <gr4-present/Number.hpp>

#include <imgui_internal.h> // ImTextCountUtf8BytesFromChar

#include <algorithm>
#include <array>
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

constexpr float kDarkBackdrop      = 0.5f;  // a master darker than this under an area takes the dark variant of `a | b`
constexpr float kSlideNumberShare  = 0.45f; // of a `slide_number` box's height, what its number's type takes
constexpr float kFollowedAway      = 0.35f; // `{follow}`: what a code line not of the current step keeps of its opacity
constexpr float kColumnMarginEms   = 6.0f;  // three ems of margin on each side of the reading column
constexpr float kColumnMarginShare = 0.04f; // of the window width, the most a margin may take when it is narrow
// A drawing the camera moves over has anchors but often no box for words. Those get a caption band along the
// foot of the view rather than a column down the middle of it, which would sit over whatever is being framed.
constexpr float kTitleAirEms         = 1.6f;          // above and below a heading pinned to the top of the view
constexpr float kAnchorAir           = 0.06f;         // of an anchor's size, on each side, where a drawing is framed under a caption
constexpr float kTitleTopEms         = 0.2f;          // a slide with no master puts its title this close to the top, in body heights
constexpr float kFooterClearEms      = 2.1f;          // and keeps this much clear at the foot for the footer
constexpr float kFooterSmallest      = 0.7f;          // of the status size: the least a footer line shrinks before it is cut
constexpr float kRiseEms             = 0.6f;          // how far below its place a `rise` reveal starts
constexpr float kSubtitleRatio       = 20.0f / 36.0f; // a sub-title after `<br>` in a title: 20 pt under a 36 pt title
constexpr float kSubtitleGapEms      = 0.79f;         // of a title line, under a title: a subtitle's line at 1.5 times smaller, and a small margin
constexpr float kCaptionShare        = 0.30f;         // of the height, at most
constexpr float kCaptionLines        = 7.0f;          // of the body size, at most
constexpr float kParagraphGap        = 0.55f;         // of a line height
constexpr float kHeadingGapAbove     = 1.1f;
constexpr float kListIndent          = 1.6f; // of a body line height, per nesting level
constexpr float kRulePadding         = 0.8f;
constexpr float kRegionAspect        = 0.5625f; // 16:9, until a live region declares its own
constexpr float kPlaceholderRounding = 4.0f;
constexpr float kBarLines            = 1.6f;                             // a toolbar or status bar beside a region's charts, in body lines
constexpr float kCodeFontScale       = 0.86f;                            // of the body size: a code line is long and every glyph is full width
constexpr float kCodePadding         = 0.6f;                             // of a code line height, on every side of the panel
constexpr float kCodeHangingIndent   = 2.0f;                             // in characters, so a wrapped continuation reads as one
constexpr float kCellFloor           = 0.40f;                            // of the table's size: below this a cell stops matching the rest of it
constexpr float kStripeWash          = 0.20f;                            // opacity of the tint: visible on both schemes, and far enough from the text to leave it legible
constexpr float kHeaderWash          = 0.34f;                            // a table header is the strongest wash, so the column titles read as a band
constexpr float kInfoBoxBackdrop     = 0.70f;                            // the deck's background at 70 %: the photograph stays faintly visible through it
constexpr float kInfoBoxFrame        = 0.60f;                            // opacity of the frame drawn round it
constexpr float kInfoBoxFrameWidth   = 1.5f;                             // px
constexpr float kInfoBoxPaddingEms   = 0.6f;                             // of the body size, inside the frame
constexpr float kInfoBoxGapEms       = 0.8f;                             // of the body size, between the box and the region it describes
constexpr float kInfoBoxMinShare     = 0.22f;                            // of the screen width
constexpr float kInfoBoxMaxShare     = 0.34f;                            // of the screen width
constexpr float kInfoBoxReadableEms  = 9.0f;                             // of the body size: narrower than this, a box beside the region is a column of single words
constexpr float kInfoBoxWideEms      = 22.0f;                            // of the body size: how wide a box above or below the region may grow
constexpr ImU32 kStripeTint          = IM_COL32(0xC0, 0xC0, 0xC0, 0xFF); // light grey: neutral, so a band never reads as a highlight
constexpr float kTableColumnGap      = 1.0f;                             // of a body line height, between one column and the next
constexpr float kTableRowGap         = 0.35f;                            // of a body line height, between one row and the next
constexpr float kFooterTextShare     = 0.75f;                            // of the footer band's height: what is left of it above the slide's bottom edge
constexpr float kFramePaddingEms     = 0.4f;                             // the air inside a `{frame}` box, in body lines
constexpr float kStatusBarLines      = 1.6f;                             // a status region: one line of text with the air of a bar
constexpr float kFigureMaxShare      = 0.95f;                            // of the room, divided between the pictures in the flow, before one narrows
constexpr float kPlotMaxShare        = 0.95f;                            // of the room: a plot alone in a box takes it, and one beside words gives way with them
constexpr float kDisplayFormulaScale = 1.35f;                            // of the body size: a displayed equation is set larger than the prose
constexpr float kFootnoteMarkerScale = 0.7f;                             // of the surrounding text, raised, which is what a superscript is
constexpr float kFootnoteScale       = 0.62f;                            // of the body size: a note is read from the front row, not the back
constexpr float kReferenceScale      = 0.72f;                            // of the body size: a reference list is read, not presented
constexpr float kReferenceIndent     = 1.6f;                             // of a body line height, so a wrapped reference hangs under its text
constexpr float kQrLines             = 9.5f;                             // of a body line height: large enough to scan from the back of a room,
constexpr float kQrUrlScale          = 0.75f;                            // of the body size: the URL beside a code is read, not presented
constexpr float kQrShareOfWidth      = 0.20f;                            // of the screen width
constexpr float kQrAddressSplit      = 1.6f;                             // of a code's width: an address longer than this breaks before its query
constexpr float kVideoBarHeight      = 0.18f;                            // of a body line height
constexpr float kVideoButtonSize     = 0.9f;                             // of a body line height: a square a finger can hit
constexpr float kVideoBarGap         = 0.25f;                            // of a body line height, above the bar
constexpr float kVideoButtonGap      = 0.2f;                             // of a body line height, between the bar and the buttons
constexpr float kVideoFootAir        = 0.3f;                             // of a body line height, below the buttons
constexpr float kVideoControlsHeight = kVideoBarGap + kVideoBarHeight + kVideoButtonGap + kVideoButtonSize + kVideoFootAir;

/**
 * A heading's size, from the deck's ladder rather than from a multiple of the body.
 *
 * The slide's own title is 36 pt and every slide's is the same, which is what makes a deck read as one deck. A
 * heading inside the flow steps down from 24 pt towards the body: 24, then a size between, then the body itself,
 * so a sub-heading is distinguishable without being another title.
 */
[[nodiscard]] float headingSize(int level, float bodyPixels) noexcept {
    if (level <= 1) {
        return bodyPixels * Fonts::titleRatio();
    }
    const float heading = bodyPixels * Fonts::headingRatio();
    const float body    = bodyPixels;
    const float t       = std::clamp(static_cast<float>(level - 1) / 3.0f, 0.0f, 1.0f);
    return heading + (body * 1.15f - heading) * t;
}

/// a heading's line and its own padding, in body heights, so a caption's title is never scaled down to fit
[[nodiscard]] float titleHeadingEms() noexcept { return Fonts::titleRatio() * 1.5f + 1.2f; }

/// the faces a span is set in: its own `font=`, or what the slide or box chose
[[nodiscard]] FaceSet facesOf(const InlineSpan& span) {
    const Fonts& fonts = Fonts::instance();
    return span.font.empty() ? fonts.activeFaces() : fonts.faceSet(span.font).value_or(fonts.activeFaces());
}

/// the face a span is drawn and measured in; `textFace` replaces the regular one for plain words, as a table's header is bold
[[nodiscard]] ImFont* faceOf(const InlineSpan& span, ImFont* textFace) {
    const bool plain = span.kind == InlineKind::text || span.kind == InlineKind::image;
    return plain && span.font.empty() && textFace != nullptr ? textFace : facesOf(span).forKind(span.kind);
}

/// a span's own `size=`, in pixels: points at the deck's reference, or a share of the size `around` it
[[nodiscard]] float sizeOf(const InlineSpan& span, float around) {
    const std::optional<TypeSize> said = span.size.empty() ? std::nullopt : parseTypeSize(span.size);
    if (!said) {
        return around;
    }
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    return said->unit == TypeSize::Unit::relative ? around * said->value : Fonts::pointsToPixels(said->value, viewport.x, viewport.y);
}

/**
 * A body size the author set, for a slide or a box: points at the deck's reference, or a share of the size it
 * would otherwise have. Auto-shrink then stops at the deck's floor -- or at once, for a size already below it,
 * because the floor bounds what the viewer does, never what the author asked for. A slide's title keeps its size.
 */
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

/// turns the vertices [from, end) of `list` by -`degrees` about `centre`: the camera turning one way is the scene
/// turning the other
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

/// the picture just recorded for an export turns as its vertices did, so the page shows the view as the screen did
void turnRecorded(PageRecording* recording, ImVec2 pivot, float degrees) {
    if (recording == nullptr || recording->primitives.empty() || degrees == 0.0f) {
        return;
    }
    if (auto* image = std::get_if<RecordedImage>(&recording->primitives.back())) {
        image->turn   = degrees;
        image->pivotX = pivot.x;
        image->pivotY = pivot.y;
    }
}

/// what a reveal does to the vertices [from, end) of `list` at `progress` (0 to 1): `fade` brings their opacity up,
/// `rise` also lifts them into place from `lift` below, `wipe` uncovers them from the left edge of [left, left + width)
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

/// where a step's or a box's drawing starts in the draw list: its first vertex, and a command of its own, so a wipe
/// can cut it by its clip rectangles without cutting what was drawn before it
struct DrawnFrom {
    int vertex  = 0;
    int command = 0;
};

[[nodiscard]] DrawnFrom startDrawing(ImDrawList& list) {
    list.AddDrawCmd();
    return DrawnFrom{.vertex = list.VtxBuffer.Size, .command = list.CmdBuffer.Size - 1};
}

/**
 * Moves what was drawn since `start` to where it is `progress` of the way into place, the way `kind` arrives.
 *
 * `fade` raises its alpha; `rise` also brings it `lift` pixels from `from` (below by default); `grow` scales it from
 * 0.9 about its own centre; `wipe` moves an edge across it from `from` (the left by default) on the clip rectangles
 * of its commands, so a picture or a code block's background is cut as cleanly as its words.
 */
void arrive(ImDrawList& list, DrawnFrom start, std::string_view kind, std::string_view from, float progress, float lift, const DocumentView::ThroughEffect& through = {}, std::string_view key = {}) {
    const float eased = smoothstep(progress);
    ImVec2      low{FLT_MAX, FLT_MAX};
    ImVec2      high{-FLT_MAX, -FLT_MAX};
    for (const ImDrawVert& vertex : list.VtxBuffer | std::views::drop(start.vertex)) {
        low  = ImVec2{std::min(low.x, vertex.pos.x), std::min(low.y, vertex.pos.y)};
        high = ImVec2{std::max(high.x, vertex.pos.x), std::max(high.y, vertex.pos.y)};
    }
    if (low.x > high.x) {
        return; // nothing was drawn
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
        list.AddDrawCmd(); // what is drawn next keeps the clip rectangle it was given
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

/// scales what was drawn since `start` -- vertices, clip rectangles, and what an export, a text selection and the
/// links took of it -- by `factor` about `origin`, so a box drawn too large for its place fits it after all
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
                        recorded = RecordedText{.text = std::move(recorded.text), .face = recorded.face, .size = recorded.size * factor, .x = x(recorded.x), .y = y(recorded.y), .colour = recorded.colour, .clip = scaled(recorded.clip), .deckFace = std::move(recorded.deckFace)};
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
    list.AddDrawCmd(); // what is drawn next keeps the clip rectangle it was given, not the scaled one
}

/// the theme a pass paints with, faded to the view's opacity so a cross-fade needs no special case anywhere else
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

/// draws one inline run and returns the pen position after it, wrapping at `right`
struct InlinePen {
    Canvas                               canvas;
    ImVec2                               origin;
    ImVec2                               pen;
    float                                right      = 0.0f;
    float                                lineHeight = 0.0f;
    ImFont*                              face       = nullptr; // replaces the body face where one is wanted, which is how a table header is bold
    const DocumentView::FormulaLookup*   formulas   = nullptr;
    const std::vector<std::string>*      numbering  = nullptr; // referenced footnote labels, in the order they appear
    std::string_view                     linkTarget = {};      // while a link's words are drawn: where it leads
    std::vector<DocumentView::LinkArea>* links      = nullptr; // where a click on them is caught; none when nobody asks
    float                                baseSize   = 0.0f;    // the size of the words around a span set larger or smaller
    float                                lineGrowth = 0.0f;    // how far a larger span on this line reaches below it

    void newline(std::string_view separator = " ") {
        pen.x = origin.x;
        pen.y += lineHeight + lineGrowth;
        lineGrowth = 0.0f;
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith(separator); // a wrapped line reads on with a space; a `<br>` is a line break
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

    /// splits on spaces so wrapping happens between words rather than mid-word
    void run(std::string_view text, ImU32 colour, ImFont* font, float size) {
        std::size_t start = 0UZ;
        while (start < text.size()) {
            const auto space = text.find(' ', start);
            const auto end   = space == std::string_view::npos ? text.size() : space + 1UZ;
            word(text.substr(start, end - start), colour, font, size);
            start = end;
        }
    }

    /// a raised, smaller run: a footnote marker, which is what a superscript is for here
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

    /// emphasis, strong emphasis and code each get their own face, which is the only way they are visible
    void span(const InlineSpan& inlineSpan, const Theme& theme, float size) {
        baseSize             = size;
        ImFont* const font   = faceOf(inlineSpan, face);
        const float   spanSz = sizeOf(inlineSpan, size);
        switch (inlineSpan.kind) {
        case InlineKind::strong:
        case InlineKind::emphasis: run(inlineSpan.text, theme.text, font, spanSz); return;
        case InlineKind::code: run(inlineSpan.text, theme.fill, font, spanSz); return;
        case InlineKind::math: {
            const PlacedFormula* rendered = formulas != nullptr && *formulas ? (*formulas)(inlineSpan.text, spanSz, false) : nullptr;
            if (rendered == nullptr || rendered->texture.id == 0) {
                run(inlineSpan.text, theme.fill, font, spanSz); // the source, so the author can see what failed
                return;
            }
            formula(*rendered, inlineSpan.text, theme.text, spanSz);
            return;
        }
        case InlineKind::footnote: {
            const int number = numberOf(inlineSpan.target);
            superscript(number > 0 ? std::to_string(number) : std::string{"?"}, theme.fill, size); // an undefined label is visible, not silent
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

/// the width one cell would take on a single line, which is what decides how wide its column wants to be
[[nodiscard]] float naturalWidth(const std::vector<InlineSpan>& spans, float size, ImFont* face) {
    float width = 0.0f;
    for (const InlineSpan& span : spans) { // a formula is measured from its source, since its raster exists only while painting
        width += faceOf(span, face)->CalcTextSizeA(sizeOf(span, size), FLT_MAX, 0.0f, span.text.data(), span.text.data() + span.text.size()).x;
    }
    return width;
}

/// the widest single word of a cell, which is the one thing breaking lines cannot make narrower
[[nodiscard]] float widestWord(const std::vector<InlineSpan>& cell, float size, ImFont* face) {
    float widest = 0.0f;
    for (const InlineSpan& span : cell) {
        // measured in the face the span is drawn in: a symbol in `code` is set in the monospace face, which is
        // wider than the body's, and measuring it in the wrong one is how a cell comes out too big to fit
        ImFont* const font     = faceOf(span, face);
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

/**
 * Lays a table out inside [left, right] and returns the height it needs.
 *
 * Each column asks for the width of its widest cell on one line. If the columns together want more than there is,
 * every column gives up the same proportion and the cells that no longer fit wrap: shrinking one column alone
 * would make the table's shape depend on the order its content happens to be in. A column keeps its natural width
 * when everything fits, so a narrow table stays narrow rather than being stretched across the slide.
 *
 * `striped` washes every second body row, which a box asks for with `{striped}`. A row must be laid out before it
 * can be washed, since its height is whatever its tallest cell wrapped to, so a striped row is laid out twice: once
 * to measure and once to draw over the wash. Tables in a deck hold a few rows, and a shaded row that covers its own
 * words is not a table.
 */
float drawTable(Canvas canvas, const Theme& theme, const Block& table, float left, float right, float top, float size, const DocumentView::FormulaLookup& formulas, bool striped) {
    const std::size_t columns = table.columns.size();
    const std::size_t rows    = table.rowCount();
    if (columns == 0UZ || rows == 0UZ) {
        return 0.0f;
    }

    ImFont* const bold       = Fonts::instance().activeFaces().bold;
    const float   lineHeight = ImGui::GetTextLineHeight();
    const float   gap        = lineHeight * kTableColumnGap;

    std::vector<float> wanted(columns, 0.0f);
    for (std::size_t row = 0UZ; row < rows; ++row) {
        for (std::size_t column = 0UZ; column < columns; ++column) {
            wanted[column] = std::max(wanted[column], naturalWidth(table.cellAt(row, column), size, row == 0UZ ? bold : nullptr));
        }
    }

    const float asked     = std::accumulate(wanted.begin(), wanted.end(), 0.0f);
    const float available = right - left - gap * static_cast<float>(columns - 1UZ);

    // A column that wants no more than its fair share keeps what it asked for, and only the columns above that
    // share what is left, in proportion to what they asked. Shrinking every column by the same proportion instead
    // lets one very wide column wrap all the narrow ones, which is what makes a table unreadable.
    std::vector<float> width = wanted;
    if (asked > available) {
        const float fair   = available / static_cast<float>(columns);
        float       modest = 0.0f;
        float       greedy = 0.0f;
        for (const float column : wanted) {
            (column <= fair ? modest : greedy) += column;
        }
        const float leftOver = std::max(0.0f, available - modest);
        for (std::size_t column = 0UZ; column < columns; ++column) {
            if (wanted[column] > fair) {
                width[column] = greedy > 0.0f ? wanted[column] / greedy * leftOver : fair;
            }
        }
    }
    const float drawn = std::accumulate(width.begin(), width.end(), 0.0f) + gap * static_cast<float>(columns - 1UZ);

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

                // Breaking lines comes first and settles almost every cell. What it cannot settle is a single word
                // wider than its column -- a long identifier, a URL -- which would run into the column beside it.
                // Only that cell is made smaller, and only as far as it has to be, so the rest of the table keeps
                // the size it was set at.
                const float cellSize = std::max(size * std::min(1.0f, cellWidth / std::max(widestWord(cell, size, face), 1.0f)), size * kCellFloor);
                const float slack    = std::max(0.0f, cellWidth - naturalWidth(cell, cellSize, row == 0UZ ? bold : nullptr));
                // a cell that had to wrap has no slack, so it aligns left whatever the column asked for
                const float offset = table.columns[column] == Alignment::right ? slack : table.columns[column] == Alignment::centre ? slack * 0.5f : 0.0f;

                InlinePen pen{.canvas = into, .origin = ImVec2{x + offset, y}, .pen = ImVec2{x + offset, y}, .right = x + cellWidth, .lineHeight = lineHeight, .face = row == 0UZ ? bold : nullptr, .formulas = &formulas};
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
            const float pad      = lineHeight * kTableRowGap * 0.5f;
            canvas.filledRect(ImVec2{left, y - pad}, ImVec2{left + drawn, measured + pad}, dimmed(kStripeTint, row == 0UZ ? kHeaderWash : kStripeWash));
        }
        const float bottom = layRow(canvas);
        y                  = bottom + lineHeight * kTableRowGap;
        if (row == 0UZ && canvas) {
            const float rule = y - lineHeight * kTableRowGap * 0.5f;
            canvas.line(ImVec2{left, rule}, ImVec2{left + drawn, rule}, dimmed(theme.text, 0.4f));
        }
    }
    return y - top;
}

/// the byte length of the UTF-8 sequence at `begin`, clamped to what is left
/**
 * Lays a fenced block out inside [left, right] and returns the height it needs.
 *
 * A source line longer than the column is wrapped rather than allowed to run off the slide, and a continuation is
 * indented so it reads as one: code cannot be re-flowed at word boundaries the way prose can, because in code the
 * line break is part of the meaning. Called once with no canvas to size the panel, then again to paint into it.
 */
// `lineStarts`, when given, gets the first vertex of each source line drawn and, last, the end of the final one
float drawCode(Canvas canvas, const Theme& theme, const Block& block, float left, float right, float top, float size, float opacity, std::vector<int>* lineStarts = nullptr) {
    ImFont* const font = Fonts::instance().activeFaces().mono;

    const ImVec2 emBox      = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "M");
    const float  lineHeight = emBox.y;
    const float  padding    = lineHeight * kCodePadding;
    const float  textLeft   = left + padding;
    const float  textRight  = right - padding;
    const float  hanging    = textLeft + emBox.x * kCodeHangingIndent;

    float y = top + padding;
    for (const TokenLine& tokens : highlight(block.lines, languageOf(block.info))) {
        if (canvas && lineStarts != nullptr) {
            lineStarts->push_back(canvas.list->VtxBuffer.Size);
        }
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith("\n"); // a source line; a line wrapped to fit the panel copies as the one it is
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
                    if (!fresh) { // nothing fits in what is left of the line, so start a new one
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

[[nodiscard]] DocumentView::Placement placementFor(const Layout* layout, const Texture* backdrop, std::string_view backdropSource, std::span<const std::string> backdropHidden, const ImGuiViewport& viewport, Canvas canvas, float scale, const Rectangle& cameraFrame, float opacity, std::span<const std::string> claimed, float rotation) {
    // No master: the text is sized from the viewport itself, so a phone held either way up reads the same.
    if (layout == nullptr || layout->width <= 0.0f || layout->height <= 0.0f) {
        const float body  = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);
        const float width = DocumentView::columnWidth(viewport.Size.x, body);
        // the room is what is left below the top margin, less as much again at the foot: the height a pass is
        // allowed, not the height of the window, or content would be told it fits when its last line is off screen
        return DocumentView::Placement{.contentLeft = viewport.Pos.x + (viewport.Size.x - width) * 0.5f, .contentTop = viewport.Pos.y + body * kTitleTopEms, .contentWidth = width, .available = std::max(viewport.Size.y - body * (kTitleTopEms + kFooterClearEms), body), .bodyPixels = body, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
    }

    // A master drawn in three groups, `top`, `middle` and `bottom`, is composed to the screen rather than
    // letterboxed: the top and bottom bands keep their shape at the screen's width and are pinned to its edges, and
    // the middle band, background and padding, stretches to the height between them. So one drawing serves a screen
    // held either way up. The type is set from the screen's shape, as on a generated layout.
    const Area* splitTopArea    = layout->generated ? nullptr : layout->find("top");
    const Area* splitBottomArea = layout->generated ? nullptr : layout->find("bottom");
    if (splitTopArea != nullptr && splitBottomArea != nullptr && cameraFrame.width <= 0.0f && layout->find("content") != nullptr) {
        const float factor       = viewport.Size.x / layout->width;
        const float splitTop     = splitTopArea->y + splitTopArea->height;
        const float splitBottom  = splitBottomArea->y;
        const float screenBottom = viewport.Pos.y + viewport.Size.y;
        const float middle       = viewport.Size.y - (splitTop + layout->height - splitBottom) * factor;
        if (middle > 0.0f && splitBottom > splitTop) {
            DocumentView::Placement split{.contentLeft = 0.0f, .contentTop = 0.0f, .contentWidth = 0.0f, .available = 0.0f, .bodyPixels = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y), .titlePixels = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y), .title = {}, .footer = {}, .options = {}, .inset = 0.6f, .factor = factor, .originX = viewport.Pos.x, .originY = viewport.Pos.y, .splitTop = splitTop, .splitBottom = splitBottom, .splitHeight = layout->height, .middleFactor = middle / (splitBottom - splitTop), .screenBottom = screenBottom};
            if (canvas && backdrop != nullptr && backdrop->id != 0) {
                const ImU32 tint  = dimmed(IM_COL32_WHITE, opacity);
                const float right = viewport.Pos.x + viewport.Size.x;
                const auto  band  = [&](float fromY, float toY) { canvas.image(backdrop->id, ImVec2{viewport.Pos.x, split.mapY(fromY)}, ImVec2{right, split.mapY(toY)}, ImVec2{0.0f, fromY / layout->height}, ImVec2{1.0f, toY / layout->height}, tint, RecordedImageKind::master, backdropSource, 0.0f, false, backdropHidden); };
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

    // the camera frames part of the layout and that part fills the view; with no frame it is the whole document,
    // projected like a slide master. Interpolating the frame between two sections is the spatial move.
    const Rectangle framed = cameraFrame.width > 0.0f && cameraFrame.height > 0.0f ? cameraFrame : Rectangle{.x = 0.0f, .y = 0.0f, .width = layout->width, .height = layout->height};

    const Area* content = layout->find("content");
    if (content == nullptr && layout->generated) {
        // A grid need not have a box called `content`. Prose that is in no slot then goes in the first box that
        // nothing else has claimed -- where a reader looks first, and what other slide tools do with an unplaced
        // block. A box already holding a slot or a picture is skipped, or the two would be drawn over each other.
        const auto first = std::ranges::find_if(layout->areas, [claimed](const Area& area) { return area.id != "title" && area.id != "footer" && area.id != "slide_number" && area.width > 0.0f && std::ranges::find(claimed, area.id) == claimed.end(); });
        content          = first == layout->areas.end() ? nullptr : &*first;
    }

    // The author drew a picture, not a slide: there are anchors for the camera and no box for words. The heading
    // then takes a band along the top of the *screen* and the prose a band along its foot, the way a caption does,
    // both pinned to the view while the picture moves behind them.
    //
    // The bands are taken out before the drawing is letterboxed, so the picture sits below its title rather than
    // under it. They are sized from the type at the full scale, which can only reserve too much and never too
    // little, since a smaller drawing sets smaller type.
    // Only a drawing can be without a box for words. A grid whose every box is a named slot has none either, and
    // is not a drawing: its flow is simply empty, and it keeps the title and footer the grid gave it.
    const bool  caption = !layout->generated && (content == nullptr || content->width <= 0.0f);
    const float band    = caption ? Fonts::slideBodySize(viewport.Size.x, viewport.Size.y) : 0.0f; // the deck's ladder, so a caption reads like any other slide
    const float above   = caption ? band * (titleHeadingEms() + kTitleAirEms * 0.5f) : 0.0f;
    const float below   = caption ? std::min(viewport.Size.y * kCaptionShare, band * kCaptionLines) + band : 0.0f;
    const float room    = std::max(viewport.Size.y - above - below, 1.0f);

    // An anchor is where the author drew the thing, so a frame exactly on its outline cuts off the strokes and the
    // labels that reach past it. A drawing under a caption is shown with air around the anchor instead.
    const Rectangle frame = caption && cameraFrame.width > 0.0f && cameraFrame.height > 0.0f ? Rectangle{.x = framed.x - framed.width * kAnchorAir, .y = framed.y - framed.height * kAnchorAir, .width = framed.width * (1.0f + 2.0f * kAnchorAir), .height = framed.height * (1.0f + 2.0f * kAnchorAir)} : framed;

    // A frame that is part of a drawing is shown large enough for the drawing to cover the view, which leaves no
    // letterbox bars of the wrong colour beside it. The whole drawing is still shown whole, and the two blend as
    // the frame grows, so a move between them does not jump.
    const float fitted   = std::min(viewport.Size.x / frame.width, room / frame.height);
    const float covering = std::max(viewport.Size.x / layout->width, room / layout->height);
    const float partial  = std::clamp((1.0f - std::min(frame.width / layout->width, frame.height / layout->height)) * 4.0f, 0.0f, 1.0f);
    const float factor   = fitted + std::max(covering - fitted, 0.0f) * partial;
    // The camera keeps to the drawing: a frame near an edge slides inwards rather than showing what lies beyond
    // it. An axis on which the drawing is smaller than the view stays centred. Under a caption the view is the room
    // between title and words, not the window, or a frame near the drawing's top would slide behind the title.
    // A drawing that turns turns about the middle of the room, so the frame has to stay there: the keeping-in is
    // given up in proportion to the turn, which lets a move that starts upright begin without a jump.
    const float turned  = std::clamp(std::abs(rotation) / 90.0f, 0.0f, 1.0f);
    const auto  kept    = [turned](float wanted, float from, float extent, float drawn) { return std::lerp(drawn >= extent ? std::clamp(wanted, from + extent - drawn, from) : wanted, wanted, turned); };
    const float originX = kept(viewport.Pos.x + viewport.Size.x * 0.5f - (frame.x + frame.width * 0.5f) * factor, viewport.Pos.x, viewport.Size.x, layout->width * factor);
    const float originY = kept(viewport.Pos.y + above + room * 0.5f - (frame.y + frame.height * 0.5f) * factor, viewport.Pos.y + above, room, layout->height * factor);

    if (canvas && backdrop != nullptr && backdrop->id != 0) {
        const ImU32 tint = dimmed(IM_COL32_WHITE, opacity);
        // a drawing under a caption is cut off at the room it was given, so it never runs behind the title or the words
        if (caption) {
            canvas.list->PushClipRect(ImVec2{viewport.Pos.x, viewport.Pos.y + above}, ImVec2{viewport.Pos.x + viewport.Size.x, viewport.Pos.y + above + room}, true);
        }
        const int turnFrom = canvas.list->VtxBuffer.Size;
        canvas.image(backdrop->id, ImVec2{originX, originY}, ImVec2{originX + layout->width * factor, originY + layout->height * factor}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, tint, RecordedImageKind::master, backdropSource, 0.0f, false, backdropHidden);
        if (caption) {
            // only a drawing under a caption turns: its words are pinned to the view, so nothing on it needs to stay upright
            const ImVec2 pivot{viewport.Pos.x + viewport.Size.x * 0.5f, viewport.Pos.y + above + room * 0.5f};
            turnVertices(*canvas.list, turnFrom, pivot, rotation);
            turnRecorded(canvas.recording, pivot, rotation);
            canvas.list->PopClipRect();
        }
    }

    // With a drawn master the authored proportions win: the master is letterboxed into the window and the type
    // scales with it, so a drawing and its labels keep the relationship the author drew. A generated layout has
    // no such proportions -- it was made to fit this viewport -- so its type comes from the screen's own shape,
    // which is what keeps a heading the same size on every slide of a deck.
    const float masterBody = layout->generated ? Fonts::slideBodySize(viewport.Size.x, viewport.Size.y) : Fonts::bodySize(layout->height * factor);

    if (caption) {
        // sized from `band` rather than from the drawing: the words are pinned to the view, so they keep the size
        // they would have had if the picture had filled it, and only the picture gives way to make room for them
        const float     width = DocumentView::columnWidth(viewport.Size.x, band);
        const float     left  = viewport.Pos.x + (viewport.Size.x - width) * 0.5f;
        const Rectangle heading{.x = left, .y = viewport.Pos.y, .width = width, .height = band * titleHeadingEms()};
        return DocumentView::Placement{.contentLeft = left, .contentTop = viewport.Pos.y + viewport.Size.y - below, .contentWidth = width, .available = below - band, .bodyPixels = band, .title = heading, .footer = {}, .options = BoxOptions{.bottom = true}, .factor = factor, .originX = originX, .originY = originY};
    }
    (void)scale;
    // an author draws the box where content should sit, not where glyphs should touch its edge
    // A generated layout already puts its boxes on the margin its grid boxes use, so the title and a single column
    // share the left edge of the boxes beside them; only a drawn master needs the air.
    const float inset  = masterBody * 0.5f;
    const float insetX = layout->generated ? 0.0f : inset;
    const float insetY = layout->generated ? 0.0f : inset;
    const auto  exact  = [originX, originY, factor](const Area* area) { return area == nullptr ? Rectangle{} : Rectangle{.x = originX + area->x * factor, .y = originY + area->y * factor, .width = area->width * factor, .height = area->height * factor}; }; // a mark, not a text box: no inset
    const auto  mapped = [originX, originY, factor, insetY, insetX](const Area* area) { return area == nullptr ? Rectangle{} : Rectangle{.x = originX + area->x * factor + insetX, .y = originY + area->y * factor + insetY, .width = area->width * factor - insetX * 2.0f, .height = area->height * factor - insetY * 2.0f}; };
    // Every box gets its whole width, prose included. A measure of about forty-six ems was capped here, which is
    // where a line stops being comfortable to read; the author asked for the width instead, and a cap that is
    // wanted again belongs on the box that wants it rather than on every generated layout in the deck.
    const Rectangle body = mapped(content);
    return DocumentView::Placement{.contentLeft = body.x, .contentTop = body.y, .contentWidth = body.width, .available = body.height, .bodyPixels = masterBody, .titlePixels = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y), .title = mapped(layout->find("title")), .footer = mapped(layout->find("footer")), .slideNumber = exact(layout->find("slide_number")), .options = {}, .inset = layout->generated ? 0.0f : 0.6f, .factor = factor, .originX = originX, .originY = originY};
}

} // namespace
/// a live region's box shared out between its charts and the bars it asks for: `here` takes a strip of the box, any
/// other name the layout area of that name, and an area the layout does not have a strip as well
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
            withoutArea.push_back(std::format("{}/{}", block.id.empty() ? block.info : block.id, asked));
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
// namespace

DocumentView::CameraGeometry DocumentView::cameraGeometry(float reservedTop, const Rectangle& wantedUv, const CameraMove* move, float reservedBottom) const {
    if (cameraImage == nullptr || cameraImage->id == 0 || cameraImage->width <= 0 || cameraImage->height <= 0) {
        return CameraGeometry{};
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          body     = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);

    // One composition, whatever the camera's scope: the picture gets the room the words leave, so a slide-scope
    // stop starts from the same picture in the same place as an image-scope one. `slide` then moves and magnifies
    // that composition whole, which the caller does rather than this laying the slide out differently.
    const Rectangle box{.x = viewport.Pos.x, .y = viewport.Pos.y + reservedTop, .width = viewport.Size.x, .height = std::max(viewport.Size.y - reservedTop - reservedBottom - body * 1.2f, 1.0f)};

    const float     imageAspect  = static_cast<float>(cameraImage->width) / static_cast<float>(cameraImage->height);
    const Rectangle wanted       = wantedUv.width > 0.0f && wantedUv.height > 0.0f ? wantedUv : Rectangle{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
    const float     screenAspect = box.width / box.height;

    if (move != nullptr) {
        // a move is drawn unclamped, so it may show a margin past the picture; only the part of the texture that
        // exists is sampled, and it lands where the frame puts it
        const Rectangle frame = cameraFrameAt(move->from, move->via, move->to, move->progress, screenAspect, imageAspect);
        const float     left  = std::max(frame.x, 0.0f);
        const float     top   = std::max(frame.y, 0.0f);
        const float     right = std::min(frame.x + frame.width, 1.0f);
        const float     below = std::min(frame.y + frame.height, 1.0f);
        if (right <= left || below <= top) {
            return CameraGeometry{.box = box, .drawn = Rectangle{}, .uv = Rectangle{}, .frame = frame};
        }
        const auto onScreenX = [&](float u) { return box.x + (u - frame.x) / frame.width * box.width; };
        const auto onScreenY = [&](float v) { return box.y + (v - frame.y) / frame.height * box.height; };
        return CameraGeometry{.box = box, .drawn = Rectangle{.x = onScreenX(left), .y = onScreenY(top), .width = onScreenX(right) - onScreenX(left), .height = onScreenY(below) - onScreenY(top)}, .uv = Rectangle{.x = left, .y = top, .width = right - left, .height = below - top}, .frame = frame};
    }

    const Rectangle uv = fittedToAspect(wanted, screenAspect, imageAspect);

    // The frame usually has the box's shape exactly, because that is what fitting it did. It cannot when the
    // picture ran out -- a very wide region on a phone held upright -- and then the picture is centred in the
    // box rather than stretched, because a stretched photograph is worse than a margin.
    const float framedAspect = uv.width * imageAspect / uv.height;
    const float drawnWidth   = std::min(box.width, box.height * framedAspect);
    const float drawnHeight  = drawnWidth / framedAspect;

    return CameraGeometry{.box = box, .drawn = Rectangle{.x = box.x + (box.width - drawnWidth) * 0.5f, .y = box.y + (box.height - drawnHeight) * 0.5f, .width = drawnWidth, .height = drawnHeight}, .uv = uv, .frame = uv};
}

Rectangle DocumentView::CameraGeometry::onScreen(const Rectangle& part) const noexcept {
    if (uv.width <= 0.0f || uv.height <= 0.0f) {
        return Rectangle{};
    }
    const float scaleX = drawn.width / uv.width;
    const float scaleY = drawn.height / uv.height;
    return Rectangle{.x = drawn.x + (part.x - uv.x) * scaleX, .y = drawn.y + (part.y - uv.y) * scaleY, .width = part.width * scaleX, .height = part.height * scaleY};
}

void DocumentView::drawInfoBox(const Theme& theme, const Rectangle& face, const Rectangle& screen) {
    if (infoBox.opacity <= 0.0f || infoBox.contents.blocks.empty() || face.width <= 0.0f) {
        return;
    }
    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    const float          body     = Fonts::slideBodySize(viewport.Size.x, viewport.Size.y);
    const float          pad      = body * kInfoBoxPaddingEms;
    const float          gap      = body * kInfoBoxGapEms;

    // Beside the region, as wide as the roomier side allows within a band that keeps a few short lines a few short
    // lines. Where neither side holds a readable box -- a portrait screen, where the region takes the whole width --
    // it goes above or below at up to the screen's width, which also keeps it short enough to fit there.
    const float readable = body * kInfoBoxReadableEms;
    const float beside   = std::max(face.x - screen.x, screen.x + screen.width - (face.x + face.width)) - 2.0f * gap;
    const float width    = beside >= readable ? std::clamp(beside, std::max(screen.width * kInfoBoxMinShare, readable), std::max(screen.width * kInfoBoxMaxShare, readable)) : std::min(screen.width - 2.0f * gap, body * kInfoBoxWideEms);
    const Theme shown    = fadedTo(theme, opacity * infoBox.opacity);
    const auto  drawBox  = [&](const Document& contents, std::string_view side) {
        // measured with its heading, which `heightOf` leaves out because a slide's heading lives in the title band
        const Document outerDocument = document;
        document                     = contents;
        const float     text         = layoutPass(shown, 1.0f, false, Placement{.contentLeft = 0.0f, .contentTop = 0.0f, .contentWidth = width - 2.0f * pad, .available = screen.height, .bodyPixels = body, .title = {}, .footer = {}, .options = {}, .inset = 0.0f}, Selection::all);
        const Rectangle box          = besideRegion(face, screen, width, text + 2.0f * pad, side, gap);

        const Canvas canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        canvas.filledRect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, dimmed(theme.background, kInfoBoxBackdrop * opacity * infoBox.opacity), kPlaceholderRounding);
        canvas.rect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, dimmed(theme.text, kInfoBoxFrame * opacity * infoBox.opacity), kPlaceholderRounding, 0, kInfoBoxFrameWidth);
        const Placement inside{.contentLeft = box.x + pad, .contentTop = box.y + pad, .contentWidth = box.width - 2.0f * pad, .available = box.height - 2.0f * pad, .bodyPixels = body, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
        layoutPass(shown, 1.0f, true, inside, Selection::all);
        document = outerDocument;
        return box;
    };
    const Rectangle box = drawBox(infoBox.contents, infoBox.side);
    if (!infoBox.aside.blocks.empty()) {
        // the side the caption did not take, so the two frame what they describe between them
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
    const Canvas into{ImGui::GetWindowDrawList(), recording, &textRuns};
    const ImU32  tint = dimmed(IM_COL32_WHITE, opacityNow);
    into.list->PushClipRect(ImVec2{clip.x, clip.y}, ImVec2{clip.x + clip.width, clip.y + clip.height}, true);
    const int turnFrom = into.list->VtxBuffer.Size;
    into.image(cameraImage->id, ImVec2{geometry.drawn.x, geometry.drawn.y}, ImVec2{geometry.drawn.x + geometry.drawn.width, geometry.drawn.y + geometry.drawn.height}, //
        ImVec2{geometry.uv.x, geometry.uv.y}, ImVec2{geometry.uv.x + geometry.uv.width, geometry.uv.y + geometry.uv.height}, tint, RecordedImageKind::picture, cameraSource);
    if (cameraScope == CameraScope::image) {
        const ImVec2 pivot{geometry.box.x + geometry.box.width * 0.5f, geometry.box.y + geometry.box.height * 0.5f};
        turnVertices(*into.list, turnFrom, pivot, cameraRotation);
        turnRecorded(recording, pivot, cameraRotation);
    }
    into.list->PopClipRect();
    return geometry.drawn;
}

float DocumentView::columnWidth(float viewportWidth, float bodyPixels) noexcept {
    // three ems of margin, but never more than a small share of a narrow window: on a phone three ems a side eats a
    // quarter of the screen and leaves under forty characters on a line
    const float margin = std::min(kColumnMarginEms * 0.5f * bodyPixels, kColumnMarginShare * viewportWidth);
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
            overlay += std::format(" {}={}", std::string_view{key}.substr(8UZ), value);
        }
    }
    return BoxOptions{.shrink = flag("shrink", true), .centre = flag("centre", false), .striped = flag("striped", false), .notes = flag("notes", true), .bottom = flag("bottom", false), .frame = flag("frame", false), .left = flag("left", false), .fit = flag("fit", false), .slideNotes = block.field("notes") == "all", .size = parseTypeSize(block.field("size")), .font = std::string{block.field("font")}, .in = std::string{block.field("in")}, .inSeconds = inSeconds && *inSeconds > 0.0f ? inSeconds : std::nullopt, .afterSeconds = std::max(afterSeconds, 0.0f), .inFrom = std::string{block.field("from")}, .overlay = std::move(overlay)};
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
    return placementFor(layout, layoutBackdrop, layoutSource, layoutHidden, *viewport, Canvas{paint ? ImGui::GetWindowDrawList() : nullptr, recording, &textRuns}, 1.0f, cameraFrame, opacity, claimed, cameraRotation);
}

float DocumentView::heightOf(const Theme& theme, const Document& contents, float width, float bodyPixels, float room, const BoxOptions& options) {
    const Document outer = document;
    document             = contents;
    Placement box{.contentLeft = 0.0f, .contentTop = 0.0f, .contentWidth = width, .available = room, .bodyPixels = bodyPixels, .title = {}, .footer = {}, .options = options, .inset = 0.0f};
    sizedAsSaid(box, slideSize); // measured at the size it is drawn at: the slide's, then the box's own
    sizedAsSaid(box, options.size);
    const float needed = layoutPass(theme, 1.0f, false, box, Selection::exceptHeading);
    document           = outer;
    // a row is followed by a gutter, so the space its last block leaves after itself is not part of what it needs:
    // counted, a row sized to its words came out a gap taller than they are
    return needed - trailingGap;
}

/// every named area of the slide's layout as a thin frame with its name in its corner: where a master or a grid puts
/// what, for the author laying a slide out and for the slide that explains masters
void DocumentView::drawAreaOutlines(const Theme& theme, const Placement& where) const {
    ImDrawList& list  = *ImGui::GetWindowDrawList();
    const ImU32 frame = dimmed(theme.fill, 0.9f);
    const float size  = where.bodyPixels * 0.55f;
    for (const Area& area : layout->areas) {
        if (area.id.empty()) {
            continue;
        }
        const Rectangle box = where.map(area);
        list.AddRect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, frame, 0.0f, ImDrawFlags_None, 1.5f);
        const std::string label = area.kind.empty() ? area.id : area.id + " (" + area.kind + ")";
        // in the corner where text least often is, so it names the box without covering what is in it
        const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
        list.AddText(ImGui::GetFont(), size, ImVec2{box.x + box.width - extent.x - size * 0.3f, box.y + box.height - extent.y - size * 0.15f}, frame, label.c_str());
    }
}

void DocumentView::draw(const Theme& theme) {
    drawnBlocks.clear();
    minimumScale = Fonts::instance().scale.floor / Fonts::instance().scale.body;
    placedRegions.clear();
    clipped.clear();
    barsWithoutArea.clear();
    videoButtons.clear();
    linkAreas.clear();
    textRuns.clear();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
    const ScopedWindow         scene(windowName, nullptr, flags);
    drawnInto = ImGui::GetWindowDrawList();
    if (beforeDrawing) {
        beforeDrawing(*drawnInto);
    }
    const struct AfterDrawing {
        const std::function<void(ImDrawList&)>& after;
        ImDrawList&                             list;
        ~AfterDrawing() {
            if (after) {
                after(list);
            }
        }
    } afterDrawn{.after = afterDrawing, .list = *drawnInto};

    const Theme faded = fadedTo(theme, opacity);
    infoBoxDrawn      = Rectangle{};
    infoFaceDrawn     = Rectangle{};
    cameraFrameDrawn  = Rectangle{};

    // The words are measured before the picture is placed, so a section keeps the room it actually needs and the
    // picture takes the rest. Reserving a fixed band instead crushed the prose to a grey smear on a full slide.
    float wordsNeed = 0.0f;
    float notesNeed = 0.0f;
    if (cameraImage != nullptr) {
        // measured for both scopes, because both start from the same composition: the words keep the room they
        // need and the picture takes the rest, and `slide` then moves that whole arrangement rather than a
        // different one. Reserving a fixed band instead crushed the prose to a grey smear on a full slide.
        // a slide's notes go beneath the picture, the citation of a photograph being its caption
        Placement probe = bodyPlacement(false);
        sizedAsSaid(probe, slideSize, true);
        const float withNotes = layoutPass(faded, 1.0f, false, probe, Selection::all);
        probe.options.notes   = false;
        const float without   = layoutPass(faded, 1.0f, false, probe, Selection::all);
        notesNeed             = std::max(withNotes - without, 0.0f);
        wordsNeed             = std::min(layoutPass(faded, 1.0f, false, probe, Selection::all) + probe.bodyPixels, viewport->Size.y * 0.45f);
    }

    Placement where = bodyPlacement(true); // this is what draws the backdrop
    sizedAsSaid(where, slideSize, true);
    if (afterBackdrop) {
        afterBackdrop(*drawnInto);
    }
    const bool split = where.title.width > 0.0f;

    // the picture goes down after the backdrop and before the words, so the words read over it rather than under
    Rectangle picture;
    float     reservedTop = 0.0f;
    Rectangle infoFace;   // the region a stop's box describes, on screen
    Rectangle infoScreen; // the room the box has to stay inside
    Rectangle notesUnder; // where the notes start beneath the picture, and how wide they may run
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
            // `slide` is a magnifying glass over the whole slide. The composition is the image scope's own -- the
            // words where they would be, the whole picture in the room they leave -- and one transform is then
            // applied to all of it, so the words travel and scale with the scenery and can leave the view. With no
            // region the transform is the identity, which is what makes a stop without one indistinguishable from
            // an image-scope stop; a region is magnified about its centre until it fills the picture's box.
            const CameraGeometry whole = cameraGeometry(reservedTop, Rectangle{}, nullptr, notesNeed);

            struct Glass {
                float factor  = 1.0f;
                float offsetX = 0.0f;
                float offsetY = 0.0f;
            };
            const auto glassFor = [&](const Rectangle& region) {
                const CameraGeometry framed = cameraGeometry(reservedTop, region, nullptr, notesNeed);
                // the region fills the screen's width, as it does under the image scope: the glass magnifies the
                // picture's drawn width, which is narrower than the screen wherever the picture is pillarboxed
                const float  uvRatio = framed.frame.width > 0.0f ? whole.frame.width / framed.frame.width : 1.0f;
                const float  ratio   = region.width > 0.0f && whole.drawn.width > 0.0f ? uvRatio * whole.box.width / whole.drawn.width : uvRatio;
                const ImVec2 centre{whole.drawn.x + whole.drawn.width * 0.5f, whole.drawn.y + whole.drawn.height * 0.5f};
                const ImVec2 aimed{whole.drawn.x + (framed.frame.x + framed.frame.width * 0.5f - whole.frame.x) / whole.frame.width * whole.drawn.width, whole.drawn.y + (framed.frame.y + framed.frame.height * 0.5f - whole.frame.y) / whole.frame.height * whole.drawn.height};
                // no region is exactly the identity, not the identity to within rounding, or a stop would differ from
                // an image-scope one by a row of pixels at the picture's edge
                constexpr float kNoise = 1e-3f;
                if (std::abs(ratio - 1.0f) < kNoise && std::abs(aimed.x - centre.x) < kNoise && std::abs(aimed.y - centre.y) < kNoise) {
                    return Glass{};
                }
                const float offsetX = centre.x - aimed.x * ratio;
                const float offsetY = centre.y - aimed.y * ratio;
                // the glass keeps to the picture: where the magnified picture is larger than the screen it is slid until
                // it covers it, rather than showing the black beyond its edge
                const auto  kept   = [](float wanted, float from, float extent, float drawn) { return drawn >= extent ? std::clamp(wanted, from + extent - drawn, from) : wanted; };
                const float shiftX = kept(whole.drawn.x * ratio + offsetX, screen.x, screen.width, whole.drawn.width * ratio) - (whole.drawn.x * ratio + offsetX);
                const float shiftY = kept(whole.drawn.y * ratio + offsetY, screen.y, screen.height, whole.drawn.height * ratio) - (whole.drawn.y * ratio + offsetY);
                return Glass{.factor = ratio, .offsetX = offsetX + shiftX, .offsetY = offsetY + shiftY};
            };

            Glass glass = glassFor(cameraUv);
            if (moving != nullptr) {
                // A move is made between the stops' own glasses, each settled as at rest, by moving the window of the
                // slide the glass shows -- the same thing the image scope moves -- so the words take the same zoom,
                // travel and zoom as the picture. Clamping each frame of the path to the picture would make it stick
                // and jump, exactly as it does for the image scope.
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
            // the part of the picture on screen, in its own fractions, which is what the image scope reports too
            const float shownX = (screen.x - glass.offsetX) / glass.factor;
            const float shownY = (screen.y - glass.offsetY) / glass.factor;
            cameraFrameDrawn   = Rectangle{.x = whole.uv.x + (shownX - whole.drawn.x) / whole.drawn.width * whole.uv.width, .y = whole.uv.y + (shownY - whole.drawn.y) / whole.drawn.height * whole.uv.height, .width = screen.width / glass.factor / whole.drawn.width * whole.uv.width, .height = screen.height / glass.factor / whole.drawn.height * whole.uv.height};
            const ImVec2 below = moved(whole.drawn.x, whole.drawn.y + whole.drawn.height);
            notesUnder         = Rectangle{.x = below.x, .y = below.y, .width = whole.drawn.width * glass.factor, .height = 0.0f};
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
        // bodyPixels is what every other size is a multiple of, this heading's included; leaving it at zero drew
        // the title at nothing and let the atlas substitute, which is why a master's heading came out tiny
        const bool drawn = layout != nullptr && !layout->generated;
        Placement  titleArea{.contentLeft = where.title.x, .contentTop = where.title.y, .contentWidth = where.title.width, .available = where.title.height, .bodyPixels = where.titlePixels > 0.0f ? where.titlePixels : where.bodyPixels, .title = {}, .footer = {}, .options = {}, .inset = 0.0f};
        // A title never shrinks to make room for the slide's content: a deck whose headings are all one size reads
        // as one deck, and one whose headings are each sized to their own length reads as a series of posters. So
        // a generated layout wraps a long heading inside its band and, past that, clips it.
        //
        // A drawn master is no different: the heading is the deck's size, and the box the author drew says where it
        // sits. A box too shallow for one line at that size lends it the air above and below instead.
        const float titleNeeded = layoutPass(faded, 1.0f, false, titleArea, Selection::headingOnly);
        const float lent        = drawn ? std::max(titleNeeded - where.title.height, 0.0f) * 0.5f : 0.0f;
        titleArea.contentTop -= lent;
        titleArea.available += 2.0f * lent;
        const Canvas canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        canvas.list->PushClipRect(ImVec2{where.title.x, where.title.y - lent}, ImVec2{where.title.x + where.title.width, where.title.y + where.title.height + lent}, true);
        layoutPass(faded, 1.0f, true, titleArea, Selection::headingOnly);
        canvas.list->PopClipRect();
    }

    const bool notesBeneath = notesNeed > 0.0f && picture.width > 0.0f;
    if (notesBeneath) {
        where.options.notes = false;
    }
    const Selection body    = split ? Selection::exceptHeading : Selection::all;
    const float     needed  = layoutPass(faded, 1.0f, false, where, body);
    appliedScale            = fittingScale(faded, where, body, needed);
    const float drawnHeight = needed > where.available ? layoutPass(faded, appliedScale, false, where, body) : needed;
    const bool  atFloor     = drawnHeight - trailingGap > where.available + kFitTolerance;
    if (where.options.bottom) {
        where.contentTop += std::max(0.0f, where.available - drawnHeight);
    }
    // what still does not fit at the floor is cut off at the bottom of the body, sideways left alone
    if (atFloor) {
        ImGui::GetWindowDrawList()->PushClipRect(ImVec2{0.0f, where.contentTop}, ImVec2{ImGui::GetIO().DisplaySize.x, where.contentTop + where.available}, true);
        clipped.push_back(Overrun{.id = "content", .wanted = drawnHeight, .available = where.available, .shrank = true});
    }
    layoutPass(faded, appliedScale, true, where, body);
    if (atFloor) {
        ImGui::GetWindowDrawList()->PopClipRect();
    }
    if (notesBeneath) {
        // the notes start at the picture's lower edge and keep to its width, as a caption does, and under `slide`
        // travel and scale with it
        Placement under{.contentLeft = notesUnder.x, .contentTop = notesUnder.y, .contentWidth = notesUnder.width, .available = 0.0f, .bodyPixels = where.bodyPixels, .title = {}, .footer = {}, .options = where.options, .inset = 0.0f};
        under.options.notes = true;
        layoutPass(faded, 1.0f, true, under, Selection::notesOnly);
    }

    // a `:::<name>` block puts prose in the box of that name: the same layout pass as the main flow, run into a
    // box of its own, so a two-column slide is two ordinary columns rather than a second way of writing one
    slideNotes.clear();
    for (const AreaDocument& entry : areaDocuments) {
        for (const Block& block : entry.contents.blocks) {
            for (const InlineSpan& span : block.spans) {
                if (block.step <= step && span.kind == InlineKind::footnote && std::ranges::find(slideNotes, span.target) == slideNotes.end()) {
                    slideNotes.push_back(span.target);
                }
            }
        }
    }
    for (const AreaDocument& entry : areaDocuments) {
        const Area* area = layout == nullptr ? nullptr : layout->find(entry.id);
        if (area == nullptr) {
            continue;
        }
        const Rectangle framed = where.map(*area);
        // a framed box is drawn, and its words keep clear of the border by a little less than half a line
        const float     padding = entry.options.frame ? where.bodyPixels * kFramePaddingEms : 0.0f;
        const Rectangle box{.x = framed.x + padding, .y = framed.y + padding, .width = framed.width - 2.0f * padding, .height = framed.height - 2.0f * padding};
        if (entry.options.frame) {
            ImDrawList& list = *ImGui::GetWindowDrawList();
            list.AddRectFilled(ImVec2{framed.x, framed.y}, ImVec2{framed.x + framed.width, framed.y + framed.height}, faded.background, kPlaceholderRounding);
            list.AddRect(ImVec2{framed.x, framed.y}, ImVec2{framed.x + framed.width, framed.y + framed.height}, dimmed(faded.text, 0.45f), kPlaceholderRounding, ImDrawFlags_None, 1.5f);
        }
        Placement slot{.contentLeft = box.x, .contentTop = box.y, .contentWidth = box.width, .available = box.height, .bodyPixels = where.bodyPixels, .title = {}, .footer = entry.options.slideNotes ? where.footer : Rectangle{}, .options = entry.options, .inset = 0.0f};
        sizedAsSaid(slot, entry.options.size);
        const Document outer = document;
        document             = entry.contents;

        const float wanted   = layoutPass(faded, 1.0f, false, slot, Selection::all);
        const bool  overruns = wanted - trailingGap > slot.available;
        // Content that does not fit is scaled down, unless the author fixed the size: then the box keeps the
        // shape they designed, what does not fit is cut off at its edge, and the box is named in the problems
        // list. A slide that quietly loses its last line is worse than one that says it did.
        const float scale = overruns && entry.options.shrink ? fittingScale(faded, slot, Selection::all, wanted) : 1.0f;
        if (overruns && !entry.options.shrink) {
            clipped.push_back(Overrun{.id = entry.id, .wanted = wanted, .available = slot.available, .shrank = false});
        }
        if (entry.options.centre) {
            slot.contentTop += std::max(0.0f, slot.available - wanted * scale) * 0.5f;
        } else if (entry.options.bottom) {
            slot.contentTop += std::max(0.0f, slot.available - wanted * scale);
        }
        // A box that was told not to shrink is clipped at its edge at once; one that may shrink is clipped only when
        // it has reached the floor and still does not fit, and either way the box is named in the problems list.
        const float  boxHeight  = overruns && entry.options.shrink ? layoutPass(faded, scale, false, slot, Selection::all) : wanted * scale;
        const bool   boxAtFloor = overruns && entry.options.shrink && boxHeight - trailingGap > slot.available + kFitTolerance;
        const bool   boxFits    = boxAtFloor && entry.options.fit; // drawn at the floor, then made smaller still
        const Canvas into{ImGui::GetWindowDrawList(), recording, &textRuns};
        if (!entry.options.shrink || (boxAtFloor && !boxFits)) {
            into.list->PushClipRect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, true);
        }
        const float     boxSeconds  = entry.options.inSeconds.value_or(revealSeconds ? revealSeconds(entry.options.in) : kRevealSeconds);
        const float     boxProgress = (arrivedSeconds - entry.options.afterSeconds) / boxSeconds;
        const bool      boxArrives  = arriving && !entry.options.in.empty() && boxProgress < 1.0f;
        const bool      overlaid    = !entry.options.overlay.empty() && entry.options.overlay != "none";
        const DrawnFrom overlayFrom = overlaid ? startDrawing(*into.list) : DrawnFrom{};
        const DrawnFrom boxFrom     = boxArrives ? startDrawing(*into.list) : DrawnFrom{};
        const DrawnFrom fitFrom     = boxFits ? startDrawing(*into.list) : DrawnFrom{};
        const auto      marks       = std::array{recording != nullptr ? recording->primitives.size() : 0UZ, textRuns.runs.size(), linkAreas.size()};
        layoutPass(faded, scale, true, slot, Selection::all);
        if (boxFits) {
            scaleDrawn(*into.list, fitFrom, recording, marks[0], textRuns, marks[1], linkAreas, marks[2], ImVec2{box.x, slot.contentTop}, slot.available / std::max(boxHeight - trailingGap, 1.0f));
        }
        if (boxArrives) {
            arrive(*into.list, boxFrom, entry.options.in, entry.options.inFrom, std::clamp(boxProgress, 0.0f, 1.0f), slot.bodyPixels * kRiseEms, throughEffect, std::format("box:{}", entry.id));
        }
        if (overlaid && throughEffect) {
            std::ignore = throughEffect(*into.list, overlayFrom.command, entry.options.overlay, std::format("overlay:{}", entry.id), 1.0f, ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height});
        }
        if (!entry.options.shrink || (boxAtFloor && !boxFits)) {
            into.list->PopClipRect();
        }
        if (boxAtFloor && !boxFits) {
            clipped.push_back(Overrun{.id = entry.id, .wanted = boxHeight, .available = slot.available, .shrank = true});
        }
        document = outer;
    }
    if (outlineAreas && layout != nullptr) {
        drawAreaOutlines(faded, where);
    }

    // an author may fill any other named area with an image or a drawing, which the flow never sees
    for (const auto& [id, files] : areaContent) {
        const SchemeFiles schemeFiles = schemeFilesOf(files);
        const Area*       area        = layout == nullptr ? nullptr : layout->find(id);
        // the variant for what the picture will stand on: the master's own brightness there where it paints the
        // area, else the scheme's background
        const bool        darkBehind = area != nullptr && area->backdropLight >= 0.0f ? area->backdropLight < kDarkBackdrop : faded.scheme == ColourScheme::dark;
        const std::string reference{darkBehind ? schemeFiles.dark : schemeFiles.light};
        const Texture*    art = imageFor ? imageFor(reference) : nullptr;
        if (area == nullptr) {
            continue;
        }
        const Rectangle box = where.map(*area);
        const Canvas    into{ImGui::GetWindowDrawList(), recording, &textRuns};
        const ImU32     tint = dimmed(IM_COL32_WHITE, opacity);
        if (art != nullptr && art->id != 0) {
            // fitted inside the box, keeping its aspect: an author's box is a frame, not a stretch target
            const float  factor = std::min(box.width / art->width, box.height / art->height);
            const ImVec2 size{art->width * factor, art->height * factor};
            const ImVec2 origin{box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f};
            into.image(art->id, origin, ImVec2{origin.x + size.x, origin.y + size.y}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, tint, imageKindOf(reference), reference);
        } else {
            into.rect(ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, dimmed(faded.text, 0.35f), kPlaceholderRounding);
            const std::string label = "missing: " + reference;
            into.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{box.x + 6.0f, box.y + 6.0f}, dimmed(faded.text, 0.6f), label.c_str());
        }
    }

    if (where.slideNumber.width > 0.0f && !footerNumber.empty()) {
        // a master that drew a place for the number has it centred there, sized by the box
        const float      numberPixels = where.slideNumber.height * kSlideNumberShare;
        const ScopedFont numberFont(numberPixels);
        const ImVec2     size = ImGui::CalcTextSize(footerNumber.c_str());
        const Canvas     canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        canvas.text(ImGui::GetFont(), numberPixels, ImVec2{where.slideNumber.x + (where.slideNumber.width - size.x) * 0.5f, where.slideNumber.y + (where.slideNumber.height - size.y) * 0.5f}, dimmed(faded.text, 0.8f), footerNumber.c_str());
    }
    if (where.footer.width > 0.0f) {
        // the slide's number sits against the right edge, where a reader's eye goes to find it; the line left of it
        // shrinks to what is left, then is cut, rather than running into the number on a narrow screen
        // sized from the window's height, which on a phone held upright set it taller than the slide has room for
        // below the band's top: the grid puts that top 1.1 body above the slide's bottom edge and makes the band 1.47
        // body tall, so the text may take three quarters of the band
        const bool       gridBand     = layout == nullptr || layout->generated; // a master's footer box is the author's own
        const float      statusPixels = gridBand ? std::min(Fonts::statusSize(viewport->Size.y), where.footer.height * kFooterTextShare) : Fonts::statusSize(viewport->Size.y);
        const ScopedFont status(statusPixels);
        const bool       numberHere = !footerNumber.empty() && where.slideNumber.width <= 0.0f; // else the master's own box takes it
        const float      numberWide = numberHere ? ImGui::CalcTextSize(footerNumber.c_str()).x : 0.0f;
        const float      room       = where.footer.width - numberWide - (numberHere ? statusPixels : 0.0f);
        const Canvas     canvas{ImGui::GetWindowDrawList(), recording, &textRuns};
        if (numberHere) {
            canvas.text(ImGui::GetFont(), statusPixels, ImVec2{where.footer.x + where.footer.width - numberWide, where.footer.y}, dimmed(faded.text, 0.55f), footerNumber.c_str());
        }
        if (!footerText.empty() && room > 0.0f) {
            const float       textWide = ImGui::CalcTextSize(footerText.c_str()).x;
            const ScopedFont  fitting(textWide > room ? std::max(statusPixels * room / textWide, statusPixels * kFooterSmallest) : statusPixels);
            const std::string line = Fonts::fitted(footerText, room);
            canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{where.footer.x, where.footer.y + (statusPixels - ImGui::GetFontSize())}, dimmed(faded.text, 0.55f), line.c_str());
        }
    }

    // last, so a stop's words sit over everything else on the slide, magnified words included
    drawInfoBox(theme, infoFace, infoScreen);
}

float DocumentView::fittingScale(const Theme& theme, const Placement& where, Selection selection, float wanted) {
    // `wanted` comes from the pass just made, and the space after its last block need not fit: a box sized to its
    // words, as a grid row is, would otherwise shrink them to make room for nothing
    if (wanted - trailingGap <= where.available) {
        return 1.0f;
    }
    // Text wraps at the box's width at every scale, so only height decides. The proportional estimate is a lower
    // bound in practice (fewer lines wrap at a smaller size), and the search recovers the size it gave away.
    const auto fits = [&](float scale) {
        const float needed = layoutPass(theme, scale, false, where, selection);
        return needed - trailingGap <= where.available + kFitTolerance;
    };
    const float floor             = where.floorScale > 0.0f ? where.floorScale : minimumScale;
    float       low               = floor;
    float       high              = 1.0f;
    const float estimate          = std::max(where.available / wanted, floor);
    (fits(estimate) ? low : high) = estimate;
    if (!fits(low)) {
        return low;
    }
    for (int refinement = 0; refinement < 6; ++refinement) {
        const float middle          = 0.5f * (low + high);
        (fits(middle) ? low : high) = middle;
    }
    return low;
}

/// what one layout pass shares between the blocks it lays out: where the column is, how large, and how far down
struct DocumentView::Pass {
    const Theme&                    theme;
    const Placement&                where;
    const Canvas&                   canvas;
    Selection                       selection;
    float                           scale;
    float                           left;
    float                           right;
    float                           width;
    float                           top;
    float                           bodyLine;
    float                           ceiling; // the height a picture may take, its share of the box
    const std::vector<std::string>& numbering;
    float                           cursor; // where the next block starts
};

/// what this view cites, in the order it first cites it: the notes listed under the rule at its foot
std::vector<std::string> DocumentView::citedLabels(Selection selection, const Placement& where) const {
    std::vector<std::string> referenced;
    for (const Block& block : document.blocks) {
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
        const auto note = [&referenced](const std::vector<InlineSpan>& spans) {
            for (const InlineSpan& span : spans) {
                if (span.kind == InlineKind::footnote && std::ranges::find(referenced, span.target) == referenced.end()) {
                    referenced.push_back(span.target);
                }
            }
        };
        note(block.spans);
        for (const std::vector<InlineSpan>& cell : block.cells) {
            note(cell);
        }
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

/// a clip with its controls, as large as its share of the box allows
void DocumentView::layoutVideo(Pass& pass, const Block& block) {
    const std::string_view source   = block.field("src");
    const bool             autoplay = block.field("autoplay") != "false";
    const bool             loop     = block.field("loop") != "false";
    const bool             sound    = block.field("audio") == "true"; // silent unless the author asks
    const PlayingVideo*    clip     = source.empty() || !videoFor ? nullptr : videoFor(source, autoplay, loop, sound);

    if (clip != nullptr && clip->texture.id != 0) {
        // capped like any other picture: it is the one thing in the flow whose height would otherwise
        // not give way at all, and then no pass.scale would make the slide fit
        // A clip is as large as its box allows, bar and mark included, and sits in the middle of it: a
        // film is the thing on its slide, so it takes the room rather than a share of it.
        const float barsAndAir = pass.bodyLine * kVideoControlsHeight;
        const bool  alone      = std::ranges::count_if(document.blocks, [this, &pass](const Block& other) { return other.step <= step && (other.kind != BlockKind::heading || pass.selection == Selection::all); }) <= 1;
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

            // a progress bar, then play or pause and rewind at the pass.left and mute at the pass.right. The
            // marks are drawn rather than typed: a triangle and a pair of bars are not in the subset
            // face, and a missing glyph on a slide is worse than a shape.
            const float barTop = pass.cursor + size.y + pass.bodyLine * kVideoBarGap;
            const float height = pass.bodyLine * kVideoBarHeight;
            pass.canvas.filledRect(ImVec2{frameLeft, barTop}, ImVec2{frameLeft + size.x, barTop + height}, dimmed(pass.theme.track, 0.9f));
            const float through = clip->length > 0.0 ? std::clamp(static_cast<float>(clip->position / clip->length), 0.0f, 1.0f) : 0.0f;
            pass.canvas.filledRect(ImVec2{frameLeft, barTop}, ImVec2{frameLeft + size.x * through, barTop + height}, pass.theme.fill);

            const float buttonSide = pass.bodyLine * kVideoButtonSize;
            const float buttonTop  = barTop + height + pass.bodyLine * kVideoButtonGap;
            const ImU32 markColour = dimmed(pass.theme.text, 0.75f);
            const auto  place      = [&](float x, VideoAction action, bool active) {
                const Rectangle area{.x = x, .y = buttonTop, .width = buttonSide, .height = buttonSide};
                videoButtons.push_back(VideoButton{.source = std::string{source}, .action = action, .area = area, .active = active});
                return ImVec2{x, buttonTop};
            };
            const float unit   = buttonSide * 0.7f; // the mark fills the middle of its button
            const auto  markAt = [&](ImVec2 corner) { return ImVec2{corner.x + buttonSide * 0.15f, corner.y + buttonSide * 0.15f}; };

            const ImVec2 playAt = markAt(place(frameLeft, VideoAction::playPause, clip->playing));
            if (clip->playing) {
                pass.canvas.filledRect(ImVec2{playAt.x, playAt.y}, ImVec2{playAt.x + unit * 0.35f, playAt.y + unit}, markColour);
                pass.canvas.filledRect(ImVec2{playAt.x + unit * 0.65f, playAt.y}, ImVec2{playAt.x + unit, playAt.y + unit}, markColour);
            } else {
                pass.canvas.filledTriangle(ImVec2{playAt.x, playAt.y}, ImVec2{playAt.x, playAt.y + unit}, ImVec2{playAt.x + unit, playAt.y + unit * 0.5f}, markColour);
            }

            const ImVec2 rewindAt = markAt(place(frameLeft + buttonSide * 1.2f, VideoAction::rewind, false));
            pass.canvas.filledRect(ImVec2{rewindAt.x, rewindAt.y}, ImVec2{rewindAt.x + unit * 0.2f, rewindAt.y + unit}, markColour);
            pass.canvas.filledTriangle(ImVec2{rewindAt.x + unit, rewindAt.y}, ImVec2{rewindAt.x + unit, rewindAt.y + unit}, ImVec2{rewindAt.x + unit * 0.2f, rewindAt.y + unit * 0.5f}, markColour);

            if (clip->hasSound) {
                const ImVec2 soundAt = markAt(place(frameLeft + size.x - buttonSide, VideoAction::muteToggle, clip->muted));
                const float  mid     = soundAt.y + unit * 0.5f;
                pass.canvas.filledRect(ImVec2{soundAt.x, mid - unit * 0.2f}, ImVec2{soundAt.x + unit * 0.25f, mid + unit * 0.2f}, markColour);
                pass.canvas.filledTriangle(ImVec2{soundAt.x + unit * 0.25f, mid - unit * 0.2f}, ImVec2{soundAt.x + unit * 0.25f, mid + unit * 0.2f}, ImVec2{soundAt.x + unit * 0.65f, mid + unit * 0.5f}, markColour);
                pass.canvas.filledTriangle(ImVec2{soundAt.x + unit * 0.25f, mid - unit * 0.2f}, ImVec2{soundAt.x + unit * 0.65f, mid - unit * 0.5f}, ImVec2{soundAt.x + unit * 0.65f, mid + unit * 0.5f}, markColour);
                if (clip->muted) {
                    pass.canvas.line(ImVec2{soundAt.x + unit * 0.75f, mid - unit * 0.3f}, ImVec2{soundAt.x + unit, mid + unit * 0.3f}, markColour, pass.bodyLine * 0.08f);
                    pass.canvas.line(ImVec2{soundAt.x + unit * 0.75f, mid + unit * 0.3f}, ImVec2{soundAt.x + unit, mid - unit * 0.3f}, markColour, pass.bodyLine * 0.08f);
                } else {
                    pass.canvas.line(ImVec2{soundAt.x + unit * 0.8f, mid - unit * 0.25f}, ImVec2{soundAt.x + unit * 0.8f, mid + unit * 0.25f}, markColour, pass.bodyLine * 0.08f);
                    pass.canvas.line(ImVec2{soundAt.x + unit * 1.0f, mid - unit * 0.4f}, ImVec2{soundAt.x + unit * 1.0f, mid + unit * 0.4f}, markColour, pass.bodyLine * 0.08f);
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
        pass.cursor += (pass.right - pass.left) * kRegionAspect + (trailingGap = pass.bodyLine * kParagraphGap);
    }
}

/// a QR code of a link, with the link written beside it
void DocumentView::layoutQr(Pass& pass, const Block& block) {
    const std::string_view url  = block.field("url");
    const float            side = std::min(pass.bodyLine * kQrLines, pass.right - pass.left);
    const PlacedQrCode*    code = url.empty() || !qrFor ? nullptr : qrFor(url, side);
    if (code != nullptr && code->texture.id != 0) {
        if (pass.canvas) {
            pass.canvas.image(code->texture.id, ImVec2{pass.left, pass.cursor}, ImVec2{pass.left + side, pass.cursor + side}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), RecordedImageKind::qrCode, url);
        }
        // the label and the URL beside the code, so the room can read what they are about to scan
        InlinePen pen{.canvas = pass.canvas, .origin = ImVec2{pass.left + side + pass.bodyLine, pass.cursor + pass.bodyLine * 0.5f}, .pen = ImVec2{pass.left + side + pass.bodyLine, pass.cursor + pass.bodyLine * 0.5f}, .right = pass.right, .lineHeight = pass.bodyLine, .face = nullptr, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        if (const std::string_view label = block.field("label"); !label.empty()) {
            pen.run(std::string{label}, pass.theme.text, ImGui::GetFont(), ImGui::GetFontSize());
            pen.newline();
        }
        pen.run(std::string{url}, dimmed(pass.theme.fill, 0.9f), Fonts::instance().activeFaces().mono, ImGui::GetFontSize() * kQrUrlScale);
        pass.cursor += side + (trailingGap = pass.bodyLine * kParagraphGap);
    } else if (pass.canvas) {
        const std::string message = code == nullptr ? "a qr directive needs a `url:`" : code->problem;
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left, pass.cursor}, dimmed(pass.theme.text, 0.7f), message.c_str());
        pass.cursor += pass.bodyLine + (trailingGap = pass.bodyLine * kParagraphGap);
    } else {
        pass.cursor += pass.bodyLine + (trailingGap = pass.bodyLine * kParagraphGap);
    }
}

/// the deck's references and links, with the codes a room photographs
void DocumentView::layoutReferences(Pass& pass, const Block& block) {
    // The deck's codes belong here rather than on a slide of their own: this is the page a room
    // photographs. They stand in a column on the pass.right -- the deck, then its PDF slide by slide and with
    // every step -- each titled above and with its address beneath, set to the code's pass.width so the two
    // read as one block; the address is a link too, and the list takes what is pass.left. In a browser they
    // encode the address the deck was opened from, which is pass.where the room can reach it.
    float listRight = pass.right;
    if (const std::string_view written = block.field("qr"); !written.empty() && qrFor) {
        const std::string                                               url       = deckAddress.empty() ? std::string{written} : deckAddress;
        const std::string_view                                          label     = block.field("label");
        const std::string                                               joiner    = url.contains('?') ? "&" : "?";
        const float                                                     labelSize = pass.bodyLine * kReferenceScale;
        const float                                                     labelLine = labelSize * 1.5f;
        ImFont*                                                         mono      = Fonts::instance().activeFaces().mono;
        const std::array<std::pair<std::string, std::string_view>, 3UZ> codes{{{url, label.empty() ? std::string_view{"this deck"} : label}, {url + joiner + "export=slides", "PDF, slides"}, {url + joiner + "export=steps", "PDF, every step"}}};
        const float                                                     room           = ImGui::GetIO().DisplaySize.y - pass.cursor - pass.bodyLine;
        const float                                                     itemLabelSpace = 2.6f * labelLine;
        const float                                                     side           = std::max(std::min(ImGui::GetIO().DisplaySize.x * kQrShareOfWidth, (room - static_cast<float>(codes.size()) * itemLabelSpace) / static_cast<float>(codes.size())), pass.bodyLine);
        const float                                                     box            = pass.right - side;
        float                                                           codeTop        = pass.cursor;
        for (const auto& [target, title] : codes) {
            const PlacedQrCode* code = qrFor(target, side);
            if (pass.canvas && code != nullptr && code->texture.id != 0) {
                const float titleWide = ImGui::GetFont()->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, title.data(), title.data() + title.size()).x;
                pass.canvas.text(ImGui::GetFont(), labelSize, ImVec2{box + std::max(side - titleWide, 0.0f) * 0.5f, codeTop}, dimmed(pass.theme.text, 0.8f), std::string{title}.c_str());
                const float codeY = codeTop + labelLine;
                pass.canvas.image(code->texture.id, ImVec2{box, codeY}, ImVec2{box + side, codeY + side}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, dimmed(IM_COL32_WHITE, opacity), RecordedImageKind::qrCode, target);
                // a click on the code opens what scanning it opens, on screen and on paper alike
                const Rectangle codeArea{.x = box, .y = codeY, .width = side, .height = side};
                linkAreas.push_back(LinkArea{.target = target, .area = codeArea});
                if (pass.canvas.recording != nullptr) {
                    pass.canvas.recording->primitives.emplace_back(RecordedLink{.target = target, .at = codeArea});
                }
                // the scheme is not shown: a scanner reads it out of the code, and a reader checking the
                // address against the code wants the host and the path, which are what identify it
                std::string_view shown = target;
                for (const std::string_view scheme : {std::string_view{"https://"}, std::string_view{"http://"}}) {
                    if (shown.starts_with(scheme)) {
                        shown.remove_prefix(scheme.size());
                    }
                }
                // too long to read at the code's pass.width, it breaks before its query, each line fitted
                const auto                              widthAt = [&](std::string_view text) { return text.empty() ? 0.0f : mono->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x; }; // an empty view has no data, which ImGui would read as a NUL-terminated string
                const auto                              query   = shown.find('?');
                const bool                              split   = widthAt(shown) > side * kQrAddressSplit && query != std::string_view::npos;
                const std::array<std::string_view, 2UZ> lines{split ? shown.substr(0UZ, query) : shown, split ? shown.substr(query) : std::string_view{}};
                const float                             widest  = std::max(widthAt(lines[0]), widthAt(lines[1]));
                const float                             fitted  = widest > side ? labelSize * side / widest : labelSize;
                float                                   lineTop = codeY + side + labelSize * 0.25f;
                for (const std::string_view line : lines | std::views::filter([](std::string_view text) { return !text.empty(); })) {
                    const ImVec2 under{box + std::max(side - widthAt(line) * fitted / labelSize, 0.0f) * 0.5f, lineTop};
                    InlinePen    pen{.canvas = pass.canvas, .origin = under, .pen = under, .right = box + side + 1.0f, .lineHeight = labelLine, .face = nullptr, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
                    pen.linkTarget = target;
                    pen.word(line, dimmed(pass.theme.fill, 0.95f), mono, fitted);
                    lineTop += fitted * 1.2f;
                }
            }
            codeTop += side + itemLabelSpace;
        }
        listRight = box - pass.bodyLine; // a gutter, so a long reference does not run under the codes
    }
    // Both lists are gathered from the whole deck by the caller, because a view cannot see the others.
    // Citations come first and keep the numbers their markers showed, which is what IEEE pass.numbering is
    // for: `[3]` on a slide and `[3]` here are the same work.
    const float smaller = ImGui::GetFontSize() * kReferenceScale;
    for (std::size_t index = 0UZ; index < citations.size(); ++index) {
        if (pass.canvas.runs != nullptr) {
            pass.canvas.runs->breakWith("\n");
        }
        const std::vector<InlineSpan>* text = document.footnote(citations[index]);
        if (text == nullptr) {
            continue; // cited somewhere but never defined; the slide already drew a `?` for it
        }
        InlinePen pen{.canvas = pass.canvas, .origin = ImVec2{pass.left + pass.bodyLine * kReferenceIndent, pass.cursor}, .pen = ImVec2{pass.left, pass.cursor}, .right = listRight, .lineHeight = pass.bodyLine, .face = nullptr, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        pen.run("[" + std::to_string(index + 1UZ) + "]  ", dimmed(pass.theme.fill, 0.9f), ImGui::GetFont(), smaller);
        for (const InlineSpan& span : *text) {
            pen.span(span, pass.theme, smaller);
        }
        pass.cursor = pen.pen.y + pass.bodyLine * 1.1f;
    }

    if (!citations.empty() && !references.empty()) {
        pass.cursor += (trailingGap = pass.bodyLine * kParagraphGap);
    }
    for (const auto& [label, target] : references) {
        if (pass.canvas.runs != nullptr) {
            pass.canvas.runs->breakWith("\n");
        }
        InlinePen pen{.canvas = pass.canvas, .origin = ImVec2{pass.left, pass.cursor}, .pen = ImVec2{pass.left, pass.cursor}, .right = listRight, .lineHeight = pass.bodyLine, .face = nullptr, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
        pen.run(label + "  ", pass.theme.text, ImGui::GetFont(), smaller);
        pen.linkTarget = target;
        pen.run(target, dimmed(pass.theme.fill, 0.9f), Fonts::instance().activeFaces().mono, smaller * 0.9f);
        pass.cursor = pen.pen.y + pass.bodyLine * 1.1f;
    }
    if (citations.empty() && references.empty() && pass.canvas) {
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left, pass.cursor}, dimmed(pass.theme.text, 0.6f), "this deck cites and links to nothing");
    }
}

/// a `:::gr4` live region: where it goes, which the viewer then fills, or a placeholder
void DocumentView::layoutRegion(Pass& pass, const Block& block) {
    // a region may name an area of the master, in which case the author has already said pass.where it goes
    if (layout != nullptr) {
        std::string_view named = block.field("region");
        if (named.starts_with('#')) {
            named.remove_prefix(1UZ);
        }
        if (const Area* area = layout->find(named); area != nullptr) {
            const Rectangle   placed = pass.where.map(*area);
            const std::string region = block.id.empty() ? block.info : block.id;
            // only while painting: a measuring pass must not run a frame of somebody else's widget
            const bool filled = pass.canvas && regionFor && regionFor(region, regionBoxes(block, placed, layout, pass.where, pass.bodyLine, barsWithoutArea));
            if (pass.canvas && !filled) {
                pass.canvas.rect(ImVec2{placed.x, placed.y}, ImVec2{placed.x + placed.width, placed.y + placed.height}, dimmed(pass.theme.fill, 0.8f), kPlaceholderRounding);
                pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{placed.x + pass.bodyLine * 0.4f, placed.y + pass.bodyLine * 0.4f}, dimmed(pass.theme.text, 0.7f), region.c_str());
            }
            placedRegions.push_back(PlacedRegion{.id = block.id.empty() ? block.info : block.id, .min = ImVec2{placed.x, placed.y}, .max = ImVec2{placed.x + placed.width, placed.y + placed.height}});
            return; // it does not take part in the flow, so the pass.cursor does not move
        }
    }
    // a status bar is one line of text and its dots; anything else is a chart, as wide as the column and in proportion
    const float  height = block.field("widget") == "status" ? pass.bodyLine * kStatusBarLines : pass.width * kRegionAspect;
    const ImVec2 min{pass.left, pass.cursor};
    const ImVec2 max{pass.right, pass.cursor + height};
    const bool   filled = pass.canvas && regionFor && regionFor(block.id.empty() ? block.info : block.id, regionBoxes(block, Rectangle{.x = pass.left, .y = pass.cursor, .width = pass.right - pass.left, .height = height}, layout, pass.where, pass.bodyLine, barsWithoutArea));
    if (pass.canvas && !filled) {
        pass.canvas.rect(min, max, dimmed(pass.theme.fill, 0.8f), kPlaceholderRounding);
        const std::string label = block.id.empty() ? std::string{block.info} : block.info + " \xe2\x80\xa2 " + block.id;
        pass.canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{pass.left + pass.bodyLine * 0.5f, pass.cursor + pass.bodyLine * 0.5f}, dimmed(pass.theme.text, 0.7f), label.c_str());
    }
    placedRegions.push_back(PlacedRegion{.id = block.id.empty() ? block.info : block.id, .min = min, .max = max});
    pass.cursor += height + (trailingGap = pass.bodyLine * kParagraphGap);
}

/// The notes themselves, under a short rule, in the order their markers were numbered. They are pinned to the
/// foot of the slide -- just above the footer where the box reaches it -- so a citation is in the same place on
/// every slide that has one, and they follow the flow only once the words have grown down to meet them.
///
/// The height reported is the stacked one, with the notes straight after the content. Reporting the pinned
/// height would say the slide reaches the footer whatever is on it, and every slide that cited anything would
/// shrink itself to fit a gap it had left on purpose.
void DocumentView::layoutFootnotes(Pass& pass, std::span<const std::string> referenced) {
    if (referenced.empty() || !pass.where.options.notes) {
        return;
    }
    const float noteSize = pass.where.bodyPixels * kFootnoteScale * pass.scale;
    const auto  notes    = [&](float from, Canvas into) {
        float y = from + pass.bodyLine * kParagraphGap;
        if (into) {
            into.line(ImVec2{pass.left, y}, ImVec2{pass.left + (pass.right - pass.left) * 0.3f, y}, dimmed(pass.theme.text, 0.3f));
        }
        y += pass.bodyLine * 0.5f;

        ImGui::PushFont(Fonts::instance().activeFaces().regular, noteSize);
        const float noteLine = ImGui::GetTextLineHeight();
        for (std::size_t index = 0UZ; index < referenced.size(); ++index) {
            if (into.runs != nullptr) {
                into.runs->breakWith("\n");
            }
            InlinePen  pen{.canvas = into, .origin = ImVec2{pass.left, y}, .pen = ImVec2{pass.left, y}, .right = pass.right, .lineHeight = noteLine, .face = nullptr, .formulas = &formulaFor, .numbering = &pass.numbering, .links = &linkAreas};
            const auto known  = std::ranges::find(pass.numbering, referenced[index]);
            const auto number = known == pass.numbering.end() ? index : static_cast<std::size_t>(known - pass.numbering.begin());
            pen.superscript(std::to_string(number + 1UZ), dimmed(pass.theme.fill, 0.9f), noteSize);
            pen.word(" ", dimmed(pass.theme.text, 0.7f), ImGui::GetFont(), noteSize);
            if (const std::vector<InlineSpan>* text = document.footnote(referenced[index]); text != nullptr) {
                for (const InlineSpan& span : *text) {
                    pen.span(span, pass.theme, noteSize);
                }
            } else {
                pen.run("no note is defined for [^" + referenced[index] + "]", dimmed(pass.theme.fill, 0.9f), ImGui::GetFont(), noteSize);
            }
            y = pen.pen.y + noteLine * 1.25f;
        }
        ImGui::PopFont();
        return y;
    };

    const float stacked = notes(pass.cursor, Canvas{});
    if (pass.canvas) {
        const float foot = pass.where.footer.width > 0.0f ? pass.where.footer.y - pass.where.bodyPixels * pass.scale : pass.where.contentTop + pass.where.available;
        notes(std::max(pass.cursor, foot - (stacked - pass.cursor)), pass.canvas);
    }
    pass.cursor = stacked;
    trailingGap = 0.0f; // the notes end the box, and they end on their last line
}

float DocumentView::layoutPass(const Theme& theme, float scale, bool paint, const Placement& where, Selection selection) {

    const Canvas canvas{paint ? ImGui::GetWindowDrawList() : nullptr, recording, &textRuns};
    // The box keeps its width whatever the scale. Narrowing it as the type shrank kept the measure in characters
    // constant, which is a typographic virtue and not what was asked for: a slide with a little too much on it
    // then drew itself down the left half of the screen. Only the type and the pictures give way now, and since
    // every one of them is at most proportional to the scale, a single pass still fits.
    const float width = where.contentWidth;
    const float left  = where.contentLeft;
    const float right = left + width;
    const float top   = where.contentTop + where.bodyPixels * where.inset * scale;

    const Fonts&      fonts = Fonts::instance();
    const FaceSet     faces = fonts.faceSet(where.options.font.empty() ? slideFont : where.options.font).value_or(fonts.roles());
    const ScopedFaces chosen(faces);
    const ScopedFont  body(where.bodyPixels * scale, faces.regular);
    const float       bodyLine = ImGui::GetTextLineHeight();

    // Two different lists. `referenced` is what this view cites, which is what goes under the rule at its foot.
    // `numbering` decides the number a marker shows: the deck-wide order when the caller knows it, which is IEEE's
    // rule and what lets the reference view agree with the slides, and this view's own order when it does not.
    const std::vector<std::string>  referenced = citedLabels(selection, where);
    const std::vector<std::string>& numbering  = citations.empty() ? referenced : citations;

    // The room a picture may take is shared between the pictures there are, so the one on a slide of its own fills
    // the box and two split it. Without a ceiling a picture's height does not answer to the scale at all, and a
    // slide holding one could never be made to fit; with a fixed one, a slide holding only a film wasted half its
    // screen.
    const auto  isFigure = [](const Block& block) { return (block.kind == BlockKind::paragraph && block.spans.size() == 1UZ && block.spans.front().kind == InlineKind::image) || (block.kind == BlockKind::directive && block.info == "video"); };
    const auto  figures  = static_cast<float>(std::ranges::count_if(document.blocks, [this, &isFigure](const Block& block) { return block.step <= step && isFigure(block); }));
    const float ceiling  = where.available * kFigureMaxShare * scale / std::max(figures, 1.0f);
    Pass        pass{.theme = theme, .where = where, .canvas = canvas, .selection = selection, .scale = scale, .left = left, .right = right, .width = width, .top = top, .bodyLine = bodyLine, .ceiling = ceiling, .numbering = numbering, .cursor = top};
    float&      cursor = pass.cursor;

    std::array<int, 8UZ> ordinals{}; // one running number per nesting level, reset when a list ends

    trailingGap         = 0.0f;
    const auto gapAfter = [this](float gap) { return trailingGap = gap; };
    for (const Block& block : document.blocks) {
        if (block.step > step) {
            continue;
        }
        const bool isHeading = block.kind == BlockKind::heading;
        if ((selection == Selection::headingOnly && !isHeading) || (selection == Selection::exceptHeading && isHeading) || selection == Selection::notesOnly) {
            continue; // a layout with a title area takes the heading out of the body flow
        }
        trailingGap = 0.0f;
        if (canvas.runs != nullptr) {
            canvas.runs->breakWith("\n");
        }
        const struct RecordDrawn {
            std::vector<DrawnPiece>* blocks;
            ImDrawList*              list;
            std::string              key;
            int                      from;
            ~RecordDrawn() {
                if (blocks != nullptr && list->VtxBuffer.Size > from) {
                    blocks->push_back(DrawnPiece{.key = key, .vertexFrom = from, .vertexTo = list->VtxBuffer.Size});
                }
            }
        } recorded{.blocks = canvas ? &drawnBlocks : nullptr, .list = canvas.list, .key = canvas ? morphKeyOf(block) : std::string{}, .from = canvas ? canvas.list->VtxBuffer.Size : 0};
        if (block.kind != BlockKind::listItem) {
            ordinals.fill(0);
        } else {
            std::ranges::fill(ordinals | std::views::drop(static_cast<std::size_t>(block.level) + 1UZ), 0);
        }

        // the blocks of the step just reached arrive the way the step asked, once they are drawn, whichever way
        // the drawing of each one ends
        struct ArriveAfterDrawing {
            ImDrawList*                        list;
            DrawnFrom                          start;
            std::string_view                   kind, from;
            float                              progress, lift;
            const DocumentView::ThroughEffect& through;
            std::string                        key;
            ~ArriveAfterDrawing() {
                if (list != nullptr) {
                    arrive(*list, start, kind, from, progress, lift, through, key);
                }
            }
        };
        const Document::StepReveal* stepReveal  = document.revealOf(block.step);
        const std::string_view      revealKind  = !block.revealKind.empty() ? std::string_view{block.revealKind} : (stepReveal != nullptr ? std::string_view{stepReveal->kind} : std::string_view{});
        const std::optional<float>  revealSaid  = !block.revealKind.empty() ? block.revealSeconds : (stepReveal != nullptr ? stepReveal->seconds : std::nullopt);
        const float                 revealTime  = revealSaid.value_or(revealSeconds ? revealSeconds(revealKind) : kRevealSeconds);
        const float                 revealed    = stepSeconds < 0.0f ? 1.0f : std::clamp(stepSeconds / std::max(revealTime, 0.01f), 0.0f, 1.0f);
        const bool                  blockArrive = canvas && step > 0 && block.step == step && !revealKind.empty() && revealed < 1.0f;
        const std::string_view      revealFrom  = !block.revealKind.empty() ? std::string_view{block.revealFrom} : (stepReveal != nullptr ? std::string_view{stepReveal->from} : std::string_view{});
        const ArriveAfterDrawing    arrival{.list = blockArrive ? canvas.list : nullptr, .start = blockArrive ? startDrawing(*canvas.list) : DrawnFrom{}, .kind = revealKind, .from = revealFrom, .progress = revealed, .lift = bodyLine * kRiseEms, .through = throughEffect, .key = blockArrive ? std::format("block:{}", &block - document.blocks.data()) : std::string{}};

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
            ImFont* const                   headingFace = block.level == 1 ? Fonts::instance().activeFaces().title : Fonts::instance().activeFaces().regular;
            std::optional<const ScopedFont> heading{std::in_place, size, headingFace};
            // a third-level heading and below names a thing rather than a slide, and reads as a bold label
            ImFont* const labelFace = block.level >= 3 ? Fonts::instance().activeFaces().bold : nullptr;
            // a heading in its own title box, or first in its box, needs no space above it; one in the flow does
            cursor += selection == Selection::headingOnly || block.level == 1 || cursor <= top ? 0.0f : ImGui::GetTextLineHeight() * kHeadingGapAbove;
            InlinePen pen{.canvas = canvas, .origin = ImVec2{left, cursor}, .pen = ImVec2{left, cursor}, .right = right, .lineHeight = ImGui::GetTextLineHeight(), .face = labelFace, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            bool      subtitled = false;
            for (const InlineSpan& inlineSpan : block.spans) {
                // in a title, what follows a `<br>` is its sub-title, set smaller on a line of its own
                if (inlineSpan.kind == InlineKind::lineBreak && block.level == 1 && !subtitled) {
                    subtitled = true;
                    pen.newline("\n");
                    heading.reset();
                    heading.emplace(size * kSubtitleRatio, Fonts::instance().activeFaces().regular);
                    pen.lineHeight = ImGui::GetTextLineHeight();
                    continue;
                }
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + ImGui::GetTextLineHeight() + gapAfter(ImGui::GetTextLineHeight() * (block.level == 1 && selection != Selection::headingOnly ? kSubtitleGapEms : kParagraphGap));
            break;
        }
        case BlockKind::codeBlock: {
            const float codeSize = ImGui::GetFontSize() * kCodeFontScale;
            const float height   = drawCode(Canvas{}, theme, block, left, right, cursor, codeSize, opacity);
            if (canvas) {
                // the panel is drawn first because a draw list paints in order, and it must sit behind the text
                canvas.filledRect(ImVec2{left, cursor}, ImVec2{right, cursor + height}, dimmed(codePanel(theme), opacity), kPlaceholderRounding);
                // lines a later step reveals keep their place, blank, so nothing below the panel moves as they arrive;
                // under `{follow}` every line shows from the start and the step picks out its own
                const bool follow = !block.field("follow").empty();
                Block      shown  = block;
                for (std::size_t line = 0UZ; !follow && line < shown.lineSteps.size() && line < shown.lines.size(); ++line) {
                    if (shown.lineSteps[line] > step) {
                        shown.lines[line].clear();
                    }
                }
                // each line a slide keeps is a piece of its own, so a morph can move it on its own: by its text, without the
                // indentation a refactoring changes, within the block's `{#id}` or among all code when it has none
                std::vector<int> lineStarts;
                drawCode(canvas, theme, shown, left, right, cursor, codeSize, opacity, &lineStarts);
                if (follow && std::ranges::contains(block.lineSteps, step)) {
                    for (std::size_t line = 0UZ; line + 1UZ < lineStarts.size() && line < block.lineSteps.size(); ++line) {
                        if (block.lineSteps[line] == step) {
                            continue;
                        }
                        for (ImDrawVert& vertex : std::span{canvas.list->VtxBuffer.Data + lineStarts[line], static_cast<std::size_t>(lineStarts[line + 1UZ] - lineStarts[line])}) {
                            vertex.col = dimmed(vertex.col, kFollowedAway);
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
            cursor += height + gapAfter(bodyLine * kParagraphGap);
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
                // centred in the column, which is what a displayed equation is, unless its box asks for the left edge
                const float drawnWidth  = rendered->texture.width;
                const float drawnHeight = rendered->texture.height;
                const float factor      = drawnWidth > right - left ? (right - left) / drawnWidth : 1.0f;
                const float origin      = where.options.left ? left : left + (right - left - drawnWidth * factor) * 0.5f;
                if (canvas) {
                    canvas.image(rendered->texture.id, ImVec2{origin, cursor}, ImVec2{origin + drawnWidth * factor, cursor + drawnHeight * factor}, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f}, theme.text, RecordedImageKind::formula, latex, size, true);
                }
                cursor += drawnHeight * factor + gapAfter(bodyLine * kParagraphGap);
            } else {
                if (canvas) {
                    const std::string message = rendered == nullptr ? latex : rendered->problem;
                    canvas.text(Fonts::instance().activeFaces().mono, ImGui::GetFontSize(), ImVec2{left, cursor}, dimmed(theme.text, 0.7f), message.c_str());
                }
                cursor += bodyLine + gapAfter(bodyLine * kParagraphGap);
            }
            break;
        }
        case BlockKind::plot: {
            // parsed every pass rather than cached: a deck holds a handful of plots of a handful of rows, and a
            // cache keyed on a block would have to be invalidated when the package is reloaded
            Plot plot = parsePlot(block.lines);
            if (!plot.source.empty() && plot.series.empty() && plot.problem.empty()) {
                const std::string_view csv = dataFor ? dataFor(plot.source) : std::string_view{};
                if (csv.empty()) {
                    plot.problem = "cannot read " + plot.source;
                } else {
                    readCsv(plot, csv);
                }
            }
            // A plot is as wide as its box and as tall as its aspect makes it, until that would take more of the
            // room than a slide can spare. Past that it keeps the width and flattens, rather than narrowing to
            // hold the aspect: a plot's shape is a convenience, not a photograph's, and narrowing it left the
            // plots slide using half its width while the words beside it used all of theirs.
            // The share is of the room at the scale being drawn, so that a plot shrinks with everything around it.
            // A cap in fixed pixels does not: the words beside it gave way and the plot did not, so a slide that
            // was a little too tall came out narrow and still overflowed.
            const float wide = right - left;
            // A plot with nothing but plots after it takes the room that is left, shared between them, in both
            // directions like a live chart does; one with words after it keeps its aspect and leaves them room.
            const auto  isPlot     = [this](const Block& candidate) { return candidate.step <= step && candidate.kind == BlockKind::plot; };
            const auto  here       = std::ranges::find_if(document.blocks, [&block](const Block& candidate) { return &candidate == &block; });
            const auto  trailing   = std::ranges::find_if(here, document.blocks.end(), [&](const Block& candidate) { return candidate.step <= step && candidate.kind != BlockKind::plot; });
            const float plotsLeft  = static_cast<float>(std::ranges::count_if(here, document.blocks.end(), isPlot));
            const float roomLeft   = where.available * kPlotMaxShare * scale - (cursor - top);
            const float aspectTall = std::min(wide * kPlotAspect, where.available * kPlotMaxShare * scale);
            const float tall       = trailing == document.blocks.end() ? std::max(aspectTall, roomLeft / plotsLeft) : aspectTall;
            drawPlot(canvas, theme, plot, Rectangle{.x = left, .y = cursor, .width = wide, .height = tall}, Fonts::chartLabelPixels(ImGui::GetFontSize(), ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y));
            cursor += tall + gapAfter(bodyLine * kParagraphGap);
            break;
        }
        case BlockKind::table: {
            const float height = drawTable(canvas, theme, block, left, right, cursor, ImGui::GetFontSize(), formulaFor, where.options.striped);
            cursor += height + gapAfter(bodyLine * kParagraphGap);
            break;
        }
        case BlockKind::listItem: {
            const float       indent = left + bodyLine * kListIndent * static_cast<float>(block.level + 1);
            const std::string bullet = block.ordered ? std::to_string(++ordinals[static_cast<std::size_t>(block.level)]) + "." : std::string{"\xe2\x80\xa2"};
            if (canvas) {
                // a bullet is decoration and is not copied; an ordered item's number is what the author wrote, and
                // copies with the space that separates it from the item
                const Canvas marker{canvas.list, canvas.recording, block.ordered ? canvas.runs : nullptr};
                marker.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{indent - bodyLine, cursor}, dimmed(theme.text, 0.7f), block.ordered ? bullet + " " : bullet);
            }
            InlinePen pen{.canvas = canvas, .origin = ImVec2{indent, cursor}, .pen = ImVec2{indent, cursor}, .right = right, .lineHeight = bodyLine, .face = nullptr, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            for (const InlineSpan& inlineSpan : block.spans) {
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + bodyLine + gapAfter(bodyLine * kParagraphGap * 0.4f);
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
                layoutRegion(pass, block); // an effect viewport or a stage takes its place like a live region
            } // a layout or notes directive is consumed elsewhere, not drawn
            break;
        }
        case BlockKind::paragraph: {
            const bool isImageOnly = block.spans.size() == 1UZ && block.spans.front().kind == InlineKind::image;
            if (isImageOnly) {
                const InlineSpan& image   = block.spans.front();
                const Texture*    texture = imageFor ? imageFor(image.target) : nullptr;
                if (texture != nullptr && texture->id != 0) {
                    // As wide as its box, until that makes it taller than a slide can spare for one figure. Past
                    // that it keeps its shape and narrows, centred, because a stretched photograph is worse than a
                    // margin -- the opposite of a plot, whose shape is only a convenience.
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
                    cursor += size.y + gapAfter(bodyLine * kParagraphGap);
                } else {
                    const float height = width * kRegionAspect * 0.6f;
                    if (canvas) {
                        canvas.rect(ImVec2{left, cursor}, ImVec2{right, cursor + height}, dimmed(theme.text, 0.35f), kPlaceholderRounding);
                    }
                    const std::string label = "missing image: " + image.target;
                    if (canvas) {
                        canvas.text(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{left + bodyLine * 0.5f, cursor + bodyLine * 0.5f}, dimmed(theme.text, 0.6f), label.c_str());
                    }
                    cursor += height + gapAfter(bodyLine * kParagraphGap);
                }
                break;
            }
            InlinePen pen{.canvas = canvas, .origin = ImVec2{left, cursor}, .pen = ImVec2{left, cursor}, .right = right, .lineHeight = bodyLine, .face = nullptr, .formulas = &formulaFor, .numbering = &numbering, .links = &linkAreas};
            for (const InlineSpan& inlineSpan : block.spans) {
                pen.span(inlineSpan, theme, ImGui::GetFontSize());
            }
            cursor = pen.pen.y + bodyLine + gapAfter(bodyLine * kParagraphGap);
            break;
        }
        }
    }

    layoutFootnotes(pass, referenced);
    return cursor - where.contentTop + where.bodyPixels * 2.0f * where.inset * scale;
}

} // namespace gr::present
