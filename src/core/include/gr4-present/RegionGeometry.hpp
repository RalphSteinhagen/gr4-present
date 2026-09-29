#ifndef GR4_PRESENT_REGION_GEOMETRY_HPP
#define GR4_PRESENT_REGION_GEOMETRY_HPP

#include <gr4-present/LiveRegion.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// browser-to-OpenDigitizer bridge state: the browser owns layout and forwards each region's geometry in CSS pixels
/// relative to the page-level canvas; nothing here positions anything itself
struct Rectangle {
    float x      = 0.0f; // CSS pixels, relative to the page-level canvas origin
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    bool operator==(const Rectangle&) const = default;
};

/// stable region id -> the binding and the geometry most recently forwarded by the browser
struct RegionRegistry {
    struct Entry {
        std::string id; // the anchor named by the presentation document, e.g. "#spectrum"
        LiveRegion  binding;
    };

    std::vector<Entry> entries;

    template<typename Self>
    [[nodiscard]] auto* find(this Self&& self, std::string_view id) noexcept {
        const auto entry = std::ranges::find(self.entries, id, &Entry::id);
        return entry == self.entries.end() ? nullptr : &*entry;
    }

    /// registers a binding, replacing any earlier binding of the same region id
    void bind(std::string id, LiveRegion binding);
};

} // namespace gr::present

#endif // GR4_PRESENT_REGION_GEOMETRY_HPP
