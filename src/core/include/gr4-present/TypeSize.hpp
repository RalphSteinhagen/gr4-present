#ifndef GR4_PRESENT_TYPE_SIZE_HPP
#define GR4_PRESENT_TYPE_SIZE_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace gr::present {

/// a type size as an author writes it: points of the 1280 x 720 reference (`14pt`, `14`) or relative to the size
/// around it (`80%`, `1.2em`)
struct TypeSize {
    enum class Unit : std::uint8_t { points, relative, caption }; // caption: the deck's `sizes: caption`

    Unit  unit  = Unit::points;
    float value = 0.0f; // points, or a factor of the surrounding size

    [[nodiscard]] constexpr float pointsWithin(float surroundingPoints, float captionPoints = 13.0f) const noexcept {
        switch (unit) {
        case Unit::points: return value;
        case Unit::caption: return captionPoints;
        default: return value * surroundingPoints;
        }
    }

    bool operator==(const TypeSize&) const = default;
};

/// `14pt`, `14`, `80%`, `1.2em` or `caption`; nothing for anything else, and for a size that is not positive
[[nodiscard]] std::optional<TypeSize> parseTypeSize(std::string_view text) noexcept;

/// the deck's type in points: `sizes: { title: 36pt, subtitle: 20pt, body: 18pt, floor: 12pt }` in `index.yml`
struct TypeScale {
    float title    = 36.0f;
    float subtitle = 20.0f; // the line a title's `<br>` starts
    float body     = 18.0f;
    float floor    = 12.0f; // auto-shrink stops here; a size the author set is never raised to it
    float caption  = 13.0f; // `{size=caption}`: captions, citations, small print

    bool operator==(const TypeScale&) const = default;
};

} // namespace gr::present

#endif // GR4_PRESENT_TYPE_SIZE_HPP
