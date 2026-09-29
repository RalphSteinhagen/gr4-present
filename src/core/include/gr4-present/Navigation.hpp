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

/// a presentation is a graph of named views; the cursor is a `(view, step)` pair and `next()` advances the step first
/// view ids are the document API and stay stable across reloads
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

/// the id a section is known by on the graph and in a link; the untitled leading section is `start`
[[nodiscard]] std::string_view viewIdOf(const Section& section) noexcept;

/// the section whose view id is `viewId`; null when the deck has none
[[nodiscard]] const Section* sectionOf(std::span<const Section> sections, std::string_view viewId) noexcept;

/// the graph a parsed document implies: one view per section, each with the steps and `:::view` edges it declares
/// shared by the viewer and the tests, so a test walks the shipped deck as the application does
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
