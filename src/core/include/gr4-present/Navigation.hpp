#ifndef GR4_PRESENT_NAVIGATION_HPP
#define GR4_PRESENT_NAVIGATION_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * A presentation is a graph of named views, not a vector of slides. The cursor is a `(view, step)` pair so that
 * revealing within a view stays distinct from moving between views: `next()` advances the step first and only then
 * follows an outgoing edge. View ids are the document API and must stay stable across reloads.
 */
struct Cursor {
    std::string viewId;
    std::size_t step = 0UZ;

    bool operator==(const Cursor&) const = default;
};

struct View {
    std::string              id;
    std::size_t              stepCount = 1UZ; // reveal/staging steps, at least one
    std::vector<std::string> next;            // outgoing edges; the first is the default, empty means document order

    bool operator==(const View&) const = default;
};

enum class NavigationError { noViews, emptyViewId, duplicateViewId, emptyStepCount, unknownNavigationTarget };

[[nodiscard]] constexpr std::string_view message(NavigationError error) noexcept {
    switch (error) {
    case NavigationError::noViews: return "presentation defines no views";
    case NavigationError::emptyViewId: return "view without a stable symbolic id";
    case NavigationError::duplicateViewId: return "duplicate view id";
    case NavigationError::emptyStepCount: return "view with zero reveal steps (expected at least one)";
    case NavigationError::unknownNavigationTarget: return "navigation edge points at an unknown view";
    }
    return "unknown navigation error";
}

struct NavigationGraph {
    std::vector<View> views; // document order, which also supplies the implicit linear navigation edges

    [[nodiscard]] const View*                          find(std::string_view viewId) const noexcept;
    [[nodiscard]] std::string                          defaultNext(std::string_view viewId) const;
    [[nodiscard]] std::expected<void, NavigationError> validate() const;
};

struct Navigator {
    NavigationGraph     graph;
    Cursor              cursor;
    std::vector<Cursor> history;

    bool next();
    bool previous();
    bool jumpTo(std::string_view viewId);
};

} // namespace gr::present

#endif // GR4_PRESENT_NAVIGATION_HPP
