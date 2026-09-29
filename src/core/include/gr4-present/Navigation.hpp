#ifndef GR4_PRESENT_NAVIGATION_HPP
#define GR4_PRESENT_NAVIGATION_HPP

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gr4-present/Markdown.hpp>

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

/// `next: architecture, demo` from a `:::view` directive; blank entries are dropped, order is the author's
[[nodiscard]] std::vector<std::string> parseEdgeList(std::string_view list);

/**
 * The graph a parsed document implies: one view per section, in document order, each with as many steps as the
 * section declares and whatever edges its `:::view` directive names.
 *
 * The viewer builds its graph with this and then raises a view's step count where an SVG master reveals more
 * groups than the Markdown does. Keeping the common part here is what lets a test walk the shipped deck the way
 * the application walks it, rather than re-deriving it and testing the copy.
 */
[[nodiscard]] NavigationGraph graphOf(std::span<const Section> sections);

struct Navigator {
    NavigationGraph     graph;
    Cursor              cursor;
    std::vector<Cursor> history;

    bool next();
    /// where `next` would go, without going there: the next step, else the next view, else here
    [[nodiscard]] Cursor nextCursor() const;
    /// to the next view straight away, past whatever steps of this one are still to come
    bool nextView();
    bool previous();
    bool jumpTo(std::string_view viewId);
};

} // namespace gr::present

#endif // GR4_PRESENT_NAVIGATION_HPP
