#include <boost/ut.hpp>

#include "Animation.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

[[nodiscard]] std::vector<std::uint8_t> readMedia(const std::string& name) {
    std::ifstream file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/media/" + name, std::ios::binary);
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// mean absolute difference per channel between two images of the same size, 0 to 255
[[nodiscard]] double meanDifference(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) {
    double total = 0.0;
    for (std::size_t at = 0UZ; at < left.size(); ++at) {
        total += std::abs(static_cast<int>(left[at]) - static_cast<int>(right[at]));
    }
    return left.empty() ? 0.0 : total / static_cast<double>(left.size());
}

} // namespace

// Ground truth for the frame counts is ImageMagick, an independent decoder: it reports eleven frames in each of the
// horse files. Ground truth for the still decoders is that the JPEG and the WebP are two encodings of one
// photograph, so two correct decoders must produce nearly the same pixels by different routes.
const suite<"Animation"> animationTests = [] {
    "a still image decodes as a one-frame animation"_test = [] {
        const auto decoded = decodeAnimation(readMedia("alpha-circles.png"));
        expect(decoded.has_value()) << (decoded.has_value() ? std::string{} : decoded.error());
        if (!decoded.has_value()) {
            return;
        }
        expect(eq(decoded->width, 480U));
        expect(eq(decoded->height, 360U));
        expect(eq(decoded->frames.size(), 1UZ));
        expect(!decoded->animated());
        expect(eq(decoded->frames.front().size(), 480UZ * 360UZ * 4UZ));
        expect(eq(decoded->frameAt(0.0), 0UZ));
        expect(eq(decoded->frameAt(1000.0), 0UZ)) << "a still image has nothing to advance to";
    };

    "a PNG keeps its transparency"_test = [] {
        const auto decoded = decodeAnimation(readMedia("alpha-circles.png"));
        expect(decoded.has_value());
        if (!decoded.has_value()) {
            return;
        }
        // the corners are outside every circle, so they must be fully transparent; the middle is inside one
        const auto alphaAt = [&decoded](std::uint32_t x, std::uint32_t y) { return decoded->frames.front()[(static_cast<std::size_t>(y) * decoded->width + x) * 4UZ + 3UZ]; };
        expect(eq(alphaAt(2U, 2U), std::uint8_t{0})) << "the corner is not transparent, so the alpha channel was dropped";
        expect(gt(alphaAt(180U, 150U), std::uint8_t{128})) << "the middle of a circle is not opaque";
    };

    "an animated GIF yields every frame ImageMagick sees"_test = [] {
        const auto decoded = decodeAnimation(readMedia("horse-in-motion.gif"));
        expect(decoded.has_value()) << (decoded.has_value() ? std::string{} : decoded.error());
        if (!decoded.has_value()) {
            return;
        }
        expect(eq(decoded->frames.size(), 11UZ)) << "ImageMagick reports eleven frames in this file";
        expect(decoded->animated());
        expect(eq(decoded->delaysMs.size(), decoded->frames.size()));
        expect(gt(decoded->totalMs(), 0));
        for (const std::vector<std::uint8_t>& frame : decoded->frames) {
            expect(eq(frame.size(), static_cast<std::size_t>(decoded->width) * decoded->height * 4UZ)) << "a frame is not a whole canvas, so it was not composited";
        }
        // successive frames of a galloping horse are not the same picture
        expect(gt(meanDifference(decoded->frames.at(0UZ), decoded->frames.at(5UZ)), 1.0)) << "every frame decoded identically";
    };

    "an animated WebP yields the same frames as the GIF it came from"_test = [] {
        const auto webp = decodeAnimation(readMedia("horse-in-motion.webp"));
        expect(webp.has_value()) << (webp.has_value() ? std::string{} : webp.error());
        if (!webp.has_value()) {
            return;
        }
        expect(eq(webp->frames.size(), 11UZ)) << "ImageMagick reports eleven frames in this file too";
        expect(eq(webp->width, 340U));
        expect(eq(webp->height, 230U));
        expect(gt(webp->totalMs(), 0));
    };

    "a WebP and a JPEG of one photograph decode to nearly the same pixels"_test = [] {
        // two encodings of the same source through two independent decoders: libwebp and stb's JPEG reader
        const auto asJpeg = decodeAnimation(readMedia("hubble-deep-field.jpg"));
        const auto asWebp = decodeAnimation(readMedia("hubble-deep-field.webp"));
        expect(asJpeg.has_value()) << (asJpeg.has_value() ? std::string{} : asJpeg.error());
        expect(asWebp.has_value()) << (asWebp.has_value() ? std::string{} : asWebp.error());
        if (!asJpeg.has_value() || !asWebp.has_value()) {
            return;
        }
        expect(eq(asJpeg->width, asWebp->width));
        expect(eq(asJpeg->height, asWebp->height));
        const double difference = meanDifference(asJpeg->frames.front(), asWebp->frames.front());
        expect(lt(difference, 12.0)) << "the two decoders disagree about the same photograph by " << difference << " levels per channel";
        expect(gt(difference, 0.0)) << "identical to the last bit, which two lossy encoders cannot be";
    };

    "the frame showing at a time follows the delays"_test = [] {
        Animation clip{.width = 1U, .height = 1U, .frames = {{}, {}, {}}, .delaysMs = {100, 200, 300}};
        expect(eq(clip.totalMs(), 600));
        expect(eq(clip.frameAt(0.0), 0UZ));
        expect(eq(clip.frameAt(0.05), 0UZ));
        expect(eq(clip.frameAt(0.15), 1UZ)) << "100 ms in, the second frame is showing";
        expect(eq(clip.frameAt(0.35), 2UZ));
        expect(eq(clip.frameAt(0.65), 0UZ)) << "it loops, so 650 ms is 50 ms into the second time round";
        expect(eq(clip.frameAt(-1.0), 0UZ)) << "a clock before the start shows the first frame";
    };

    "bytes that are not an image come back as a message"_test = [] {
        const std::string               nonsense = "this is not an image at all, nor a WebP";
        const std::vector<std::uint8_t> bytes{nonsense.begin(), nonsense.end()};
        expect(!decodeAnimation(bytes).has_value());
        expect(!decodeAnimation({}).has_value());
    };
};

int main() { return 0; }
