#include "ScreenshotDiff.hpp"

#include <boost/ut.hpp>

#include <filesystem>
#include <random>
#include <tuple>
#include <vector>

using namespace boost::ut;
using namespace gr::present::test;

namespace {

constexpr int kWidth  = 120;
constexpr int kHeight = 90;

struct Canvas {
    std::vector<unsigned char> pixels = std::vector<unsigned char>(static_cast<std::size_t>(kWidth * kHeight * 4), 0xFFU);

    void set(int x, int y, unsigned char red, unsigned char green, unsigned char blue) {
        const auto offset  = (static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + static_cast<std::size_t>(x)) * 4UZ;
        pixels[offset]     = red;
        pixels[offset + 1] = green;
        pixels[offset + 2] = blue;
        pixels[offset + 3] = 0xFFU;
    }

    void fillRectangle(int x0, int y0, int w, int h, unsigned char value) {
        for (int y = y0; y < y0 + h; ++y) {
            for (int x = x0; x < x0 + w; ++x) {
                set(x, y, value, value, value);
            }
        }
    }

    [[nodiscard]] std::filesystem::path writeTo(const std::string& name) const {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::ignore     = writePng(path, pixels, kWidth, kHeight);
        return path;
    }
};

} // namespace

const suite<"ScreenshotDiff"> tests = [] {
    "an identical capture matches"_test = [] {
        Canvas canvas;
        canvas.fillRectangle(10, 10, 40, 30, 0x20);
        const auto reference = canvas.writeTo("diff_identical_reference.png");
        const auto candidate = canvas.writeTo("diff_identical_candidate.png");
        const auto result    = compareScreenshot(candidate, reference);
        expect(result.matches) << result.message;
        expect(eq(result.differingPixels, 0UZ));
    };

    "anti-aliasing noise scattered over the image is tolerated"_test = [] {
        Canvas reference;
        reference.fillRectangle(10, 10, 40, 30, 0x20);
        Canvas                             noisy = reference;
        std::mt19937                       generator{42U};
        std::uniform_int_distribution<int> columns{0, kWidth - 1};
        std::uniform_int_distribution<int> rows{0, kHeight - 1};
        for (int speck = 0; speck < 60; ++speck) { // isolated pixels, as a different GPU or font hinting would produce
            noisy.set(columns(generator), rows(generator), 0x80, 0x80, 0x80);
        }
        const auto result = compareScreenshot(noisy.writeTo("diff_noise_candidate.png"), reference.writeTo("diff_noise_reference.png"));
        expect(result.matches) << result.message;
        expect(result.largestCluster < 24UZ) << "scattered specks must not form a blob";
    };

    "a sub-threshold shift of every pixel is tolerated"_test = [] {
        Canvas reference;
        reference.fillRectangle(10, 10, 40, 30, 0x20);
        Canvas shifted;
        shifted.fillRectangle(10, 10, 40, 30, 0x28); // 8 levels, below the default channel tolerance
        for (std::size_t index = 0UZ; index < shifted.pixels.size(); index += 4UZ) {
            if (shifted.pixels[index] == 0xFFU) {
                shifted.pixels[index] = shifted.pixels[index + 1] = shifted.pixels[index + 2] = 0xF9U;
            }
        }
        const auto result = compareScreenshot(shifted.writeTo("diff_shift_candidate.png"), reference.writeTo("diff_shift_reference.png"));
        expect(result.matches) << result.message;
    };

    "a moved widget is a regression"_test = [] {
        Canvas reference;
        reference.fillRectangle(10, 10, 40, 30, 0x20);
        Canvas moved;
        moved.fillRectangle(14, 10, 40, 30, 0x20); // same widget, four pixels to the right
        const auto result = compareScreenshot(moved.writeTo("diff_moved_candidate.png"), reference.writeTo("diff_moved_reference.png"));
        expect(!result.matches);
        expect(result.largestCluster >= 24UZ) << "a coherent change must form a blob";
    };

    "a recoloured widget is a regression even when it does not move"_test = [] {
        Canvas reference;
        reference.fillRectangle(10, 10, 40, 30, 0x20);
        Canvas recoloured;
        recoloured.fillRectangle(10, 10, 40, 30, 0xC0);
        const auto result = compareScreenshot(recoloured.writeTo("diff_colour_candidate.png"), reference.writeTo("diff_colour_reference.png"));
        expect(!result.matches);
    };

    "a changed capture size fails outright"_test = [] {
        Canvas                           reference;
        const auto                       referencePath = reference.writeTo("diff_size_reference.png");
        const std::vector<unsigned char> smaller(static_cast<std::size_t>(60 * 45 * 4), 0xFFU);
        const auto                       candidatePath = std::filesystem::temp_directory_path() / "diff_size_candidate.png";
        std::ignore                                    = writePng(candidatePath, smaller, 60, 45);
        const auto result                              = compareScreenshot(candidatePath, referencePath);
        expect(!result.matches);
        expect(result.message.contains("size changed"));
    };

    "a missing reference names the way to record it"_test = [] {
        Canvas     canvas;
        const auto result = compareScreenshot(canvas.writeTo("diff_missing_candidate.png"), std::filesystem::temp_directory_path() / "diff_absent_reference.png");
        expect(!result.matches);
        expect(result.message.contains("GR4_PRESENT_UPDATE_REFERENCES"));
    };
};

int main() { return 0; }
