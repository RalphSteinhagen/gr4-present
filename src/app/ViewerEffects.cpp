#include "Viewer.hpp"

#include "Fonts.hpp"

#include <gr4-present/Number.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <ranges>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace gr::present {

namespace {

[[nodiscard]] std::string_view nameOf(std::string_view spec) noexcept { return spec.substr(0UZ, std::min(spec.find(' '), spec.size())); }

[[nodiscard]] std::string_view settingsOf(std::string_view spec) noexcept { return spec.substr(std::min(spec.find(' '), spec.size())); }

/// `iMouse` for an effect in the box `min`-`max`, as `nextMouse` steps it
void followPointer(const Viewer& viewer, EffectInstance& instance, ImVec2 min, ImVec2 max) {
    const ImGuiIO& io = ImGui::GetIO();
    // the box is where the slide put it; the zoom moves both it and the picture, so the pointer goes back through it
    const float      across = (viewer.zoom.slideX(io.MousePos.x) - min.x) / (max.x - min.x);
    const float      down   = (viewer.zoom.slideY(io.MousePos.y) - min.y) / (max.y - min.y);
    const MouseState next   = nextMouse(MouseState{.mouse = instance.mouse, .pressed = instance.pressed}, PointerFrame{.x = across * static_cast<float>(instance.size[0]), .y = (1.0f - down) * static_cast<float>(instance.size[1]), .inside = across >= 0.0f && across <= 1.0f && down >= 0.0f && down <= 1.0f, .clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left), .down = io.MouseDown[ImGuiMouseButton_Left]});
    instance.mouse          = next.mouse;
    instance.pressed        = next.pressed;
}

constexpr float kStillRate = 60.0f; // frames a second an exported page steps an effect at to its still time (D25)

void renderInstance(const ImDrawList*, const ImDrawCmd* command) {
    auto& instance = *static_cast<EffectInstance*>(command->UserCallbackData);
    if (instance.renderer == nullptr) {
        return;
    }
    if (instance.stillFrames <= 0) {
        std::ignore = instance.renderer->drawToTexture(instance.size[0], instance.size[1], instance.inputs);
        return;
    }
    // an exported page: from cleared buffers, one frame at a time to the still time, whatever the clock says
    instance.renderer->clearBuffers();
    EffectInputs inputs = instance.inputs;
    inputs.timeDelta    = 1.0f / kStillRate;
    inputs.frameRate    = kStillRate;
    for (int frame = 0; frame < instance.stillFrames; ++frame) {
        inputs.frame = frame;
        inputs.time  = static_cast<float>(frame) / kStillRate;
        std::ignore  = instance.renderer->drawToTexture(instance.size[0], instance.size[1], inputs);
    }
}

constexpr float kSlowestScale   = 0.25f; // auto-halving stops here (D26)
constexpr float kTouchScale     = 0.5f;  // where a background or viewport starts on a touch device (D26)
constexpr float kSlowFrameShare = 0.9f;  // a frame rate under this share of the display's counts as too slow
constexpr float kSlowSeconds    = 2.0f;  // for this long before a scale is halved

/// an export, the presenter's window, or a test page opened with `?frozen`: every effect at its still time and at the
/// scale it was given, never one the device or the clock chose (D20, D25, D37)
[[nodiscard]] bool frozen(const Viewer& viewer) noexcept { return viewer.exporting || viewer.presenter || viewer.options.contains("frozen"); }

[[nodiscard]] bool onTouchDevice() noexcept {
#ifdef __EMSCRIPTEN__
    static const bool coarse = EM_ASM_INT({ return window.matchMedia('(pointer: coarse)').matches ? 1 : 0; }) != 0;
    return coarse;
#else
    return false;
#endif
}

