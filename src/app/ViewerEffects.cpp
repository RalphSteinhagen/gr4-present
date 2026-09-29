#include "Canvas.hpp"
#include "Viewer.hpp"

#include "Fonts.hpp"

#include <gr4-present/EffectSource.hpp>
#include <gr4-present/Number.hpp>
#include <gr4-present/Slice.hpp>

#include <imgui.h>

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <format>
#include <ranges>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace gr::present {

namespace {

[[nodiscard]] std::string_view nameOf(std::string_view spec) noexcept { return slice(spec, 0UZ, spec.find(' ')); }

[[nodiscard]] std::string_view settingsOf(std::string_view spec) noexcept { return slice(spec, spec.find(' ')); }

void followPointer(const Viewer& viewer, EffectInstance& instance, ImVec2 min, ImVec2 max) {
    const ImGuiIO& io = ImGui::GetIO();
    // the zoom moves the box and the picture, so the pointer goes back through it
    const float        across = (viewer.zoom.slideX(io.MousePos.x) - min.x) / (max.x - min.x);
    const float        down   = (viewer.zoom.slideY(io.MousePos.y) - min.y) / (max.y - min.y);
    const bool         inside = across >= 0.0f && across <= 1.0f && down >= 0.0f && down <= 1.0f;
    const PointerFrame pointer{
        .x       = across * static_cast<float>(instance.size[0]),
        .y       = (1.0f - down) * static_cast<float>(instance.size[1]),
        .inside  = inside,
        .clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        .down    = io.MouseDown[ImGuiMouseButton_Left],
    };
    const MouseState next = nextMouse(MouseState{.mouse = instance.mouse, .pressed = instance.pressed}, pointer);
    instance.mouse        = next.mouse;
    instance.pressed      = next.pressed;
}

constexpr float kStillFramesPerSecond = 60.0f; // an exported page steps an effect at this rate to its still time (D25)

void renderInstance(const ImDrawList*, const ImDrawCmd* command) {
    auto& instance = *static_cast<EffectInstance*>(command->UserCallbackData);
    if (instance.renderer == nullptr) {
        return;
    }
    if (instance.stillFrames <= 0) {
        std::ignore = instance.renderer->drawToTexture(instance.size[0], instance.size[1], instance.inputs);
        return;
    }
    // an exported page: from cleared buffers, frame by frame to the still time, whatever the clock says
    instance.renderer->clearBuffers();
    EffectInputs inputs = instance.inputs;
    inputs.timeDelta    = 1.0f / kStillFramesPerSecond;
    inputs.frameRate    = kStillFramesPerSecond;
    for (int frame = 0; frame < instance.stillFrames; ++frame) {
        inputs.frame = frame;
        inputs.time  = static_cast<float>(frame) / kStillFramesPerSecond;
        std::ignore  = instance.renderer->drawToTexture(instance.size[0], instance.size[1], inputs);
    }
}

constexpr float kSlowestScale   = 0.25f; // auto-halving stops here (D26)
constexpr float kTouchScale     = 0.5f;  // where a background or viewport starts on a touch device (D26)
constexpr float kSlowFrameShare = 0.9f;  // a frame rate under this share of the display's counts as too slow
constexpr float kSlowSeconds    = 2.0f;

/// an export, the presenter's window or a `?frozen` page: effects at their still time and given scale (D20, D25, D37)
[[nodiscard]] bool frozen(const Viewer& viewer) noexcept { return viewer.exporting || viewer.presenter || viewer.options.contains("frozen"); }

[[nodiscard]] bool onTouchDevice() noexcept {
#ifdef __EMSCRIPTEN__
    static const bool coarse = EM_ASM_INT({ return window.matchMedia('(pointer: coarse)').matches ? 1 : 0; }) != 0;
    return coarse;
#else
    return false;
#endif
}

/// the instance `key` draws `spec` as, started over unless drawn last frame or kept running; null when undrawable
[[nodiscard]] EffectInstance* prepared(Viewer& viewer, std::string_view key, std::string_view spec, ImVec2 min, ImVec2 max, const EffectRun& run) {
    if (spec.empty() || spec == "none" || max.x <= min.x || max.y <= min.y) {
        return nullptr;
    }
    auto found = viewer.effectRuns.instances.find(key);
    if (found == viewer.effectRuns.instances.end() || found->second.spec != spec) {
        const float    scale = run.scale.value_or(onTouchDevice() && !frozen(viewer) ? kTouchScale : 1.0f);
        EffectInstance fresh{.spec = std::string{spec}, .renderer = viewer.effects.renderer(nameOf(spec)), .scale = scale};
        if (fresh.renderer != nullptr) {
            fresh.inputs.parameters = viewer.effects.parametersOf(fresh.renderer->effect(), settingsOf(spec));
        }
        found = viewer.effectRuns.instances.insert_or_assign(std::string{key}, std::move(fresh)).first;
    }
    EffectInstance& instance = found->second;
    if (instance.renderer == nullptr) {
        return nullptr; // reported where the deck was checked
    }
    if (!instance.renderer->ready()) {
        viewer.effects.reportFailure(*instance.renderer);
        return nullptr;
    }
    viewer.effects.reportProblems(*instance.renderer); // found while the last frame was rendered
    const int frame = ImGui::GetFrameCount();
    if (!instance.lastFrame || (*instance.lastFrame < frame - 1 && !run.keepsRunning)) {
        instance.startedAt = ImGui::GetTime();
        instance.rendered  = 0;
        instance.renderedAt.reset();
        instance.renderer->clearBuffers();
    }
    instance.lastFrame = frame;

    const ImVec2 pixels        = ImGui::GetIO().DisplayFramebufferScale;
    instance.size              = {std::max(1, static_cast<int>(std::lround((max.x - min.x) * pixels.x * instance.scale))), std::max(1, static_cast<int>(std::lround((max.y - min.y) * pixels.y * instance.scale)))};
    auto parameters            = std::move(instance.inputs.parameters);
    instance.inputs            = effectInputsFor(viewer);
    instance.inputs.parameters = std::move(parameters);
    instance.inputs.time       = static_cast<float>(ImGui::GetTime() - instance.startedAt);
    instance.inputs.frame      = instance.rendered;
    followPointer(viewer, instance, min, max);
    instance.inputs.mouse = instance.mouse;
    instance.stillFrames  = frozen(viewer) ? 1 + static_cast<int>(std::lround(instance.renderer->effect().stillSeconds.value_or(0.0f) * kStillFramesPerSecond)) : 0;
    return &instance;
}

void compositeInstance(const ImDrawList*, const ImDrawCmd* command) {
    auto& instance = *static_cast<EffectInstance*>(command->UserCallbackData);
    if (instance.renderer == nullptr || instance.capture == nullptr) {
        return;
    }
    instance.inputs.content    = instance.capture->texture();
    const auto [width, height] = instance.capture->size();
    instance.renderer->drawInto({0, 0, width, height}, instance.inputs);
}

constexpr std::string_view kRecordedEffectPrefix = "effect:";
constexpr std::string_view kRecordedStagePrefix  = "stage:";

/// an exported page shows an effect as a picture read back once the frame is rendered, so words over it stay words
void recordPicture(Viewer& viewer, std::string source, const ImDrawList& list, const Rectangle& box) {
    if (viewer.documentView.recording == nullptr) {
        return;
    }
    const ImVec2 low  = list.GetClipRectMin();
    const ImVec2 high = list.GetClipRectMax();
    viewer.documentView.recording->primitives.emplace_back(RecordedImage{.kind = RecordedImageKind::chart, .source = std::move(source), .at = box, .clip = Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y}});
}

} // namespace

