#include "Viewer.hpp"

#include <gr4-present/EffectSource.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace gr::present {

namespace {

constexpr float kStageHoldSeconds = 1.0f; // how long a stage holds each picture between two plays of its transition
constexpr float kStageZoomReach   = 0.1f; // as `kZoomTransitionReach`: how far the zoom move scales either picture

[[nodiscard]] std::string_view trimmed(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t");
    const auto last  = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first, last - first + 1UZ);
}

[[nodiscard]] const Block* stageOf(const SectionVisual& visual) {
    if (visual.section == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(visual.section->document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && block.info == "stage"; });
    return found == visual.section->document.blocks.end() ? nullptr : &*found;
}

[[nodiscard]] std::string stageKey(std::string_view viewId, const Block& block) { return std::format("{}/{}", viewId, block.id.empty() ? std::string_view{"stage"} : std::string_view{block.id}); }

/// a slide as it looks once all of it is shown
[[nodiscard]] Cursor completed(const Viewer& viewer, std::string_view viewId) {
    const auto& views = viewer.navigator.graph.views;
    const auto  view  = std::ranges::find(views, viewId, &View::id);
    return Cursor{.viewId = std::string{viewId}, .step = view == views.end() ? 0UZ : view->stepCount - 1UZ};
}

/// where the stage is in its cycle: which picture leaves, which arrives, and how far (eased) the move has come
struct StagePlay {
    bool  backwards = false;
    float progress  = 0.0f;
};

[[nodiscard]] StagePlay playAt(float seconds, float duration) noexcept {
    const float cycle = 2.0f * (duration + kStageHoldSeconds);
    const float at    = std::fmod(std::max(seconds, 0.0f), cycle);
    const float half  = duration + kStageHoldSeconds;
    const bool  back  = at >= half;
    const float into  = back ? at - half : at;
    return StagePlay{.backwards = back, .progress = smoothstep(std::clamp(into / std::max(duration, 0.001f), 0.0f, 1.0f))};
}

void drawPicture(ImDrawList& list, unsigned texture, ImVec2 low, ImVec2 high, float alpha) {
    if (texture == 0U || alpha <= 0.0f) {
        return;
    }
    // a GL texture's first row is its bottom one
    list.AddImage(static_cast<ImTextureID>(texture), low, high, ImVec2{0.0f, 1.0f}, ImVec2{1.0f, 0.0f}, IM_COL32(255, 255, 255, static_cast<int>(std::lround(255.0f * std::clamp(alpha, 0.0f, 1.0f)))));
}