/// the instance `key` draws `spec` as, started over when it was not drawn last frame unless `run` keeps it running;
/// nothing when it cannot be drawn
[[nodiscard]] EffectInstance* prepared(Viewer& viewer, std::string_view key, std::string_view spec, ImVec2 min, ImVec2 max, const EffectRun& run) {
    if (spec.empty() || spec == "none" || max.x <= min.x || max.y <= min.y) {
        return nullptr;
    }
    auto found = viewer.effectInstances.find(key);
    if (found == viewer.effectInstances.end() || found->second.spec != spec) {
        const float    scale = run.scale.value_or(onTouchDevice() && !frozen(viewer) ? kTouchScale : 1.0f);
        EffectInstance fresh{.spec = std::string{spec}, .renderer = viewer.effects.renderer(nameOf(spec)), .scale = scale};
        if (fresh.renderer != nullptr) {
            fresh.inputs.parameters = viewer.effects.parametersOf(fresh.renderer->effect(), settingsOf(spec));
        }
        found = viewer.effectInstances.insert_or_assign(std::string{key}, std::move(fresh)).first;
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
    if (instance.lastFrame == -2 || (instance.lastFrame < frame - 1 && !run.keepsRunning)) {
        // not drawn last frame: the slide was just entered, so the effect starts from its beginning
        instance.startedAt  = ImGui::GetTime();
        instance.rendered   = 0;
        instance.renderedAt = -1.0;
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
    instance.stillFrames  = frozen(viewer) ? 1 + static_cast<int>(std::lround(instance.renderer->effect().still.value_or(0.0f) * kStillRate)) : 0;
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

constexpr std::string_view kRecordedEffect = "effect:"; // an exported picture's source: the instance it is read back from
constexpr std::string_view kRecordedStage  = "stage:";  // or the stage whose slide it shows

/// an exported page shows an effect as a picture of what it rendered, read back from its own texture once the frame is
/// rendered, so the words drawn over it stay words
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
    const bool effect = source.starts_with(kRecordedEffect);
    if (!effect && !source.starts_with(kRecordedStage)) {
        return std::nullopt;
    }
    RecordedPixels pixels;
    if (effect) {
        const auto found = viewer.effectInstances.find(source.substr(kRecordedEffect.size()));
        if (found == viewer.effectInstances.end() || found->second.renderer == nullptr || !found->second.renderer->ready()) {
            return RecordedPixels{}; // nothing to show; the page keeps the box empty
        }
        const auto [width, height] = found->second.size;
        pixels                     = RecordedPixels{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ)};
        found->second.renderer->readOutput(width, height, pixels.rgba);
    } else {
        const std::string_view rest  = source.substr(kRecordedStage.size());
        const std::size_t      slash = rest.rfind('/');
        const auto             found = viewer.stages.find(rest.substr(0UZ, slash));
        if (found == viewer.stages.end() || slash == std::string_view::npos) {
            return RecordedPixels{};
        }
        const ContentCapture& capture = rest.substr(slash + 1UZ) == "to" ? found->second.to : found->second.from;
        const auto [width, height]    = capture.size();
        pixels                        = RecordedPixels{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ)};
        capture.readPixels(pixels.rgba);
    }
    // a texture counts rows from the bottom; a recording, from the top
    const std::size_t stride = static_cast<std::size_t>(pixels.width) * 4UZ;
    for (std::size_t row = 0UZ; row < pixels.height / 2U; ++row) {
        std::swap_ranges(pixels.rgba.begin() + static_cast<std::ptrdiff_t>(row * stride), pixels.rgba.begin() + static_cast<std::ptrdiff_t>((row + 1UZ) * stride), pixels.rgba.begin() + static_cast<std::ptrdiff_t>((pixels.height - 1UZ - row) * stride));
    }
    for (std::size_t alpha = 3UZ; alpha < pixels.rgba.size(); alpha += 4UZ) {
        pixels.rgba[alpha] = 255U; // what shows is opaque, as the frame's read-back is
    }
    return pixels;
}

void recordStagePicture(Viewer& viewer, std::string_view stageKey, bool arrived, const ImDrawList& list, const Rectangle& box) { recordPicture(viewer, std::format("{}{}/{}", kRecordedStage, stageKey, arrived ? "to" : "from"), list, box); }

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
    const auto found = viewer.effectInstances.find(key);
    if (found == viewer.effectInstances.end() || found->second.capture == nullptr || found->second.lastFrame != ImGui::GetFrameCount()) {
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
    instance->inputs.focus    = {0.5f, 0.5f}; // a ripple or the like starts from the middle of the box
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
    // under an `fps` cap a frame between two renders shows the last one again; one drawn twice in a frame, as a
    // background is on both slides of a transition, renders once
    const double now = ImGui::GetTime();
    if (instance.renderedAt != now && (run.fps <= 0.0f || instance.renderedAt < 0.0 || now - instance.renderedAt >= 1.0 / static_cast<double>(run.fps))) {
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
    // `aspect:` keeps that shape, as large as the region allows and centred in it
    const float       aspect = aspectOf(block.field("aspect"));
    const float       width  = aspect <= 0.0f ? region.width : std::min(region.width, region.height * aspect);
    const float       height = aspect <= 0.0f ? region.height : width / aspect;
    const Rectangle   box{.x = region.x + (region.width - width) * 0.5f, .y = region.y + (region.height - height) * 0.5f, .width = width, .height = height};
    const std::string spec  = block.field("params").empty() ? std::string{effect} : std::format("{} {}", effect, block.field("params"));
    const auto        scale = parseNumber<float>(block.field("scale"));
    const EffectRun   run{.scale = scale ? std::optional{std::clamp(*scale, 0.05f, 1.0f)} : std::nullopt, .fps = std::max(parseNumber<float>(block.field("fps")).value_or(0.0f), 0.0f), .keepsRunning = block.field("lifecycle") == "persistent" || block.field("lifecycle") == "keep-alive"};
    ImDrawList&       list = *ImGui::GetWindowDrawList();
    const std::string key  = std::format("viewport:{}/{}", viewId, block.id.empty() ? block.info : block.id);
    if (drawEffectInto(viewer, key, spec, list, ImVec2{box.x, box.y}, ImVec2{box.x + box.width, box.y + box.height}, run) != nullptr) {
        recordPicture(viewer, std::format("{}{}", kRecordedEffect, key), list, box);
        return true;
    }
    // an effect that cannot be drawn here -- effects off, unknown, not compiling -- shows its `fallback:` still
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
        const ImVec2 low  = list.GetClipRectMin();
        const ImVec2 high = list.GetClipRectMax();
        viewer.documentView.recording->primitives.emplace_back(RecordedImage{.kind = RecordedImageKind::figure, .source = std::string{still}, .at = Rectangle{.x = origin.x, .y = origin.y, .width = size.x, .height = size.y}, .clip = Rectangle{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y}});
    }
    return true;
}

