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

bool Navigator::next() {
    const View* view = graph.find(cursor.viewId);
    if (view == nullptr) {
        return false;
    }
    if (cursor.step + 1UZ < view->stepCount) {
        history.push_back(cursor);
        ++cursor.step;
        return true;
    }
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
