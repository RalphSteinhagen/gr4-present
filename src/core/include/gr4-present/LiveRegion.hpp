#ifndef GR4_PRESENT_LIVE_REGION_HPP
#define GR4_PRESENT_LIVE_REGION_HPP

#include <gnuradio-4.0/annotated.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace gr::present {

/**
 * A live region is logical, not a physical canvas: N regions render from one page-level canvas and a presentation
 * document must never name a canvas. A `gr::UICategory` binds the first matching contribution, so an explicit
 * contribution id is preferred wherever one exists.
 */
enum class LifecyclePolicy { preload, lazy, startOnEnter, runWhileVisible, keepAlive, resetOnEnter, persistent };

[[nodiscard]] std::optional<LifecyclePolicy> parseLifecyclePolicy(std::string_view name) noexcept;

[[nodiscard]] std::string_view name(LifecyclePolicy policy) noexcept;

[[nodiscard]] std::optional<gr::UICategory> parseUICategory(std::string_view name) noexcept;

enum class RenderMode { live, replay, staticFallback };

struct LiveRegion {
    std::string                   workflow;        // package-relative .grc path
    std::string                   widget;          // stable UI contribution id, empty selects by category
    std::optional<gr::UICategory> category;        // used when `widget` is empty
    std::string                   region;          // presentation anchor, e.g. "#spectrum"
    std::string                   fallback;        // package-relative static/replay resource
    std::size_t                   step      = 0UZ; // reveal step this region becomes active at
    LifecyclePolicy               lifecycle = LifecyclePolicy::lazy;
    RenderMode                    mode      = RenderMode::live;

    bool operator==(const LiveRegion&) const = default;
};

} // namespace gr::present

#endif // GR4_PRESENT_LIVE_REGION_HPP
