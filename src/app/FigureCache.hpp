#ifndef GR4_PRESENT_FIGURE_CACHE_HPP
#define GR4_PRESENT_FIGURE_CACHE_HPP

#include "Animation.hpp"
#include "SvgImage.hpp"
#include "TextRuns.hpp"
#include "Texture.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// uploads a package figure once and keeps it; SVG rasterises at the width it is drawn at
/// an unreadable reference is remembered as absent; `clock` is set by the caller so a test can step animations
struct FigureCache {
    using ByteSource = std::function<std::span<const std::uint8_t>(std::string_view)>;

    ByteSource bytesFor;
    double     clock = 0.0; // seconds since the deck was opened

    [[nodiscard]] const Texture* get(std::string_view reference, std::uint32_t targetWidth);

    /// an SVG rasterised with `hiddenIds` removed, cached apart from the unmasked raster of the same reference
    [[nodiscard]] const Texture* get(std::string_view reference, std::uint32_t targetWidth, std::span<const std::string> hiddenIds);

    /// the `<a href>` links a drawing carries, in fractions of it; none for a raster picture
    [[nodiscard]] std::span<const SvgLink> links(std::string_view reference);

    /// the text a drawing carries, with `hiddenIds` left out, each with the box it covers in fractions of the drawing
    [[nodiscard]] std::span<const DrawnLabel> labels(std::string_view reference, std::span<const std::string> hiddenIds);

private:
    struct Frames {
        std::vector<Texture> textures;
        std::vector<int>     delaysMs;
    };

    std::map<std::string, Frames, std::less<>>                  _uploaded;
    std::map<std::string, std::vector<SvgLink>, std::less<>>    _links;
    std::map<std::string, std::vector<DrawnLabel>, std::less<>> _labels;
};

} // namespace gr::present

#endif // GR4_PRESENT_FIGURE_CACHE_HPP
