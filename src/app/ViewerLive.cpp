#include "Viewer.hpp"

#include "ScopedWindow.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

namespace {

constexpr auto kLiveGraphRetryDelay = std::chrono::seconds{2};

}

[[nodiscard]] static bool                     drawFallback(Viewer& viewer, const RegionRegistry::Entry& entry, const Rectangle& box);
[[nodiscard]] static bool                     graphFailed(const Viewer& viewer, std::string_view workflow);
[[nodiscard]] static std::string_view         activeWorkflow(const Viewer& viewer, const LiveRegion& binding);
[[nodiscard]] static std::vector<std::string> workflowsOf(const Viewer& viewer, std::string_view viewId, bool neighbour = false);
static void                                   requestDevicesOf(const Viewer& viewer, std::string_view viewId);
[[nodiscard]] static float                    statedAspect(const Viewer& viewer, std::string_view region);

/// the region's `fallback:` recording shown in place of a graph that could not be built, is missing or has stalled
[[nodiscard]] static bool drawFallback(Viewer& viewer, const RegionRegistry::Entry& entry, const Rectangle& box) {
    const Texture* recording = entry.binding.fallback.empty() ? nullptr : viewer.figures.get(entry.binding.fallback, static_cast<std::uint32_t>(box.width));
    if (recording == nullptr || recording->id == 0 || recording->width <= 0.0f || recording->height <= 0.0f) {
        return false;
    }
    const float  factor = std::min(box.width / recording->width, box.height / recording->height);
    const ImVec2 size{recording->width * factor, recording->height * factor};
    const ImVec2 origin{box.x + (box.width - size.x) * 0.5f, box.y + (box.height - size.y) * 0.5f};
    ImDrawList*  canvas = ImGui::GetWindowDrawList();
    canvas->AddImage(recording->id, origin, ImVec2{origin.x + size.x, origin.y + size.y});
    // labelled, so an audience does not take a recording for a measurement
    const char*  label = "recorded";
    const ImVec2 text  = ImGui::CalcTextSize(label);
    const ImVec2 at{origin.x + size.x - text.x - 8.0f, origin.y + size.y - text.y - 6.0f};
    canvas->AddRectFilled(ImVec2{at.x - 4.0f, at.y - 2.0f}, ImVec2{at.x + text.x + 4.0f, at.y + text.y + 2.0f}, dimmed(viewer.theme.background, 0.8f), 3.0f);
    canvas->AddText(at, dimmed(viewer.theme.text, 0.9f), label);
    return true;
}

[[nodiscard]] static bool graphFailed(const Viewer& viewer, std::string_view workflow) {
    if (const auto retry = viewer.live.retryAfter.find(workflow); retry != viewer.live.retryAfter.end() && retry->second > viewer.frameStart) {
        return true;
    }
    const auto graph = viewer.live.graphs.find(workflow);
    return graph != viewer.live.graphs.end() && (!graph->second->problem().empty() || graph->second->stalled(viewer.frameStart));
}

/// the standby until the device the main workflow needs is granted, and while the main one has failed
[[nodiscard]] static std::string_view activeWorkflow(const Viewer& viewer, const LiveRegion& binding) {
    if (binding.standby.empty()) {
        return binding.workflow;
    }
    if ((!binding.needs.empty() && !liveDeviceGranted(binding.needs)) || graphFailed(viewer, binding.workflow)) {
        return binding.standby;
    }
    return binding.workflow;
}

