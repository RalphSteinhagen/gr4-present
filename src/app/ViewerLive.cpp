#include "Viewer.hpp"

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

/**
 * Draws the widget a live region asks for, into the box the layout gave it.
 *
 * The graph is built the first time the slide that wants it is shown, from the workflow the loader already
 * fetched, and kept afterwards. A chart asks ImGui how much room it has rather than being told, so the box is
 * established by opening a child of that size -- which is also how OpenDigitizer's dashboard draws the same
 * blocks, one window per chart.
 */
/**
 * The recording an author keeps for a live region, `fallback:` in its block, shown in its place and marked as such.
 *
 * For a region whose graph could not be built, whose workflow is missing, or whose graph stopped making progress:
 * a talk goes on with what the chart looked like rather than an empty box. It cannot help a page whose main thread is
 * frozen, since nothing is drawn then at all.
 */
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
    // said on the slide, small: an audience should not take a recording for a measurement
    const char*  label = "recorded";
    const ImVec2 text  = ImGui::CalcTextSize(label);
    const ImVec2 at{origin.x + size.x - text.x - 8.0f, origin.y + size.y - text.y - 6.0f};
    canvas->AddRectFilled(ImVec2{at.x - 4.0f, at.y - 2.0f}, ImVec2{at.x + text.x + 4.0f, at.y + text.y + 2.0f}, dimmed(viewer.theme.background, 0.8f), 3.0f);
    canvas->AddText(at, dimmed(viewer.theme.text, 0.9f), label);
    return true;
}

/// whether a graph has given up: it could not be built, or it stopped making progress
[[nodiscard]] static bool graphFailed(const Viewer& viewer, std::string_view workflow) {
    if (const auto retry = viewer.liveGraphRetryAfter.find(workflow); retry != viewer.liveGraphRetryAfter.end() && retry->second > std::chrono::steady_clock::now()) {
        return true;
    }
    const auto graph = viewer.graphs.find(workflow);
    return graph != viewer.graphs.end() && (!graph->second->problem().empty() || graph->second->stalled());
}

