#ifndef GR4_PRESENT_LIVE_REGION_HPP
#define GR4_PRESENT_LIVE_REGION_HPP

#include <gr4-present/ChartLegend.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

struct LiveRegion {
    std::string                workflow;           // package-relative .grc path
    std::string                widget;             // stable UI contribution id
    std::string                region;             // presentation anchor, e.g. "#spectrum"
    std::string                fallback;           // package-relative static/replay resource
    std::string                standby    = {};    // package-relative .grc run until `needs` is granted, or when `workflow` fails
    std::string                needs      = {};    // the GR4 browser device `workflow` cannot run without, such as "audio"
    float                      exportWait = -1.0f; // `export_wait`: seconds this region runs before an exported page takes it; negative takes the deck's
    std::optional<ChartLegend> legend     = {};    // `legend`: where its charts share their legend; empty takes the deck's
    std::string                toolbar    = {};    // `toolbar`: `here` above its charts, else the layout area it is drawn in; empty: none
    std::string                status     = {};    // `status`: `here` below its charts, else the layout area it is drawn in; empty: none

    bool operator==(const LiveRegion&) const = default;
};

/// `workflow` with every relative `uri:` value joined onto `packageBase`; absolute paths and values with a scheme stay
[[nodiscard]] std::string withPackageUris(std::string_view workflow, std::string_view packageBase);

[[nodiscard]] LiveRegion liveRegionFrom(const std::vector<std::pair<std::string, std::string>>& fields);

} // namespace gr::present

#endif // GR4_PRESENT_LIVE_REGION_HPP
