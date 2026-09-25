#ifndef GR4_PRESENT_REGION_GEOMETRY_HPP
#define GR4_PRESENT_REGION_GEOMETRY_HPP

#include <gr4-present/LiveRegion.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * Browser-to-OpenDigitizer bridge state.
 *
 * Layout belongs to the browser: the DOM/SVG side owns where a logical live region currently is and forwards that
 * geometry here, in CSS pixels relative to the page-level canvas. The adapter then draws the bound UI contribution
 * into that rectangle. Nothing in this file positions anything itself — laying out presentation content in C++ is
 * exactly what the architecture forbids.
 */
struct Rectangle {
    float x      = 0.0f; // CSS pixels, relative to the page-level canvas origin
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    bool operator==(const Rectangle&) const = default;
};

struct RegionGeometry {
    Rectangle bounds;
    Rectangle clip;
    float     opacity = 1.0f; // linear, not perceptual
    bool      visible = true;

    bool operator==(const RegionGeometry&) const = default;
};

/// stable region id -> the binding and the geometry most recently forwarded by the browser
struct RegionRegistry {
    struct Entry {
        std::string    id; // the anchor named by the presentation document, e.g. "#spectrum"
        LiveRegion     binding;
        RegionGeometry geometry;
    };

    std::vector<Entry> entries;

    template<typename Self>
    [[nodiscard]] auto* find(this Self&& self, std::string_view id) noexcept {
        const auto entry = std::ranges::find(self.entries, id, &Entry::id);
        return entry == self.entries.end() ? nullptr : &*entry;
    }

    /// registers a binding, replacing any earlier binding of the same region id
    void bind(std::string id, LiveRegion binding);

    /// forwards browser-computed geometry; returns false when the region was never bound
    bool updateGeometry(std::string_view id, const RegionGeometry& geometry);
};

} // namespace gr::present

#endif // GR4_PRESENT_REGION_GEOMETRY_HPP