/// which of a region's workflows it runs: its standby until the device the main one needs is granted, and for as
/// long as the main one has failed -- natively, say, on a machine with no microphone
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
        if (ImGui::BeginChild((std::string{id} + "/status").c_str(), ImVec2{box.width, box.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground)) {
            drawSessionStatusBar();
        }
        ImGui::EndChild();
        return true;
    }
    if (entry == nullptr || entry->binding.workflow.empty()) {
        return false; // a region without a workflow has nothing to run
    }
    const Rectangle& box = boxes.charts;
    if (viewer.documentView.recording != nullptr) {
        // an exported page takes the region, and the bars it asked for, as the frame shows them, read back once the
        // frame is rendered
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
        return drawFallback(viewer, *entry, box); // the presenter's window runs no graph of its own
    }

    constexpr double       kGraphExpectedWithin = 2.0; // seconds: building one takes a frame or two, never this long
    const std::string_view workflow             = deviceless ? std::string_view{entry->binding.standby} : activeWorkflow(viewer, entry->binding);
    viewer.liveSources.insert_or_assign(std::string{id}, workflow == entry->binding.workflow ? "workflow" : "standby");
    const auto graph = viewer.graphs.find(workflow);
    if (graph == viewer.graphs.end() && ImGui::GetTime() - viewer.viewSince > kGraphExpectedWithin) {
        return drawFallback(viewer, *entry, box); // its workflow is missing, or it could not be built
    }
    if (graph != viewer.graphs.end() && (!graph->second->problem().empty() || graph->second->stalled())) {
        if (graph->second->stalled() && std::ranges::find(viewer.reportedClips, "stalled/" + std::string{id}) == viewer.reportedClips.end()) {
            viewer.reportedClips.push_back("stalled/" + std::string{id});
            viewer.diagnostics.report(DiagnosticKind::unknownLiveWidget, std::string{id}, "the graph stopped making progress; its recording is shown if it has one");
            buildSideMenu(viewer);
        }
        if (drawFallback(viewer, *entry, box)) {
            graph->second->pump(); // still tended, so it is shown live again as soon as it moves
            return true;
        }
    }
    if (graph == viewer.graphs.end()) {
        // Asked for here, built between frames. A dashboard starts a thread as it is constructed, and under
        // Emscripten starting one from the browser's main thread waits for a worker that only comes up once the
        // event loop is reached again -- which, inside a frame, it is not. The viewer froze on the slide, with
        // its address bar already moved on. It also reads the ImGui style, which is not a thing to do mid-frame.
        return false; // it is built between frames; the region shows its outline until then
    }

    graph->second->pump(); // what the scheduler has to say to the charts, once a frame

    // A chart fills its box in both directions unless the region states a shape, `aspect: 4:3`, and then it is the
    // largest of that shape that fits, centred.
    Rectangle drawnBox = box;
    if (const float aspect = statedAspect(viewer, id); aspect > 0.0f) {
        drawnBox.width  = std::min(box.width, box.height * aspect);
        drawnBox.height = drawnBox.width / aspect;
        drawnBox.x      = box.x + (box.width - drawnBox.width) * 0.5f;
        drawnBox.y      = box.y + (box.height - drawnBox.height) * 0.5f;
    }
    ImGui::SetCursorScreenPos(ImVec2{drawnBox.x, drawnBox.y});
    const bool childOpen = ImGui::BeginChild(std::string{id}.c_str(), ImVec2{drawnBox.width, drawnBox.height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    viewer.zoomedLists.push_back(ImGui::GetWindowDrawList());
    const bool drawn = childOpen && (entry->binding.widget == "flowgraph" ? graph->second->drawFlowgraph(id) : graph->second->draw(entry->binding.legend.value_or(viewer.loader.manifest().liveLegend)));
    ImGui::EndChild();
    if (boxes.toolbar) {
        viewer.toolbarBoxes.insert_or_assign(std::string{id}, *boxes.toolbar);
        ImGui::SetCursorScreenPos(ImVec2{boxes.toolbar->x, boxes.toolbar->y});
        if (ImGui::BeginChild((std::string{id} + "/toolbar").c_str(), ImVec2{boxes.toolbar->width, boxes.toolbar->height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
            graph->second->drawToolbar();
        }
        ImGui::EndChild();
    }
    if (boxes.status) {
        ImGui::SetCursorScreenPos(ImVec2{boxes.status->x, boxes.status->y});
        if (ImGui::BeginChild((std::string{id} + "/status").c_str(), ImVec2{boxes.status->width, boxes.status->height}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
            graph->second->drawStatusBar();
        }
        ImGui::EndChild();
    }
    return drawn;
}

/**
 * The workflows the `:::gr4` blocks of one view run, which is what a slide needs built and running.
 *
 * Only the active one of a region with a standby: every graph holds five of the browser's sixteen threads for as
 * long as it lives, so the two are not held side by side. A main workflow that failed is kept with its standby,
 * so that it is not built again every frame, and comes back by itself if its graph recovers.
 */
[[nodiscard]] static std::vector<std::string> workflowsOf(const Viewer& viewer, std::string_view viewId, bool neighbour) {
    std::vector<std::string> workflows;
    const auto               section = std::ranges::find_if(viewer.sections, [viewId](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == viewId; });
    if (section == viewer.sections.end()) {
        return workflows;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind != BlockKind::directive || block.info != "gr4" || block.field("workflow").empty()) {
            continue;
        }
        const LiveRegion binding = liveRegionFrom(block.fields, 0UZ);
        if (viewer.exporting && !binding.needs.empty()) {
            // an export asks for no device: the region's standby runs in its place, else its recording stands in
            if (!binding.standby.empty()) {
                workflows.emplace_back(binding.standby);
            }
            continue;
        }
        // A graph is started as it is built, so a slide next to the one shown that needs a device would open it -- a
        // microphone listening to the room, or its recording playing, before its slide is up. Nothing of it is held.
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

/// asks for the browser devices a view's live regions need, once as it is entered: the key press that entered it
/// is the user gesture a browser wants before it shows its prompt
static void requestDevicesOf(const Viewer& viewer, std::string_view viewId) {
    const auto section = std::ranges::find_if(viewer.sections, [viewId](const Section& candidate) { return (candidate.id.empty() ? std::string{"start"} : candidate.id) == viewId; });
    if (section == viewer.sections.end()) {
        return;
    }
    for (const Block& block : section->document.blocks) {
        if (block.kind == BlockKind::directive && block.info == "gr4" && !block.field("needs").empty()) {
            requestLiveDevice(block.field("needs"));
        }
    }
}

/**
 * Keeps a window of graphs around the slide being shown: this one and the next built, this one running.
 *
 * Between frames, never inside one -- a dashboard starts a thread as it is built, and reads the ImGui style. One
 * is built per turn, so the slide that asked for one is not held up while three come up behind it.
 *
 * The next slide's graph is built but left idle. Its charts are then ready the moment it appears, without its
 * sources holding a sound card or a radio for a slide nobody is looking at yet, and anything further away than
 * one slide is not kept at all.
 */
void tendLiveGraphs(Viewer& viewer) {
    // a presenter window shows where the charts are, not the charts: running every graph twice would double the
    // threads and the load of the one window the audience sees
    if (!liveGraphsAvailable() || viewer.sections.empty() || viewer.presenter) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::erase_if(viewer.liveGraphRetryAfter, [now](const auto& retry) { return retry.second <= now; });

    const std::vector<std::string> current = workflowsOf(viewer, viewer.navigator.cursor.viewId);
    std::vector<std::string>       keep    = current;
    // a graph on the slide runs, and one the slide does not show idles
    const auto tend = [&current](const std::string& workflow, LiveGraph& graph) { graph.setRunning(std::ranges::find(current, workflow) != current.end()); };
    if (const View* view = viewer.navigator.graph.find(viewer.navigator.cursor.viewId); view != nullptr) {
        // the default outgoing edge, or the next view in document order when the author drew none
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
        // The slide behind the cursor is held as well as the one ahead of it. Dropping it meant that stepping
        // back destroyed a graph and built another, both on the thread drawing the frame -- a destructor that
        // waits for a scheduler to leave the states it may not be stopped in and then joins its thread. Three
        // graphs held, one running, is what the deck was specified to do.
        for (const std::string& neighbour : {following, preceding}) {
            for (const std::string& workflow : workflowsOf(viewer, neighbour, true)) {
                if (std::ranges::find(keep, workflow) == keep.end()) {
                    keep.push_back(workflow);
                }
            }
        }
    }

    // Building a graph reads a workflow, starts a scheduler and a thread; destroying one joins that thread. Both
    // happen here, on the thread drawing the frame. While the cursor is still moving the set below changes every
    // frame, so a presenter holding the key down had the deck build and destroy a graph per frame and stop
    // answering: eight quick presses moved it one slide and left it frozen. So nothing is built or destroyed
    // until the cursor has stopped somewhere, and the graphs already in hand simply go on running.
    const bool settled = viewer.tendedView == viewer.navigator.cursor.viewId;
    if (!settled && !viewer.exporting) {
        requestDevicesOf(viewer, viewer.navigator.cursor.viewId);
    }
    viewer.tendedView = viewer.navigator.cursor.viewId;
    if (!settled) {
        for (auto& [workflow, graph] : viewer.graphs) {
            tend(workflow, *graph);
        }
        return;
    }

    for (auto graph = viewer.graphs.begin(); graph != viewer.graphs.end(); ++graph) {
        if (!graph->second->stalled()) {
            continue;
        }
        viewer.liveGraphRetryAfter.insert_or_assign(graph->first, now + kLiveGraphRetryDelay);
        viewer.graphs.erase(graph);
        return;
    }

    // one at a time, for the same reason: a frame that drops three schedulers is a frame nobody sees
    const auto stale = std::ranges::find_if(viewer.graphs, [&keep](const auto& held) { return std::ranges::find(keep, held.first) == keep.end(); });
    if (stale != viewer.graphs.end()) {
        viewer.graphs.erase(stale);
        return;
    }

    for (const std::string& workflow : keep) {
        if (viewer.graphs.contains(workflow)) {
            continue;
        }
        if (const auto retry = viewer.liveGraphRetryAfter.find(workflow); retry != viewer.liveGraphRetryAfter.end() && retry->second > now) {
            continue;
        }
        if (std::ranges::find(current, workflow) == current.end() && !liveGraphThreadsAvailable()) {
            continue; // a neighbour is a courtesy: the slide on screen is never left waiting for one
        }
        const std::span<const std::uint8_t> bytes = viewer.loader.figureBytes(workflow);
        if (bytes.empty()) {
            continue; // the loader has already reported it as a missing resource
        }
        // a file the workflow names, such as a recording, is looked for in the package the workflow came with
        const std::string text  = withPackageUris(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()}, viewer.loader.baseUri());
        const auto        built = viewer.graphs.emplace(workflow, std::make_unique<LiveGraph>(text)).first;
        viewer.liveGraphRetryAfter.erase(workflow);
        if (!built->second->problem().empty()) {
            viewer.diagnostics.report(DiagnosticKind::unknownLiveWidget, workflow, std::string{built->second->problem()});
            buildSideMenu(viewer);
        }
        break; // one per frame
    }

    for (auto& [workflow, graph] : viewer.graphs) {
        tend(workflow, *graph);
    }
}

} // namespace gr::present
