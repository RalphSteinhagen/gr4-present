#include "BuiltinFace.hpp"

#include "EmbeddedFonts.hpp"

#include <gnuradio-4.0/Compression.hpp>

#include <array>
#include <cassert>
#include <ranges>
#include <utility>
#include <vector>

namespace gr::present {

std::span<const std::uint8_t> builtinFaceTtf(BuiltinFace face) {
    // Gzipped in the binary, 1.05 MB rather than 1.97 MB; inflating all five takes about 44 ms in a browser. Never
    // destroyed: lunasvg and the atlas read these bytes until the very end, static destructors included.
    static const auto&            inflated = *new std::array<std::vector<std::byte>, 5UZ>([] {
        const std::array<std::span<const unsigned char>, 5UZ> packed{kLiberationSansRegularTtfGz, kLiberationSansBoldTtfGz, kLiberationSansItalicTtfGz, kLiberationSansBoldItalicTtfGz, kLiberationMonoRegularTtfGz};
        std::array<std::vector<std::byte>, 5UZ>               faces;
        for (auto&& [packedFace, plainFace] : std::views::zip(packed, faces)) {
            auto plain = gr::compression::decompress(std::as_bytes(packedFace), gr::compression::Format::gzip);
            assert(plain.has_value()); // the embedded bytes are fixed at build time
            plainFace = std::move(plain).value_or(std::vector<std::byte>{});
        }
        return faces;
    }());
    const std::vector<std::byte>& ttf      = inflated[std::to_underlying(face)];
    return {reinterpret_cast<const std::uint8_t*>(ttf.data()), ttf.size()};
}

} // namespace gr::present
