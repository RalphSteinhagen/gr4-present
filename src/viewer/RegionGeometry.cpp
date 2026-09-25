#include "RegionGeometry.hpp"

#include <utility>

namespace gr::present {

void RegionRegistry::bind(std::string id, LiveRegion binding) {
    if (Entry* existing = find(id); existing != nullptr) {
        existing->binding = std::move(binding);
        return;
    }
    entries.push_back(Entry{.id = std::move(id), .binding = std::move(binding), .geometry = {}});
}

bool RegionRegistry::updateGeometry(std::string_view id, const RegionGeometry& geometry) {
    Entry* entry = find(id);
    if (entry == nullptr) {
        return false;
    }
    entry->geometry = geometry;
    return true;
}

} // namespace gr::present
