#include <boost/ut.hpp>

#include <gr4-present/export/PageRecording.hpp>

#include <cstdint>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

const suite<"PageRecording"> pageRecordingTests = [] {
    "an OpenGL read-back is turned top row first and opaque"_test = [] {
        // ground truth: rows 1, 2, 3 from the top, read back bottom row first with the framebuffer's alphas
        RecordedPixels pixels{.width = 1U, .height = 3U, .rgba = {30, 31, 32, 0, 20, 21, 22, 7, 10, 11, 12, 128}};
        uprightOpaqueFromReadBack(pixels);
        expect(pixels.rgba == std::vector<std::uint8_t>{10, 11, 12, 255, 20, 21, 22, 255, 30, 31, 32, 255});
    };

    "rows two pixels wide move whole, keeping each pixel's place in its row"_test = [] {
        RecordedPixels pixels{.width = 2U, .height = 2U, .rgba = {5, 5, 5, 5, 6, 6, 6, 6, 1, 1, 1, 1, 2, 2, 2, 2}};
        uprightOpaqueFromReadBack(pixels);
        expect(pixels.rgba == std::vector<std::uint8_t>{1, 1, 1, 255, 2, 2, 2, 255, 5, 5, 5, 255, 6, 6, 6, 255});
    };

    "a single row and an empty picture are left as they are, apart from alpha"_test = [] {
        RecordedPixels row{.width = 2U, .height = 1U, .rgba = {1, 2, 3, 0, 4, 5, 6, 0}};
        uprightOpaqueFromReadBack(row);
        expect(row.rgba == std::vector<std::uint8_t>{1, 2, 3, 255, 4, 5, 6, 255});

        RecordedPixels empty{};
        uprightOpaqueFromReadBack(empty);
        expect(empty.rgba.empty());
    };
};

int main() { return 0; }
