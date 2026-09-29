#ifndef GR4_PRESENT_LIVE_REGION_HPP
#define GR4_PRESENT_LIVE_REGION_HPP

#include <gr4-present/ChartLegend.hpp>

#include <gnuradio-4.0/annotated.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    std::string                   workflow;         // package-relative .grc path
    std::string                   widget;           // stable UI contribution id, empty selects by category
    std::optional<gr::UICategory> category;         // used when `widget` is empty
    std::string                   region;           // presentation anchor, e.g. "#spectrum"
    std::string                   fallback;         // package-relative static/replay resource
    std::size_t                   step       = 0UZ; // reveal step this region becomes active at
    LifecyclePolicy               lifecycle  = LifecyclePolicy::lazy;
    RenderMode                    mode       = RenderMode::live;
    std::string                   standby    = {};    // package-relative .grc run until `needs` is granted, or when `workflow` fails
    std::string                   needs      = {};    // the GR4 browser device `workflow` cannot run without, such as "audio"
    float                         exportWait = -1.0f; // `export_wait`: seconds this region runs before an exported page takes it; negative takes the deck\'s
    std::optional<ChartLegend>    legend     = {};    // `legend`: where its charts share their legend; empty takes the deck's
    std::string                   toolbar    = {};    // `toolbar`: `here` above its charts, else the layout area it is drawn in; empty: none
    std::string                   status     = {};    // `status`: `here` below its charts, else the layout area it is drawn in; empty: none

    bool operator==(const LiveRegion&) const = default;
};

/// `workflow` with every relative `uri:` value joined onto `packageBase`, so a file a workflow names is found in the
/// package it came with wherever the viewer runs; absolute paths and values with a scheme are left as written
[[nodiscard]] std::string withPackageUris(std::string_view workflow, std::string_view packageBase);

/// builds a binding from the `key: value` lines of a `:::gr4` document block
[[nodiscard]] LiveRegion liveRegionFrom(const std::vector<std::pair<std::string, std::string>>& fields, std::size_t step);

} // namespace gr::present

#endif // GR4_PRESENT_LIVE_REGION_HPP