void drawBackgroundEffect(Viewer& viewer, std::string_view spec, ImDrawList& list) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // one instance per background, so consecutive slides that share it keep its time running
    const std::string key = std::format("background:{}", spec);
    if (drawEffectInto(viewer, key, spec, list, viewport->Pos, ImVec2{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, EffectRun{}) != nullptr) {
        recordPicture(viewer, std::format("{}{}", kRecordedEffect, key), list, Rectangle{.x = viewport->Pos.x, .y = viewport->Pos.y, .width = viewport->Size.x, .height = viewport->Size.y});
    }
}

float revealSecondsOf(Viewer& viewer, std::string_view kind) {
    if (std::ranges::contains(kRevealNames, kind)) {
        return kRevealSeconds;
    }
    const EffectSource* effect = viewer.effects.find(kind);
    return effect != nullptr ? effect->duration.value_or(kRevealSeconds) : kRevealSeconds;
}

void tendEffects(Viewer& viewer) {
    // the frame rate is the only clock every platform has, a GPU timer query is missing from most browsers; only a
    // background or a viewport gives up resolution (D26), never an overlay, a reveal, the pointer or the break screen
    const float display  = onTouchDevice() ? 30.0f : 60.0f;
    const int   frame    = ImGui::GetFrameCount();
    const auto  halvable = [frame](const auto& entry) { return entry.second.lastFrame == frame && entry.second.renderer != nullptr && (entry.first.starts_with("background:") || entry.first.starts_with("viewport:")); };
    auto        drawn    = viewer.effectInstances | std::views::filter(halvable) | std::views::values;
    if (frozen(viewer) || std::ranges::empty(drawn) || ImGui::GetIO().Framerate >= kSlowFrameShare * display) {
        viewer.effectsSlowFor = 0.0f;
        return;
    }
    viewer.effectsSlowFor += ImGui::GetIO().DeltaTime;
    if (viewer.effectsSlowFor < kSlowSeconds) {
        return;
    }
    viewer.effectsSlowFor = 0.0f;
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
    if (!viewer.pointing || viewer.presenter || viewer.exporting || list == nullptr) {
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const std::string_view spec     = viewer.loader.manifest().pointer.empty() ? std::string_view{"laser"} : std::string_view{viewer.loader.manifest().pointer};
    const ImGuiViewport*   viewport = ImGui::GetMainViewport();
    const ImVec2           low      = viewport->Pos;
    const ImVec2           high{viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y};
    // drawn into the slide's list, so it is zoomed with the slide and the pointer goes back through the zoom to reach it
    EffectInstance* instance = drawEffectInto(viewer, "pointer", spec, *list, low, high, EffectRun{.scale = 1.0f});
    if (instance == nullptr) {
        return;
    }
    // the pointer is followed whether pressed or not; a press still shows in `zw` the way `iMouse` has it
    const ImVec2 at            = ImGui::GetIO().MousePos;
    instance->inputs.mouse[0]  = (viewer.zoom.slideX(at.x) - low.x) / (high.x - low.x) * static_cast<float>(instance->size[0]);
    instance->inputs.mouse[1]  = (1.0f - (viewer.zoom.slideY(at.y) - low.y) / (high.y - low.y)) * static_cast<float>(instance->size[1]);
    instance->inputs.keepAlpha = true;
}

void drawBreakScreen(Viewer& viewer) {
    if (!viewer.onBreak || viewer.presenter || viewer.exporting) {
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

    const double      left    = std::max(0.0, viewer.breakEndsAt - ImGui::GetTime());
    const int         seconds = static_cast<int>(std::ceil(left));
    const std::string words   = seconds > 0 ? std::format("back in {:02}:{:02}", seconds / 60, seconds % 60) : std::string{"back now"};
    ImFont* const     face    = Fonts::instance().activeFaces().title;
    const float       size    = viewport->Size.y * 0.12f;
    const ImVec2      extent  = face->CalcTextSizeA(size, FLT_MAX, 0.0f, words.c_str());
    list.AddText(face, size, ImVec2{low.x + (viewport->Size.x - extent.x) * 0.5f, low.y + (viewport->Size.y - extent.y) * 0.5f}, viewer.theme.text, words.c_str());
}

} // namespace gr::present
