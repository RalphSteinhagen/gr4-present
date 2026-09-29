#include "Viewer.hpp"

#include "ScopedWindow.hpp"

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

[[nodiscard]] std::string stageKey(std::string_view viewId, const Block& block) { return std::format("{}/{}", viewId, block.id.empty() ? std::string_view{"stage"} : std::string_view{block.id}); }

/// a slide with all its steps shown
[[nodiscard]] Cursor completed(const Viewer& viewer, std::string_view viewId) {
    const auto& views = viewer.navigator.graph.views;
    const auto  view  = std::ranges::find(views, viewId, &View::id);
    return Cursor{.viewId = std::string{viewId}, .step = view == views.end() ? 0UZ : view->stepCount - 1UZ};
}

void drawPicture(ImDrawList& list, unsigned texture, ImVec2 low, ImVec2 high, float alpha) {
    if (texture == 0U || alpha <= 0.0f) {
        return;
    }
    // a GL texture's first row is its bottom one
    list.AddImage(static_cast<ImTextureID>(texture), low, high, ImVec2{0.0f, 1.0f}, ImVec2{1.0f, 0.0f}, IM_COL32(255, 255, 255, static_cast<int>(std::lround(255.0f * std::clamp(alpha, 0.0f, 1.0f)))));
}

/// the CPU moves in a box: each picture is the whole slide shown small, moved or faded as the full-size move would
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
        scaled(leaving, 1.0f + kZoomReach * p, 1.0f - p);
        scaled(arriving, 1.0f - kZoomReach * (1.0f - p), p);
        break;
    default: // fade, and whatever a stage cannot show, which the deck check reports
        shifted(leaving, {}, 1.0f);
        shifted(arriving, {}, p);
        break;
    }
}

} // namespace

void captureStageSlides(Viewer& viewer, const SectionVisual& visual) {
    if (visual.section == nullptr) {
        return;
    }
    const auto stageBlock = std::ranges::find_if(visual.section->document.blocks, [](const Block& candidate) { return candidate.kind == BlockKind::directive && candidate.info == "stage"; });
    if (stageBlock == visual.section->document.blocks.end()) {
        return;
    }
    const Block*       block = &*stageBlock;
    StageState&        stage = viewer.effectRuns.stages[stageKey(visual.section->id, *block)];
    DocumentView&      view  = viewer.documentView;
    const EffectInputs theme = effectInputsFor(viewer);
    // drawn before the slide itself, so its links, text and draw list stay its own; an export records the slide alone
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
        drawSection(viewer, pictured, cursor.step, 1.0f, pictured.cameraFrame(), false);
    }
    view.beforeDrawing = {};
    view.afterDrawing  = {};
    view.recording     = recording;
}

bool drawStage(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& box, std::size_t step) {
    const auto found = viewer.effectRuns.stages.find(stageKey(viewId, block));
    if (found == viewer.effectRuns.stages.end() || found->second.from.texture() == 0U || found->second.to.texture() == 0U) {
        return false; // its slides are captured from the next frame on
    }
    StageState& stage = found->second;
    ImDrawList& list  = *ImGui::GetWindowDrawList();
    // the slides keep their shape, centred, with a frame where the small screen ends
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
        recordStagePicture(viewer, found->first, step > 0UZ, list, Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y});
        return true;
    }
    const ScopedClipRect inStage{&list, low, high};
    list.AddRectFilled(low, high, viewer.theme.background);
    const std::vector<std::string> transitions = parseNameList(block.field("transitions"));
    if (step == 0UZ || transitions.empty()) {
        drawPicture(list, stage.from.texture(), low, high, 1.0f);
        return true;
    }
    const std::string&     spec     = transitions[std::min(step, transitions.size()) - 1UZ];
    const Transition::Kind kind     = transitionKindFor(spec, false);
    const EffectSource*    effect   = kind == Transition::Kind::shader ? viewer.effects.find(effectNamed(spec)) : nullptr;
    const float            duration = effect != nullptr ? effect->durationSeconds.value_or(kShaderSeconds) : viewer.loader.manifest().transitionSeconds;
    const StagePlay        play     = stagePlayAt(static_cast<float>(ImGui::GetTime() - stage.startedAt), duration);
    const unsigned         leaving  = play.backwards ? stage.to.texture() : stage.from.texture();
    const unsigned         arriving = play.backwards ? stage.from.texture() : stage.to.texture();
    if (play.progress >= 1.0f) {
        // at rest between two plays the box is the slide itself
        drawPicture(list, arriving, low, high, 1.0f);
        return true;
    }
    EffectInstance* instance = effect != nullptr ? drawEffectInto(viewer, std::format("stage:{}/{}", viewId, spec), spec, list, low, high, EffectRun{.scale = 1.0f}) : nullptr;
    if (instance != nullptr) {
        // read when the frame renders, after both slides were captured earlier in the same list
        instance->inputs.slideFrom = leaving;
        instance->inputs.slideTo   = arriving;
        instance->inputs.progress  = play.progress;
        instance->inputs.focus     = {0.5f, 0.5f};
    } else {
        drawMove(list, kind == Transition::Kind::shader ? Transition::Kind::fade : kind, transitionDirectionFor(spec.substr(0UZ, spec.find(' '))), leaving, arriving, low, high, play.progress);
    }
    return true;
}

} // namespace gr::present
