#include <boost/ut.hpp>

#include "QrCode.hpp"

#include <string>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] bool isDark(const RasterImage& image, std::uint32_t x, std::uint32_t y) { return image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4UZ] < 128U; }

/// how many image pixels one module occupies, deduced from the quiet zone: the first dark pixel on the diagonal is
/// the corner of the top-left finder pattern, which sits exactly `kQrQuietModules` modules in
[[nodiscard]] std::uint32_t moduleScale(const RasterImage& image) {
    std::uint32_t at = 0U;
    while (at < image.width && !isDark(image, at, at)) {
        ++at;
    }
    return at / static_cast<std::uint32_t>(kQrQuietModules);
}

/// the module at (column, row) of the symbol itself, the quiet zone excluded
[[nodiscard]] bool moduleAt(const RasterImage& image, std::uint32_t scale, int column, int row) {
    const std::uint32_t x = static_cast<std::uint32_t>(column + kQrQuietModules) * scale + scale / 2U;
    const std::uint32_t y = static_cast<std::uint32_t>(row + kQrQuietModules) * scale + scale / 2U;
    return isDark(image, x, y);
}

} // namespace

// Ground truth is ISO/IEC 18004 itself: every QR symbol carries three finder patterns, one at each corner but the
// bottom right, and each is a 7x7 dark square holding a 5x5 light ring around a 3x3 dark centre. The encoder is
// never asked what it produced -- the structure the standard mandates is checked in the pixels.
const suite<"QrCode"> qrTests = [] {
    "a code carries the three finder patterns the standard requires"_test = [] {
        const auto code = renderQr("https://ralphsteinhagen.github.io/gr4-present", 240);
        expect(code.has_value()) << (code.has_value() ? std::string{} : code.error());
        if (!code.has_value()) {
            return;
        }
        expect(eq(code->width, code->height)) << "a symbol is square";

        const std::uint32_t scale = moduleScale(*code);
        expect(gt(scale, 0U)) << "no dark module was found at all";
        if (scale == 0U) {
            return;
        }
        const int modules = static_cast<int>(code->width / scale) - 2 * kQrQuietModules;
        // versions 1 to 40 are 21 to 177 modules, always 21 + 4(version - 1)
        expect(ge(modules, 21)) << "smaller than version 1";
        expect(eq((modules - 21) % 4, 0)) << "not a legal module count: " << modules;

        const auto finder = [&](int originX, int originY, std::string_view where) {
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 7; ++column) {
                    const bool onBorder = row == 0 || row == 6 || column == 0 || column == 6;
                    const bool inCentre = row >= 2 && row <= 4 && column >= 2 && column <= 4;
                    const bool expected = onBorder || inCentre; // dark ring, light ring, dark centre
                    expect(moduleAt(*code, scale, originX + column, originY + row) == expected) << where << " finder pattern is wrong at " << column << "," << row;
                }
            }
        };
        finder(0, 0, "the top-left");
        finder(modules - 7, 0, "the top-right");
        finder(0, modules - 7, "the bottom-left");

        // and the fourth corner has none, which is how a scanner works out the orientation
        bool bottomRightIsAFinder = true;
        for (int row = 0; row < 7 && bottomRightIsAFinder; ++row) {
            for (int column = 0; column < 7 && bottomRightIsAFinder; ++column) {
                const bool onBorder  = row == 0 || row == 6 || column == 0 || column == 6;
                const bool inCentre  = row >= 2 && row <= 4 && column >= 2 && column <= 4;
                bottomRightIsAFinder = moduleAt(*code, scale, modules - 7 + column, modules - 7 + row) == (onBorder || inCentre);
            }
        }
        expect(!bottomRightIsAFinder) << "the bottom-right corner carries a finder pattern, so a scanner cannot tell which way up it is";
    };

    "the quiet zone is light all the way round"_test = [] {
        const auto code = renderQr("gr4-present", 200);
        expect(code.has_value());
        if (!code.has_value()) {
            return;
        }
        const std::uint32_t scale = moduleScale(*code);
        const std::uint32_t band  = static_cast<std::uint32_t>(kQrQuietModules) * scale;
        for (std::uint32_t at = 0U; at < code->width; ++at) {
            for (std::uint32_t depth = 0U; depth < band; ++depth) {
                expect(!isDark(*code, at, depth)) << "a dark module in the top quiet zone";
                expect(!isDark(*code, at, code->height - 1U - depth)) << "a dark module in the bottom quiet zone";
                expect(!isDark(*code, depth, at)) << "a dark module in the left quiet zone";
                expect(!isDark(*code, code->width - 1U - depth, at)) << "a dark module in the right quiet zone";
            }
        }
    };

    "longer text needs a larger symbol"_test = [] {
        const auto brief = renderQr("a", 200);
        const auto long_ = renderQr(std::string(600UZ, 'a'), 200);
        expect(brief.has_value() && long_.has_value());
        if (!brief.has_value() || !long_.has_value()) {
            return;
        }
        const int briefModules = static_cast<int>(brief->width / moduleScale(*brief)) - 2 * kQrQuietModules;
        const int longModules  = static_cast<int>(long_->width / moduleScale(*long_)) - 2 * kQrQuietModules;
        expect(gt(longModules, briefModules)) << "600 characters fitted in the same version as one";
    };

    "text that cannot be encoded comes back as a message"_test = [] {
        // beyond the capacity of version 40 at medium error correction, which is 2331 bytes
        const auto tooLong = renderQr(std::string(4000UZ, 'x'), 200);
        expect(!tooLong.has_value()) << "an oversized payload must not pretend to have encoded";
        if (!tooLong.has_value()) {
            expect(!tooLong.error().empty());
        }
        expect(!renderQr("", 200).has_value());
    };
};

int main() { return 0; }
