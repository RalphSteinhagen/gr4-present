#include <gr4-present/RegionGeometry.hpp>

#include <utility>

namespace gr::present {

void RegionRegistry::bind(std::string id, LiveRegion binding) {
    if (Entry* existing = find(id); existing != nullptr) {
        existing->binding = std::move(binding);
        return;
    }
    entries.push_back(Entry{.id = std::move(id), .binding = std::move(binding)});
}

} // namespace gr::present
