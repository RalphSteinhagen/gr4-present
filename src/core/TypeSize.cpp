#include <gr4-present/TypeSize.hpp>

#include <gr4-present/Number.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace gr::present {

std::optional<TypeSize> parseTypeSize(std::string_view text) noexcept {
    struct Suffix {
        std::string_view text;
        TypeSize::Unit   unit;
        float            divisor; // a percentage divides, so 80% is 0.8 as written rather than 80 x 0.01
    };
    constexpr std::array kSuffixes{Suffix{"pt", TypeSize::Unit::points, 1.0f}, Suffix{"%", TypeSize::Unit::relative, 100.0f}, Suffix{"em", TypeSize::Unit::relative, 1.0f}};

    TypeSize::Unit unit    = TypeSize::Unit::points;
    float          divisor = 1.0f;
    for (const Suffix& suffix : kSuffixes) {
        if (text.ends_with(suffix.text)) {
            text.remove_suffix(suffix.text.size());
            unit    = suffix.unit;
            divisor = suffix.divisor;
            break;
        }
    }
    const std::optional<float> value = parseNumber<float>(text);
    if (!value || !std::isfinite(*value) || *value <= 0.0f) {
        return std::nullopt;
    }
    return TypeSize{.unit = unit, .value = *value / divisor};
}

} // namespace gr::present
