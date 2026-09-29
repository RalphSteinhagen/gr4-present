#include <gr4-present/Navigation.hpp>

#include <algorithm>
#include <ranges>
#include <vector>

namespace gr::present {

const View* NavigationGraph::find(std::string_view viewId) const noexcept {
    const auto it = std::ranges::find(views, viewId, &View::id);
    return it == views.cend() ? nullptr : &*it;
}

std::string NavigationGraph::defaultNext(std::string_view viewId) const {
    const auto it = std::ranges::find(views, viewId, &View::id);
    if (it == views.cend()) {
        return {};
    }
    if (!it->next.empty()) {
        return it->next.front();
    }
    const auto following = std::next(it);
    return following == views.cend() ? std::string{} : following->id;
}

std::expected<void, NavigationError> NavigationGraph::validate() const {
    if (views.empty()) {
        return std::unexpected(NavigationError::noViews);
    }
    if (std::ranges::any_of(views, [](const View& view) { return view.id.empty(); })) {
        return std::unexpected(NavigationError::emptyViewId);
    }
    if (std::ranges::any_of(views, [](const View& view) { return view.stepCount == 0UZ; })) {
        return std::unexpected(NavigationError::emptyStepCount);
    }

    auto identifiers = views | std::views::transform(&View::id) | std::ranges::to<std::vector>();
    std::ranges::sort(identifiers);
    if (std::ranges::adjacent_find(identifiers) != identifiers.cend()) {
        return std::unexpected(NavigationError::duplicateViewId);
    }

    const auto edges = views | std::views::transform(&View::next) | std::views::join;
    if (std::ranges::any_of(edges, [this](const std::string& target) { return find(target) == nullptr; })) {
        return std::unexpected(NavigationError::unknownNavigationTarget);
    }
    return {};
}

std::vector<std::string> parseEdgeList(std::string_view list) {
    std::vector<std::string> edges;
    for (const auto part : list | std::views::split(',')) {
        const std::string_view entry{part};
        const auto             first = entry.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            continue;
        }
        const auto last = entry.find_last_not_of(" \t");
        edges.emplace_back(entry.substr(first, last - first + 1UZ));
    }
    return edges;
}

NavigationGraph graphOf(std::span<const Section> sections) {
    NavigationGraph graph;
    for (const Section& section : sections) {
        // a `:::view` directive names where this section can go; without one it follows document order
        const auto declared = std::ranges::find_if(section.document.blocks, [](const Block& block) { return block.kind == BlockKind::directive && block.info == "view"; });
        const auto edges    = declared == section.document.blocks.end() ? std::vector<std::string>{} : parseEdgeList(declared->field("next"));
        graph.views.push_back(View{.id = section.id.empty() ? std::string{"start"} : section.id, .stepCount = static_cast<std::size_t>(std::max(section.document.stepCount(), 1)), .next = edges});
    }
    return graph;
}
Cursor Navigator::nextCursor() const {
    const View* view = graph.find(cursor.viewId);
    if (view == nullptr) {
        return cursor;
    }
    if (cursor.step + 1UZ < view->stepCount) {
        return Cursor{.viewId = cursor.viewId, .step = cursor.step + 1UZ};
    }
    std::string target = graph.defaultNext(cursor.viewId);
    if (target.empty() || graph.find(target) == nullptr) {
        return cursor;
    }
    return Cursor{.viewId = std::move(target), .step = 0UZ};
}

bool Navigator::next() {
    Cursor ahead = nextCursor();
    if (ahead == cursor) {
        return false;
    }
    history.push_back(cursor);
    cursor = std::move(ahead);
    return true;
}

bool Navigator::nextView() {
    std::string target = graph.defaultNext(cursor.viewId);
    if (target.empty() || graph.find(target) == nullptr) {
        return false;
    }
    history.push_back(cursor);
    cursor = Cursor{.viewId = std::move(target), .step = 0UZ};
    return true;
}

bool Navigator::previous() {
    if (history.empty()) {
        return false;
    }
    cursor = history.back();
    history.pop_back();
    return true;
}

bool Navigator::jumpTo(std::string_view viewId) {
    if (graph.find(viewId) == nullptr) {
        return false;
    }
    history.push_back(cursor);
    cursor = Cursor{.viewId = std::string{viewId}, .step = 0UZ};
    return true;
}

} // namespace gr::present
