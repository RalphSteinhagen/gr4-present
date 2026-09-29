#include "Layout.hpp"

#include <algorithm>

namespace gr::present {

const Area* Layout::find(std::string_view id) const noexcept {
    const auto area = std::ranges::find(areas, id, &Area::id);
    return area == areas.end() ? nullptr : &*area;
}

} // namespace gr::present