std::optional<RecordedPixels> recordedEffectPixels(Viewer& viewer, std::string_view source) {
    // `effect:<instance>` reads what the effect rendered; `stage:<stage>/from|to`, one of the slides a stage shows
    const bool effect = source.starts_with(kRecordedEffectPrefix);
    if (!effect && !source.starts_with(kRecordedStagePrefix)) {
        return std::nullopt;
    }
    const auto blankPixels = [](auto width, auto height) {
        const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ;
        return RecordedPixels{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .rgba = std::vector<std::uint8_t>(byteCount)};
    };
    RecordedPixels pixels;
    if (effect) {
        const auto found = viewer.effectRuns.instances.find(source.substr(kRecordedEffectPrefix.size()));
        if (found == viewer.effectRuns.instances.end() || found->second.renderer == nullptr || !found->second.renderer->ready()) {
            return RecordedPixels{};
        }
        const auto [width, height] = found->second.size;
        pixels                     = blankPixels(width, height);
        found->second.renderer->readOutput(width, height, pixels.rgba);
    } else {
        const std::string_view rest  = source.substr(kRecordedStagePrefix.size());
        const std::size_t      slash = rest.rfind('/');
        const auto             found = viewer.effectRuns.stages.find(rest.substr(0UZ, slash));
        if (found == viewer.effectRuns.stages.end() || slash == std::string_view::npos) {
            return RecordedPixels{};
        }
        const ContentCapture& capture = rest.substr(slash + 1UZ) == "to" ? found->second.to : found->second.from;
        const auto [width, height]    = capture.size();
        pixels                        = blankPixels(width, height);
        capture.readPixels(pixels.rgba);
    }
    uprightOpaqueFromReadBack(pixels);
    return pixels;
}