/// the CPU moves, in a box: each picture is the whole slide shown small, moved or faded as the full-size move would
void drawMove(ImDrawList& list, Transition::Kind kind, Transition::Direction direction, unsigned leaving, unsigned arriving, ImVec2 low, ImVec2 high, float p) {
    const ImVec2 size{high.x - low.x, high.y - low.y};
    const ImVec2 moves = [direction]() -> ImVec2 {
        switch (direction) {
        case Transition::Direction::right: return {1.0f, 0.0f};
        case Transition::Direction::up: return {0.0f, -1.0f};
        case Transition::Direction::down: return {0.0f, 1.0f};
        case Transition::Direction::left:
        case Transition::Direction::byOrder: break;
        }
        return {-1.0f, 0.0f};
    }();
    const auto shifted = [&](unsigned texture, ImVec2 by, float alpha) { drawPicture(list, texture, ImVec2{low.x + by.x, low.y + by.y}, ImVec2{high.x + by.x, high.y + by.y}, alpha); };
    const auto scaled  = [&](unsigned texture, float scale, float alpha) {
        const ImVec2 centre{(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
        drawPicture(list, texture, ImVec2{centre.x - size.x * 0.5f * scale, centre.y - size.y * 0.5f * scale}, ImVec2{centre.x + size.x * 0.5f * scale, centre.y + size.y * 0.5f * scale}, alpha);
    };
    const ImVec2 leavingTo{moves.x * p * size.x, moves.y * p * size.y};
    const ImVec2 arrivingFrom{-moves.x * (1.0f - p) * size.x, -moves.y * (1.0f - p) * size.y};
    switch (kind) {
    case Transition::Kind::cut: shifted(p < 1.0f ? leaving : arriving, {}, 1.0f); break;
    case Transition::Kind::fadeThrough:
        shifted(leaving, {}, 1.0f - 2.0f * p);
        shifted(arriving, {}, 2.0f * p - 1.0f);
        break;
    case Transition::Kind::push:
        shifted(leaving, leavingTo, 1.0f);
        shifted(arriving, arrivingFrom, 1.0f);
        break;
    case Transition::Kind::cover:
        shifted(leaving, {}, 1.0f);
        shifted(arriving, arrivingFrom, 1.0f);
        break;
    case Transition::Kind::uncover:
        shifted(arriving, {}, 1.0f);
        shifted(leaving, leavingTo, 1.0f);
        break;
    case Transition::Kind::zoom:
        scaled(leaving, 1.0f + kStageZoomReach * p, 1.0f - p);
        scaled(arriving, 1.0f - kStageZoomReach * (1.0f - p), p);
        break;
    default: // fade, and whatever a stage cannot show, which the deck check reports
        shifted(leaving, {}, 1.0f);
        shifted(arriving, {}, p);
        break;
    }
}

} // namespace

std::vector<std::string> stageTransitionsOf(const Block& block) {
    std::vector<std::string> transitions;
    for (const auto piece : std::views::split(block.field("transitions"), ',')) {
        if (const std::string_view value = trimmed(std::string_view{piece.begin(), piece.end()}); !value.empty()) {
            transitions.emplace_back(value);
        }
    }
    return transitions;
}

void captureStageSlides(Viewer& viewer, const SectionVisual& visual) {
    const Block* block = stageOf(visual);
    if (block == nullptr) {
        return;
    }
    StageState&        stage = viewer.stages[stageKey(visual.section->id, *block)];
    DocumentView&      view  = viewer.documentView;
    const EffectInputs theme = effectInputsFor(viewer);
    // drawn before the slide itself, so what the slide's own drawing records -- links, text, its draw list -- is its own;
    // an exported page records the slide alone, never the slides its stage shows
    auto* const recording = view.recording;
    view.recording        = nullptr;
    for (auto&& [field, capture] : {std::pair{std::string_view{"from"}, &stage.from}, std::pair{std::string_view{"to"}, &stage.to}}) {
        const std::string_view viewId = block->field(field);
        if (viewId.empty() || viewId == visual.section->id) {
            continue;
        }
        const Cursor        cursor   = completed(viewer, viewId);
        const SectionVisual pictured = visualFor(viewer, cursor);
        if (pictured.section == nullptr) {
            continue;
        }
        view.beforeDrawing = [capture, &theme](ImDrawList& list) { capture->begin(list, theme.themeBackground); };
        view.afterDrawing  = [capture](ImDrawList& list) { capture->end(list); };
        drawSection(viewer, pictured, cursor.step, 1.0f, pictured.image.empty() ? pictured.frame : pictured.region, false);
    }
    view.beforeDrawing = {};
    view.afterDrawing  = {};
    view.recording     = recording;
}

bool drawStage(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box, std::size_t step) {
    const auto found = viewer.stages.find(stageKey(viewId, block));
    if (found == viewer.stages.end() || found->second.from.texture() == 0U || found->second.to.texture() == 0U) {
        return false; // its slides are captured from the next frame on
    }
    StageState& stage = found->second;
    ImDrawList& list  = *ImGui::GetWindowDrawList();
    // the slides keep their shape, centred in the box, and a frame shows where the small screen ends
    const auto [slideWidth, slideHeight] = stage.from.size();
    const float  scale                   = std::min(box.width / static_cast<float>(std::max(slideWidth, 1)), box.height / static_cast<float>(std::max(slideHeight, 1)));
    const ImVec2 fitted{static_cast<float>(slideWidth) * scale, static_cast<float>(slideHeight) * scale};
    const ImVec2 low{box.x + (box.width - fitted.x) * 0.5f, box.y + (box.height - fitted.y) * 0.5f};
    const ImVec2 high{low.x + fitted.x, low.y + fitted.y};
    list.AddRect(ImVec2{low.x - 1.0f, low.y - 1.0f}, ImVec2{high.x + 1.0f, high.y + 1.0f}, dimmed(viewer.theme.text, 0.4f));
    if (stage.step != step) {
        stage.step      = step;
        stage.startedAt = ImGui::GetTime();
    }
    if (viewer.documentView.recording != nullptr) {
        // a page is still: the stage at rest, read back from the slide it shows
        recordStagePicture(viewer, found->first, step > 0UZ, list, Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y});
        return true;
    }
    list.PushClipRect(low, high, true);
    list.AddRectFilled(low, high, viewer.theme.background);
    const std::vector<std::string> transitions = stageTransitionsOf(block);
    if (step == 0UZ || transitions.empty()) {
        drawPicture(list, stage.from.texture(), low, high, 1.0f);
        list.PopClipRect();
        return true;
    }
    const std::string&     spec     = transitions[std::min(step, transitions.size()) - 1UZ];
    const Transition::Kind kind     = transitionKindFor(spec, false);
    const EffectSource*    effect   = kind == Transition::Kind::shader ? viewer.effects.find(effectNamed(spec)) : nullptr;
    const float            duration = effect != nullptr ? effect->duration.value_or(kShaderSeconds) : viewer.loader.manifest().transitionSeconds;
    const StagePlay        play     = playAt(static_cast<float>(ImGui::GetTime() - stage.startedAt), duration);
    const unsigned         leaving  = play.backwards ? stage.to.texture() : stage.from.texture();
    const unsigned         arriving = play.backwards ? stage.from.texture() : stage.to.texture();
    if (play.progress >= 1.0f) {
        // at rest between two plays the box is the slide itself, as a slide is once its transition has ended
        drawPicture(list, arriving, low, high, 1.0f);
        list.PopClipRect();
        return true;
    }
    EffectInstance* instance = effect != nullptr ? drawEffectInto(viewer, std::format("stage:{}/{}", viewId, spec), spec, list, low, high, EffectRun{.scale = 1.0f}) : nullptr;
    if (instance != nullptr) {
        // read when the frame is rendered, after both slides were captured earlier in the same list
        instance->inputs.slideFrom = leaving;
        instance->inputs.slideTo   = arriving;
        instance->inputs.progress  = play.progress;
        instance->inputs.focus     = {0.5f, 0.5f};
    } else {
        drawMove(list, kind == Transition::Kind::shader ? Transition::Kind::fade : kind, transitionDirectionFor(spec.substr(0UZ, spec.find(' '))), leaving, arriving, low, high, play.progress);
    }
    list.PopClipRect();
    return true;
}

} // namespace gr::present
