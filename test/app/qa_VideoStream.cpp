#include <boost/ut.hpp>

#include "ScreenshotDiff.hpp"
#include "VideoStream.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;
using namespace gr::present::test;

namespace {

[[nodiscard]] std::vector<std::uint8_t> readClip() {
    std::ifstream file(std::string{GR4_PRESENT_PACKAGE_DIRECTORY} + "/media/Steamboat_Willie_(1928)_by_Walt_Disney_extract.webm", std::ios::binary);
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// mean absolute difference per colour channel, alpha ignored, 0 to 255
[[nodiscard]] double meanDifference(const std::vector<std::uint8_t>& decoded, const Screenshot& reference) {
    double      total   = 0.0;
    std::size_t counted = 0UZ;
    for (std::size_t pixel = 0UZ; pixel + 3UZ < decoded.size() && pixel + 3UZ < reference.rgba.size(); pixel += 4UZ) {
        for (std::size_t channel = 0UZ; channel < 3UZ; ++channel) {
            total += std::abs(static_cast<int>(decoded[pixel + channel]) - static_cast<int>(reference.rgba[pixel + channel]));
            ++counted;
        }
    }
    return counted == 0UZ ? 255.0 : total / static_cast<double>(counted);
}

} // namespace

// Ground truth: ffmpeg frames (`select=eq(n,N)`, in test/app/reference/video) and ffprobe's dimensions, rate and length
const suite<"VideoStream"> videoTests = [] {
    "a clip reports what it holds"_test = [] {
        const std::vector<std::uint8_t> bytes = readClip();
        expect(!bytes.empty()) << "the demo clip is missing";
        auto stream = VideoStream::open(bytes);
        expect(stream.has_value()) << (stream.has_value() ? std::string{} : stream.error());
        if (!stream.has_value()) {
            return;
        }
        expect(eq(stream->width(), 330U));
        expect(eq(stream->height(), 274U));
        expect(lt(std::abs(stream->duration() - 21.336), 0.05)) << "ffprobe puts the clip at 21.336 s, not " << stream->duration();
        expect(stream->hasAudio()) << "the clip carries a Vorbis track and the demuxer did not see it";
        expect(!stream->finished());
    };

    "the frame at a time is the frame ffmpeg puts there"_test = [] {
        const std::vector<std::uint8_t> bytes  = readClip();
        auto                            stream = VideoStream::open(bytes);
        expect(stream.has_value());
        if (!stream.has_value()) {
            return;
        }
        // frames are picked by index, as a seek time is ambiguous at a boundary; frame n spans [n/24, (n+1)/24) at 24 fps
        for (const int index : {0, 24, 120, 400}) {
            const double when      = (static_cast<double>(index) + 0.5) / 24.0;
            const auto   reference = readScreenshot(std::filesystem::path{GR4_PRESENT_REFERENCE_DIRECTORY} / "video" / ("frame-" + std::to_string(index) + ".png"));
            expect(reference.has_value()) << "no reference frame " << index;
            if (!reference.has_value()) {
                continue;
            }
            const std::vector<std::uint8_t>* frame = stream->frameAt(when);
            expect(frame != nullptr) << "nothing decoded at " << when;
            if (frame == nullptr) {
                continue;
            }
            expect(eq(frame->size(), reference->rgba.size())) << "frame " << index << " is a different size";
            // the two paths differ only in how YCbCr is rounded to RGB
            const double difference = meanDifference(*frame, *reference);
            expect(lt(difference, 8.0)) << "frame " << index << " differs from ffmpeg's by " << difference << " levels per channel";
        }
    };

    "successive frames of a moving pattern are not the same picture"_test = [] {
        const std::vector<std::uint8_t> bytes  = readClip();
        auto                            stream = VideoStream::open(bytes);
        expect(stream.has_value());
        if (!stream.has_value()) {
            return;
        }
        const std::vector<std::uint8_t>* first = stream->frameAt(0.1);
        expect(first != nullptr);
        const std::vector<std::uint8_t>  copied = first == nullptr ? std::vector<std::uint8_t>{} : *first;
        const std::vector<std::uint8_t>* later  = stream->frameAt(3.0);
        expect(later != nullptr);
        if (later == nullptr || copied.empty()) {
            return;
        }
        std::size_t differing = 0UZ;
        for (std::size_t at = 0UZ; at < copied.size(); ++at) {
            differing += copied[at] != (*later)[at] ? 1UZ : 0UZ;
        }
        expect(gt(differing, copied.size() / 10UZ)) << "three seconds apart and the picture did not change";
    };

    // Ground truth: the clip runs at 24 frames per second, so a time shows the frame that starts at or before it
    "the frame time names the frame a time shows, and asking again does not move it"_test = [] {
        const std::vector<std::uint8_t> bytes  = readClip();
        auto                            stream = VideoStream::open(bytes);
        expect(stream.has_value());
        if (!stream.has_value()) {
            return;
        }
        expect(lt(stream->frameTime(), 0.0)) << "nothing decoded yet";
        expect(stream->frameAt(0.5 / 24.0) != nullptr);
        expect(approx(stream->frameTime(), 0.0, 1e-3)) << "the first frame";
        expect(stream->frameAt(24.5 / 24.0) != nullptr);
        expect(approx(stream->frameTime(), 1.0, 1e-3)) << "frame 24 starts at one second";
        expect(stream->frameAt(24.9 / 24.0) != nullptr);
        expect(approx(stream->frameTime(), 1.0, 1e-3)) << "a later time within the same frame keeps it";
    };

    "asking for an earlier time starts the clip again"_test = [] {
        const std::vector<std::uint8_t> bytes  = readClip();
        auto                            stream = VideoStream::open(bytes);
        expect(stream.has_value());
        if (!stream.has_value()) {
            return;
        }
        const std::vector<std::uint8_t>* late = stream->frameAt(3.5);
        expect(late != nullptr);
        const std::vector<std::uint8_t> atEnd = late == nullptr ? std::vector<std::uint8_t>{} : *late;

        const std::vector<std::uint8_t>* back = stream->frameAt(0.5 / 24.0);
        expect(back != nullptr) << "going back to the start produced nothing";
        if (back == nullptr || atEnd.empty()) {
            return;
        }
        expect(*back != atEnd) << "the clip did not rewind";

        const auto reference = readScreenshot(std::filesystem::path{GR4_PRESENT_REFERENCE_DIRECTORY} / "video" / "frame-0.png");
        if (reference.has_value()) {
            expect(lt(meanDifference(*back, *reference), 8.0)) << "after rewinding, the first frame is not the first frame";
        }
    };

    // Ground truth: ffprobe's reading of the container, 48 kHz stereo; the count catches dropped or duplicated packets
    "the audio track decodes at the rate and length the file declares"_test = [] {
        const std::vector<std::uint8_t> bytes  = readClip();
        auto                            stream = VideoStream::open(bytes);
        expect(stream.has_value());
        if (!stream.has_value()) {
            return;
        }
        expect(eq(stream->audioRate(), 48000)) << "the Vorbis headers were not read";
        expect(eq(stream->audioChannels(), 2)) << "the clip was encoded in stereo";

        // audio is decoded as a side effect of decoding video, so the clip has to be played through first
        constexpr double kPlayedSeconds = 4.0;
        static_cast<void>(stream->frameAt(kPlayedSeconds));
        const std::vector<float> samples = stream->takeAudio();

        const double expected = kPlayedSeconds * static_cast<double>(stream->audioRate()) * static_cast<double>(stream->audioChannels());
        const double measured = static_cast<double>(samples.size());
        // a tenth of a second of slack: decoding stops at the frame covering `played`, not at the sample
        expect(lt(std::abs(measured - expected), 0.1 * expected)) << "four seconds should be about " << expected << " samples, not " << measured;

        expect(!samples.empty());
        if (samples.empty()) {
            return;
        }
        const float loudest = *std::ranges::max_element(samples);
        expect(gt(loudest, 0.01f)) << "the decoded audio is silent";
        expect(lt(loudest, 1.01f)) << "the decoded audio is clipped or out of range";

        expect(stream->takeAudio().empty()) << "taking the audio a second time must not repeat it";
    };

    "a file that is not WebM says so"_test = [] {
        const std::string               nonsense = "\x1a\x45\xdf\xa3 this starts like Matroska and is not";
        const std::vector<std::uint8_t> bytes{nonsense.begin(), nonsense.end()};
        expect(!VideoStream::open(bytes).has_value());
        expect(!VideoStream::open({}).has_value());
    };
};

int main() { return 0; }
