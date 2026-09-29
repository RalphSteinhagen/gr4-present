#include "NotesOverlay.hpp"

#include "Fonts.hpp"
#include "ScopedWindow.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace gr::present {

namespace {

constexpr float kViewportHeightFraction    = 0.32f;
constexpr float kPaddingLineHeights        = 0.6f;
constexpr float kCompactNotesScreenShare   = 0.60f;
constexpr float kSmallestNotesFraction     = 0.6f;  // below this the notes scroll rather than shrink
constexpr float kNotesPoints               = 16.0f; // the notes' text in interface points, the size of the side menu's slide names
constexpr float kThumbTargetCssPixels      = 48.0f;
constexpr float kSlideHeightToWidth        = 9.0f / 16.0f;
constexpr float kMiniatureColumnWidthShare = 0.45f;

void drawMark(ImDrawList* canvas, NotesOverlay::Action action, ImVec2 low, ImVec2 high, ImU32 colour) {
    const float  side = high.x - low.x;
    const float  unit = side * (action == NotesOverlay::Action::nextSlide ? 0.17f : 0.22f);
    const ImVec2 mid{(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
    const auto   rightward = [&](float x) { canvas->AddTriangleFilled(ImVec2{x - unit, mid.y - unit * 1.2f}, ImVec2{x - unit, mid.y + unit * 1.2f}, ImVec2{x + unit, mid.y}, colour); };
    if (action == NotesOverlay::Action::nextStep) {
        rightward(mid.x);
    } else if (action == NotesOverlay::Action::nextSlide) {
        rightward(mid.x - unit * 0.9f);
        rightward(mid.x + unit * 0.7f);
        canvas->AddRectFilled(ImVec2{mid.x + unit * 1.75f, mid.y - unit * 1.2f}, ImVec2{mid.x + unit * 2.2f, mid.y + unit * 1.2f}, colour);
    } else {
        canvas->AddTriangleFilled(ImVec2{mid.x + unit, mid.y - unit * 1.2f}, ImVec2{mid.x + unit, mid.y + unit * 1.2f}, ImVec2{mid.x - unit, mid.y}, colour);
    }
}

[[nodiscard]] float fittingSize(std::string_view text, float size, float width, float height) {
    float fitted = size;
    for (int attempt = 0; attempt < 4; ++attempt) { // a smaller size wraps differently, so re-measure
        const float tall = ImGui::GetFont()->CalcTextSizeA(fitted, FLT_MAX, width, text.data(), text.data() + text.size()).y;
        if (tall <= height) {
            break;
        }
        fitted = std::max(size * kSmallestNotesFraction, fitted * std::sqrt(height / tall));
    }
    return fitted;
}

} // namespace

NotesOverlay::Action NotesOverlay::draw(const Theme& theme, std::string_view notes, std::string_view position, std::string_view nextTitle, float interfacePixelsPerPoint) {
    buttons        = {};
    buttonsLeft    = 0.0f;
    notesArea      = {};
    notesPixels    = 0.0f;
    notesScroll    = 0.0f;
    notesScrollMax = 0.0f;
    runs.clear();
    if (!visible) {
        return Action::none;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float          height   = compact ? viewport->Size.y : viewport->Size.y * kViewportHeightFraction;
    ImGui::SetNextWindowPos(ImVec2{viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height});
    ImGui::SetNextWindowSize(ImVec2{viewport->Size.x, height});
    ImGui::SetNextWindowBgAlpha(compact ? 1.0f : 0.92f);

    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const ScopedWindow         panel("##notes", nullptr, kWindowFlags);

    const ScopedFont status(Fonts::kInterfacePoints * interfacePixelsPerPoint);
    const float      line   = ImGui::GetTextLineHeight();
    const float      pad    = line * kPaddingLineHeights;
    const ImVec2     origin = ImGui::GetWindowPos();
    const float      width  = viewport->Size.x;

    float columnX     = 0.0f;
    float columnWidth = width;
    float columnTop   = 0.0f;
    miniatures        = {};
    if (compact) {
        if (height > width) {
            const float miniWidth  = (width - 3.0f * pad) * 0.5f;
            const float miniHeight = miniWidth * kSlideHeightToWidth;
            miniatures             = {Rectangle{.x = origin.x + pad, .y = origin.y + pad, .width = miniWidth, .height = miniHeight}, Rectangle{.x = origin.x + 2.0f * pad + miniWidth, .y = origin.y + pad, .width = miniWidth, .height = miniHeight}};
            columnTop              = pad + miniHeight;
        } else {
            const float miniWidth  = std::min(width * kMiniatureColumnWidthShare, (height - 3.0f * pad) * 0.5f / kSlideHeightToWidth);
            const float miniHeight = miniWidth * kSlideHeightToWidth;
            miniatures             = {Rectangle{.x = origin.x + pad, .y = origin.y + pad, .width = miniWidth, .height = miniHeight}, Rectangle{.x = origin.x + pad, .y = origin.y + 2.0f * pad + miniHeight, .width = miniWidth, .height = miniHeight}};
            columnX                = pad + miniWidth;
            columnWidth            = width - columnX;
        }
    }

    const std::string nextLine     = nextTitle.empty() ? std::string{} : "\xe2\x80\xa2  next: " + std::string{nextTitle};
    const float       statusWide   = ImGui::CalcTextSize(position.data(), position.data() + position.size()).x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(nextLine.c_str()).x;
    const bool        nextBeneath  = !nextLine.empty() && statusWide > columnWidth - 2.0f * pad;
    const float       statusHeight = line * (nextBeneath ? 2.0f : 1.0f) + ImGui::GetStyle().ItemSpacing.y;

    std::array<Rectangle, 3> placed{};
    Rectangle                text{};
    float                    statusTop = pad;
    if (compact) {
        const float notesBottom = std::max(columnTop + 3.0f * line, height * kCompactNotesScreenShare);
        text                    = Rectangle{.x = columnX + pad, .y = columnTop + pad, .width = columnWidth - 2.0f * pad, .height = notesBottom - columnTop - pad};
        statusTop               = notesBottom + pad * 0.5f;
        const float     bandTop = statusTop + statusHeight + pad;
        const float     band    = height - pad - bandTop;
        const float     gap     = pad * 1.5f;
        const float     large   = std::max(kThumbTargetCssPixels, std::min(band * 0.8f, (columnWidth - 2.0f * pad - 2.0f * gap) * 0.42f));
        const float     medium  = std::max(kThumbTargetCssPixels, large * 0.75f);
        const float     small   = std::max(kThumbTargetCssPixels, large * 0.6f);
        const float     left    = columnX + (columnWidth - (small + large + medium + 2.0f * gap)) * 0.5f;
        const float     centre  = bandTop + band * 0.5f;
        const Rectangle largeThumb{.x = left + small + gap, .y = centre - large * 0.5f, .width = large, .height = large};
        const Rectangle mediumThumb{.x = left + small + gap + large + gap, .y = centre - medium * 0.5f, .width = medium, .height = medium};
        const Rectangle smallThumb{.x = left, .y = centre - small * 0.5f, .width = small, .height = small};
        placed = {largeThumb, mediumThumb, smallThumb};
    } else {
        const float     gap          = line * 0.3f;
        const float     large        = height * 0.5f;
        const float     small        = (large - gap) * 0.5f;
        const float     buttonColumn = width - large - pad;
        const Rectangle largeButton{.x = buttonColumn, .y = pad, .width = large, .height = large};
        const Rectangle smallBelow{.x = buttonColumn, .y = pad + large + gap, .width = small, .height = small};
        const Rectangle smallBeside{.x = buttonColumn + small + gap, .y = pad + large + gap, .width = small, .height = small};
        placed      = {largeButton, smallBelow, smallBeside};
        buttonsLeft = origin.x + buttonColumn - pad;
        text        = Rectangle{.x = pad, .y = 0.0f, .width = buttonColumn - 2.0f * pad, .height = 0.0f};
    }

    for (const Rectangle& slide : miniatures) { // a hairline marks the edge of the slide drawn here afterwards
        if (slide.width > 0.0f) {
            ImGui::GetWindowDrawList()->AddRect(ImVec2{slide.x - 1.0f, slide.y - 1.0f}, ImVec2{slide.x + slide.width + 1.0f, slide.y + slide.height + 1.0f}, dimmed(theme.text, 0.35f));
        }
    }
    ImGui::SetCursorPos(ImVec2{columnX + pad, statusTop});
    ImGui::TextUnformatted(position.data(), position.data() + position.size());
    if (!nextLine.empty()) {
        if (!nextBeneath) {
            ImGui::SameLine();
        } else {
            ImGui::SetCursorPosX(columnX + pad);
        }
        ImGui::TextDisabled("%s", Fonts::fitted(nextLine, columnX + columnWidth - ImGui::GetCursorPosX() - pad).c_str());
    }
    if (columnX > 0.0f) {
        const float ruleY = ImGui::GetCursorScreenPos().y;
        ImGui::GetWindowDrawList()->AddLine(ImVec2{origin.x + columnX, ruleY}, ImVec2{origin.x + width, ruleY}, ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2{0.0f, ImGui::GetStyle().ItemSpacing.y});
    } else {
        ImGui::Separator();
    }
    const ImVec2 laptopNotesCursor = ImGui::GetCursorPos();

    Action      pressed = Action::none;
    ImDrawList* canvas  = ImGui::GetWindowDrawList();
    for (std::size_t index = 0UZ; index < buttons.size(); ++index) {
        const Action action = static_cast<Action>(index + 1UZ);
        ImGui::SetCursorPos(ImVec2{placed[index].x, placed[index].y});
        {
            const ScopedIndexId perButton{static_cast<int>(index)};
            if (ImGui::InvisibleButton("##notes-button", ImVec2{placed[index].width, placed[index].height})) {
                pressed = action;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
        }
        const ImVec2 low  = ImGui::GetItemRectMin();
        const ImVec2 high = ImGui::GetItemRectMax();
        buttons[index]    = Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
        canvas->AddRectFilled(low, high, dimmed(theme.text, ImGui::IsItemHovered() ? 0.22f : 0.12f), (high.x - low.x) * 0.15f);
        drawMark(canvas, action, low, high, dimmed(theme.text, 0.85f));
    }

    const float baseNotes = kNotesPoints * interfacePixelsPerPoint;
    if (!compact) {
        // not laid out by DocumentView: notes are not slide content and a second pass would cost frame time
        ImGui::SetCursorPos(laptopNotesCursor);
        const ScopedFont body(baseNotes);
        if (notes.empty()) {
            ImGui::TextDisabled("no notes for this view");
            return pressed;
        }
        // one run per wrapped line, so a reader can select and copy
        ImFont* const    font       = ImGui::GetFont();
        const float      size       = ImGui::GetFontSize();
        const float      lineHeight = ImGui::GetTextLineHeight();
        const float      wrapWidth  = text.x + text.width - ImGui::GetCursorPosX();
        const ImVec2     start      = ImGui::GetCursorScreenPos();
        const ImU32      ink        = ImGui::GetColorU32(ImGuiCol_Text);
        float            y          = start.y;
        std::string_view rest       = notes;
        while (!rest.empty()) {
            const std::size_t      newline   = rest.find('\n');
            const std::string_view paragraph = rest.substr(0UZ, newline);
            rest                             = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1UZ);
            const char*       begin          = paragraph.data();
            const char* const end            = paragraph.data() + paragraph.size();
            do {
                const char* wrapped = begin < end ? font->CalcWordWrapPosition(size, begin, end, wrapWidth) : end;
                wrapped             = wrapped == begin && begin < end ? begin + 1 : wrapped;
                canvas->AddText(font, size, ImVec2{start.x, y}, ink, begin, wrapped);
                runs.add(std::string_view{begin, wrapped}, Rectangle{.x = start.x, .y = y, .width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, wrapped).x, .height = lineHeight});
                runs.breakWith(" ");
                y += lineHeight;
                begin = wrapped;
                while (begin < end && *begin == ' ') {
                    ++begin; // skips the space the line broke at
                }
            } while (begin < end);
            runs.breakWith("\n");
        }
        ImGui::Dummy(ImVec2{wrapWidth, y - start.y});
        return pressed;
    }

    // ImGui scrolls with a wheel only, so a drag over the notes is turned into scrolling below
    notesPixels = notes.empty() ? baseNotes : fittingSize(notes, baseNotes, text.width - pad, text.height);
    ImGui::SetCursorPos(ImVec2{text.x, text.y});
    if (const ScopedChild notesText{"##notes-text", ImVec2{text.width, text.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav}; notesText) {
        const ScopedFont body(notesPixels);
        if (notes.empty()) {
            ImGui::TextDisabled("no notes for this view");
        } else {
            ImGui::PushTextWrapPos(text.width - pad);
            ImGui::TextUnformatted(notes.data(), notes.data() + notes.size());
            ImGui::PopTextWrapPos();
        }
        // the press position decides, not hover: ImGui reports a window being pressed as not hovered
        const ImVec2 pressedAt = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
        const ImVec2 low       = ImGui::GetWindowPos();
        const ImVec2 high{low.x + ImGui::GetWindowWidth(), low.y + ImGui::GetWindowHeight()};
        const bool   fromNotes = pressedAt.x >= low.x && pressedAt.x < high.x && pressedAt.y >= low.y && pressedAt.y < high.y;
        if (fromNotes && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }
        notesScroll    = ImGui::GetScrollY();
        notesScrollMax = ImGui::GetScrollMaxY();
    }
    notesArea = Rectangle{.x = origin.x + text.x, .y = origin.y + text.y, .width = text.width, .height = text.height};
    return pressed;
}

} // namespace gr::present
