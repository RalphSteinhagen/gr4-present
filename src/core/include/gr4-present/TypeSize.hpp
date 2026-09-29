#ifndef GR4_PRESENT_TYPE_SIZE_HPP
#define GR4_PRESENT_TYPE_SIZE_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace gr::present {

/**
 * A type size as an author writes it, for a deck, a slide, a box or a span of words.
 *
 * Points are those of the 1280 x 720 reference the deck is designed at (`14pt`, or a bare `14`); a relative size
 * scales the size around it (`80%`, `1.2em`), so a span in a box set at 24 pt and one in the body read alike.
 */
struct TypeSize {
    enum class Unit : std::uint8_t { points, relative };

    Unit  unit  = Unit::points;
    float value = 0.0f; // points, or a factor of the surrounding size

    [[nodiscard]] constexpr float pointsWithin(float surroundingPoints) const noexcept { return unit == Unit::points ? value : value * surroundingPoints; }

    bool operator==(const TypeSize&) const = default;
};

/// `14pt`, `14`, `80%` or `1.2em`; nothing for anything else, and for a size that is not positive
[[nodiscard]] std::optional<TypeSize> parseTypeSize(std::string_view text) noexcept;

/// the deck's type in points: `sizes: { title: 36pt, body: 18pt, floor: 12pt }` in `index.yml`
struct TypeScale {
    float title = 36.0f;
    float body  = 18.0f;
    float floor = 12.0f; // auto-shrink stops here; a size the author set is never raised to it

    bool operator==(const TypeScale&) const = default;
};

} // namespace gr::present

#endif // GR4_PRESENT_TYPE_SIZE_HPP