void recordStagePicture(Viewer& viewer, std::string_view stageKey, bool arrived, const ImDrawList& list, const Rectangle& box) { recordPicture(viewer, std::format("{}{}/{}", kRecordedStagePrefix, stageKey, arrived ? "to" : "from"), list, box); }

void beginOverlay(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, float progress) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    EffectInstance*      instance = prepared(viewer, key, spec, viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, EffectRun{.scale = 1.0f});
    if (instance == nullptr) {
        return;
    }
    instance->inputs.progress = progress;
    ++instance->rendered;
    if (instance->capture == nullptr) {
        instance->capture = std::make_unique<ContentCapture>();
    }
    instance->capture->begin(list, instance->inputs.themeBackground);
}

void endOverlay(Viewer& viewer, std::string_view key, ImDrawList& list) {
    const auto found = viewer.effectRuns.instances.find(key);
    if (found == viewer.effectRuns.instances.end() || found->second.capture == nullptr || found->second.lastFrame != ImGui::GetFrameCount()) {
        return;
    }
    found->second.capture->end(list);
    list.AddCallback(&compositeInstance, &found->second);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

bool drawThroughEffect(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, int firstCommand, float progress, ImVec2 low, ImVec2 high) {
    EffectInstance* instance = prepared(viewer, key, spec, low, high, EffectRun{.scale = 1.0f});
    if (instance == nullptr) {
        return false;
    }
    ++instance->rendered;
    if (instance->reveal == nullptr) {
        instance->reveal = std::make_unique<RevealCapture>();
    }
    instance->inputs.progress = progress;
    instance->inputs.focus    = {0.5f, 0.5f}; // ripples start from the middle of the box
    instance->reveal->insert(list, firstCommand, low, high, instance->renderer.get(), &instance->inputs);
    return true;
}

EffectInstance* drawEffectInto(Viewer& viewer, std::string_view key, std::string_view spec, ImDrawList& list, ImVec2 min, ImVec2 max, const EffectRun& run) {
    EffectInstance* found = prepared(viewer, key, spec, min, max, run);
    if (found == nullptr) {
        return nullptr;
    }
    EffectInstance& instance = *found;
    const unsigned  texture  = instance.renderer->outputTexture(instance.size[0], instance.size[1]);
    // an `fps` cap repeats the last render between two; an instance drawn twice in a frame renders once
    const double now = ImGui::GetTime();
    if (instance.renderedAt != now && (run.fps <= 0.0f || !instance.renderedAt || now - *instance.renderedAt >= 1.0 / static_cast<double>(run.fps))) {
        instance.renderedAt = now;
        ++instance.rendered;
        list.AddCallback(&renderInstance, &instance);
        list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    // a GL texture's first row is its bottom one
    list.AddImage(static_cast<ImTextureID>(texture), min, max, ImVec2{0.0f, 1.0f}, ImVec2{1.0f, 0.0f});
    return found;
}

bool drawShaderViewport(Viewer& viewer, std::string_view viewId, const Block& block, const Rectangle& region) {
    const std::string_view effect = block.field("effect");
    if (effect.empty()) {
        return false; // the placeholder names the box, and the deck check reports the missing effect
    }
    const float                aspect = aspectOf(block.field("aspect"));
    const float                width  = aspect <= 0.0f ? region.width : std::min(region.width, region.height * aspect);
    const float                height = aspect <= 0.0f ? region.height : width / aspect;
    const Rectangle            box{.x = region.x + (region.width - width) * 0.5f, .y = region.y + (region.height - height) * 0.5f, .width = width, .height = height};
    const std::string          spec         = block.field("params").empty() ? std::string{effect} : std::format("{} {}", effect, block.field("params"));
    const auto                 scale        = parseNumber<float>(block.field("scale"));
    const std::optional<float> clampedScale = scale ? std::optional{std::clamp(*scale, 0.05f, 1.0f)} : std::nullopt;
    const float                fps          = std::max(parseNumber<float>(block.field("fps")).value_or(0.0f), 0.0f);
    const std::string_view     lifecycle    = block.field("lifecycle");
    const bool                 keepsRunning = lifecycle == "persistent" || lifecycle == "keep-alive";
    const EffectRun            run{.scale = clampedScale, .fps = fps, .keepsRunning = keepsRunning};
    ImDrawList&                list = *ImGui::GetWindowDrawList();
    const std::string          key  = std::format("viewport:{}/{}", viewId, block.name());
    if (drawEffectInto(viewer, key, spec, list, lowCorner(box), highCorner(box), run) != nullptr) {
        recordPicture(viewer, std::format("{}{}", kRecordedEffectPrefix, key), list, box);
        return true;
    }
    // an effect that cannot be drawn (off, unknown, not compiling) shows its `fallback:` still
    const std::string_view still   = block.field("fallback");
    const Texture*         picture = still.empty() ? nullptr : viewer.figures.get(still, static_cast<std::uint32_t>(box.width));
    if (picture == nullptr || picture->id == 0 || picture->width <= 0.0f || picture->height <= 0.0f) {
        return false;
    }
    const float  factor = std::min(box.width / picture->width, box.height / picture->height);
    const ImVec2 size{picture->width * factor, picture->height * factor};
    const ImVec2 origin{box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f};
    list.AddImage(picture->id, origin, ImVec2{origin.x + size.x, origin.y + size.y});
    if (viewer.documentView.recording != nullptr) {
        const ImVec2    low  = list.GetClipRectMin();
        const ImVec2    high = list.GetClipRectMax();
        const Rectangle at{.x = origin.x, .y = origin.y, .width = size.x, .height = size.y};
        const Rectangle clip{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
        viewer.documentView.recording->primitives.emplace_back(RecordedImage{.kind = RecordedImageKind::figure, .source = std::string{still}, .at = at, .clip = clip});
    }
    return true;
}

void drawBackgroundEffect(Viewer& viewer, std::string_view spec, ImDrawList& list) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // one instance per background, so consecutive slides that share it keep its time running
    const std::string key = std::format("background:{}", spec);
    if (drawEffectInto(viewer, key, spec, list, viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, EffectRun{}) != nullptr) {
        recordPicture(viewer, std::format("{}{}", kRecordedEffectPrefix, key), list, Rectangle{.x = viewport->Pos.x, .y = viewport->Pos.y, .width = viewport->Size.x, .height = viewport->Size.y});
    }
}

float revealSecondsOf(Viewer& viewer, std::string_view kind) {
    if (std::ranges::contains(kRevealNames, kind)) {
        return kRevealSeconds;
    }
    const EffectSource* effect = viewer.effects.find(kind);
    return effect != nullptr ? effect->durationSeconds.value_or(kRevealSeconds) : kRevealSeconds;
}

void tendEffects(Viewer& viewer) {
    // the frame rate is the only clock every platform has; only backgrounds and viewports give up resolution (D26)
    const float display  = onTouchDevice() ? 30.0f : 60.0f;
    const int   frame    = ImGui::GetFrameCount();
    const auto  halvable = [frame](const auto& entry) { return entry.second.lastFrame == frame && entry.second.renderer != nullptr && (entry.first.starts_with("background:") || entry.first.starts_with("viewport:")); };
    auto        drawn    = viewer.effectRuns.instances | std::views::filter(halvable) | std::views::values;
    if (frozen(viewer) || std::ranges::empty(drawn) || ImGui::GetIO().Framerate >= kSlowFrameShare * display) {
        viewer.effectRuns.slowForSeconds = 0.0f;
        return;
    }
    viewer.effectRuns.slowForSeconds += ImGui::GetIO().DeltaTime;
    if (viewer.effectRuns.slowForSeconds < kSlowSeconds) {
        return;
    }
    viewer.effectRuns.slowForSeconds = 0.0f;
    for (EffectInstance& instance : drawn) {
        if (instance.scale <= kSlowestScale) {
            continue;
        }
        instance.scale = std::max(instance.scale * 0.5f, kSlowestScale);
        instance.renderer->clearBuffers();
        if (!instance.slowReported) {
            instance.slowReported = true;
            viewer.diagnostics.report(DiagnosticKind::slowEffect, effectPath(nameOf(instance.spec)), std::format("the display runs at {:.0f} frames a second; drawn at {:.0f}% of its resolution", ImGui::GetIO().Framerate, 100.0f * instance.scale));
        }
    }
}

void drawPointer(Viewer& viewer) {
    ImDrawList* list = viewer.documentView.drawnInto;
    if (!viewer.input.pointing || viewer.presenter || viewer.exporting || list == nullptr) {
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const std::string_view spec     = viewer.loader.manifest().pointer.empty() ? std::string_view{"laser"} : std::string_view{viewer.loader.manifest().pointer};
    const ImGuiViewport*   viewport = ImGui::GetMainViewport();
    const ImVec2           low      = viewport->Pos;
    const ImVec2           high{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
    // drawn into the slide's list, so it zooms with the slide
    EffectInstance* instance = drawEffectInto(viewer, "pointer", spec, *list, low, high, EffectRun{.scale = 1.0f});
    if (instance == nullptr) {
        return;
    }
    // followed whether pressed or not; a press shows in `zw` as `iMouse` has it
    const ImVec2 at            = ImGui::GetIO().MousePos;
    instance->inputs.mouse[0]  = (viewer.zoom.slideX(at.x) - low.x) / (high.x - low.x) * static_cast<float>(instance->size[0]);
    instance->inputs.mouse[1]  = (1.0f - (viewer.zoom.slideY(at.y) - low.y) / (high.y - low.y)) * static_cast<float>(instance->size[1]);
    instance->inputs.keepAlpha = true;
}

void drawBreakScreen(Viewer& viewer) {
    if (!viewer.input.onBreak || viewer.presenter || viewer.exporting) {
        return;
    }
    const Manifest&        manifest = viewer.loader.manifest();
    const std::string_view spec     = !manifest.breakEffect.empty() ? std::string_view{manifest.breakEffect} : std::string_view{manifest.background};
    const ImGuiViewport*   viewport = ImGui::GetMainViewport();
    const ImVec2           low      = viewport->Pos;
    const ImVec2           high{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
    ImDrawList&            list = *ImGui::GetForegroundDrawList();
    list.AddRectFilled(low, high, viewer.theme.background);
    std::ignore = drawEffectInto(viewer, "break", spec, list, low, high, EffectRun{});

    const double      left    = std::max(0.0, viewer.input.breakEndsAt - ImGui::GetTime());
    const int         seconds = static_cast<int>(std::ceil(left));
    const std::string words   = seconds > 0 ? std::format("back in {:02}:{:02}", seconds / 60, seconds % 60) : std::string{"back now"};
    ImFont* const     face    = Fonts::instance().roles().title;
    assert(face != nullptr);
    const float  size   = viewport->Size.y * 0.12f;
    const ImVec2 extent = face->CalcTextSizeA(size, FLT_MAX, 0.0f, words.c_str());
    list.AddText(face, size, ImVec2{low.x + (viewport->Size.x - extent.x) * 0.5f, low.y + (viewport->Size.y - extent.y) * 0.5f}, viewer.theme.text, words.c_str());
}

} // namespace gr::present