[[nodiscard]] bool drawLiveRegion(Viewer& viewer, std::string_view id, const DocumentView::RegionBoxes& boxes) {
    const RegionRegistry::Entry* entry = viewer.regions.find(id);
    if (entry != nullptr && entry->binding.workflow.empty() && entry->binding.widget == "status" && liveGraphsAvailable()) {
        const Rectangle& box = boxes.charts;
        ImGui::SetCursorScreenPos(ImVec2{box.x, box.y});
        if (const ScopedChild status{(std::string{id} + "/status").c_str(), ImVec2{box.width, box.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground}; status) {
            drawSessionStatusBar();
        }
        return true;
    }
    if (entry == nullptr || entry->binding.workflow.empty()) {
        return false;
    }
    const Rectangle& box = boxes.charts;
    if (viewer.documentView.recording != nullptr) {
        // an exported page takes the region and its bars as the frame shows them, read back once rendered
        const ImVec2    low  = ImGui::GetWindowDrawList()->GetClipRectMin();
        const ImVec2    high = ImGui::GetWindowDrawList()->GetClipRectMax();
        const Rectangle clip{.x = low.x, .y = low.y, .width = high.x - low.x, .height = high.y - low.y};
        for (const std::optional<Rectangle>& drawn : {std::optional{box}, boxes.toolbar, boxes.status}) {
            if (drawn) {
                viewer.documentView.recording->primitives.emplace_back(RecordedImage{.kind = RecordedImageKind::chart, .source = std::string{id}, .at = *drawn, .clip = clip});
            }
        }
    }
    const bool deviceless = viewer.exporting && !entry->binding.needs.empty(); // an export asks for no device
    if (viewer.presenter || (deviceless && entry->binding.standby.empty())) {
        return drawFallback(viewer, *entry, box); // the presenter's window runs no graph
    }

    constexpr double       kGraphExpectedWithinSeconds = 2.0;
    const std::string_view workflow                    = deviceless ? std::string_view{entry->binding.standby} : activeWorkflow(viewer, entry->binding);
    viewer.live.sources.insert_or_assign(std::string{id}, workflow == entry->binding.workflow ? "workflow" : "standby");
    const auto graph = viewer.live.graphs.find(workflow);
    if (graph == viewer.live.graphs.end() && ImGui::GetTime() - viewer.slides.viewSince > kGraphExpectedWithinSeconds) {
        return drawFallback(viewer, *entry, box);
    }
    if (graph != viewer.live.graphs.end() && (!graph->second->problem().empty() || graph->second->stalled(viewer.frameStart))) {
        if (graph->second->stalled(viewer.frameStart) && std::ranges::find(viewer.reportedClips, "stalled/" + std::string{id}) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back("stalled/" + std::string{id});
            viewer.diagnostics.report(DiagnosticKind::unknownLiveWidget, std::string{id}, "the graph stopped making progress; its recording is shown if it has one");
            buildSideMenu(viewer);
        }
        if (drawFallback(viewer, *entry, box)) {
            graph->second->pump(viewer.frameStart); // still tended, so it is shown live again as soon as it moves
            return true;
        }
    }
    if (graph == viewer.live.graphs.end()) {
        // built between frames: a dashboard starts a thread on construction, which under Emscripten waits for the event
        // loop and so freezes a frame, and it reads the ImGui style
        return false; // the region shows its outline until then
    }

    graph->second->pump(viewer.frameStart);

    Rectangle drawnBox = box;
    if (const float aspect = statedAspect(viewer, id); aspect > 0.0f) {
        drawnBox.width  = std::min(box.width, box.height * aspect);
        drawnBox.height = drawnBox.width / aspect;
        drawnBox.x      = box.x + (box.width - drawnBox.width) * 0.5f;
        drawnBox.y      = box.y + (box.height - drawnBox.height) * 0.5f;
    }
    ImGui::SetCursorScreenPos(ImVec2{drawnBox.x, drawnBox.y});
    const bool drawn = [&] {
        const ScopedChild chart{std::string{id}.c_str(), ImVec2{drawnBox.width, drawnBox.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
        viewer.zoomedLists.push_back(ImGui::GetWindowDrawList());
        return static_cast<bool>(chart) && (entry->binding.widget == "flowgraph" ? graph->second->drawFlowgraph(id) : graph->second->draw(entry->binding.legend.value_or(viewer.loader.manifest().liveLegend)));
    }();
    if (boxes.toolbar) {
        viewer.live.toolbarBoxes.insert_or_assign(std::string{id}, *boxes.toolbar);
        ImGui::SetCursorScreenPos(ImVec2{boxes.toolbar->x, boxes.toolbar->y});
        if (const ScopedChild toolbar{(std::string{id} + "/toolbar").c_str(), ImVec2{boxes.toolbar->width, boxes.toolbar->height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground}; toolbar) {
            graph->second->drawToolbar();
        }
    }
    if (boxes.status) {
        ImGui::SetCursorScreenPos(ImVec2{boxes.status->x, boxes.status->y});
        if (const ScopedChild status{(std::string{id} + "/status").c_str(), ImVec2{boxes.status->width, boxes.status->height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground}; status) {
            graph->second->drawStatusBar();
        }
    }
    return drawn;
}

/// the workflows of a view's `:::gr4` blocks: only the active one of a region with a standby, as a graph holds five of
/// the browser's sixteen threads; a failed main is kept with its standby, so it is not rebuilt every frame
[[nodiscard]] static std::vector<std::string> workflowsOf(const Viewer& viewer, std::string_view viewId, bool neighbour) {
    std::vector<std::string> workflows;
    const auto               section = sectionOf(viewer.sections, viewId);
    if (section == nullptr) {
        return workflows;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind != BlockKind::directive || block.info != "gr4" || block.field("workflow").empty()) {
            continue;
        }
        const LiveRegion binding = liveRegionFrom(block.fields);
        if (viewer.exporting && !binding.needs.empty()) {
            // an export asks for no device: the standby runs in its place, else the recording stands in
            if (!binding.standby.empty()) {
                workflows.emplace_back(binding.standby);
            }
            continue;
        }
        // a graph starts as it is built, so a neighbour needing a device would open it before its slide is up
        if (neighbour && !binding.needs.empty()) {
            continue;
        }
        workflows.emplace_back(activeWorkflow(viewer, binding));
        if (workflows.back() != binding.workflow && graphFailed(viewer, binding.workflow)) {
            workflows.push_back(binding.workflow);
        }
    }
    return workflows;
}

/// asks once, on entering a view, for the devices its live regions need: the key press is the gesture a browser wants
static void requestDevicesOf(const Viewer& viewer, std::string_view viewId) {
    const auto section = sectionOf(viewer.sections, viewId);
    if (section == nullptr) {
        return;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind == BlockKind::directive && block.info == "gr4" && !block.field("needs").empty()) {
            requestLiveDevice(block.field("needs"));
        }
    }
}

/// keeps the graphs of the shown slide and its neighbours built, one per call and between frames, with only the shown
/// slide's running, so no source holds a device for a slide nobody sees
void tendLiveGraphs(Viewer& viewer) {
    // the presenter window shows where the charts are; running every graph twice would double the threads and load
    if (!liveGraphsAvailable() || viewer.sections.empty() || viewer.presenter) {
        return;
    }

    const auto now = viewer.frameStart;
    std::erase_if(viewer.live.retryAfter, [now](const auto& retry) { return retry.second <= now; });

    const std::vector<std::string> current = workflowsOf(viewer, viewer.navigator.cursor.viewId);
    std::vector<std::string>       keep    = current;
    const auto                     tend    = [&current](const std::string& workflow, LiveGraph& graph) { graph.setRunning(std::ranges::find(current, workflow) != current.end()); };
    if (const View* view = viewer.navigator.graph.find(viewer.navigator.cursor.viewId); view != nullptr) {
        // the default outgoing edge, else the next view in document order
        std::string following = view->next.empty() ? std::string{} : view->next.front();
        std::string preceding;
        const auto  at = std::ranges::find(viewer.navigator.graph.views, viewer.navigator.cursor.viewId, &View::id);
        if (at != viewer.navigator.graph.views.end()) {
            if (following.empty() && std::next(at) != viewer.navigator.graph.views.end()) {
                following = std::next(at)->id;
            }
            if (at != viewer.navigator.graph.views.begin()) {
                preceding = std::prev(at)->id;
            }
        }
        // the slide behind is held too, so stepping back does not destroy and build a graph on the drawing thread
        for (const std::string& neighbour : {following, preceding}) {
            for (const std::string& workflow : workflowsOf(viewer, neighbour, true)) {
                if (std::ranges::find(keep, workflow) == keep.end()) {
                    keep.push_back(workflow);
                }
            }
        }
    }

    // building and destroying a graph happen on the drawing thread: nothing is built or destroyed while the cursor
    // moves, or a held key would do so every frame and freeze the deck
    const bool settled = viewer.live.tendedView == viewer.navigator.cursor.viewId;
    if (!settled && !viewer.exporting) {
        requestDevicesOf(viewer, viewer.navigator.cursor.viewId);
    }
    viewer.live.tendedView = viewer.navigator.cursor.viewId;
    if (!settled) {
        for (auto& [workflow, graph] : viewer.live.graphs) {
            tend(workflow, *graph);
        }
        return;
    }

    for (auto graph = viewer.live.graphs.begin(); graph != viewer.live.graphs.end(); ++graph) {
        if (!graph->second->stalled(viewer.frameStart)) {
            continue;
        }
        viewer.live.retryAfter.insert_or_assign(graph->first, now + kLiveGraphRetryDelay);
        viewer.live.graphs.erase(graph);
        return;
    }

    // one at a time, for the same reason
    const auto stale = std::ranges::find_if(viewer.live.graphs, [&keep](const auto& held) { return std::ranges::find(keep, held.first) == keep.end(); });
    if (stale != viewer.live.graphs.end()) {
        viewer.live.graphs.erase(stale);
        return;
    }

    for (const std::string& workflow : keep) {
        if (viewer.live.graphs.contains(workflow)) {
            continue;
        }
        if (const auto retry = viewer.live.retryAfter.find(workflow); retry != viewer.live.retryAfter.end() && retry->second > now) {
            continue;
        }
        if (std::ranges::find(current, workflow) == current.end() && !liveGraphThreadsAvailable()) {
            continue; // the slide on screen never waits for a neighbour
        }
        const std::span<const std::uint8_t> bytes = viewer.loader.figureBytes(workflow);
        if (bytes.empty()) {
            continue; // the loader has reported the missing resource
        }
        // files the workflow names are looked for in its package
        const std::string text  = withPackageUris(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()}, viewer.loader.baseUri());
        const auto        built = viewer.live.graphs.emplace(workflow, std::make_unique<LiveGraph>(text)).first;
        viewer.live.retryAfter.erase(workflow);
        if (!built->second->problem().empty()) {
            viewer.diagnostics.report(DiagnosticKind::unknownLiveWidget, workflow, std::string{built->second->problem()});
            buildSideMenu(viewer);
        }
        break; // one per frame
    }

    for (auto& [workflow, graph] : viewer.live.graphs) {
        tend(workflow, *graph);
    }
}

/// the width over height a `:::gr4` block asked its chart to keep; zero fills the box
[[nodiscard]] static float statedAspect(const Viewer& viewer, std::string_view region) {
    const Cursor& cursor  = viewer.navigator.cursor;
    const auto    section = sectionOf(viewer.sections, cursor.viewId);
    if (section == nullptr) {
        return 0.0f;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind != BlockKind::directive || block.info != "gr4" || block.field("id") != region) {
            continue;
        }
        return aspectOf(block.field("aspect"));
    }
    return 0.0f;
}

} // namespace gr::present
