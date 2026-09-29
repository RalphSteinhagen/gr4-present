#include "NotesOverlay.hpp"

#include "Fonts.hpp"
#include "ImGuiScoped.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace gr::present {

namespace {

constexpr float kHeightFraction       = 0.32f;        // of the viewport: enough for a paragraph without hiding the slide
constexpr float kPadding              = 0.6f;         // of a line height
constexpr float kCompactNotesShare    = 0.60f;        // of a phone's screen: the presenter's words, above the status and buttons
constexpr float kSmallestNotes        = 0.6f;         // of the notes' own size: below this they scroll rather than shrink
constexpr float kThumbTarget          = 48.0f;        // CSS px: the least a finger hits reliably
constexpr float kSlideAspect          = 9.0f / 16.0f; // a miniature's height to its width: the room's screen
constexpr float kMiniatureColumnShare = 0.45f;        // of a turned phone's width, at most, for the two slides

/// the mark for what a button does, centred in it: one arrow onwards, two and a bar for the next slide, one back
void drawMark(ImDrawList* canvas, NotesOverlay::Action action, ImVec2 low, ImVec2 high, ImU32 colour) {
    const float  side = high.x - low.x;
    const float  unit = side * (action == NotesOverlay::Action::nextSlide ? 0.17f : 0.22f); // the skip mark is the widest
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

/// the size at which `text`, wrapped to `width`, fits `height`: `size` when it already does, never below the floor
[[nodiscard]] float fittingSize(std::string_view text, float size, float width, float height) {
    float fitted = size;
    for (int attempt = 0; attempt < 4; ++attempt) { // a smaller size also wraps differently, so it is measured again
        const float tall = ImGui::GetFont()->CalcTextSizeA(fitted, FLT_MAX, width, text.data(), text.data() + text.size()).y;
        if (tall <= height) {
            break;
        }
        fitted = std::max(size * kSmallestNotes, fitted * std::sqrt(height / tall));
    }
    return fitted;
}

} // namespace

NotesOverlay::Action NotesOverlay::draw(const Theme& theme, std::string_view notes, std::string_view position, std::string_view nextTitle) {
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
    const float          height   = compact ? viewport->Size.y : viewport->Size.y * kHeightFraction;
    ImGui::SetNextWindowPos(ImVec2{viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height});
    ImGui::SetNextWindowSize(ImVec2{viewport->Size.x, height});
    ImGui::SetNextWindowBgAlpha(compact ? 1.0f : 0.92f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    const ScopedWindow         panel("##notes", nullptr, flags);

    const ScopedFont status(Fonts::statusSize(compact ? std::max(viewport->Size.x, viewport->Size.y) : viewport->Size.y));
    const float      line   = ImGui::GetTextLineHeight();
    const float      pad    = line * kPadding;
    const ImVec2     origin = ImGui::GetWindowPos();
    const float      width  = viewport->Size.x;

    // Where everything goes. On a laptop the notes sit beside a column of buttons at the panel's right: the next step
    // as a square half the panel high, the next slide and back side by side beneath it. On a phone the notes take
    // the top of the screen and the buttons one row beneath them at thumb height -- back, next step, next slide --
    // centred so either hand reaches them, with back furthest from the next step so a hurried thumb misses it.
    // the next view's title follows the position on one line, or takes a line of its own where that does not fit
    // On a phone the two slides come first: what the room sees now and what the next press shows, side by side above
    // the notes when the phone is upright, one above the other at the left when it is turned. The notes, the status
    // line and the buttons share the column that is left.
    float columnX     = 0.0f;
    float columnWidth = width;
    float columnTop   = 0.0f;
    miniatures        = {};
    if (compact) {
        if (height > width) {
            const float miniWidth  = (width - 3.0f * pad) * 0.5f;
            const float miniHeight = miniWidth * kSlideAspect;
            miniatures             = {Rectangle{.x = origin.x + pad, .y = origin.y + pad, .width = miniWidth, .height = miniHeight}, Rectangle{.x = origin.x + 2.0f * pad + miniWidth, .y = origin.y + pad, .width = miniWidth, .height = miniHeight}};
            columnTop              = pad + miniHeight;
        } else {
            const float miniWidth  = std::min(width * kMiniatureColumnShare, (height - 3.0f * pad) * 0.5f / kSlideAspect);
            const float miniHeight = miniWidth * kSlideAspect;
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
        const float notesBottom = std::max(columnTop + 3.0f * line, height * kCompactNotesShare);
        text                    = Rectangle{.x = columnX + pad, .y = columnTop + pad, .width = columnWidth - 2.0f * pad, .height = notesBottom - columnTop - pad};
        statusTop               = notesBottom + pad * 0.5f;
        const float bandTop     = statusTop + statusHeight + pad;
        const float band        = height - pad - bandTop;
        const float gap         = pad * 1.5f;
        const float large       = std::max(kThumbTarget, std::min(band * 0.8f, (columnWidth - 2.0f * pad - 2.0f * gap) * 0.42f));
        const float medium      = std::max(kThumbTarget, large * 0.75f);
        const float small       = std::max(kThumbTarget, large * 0.6f);
        const float left        = columnX + (columnWidth - (small + large + medium + 2.0f * gap)) * 0.5f;
        const float centre      = bandTop + band * 0.5f;
        placed                  = {Rectangle{.x = left + small + gap, .y = centre - large * 0.5f, .width = large, .height = large}, Rectangle{.x = left + small + gap + large + gap, .y = centre - medium * 0.5f, .width = medium, .height = medium}, Rectangle{.x = left, .y = centre - small * 0.5f, .width = small, .height = small}};
    } else {
        const float gap          = line * 0.3f;
        const float large        = height * 0.5f;
        const float small        = (large - gap) * 0.5f;
        const float buttonColumn = width - large - pad;
        placed                   = {Rectangle{.x = buttonColumn, .y = pad, .width = large, .height = large}, Rectangle{.x = buttonColumn, .y = pad + large + gap, .width = small, .height = small}, Rectangle{.x = buttonColumn + small + gap, .y = pad + large + gap, .width = small, .height = small}};
        buttonsLeft              = origin.x + buttonColumn - pad;
        text                     = Rectangle{.x = pad, .y = 0.0f, .width = buttonColumn - 2.0f * pad, .height = 0.0f}; // set beneath the status line
    }

    for (const Rectangle& slide : miniatures) { // the room's slide is drawn into these afterwards; a hairline marks its edge
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
    if (columnX > 0.0f) { // beside the miniatures, the rule spans the notes' column only
        const float ruleY = ImGui::GetCursorScreenPos().y;
        ImGui::GetWindowDrawList()->AddLine(ImVec2{origin.x + columnX, ruleY}, ImVec2{origin.x + width, ruleY}, ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2{0.0f, ImGui::GetStyle().ItemSpacing.y});
    } else {
        ImGui::Separator();
    }
    const ImVec2 flow = ImGui::GetCursorPos(); // where the notes go on a laptop, once the buttons are placed

    Action      pressed = Action::none;
    ImDrawList* canvas  = ImGui::GetWindowDrawList();
    for (std::size_t index = 0UZ; index < buttons.size(); ++index) {
        const Action action = static_cast<Action>(index + 1UZ);
        ImGui::SetCursorPos(ImVec2{placed[index].x, placed[index].y});
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::InvisibleButton("##notes-button", ImVec2{placed[index].width, placed[index].height})) {
            pressed = action;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::PopID();
        const ImVec2 low  = ImGui::GetItemRectMin();
        const ImVec2 high = ImGui::GetItemRectMax();
        buttons[index]    = Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
        canvas->AddRectFilled(low, high, dimmed(theme.text, ImGui::IsItemHovered() ? 0.22f : 0.12f), (high.x - low.x) * 0.15f);
        drawMark(canvas, action, low, high, dimmed(theme.text, 0.85f));
    }

    const float baseNotes = compact ? Fonts::bodySize(std::max(viewport->Size.x, viewport->Size.y)) * 0.7f : Fonts::bodySize(viewport->Size.y) * 0.7f;
    if (!compact) {
        // wrapped rather than laid out by DocumentView: these are the presenter's own words, not slide content, and a
        // second full layout pass for them would compete with the slide for the frame; clear of the buttons
        ImGui::SetCursorPos(flow);
        const ScopedFont body(baseNotes);
        if (notes.empty()) {
            ImGui::TextDisabled("no notes for this view");
            return pressed;
        }
        // line by line, as ImGui would wrap them, so each line is a run a reader can select and copy
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
                    ++begin; // a wrapped line does not start with the space it broke at
                }
            } while (begin < end);
            runs.breakWith("\n");
        }
        ImGui::Dummy(ImVec2{wrapWidth, y - start.y});
        return pressed;
    }

    // on a phone the notes shrink to fit their part of the screen down to a readable floor, then scroll under a
    // finger: ImGui scrolls with a wheel only, so a drag over them is turned into scrolling here
    notesPixels = notes.empty() ? baseNotes : fittingSize(notes, baseNotes, text.width - pad, text.height);
    ImGui::SetCursorPos(ImVec2{text.x, text.y});
    if (ImGui::BeginChild("##notes-text", ImVec2{text.width, text.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav)) {
        const ScopedFont body(notesPixels);
        if (notes.empty()) {
            ImGui::TextDisabled("no notes for this view");
        } else {
            ImGui::PushTextWrapPos(text.width - pad);
            ImGui::TextUnformatted(notes.data(), notes.data() + notes.size());
            ImGui::PopTextWrapPos();
        }
        // the press, not the pointer now, decides: a finger that started on the notes keeps scrolling them wherever
        // it wanders, and pressing on a window already makes ImGui report it as not hovered
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
    ImGui::EndChild();
    notesArea = Rectangle{.x = origin.x + text.x, .y = origin.y + text.y, .width = text.width, .height = text.height};
    return pressed;
}

} // namespace gr::present
