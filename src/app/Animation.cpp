#include "Animation.hpp"

#include <webp/decode.h>
#include <webp/demux.h>

#include <algorithm>
#include <cmath>
#include <numeric>

// stb's implementation lives in StbImage.cpp; only the declarations are wanted here
#include <stb_image.h>

namespace gr::present {

namespace {

constexpr int kDefaultDelayMs = 100; // what a browser uses when a GIF frame declares 0, so a deck matches the web

[[nodiscard]] bool isWebp(std::span<const std::uint8_t> bytes) noexcept { return bytes.size() >= 12UZ && std::equal(bytes.begin(), bytes.begin() + 4, "RIFF") && std::equal(bytes.begin() + 8, bytes.begin() + 12, "WEBP"); }

[[nodiscard]] std::expected<Animation, std::string> decodeWebp(std::span<const std::uint8_t> bytes) {
    WebPData data{.bytes = bytes.data(), .size = bytes.size()};

    WebPAnimDecoderOptions options;
    if (WebPAnimDecoderOptionsInit(&options) == 0) {
        return std::unexpected(std::string{"this build of libwebp cannot step an animation"});
    }
    options.color_mode = MODE_RGBA;

    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    if (decoder == nullptr) {
        return std::unexpected(std::string{"not a WebP file, or one this decoder does not understand"});
    }

    WebPAnimInfo info;
    if (WebPAnimDecoderGetInfo(decoder, &info) == 0) {
        WebPAnimDecoderDelete(decoder);
        return std::unexpected(std::string{"the WebP file carries no usable frames"});
    }

    Animation         animation{.width = info.canvas_width, .height = info.canvas_height, .frames = {}, .delaysMs = {}};
    const std::size_t frameBytes = static_cast<std::size_t>(info.canvas_width) * static_cast<std::size_t>(info.canvas_height) * 4UZ;

    // the decoder hands back each frame already composited onto the canvas, and a timestamp rather than a delay
    int previousMs = 0;
    while (WebPAnimDecoderHasMoreFrames(decoder) != 0) {
        std::uint8_t* pixels    = nullptr;
        int           timestamp = 0;
        if (WebPAnimDecoderGetNext(decoder, &pixels, &timestamp) == 0) {
            break;
        }
        animation.frames.emplace_back(pixels, pixels + frameBytes);
        animation.delaysMs.push_back(std::max(1, timestamp - previousMs));
        previousMs = timestamp;
    }
    WebPAnimDecoderDelete(decoder);

    if (animation.frames.empty()) {
        return std::unexpected(std::string{"the WebP file carries no usable frames"});
    }
    return animation;
}

[[nodiscard]] std::expected<Animation, std::string> decodeWithStb(std::span<const std::uint8_t> bytes) {
    int width    = 0;
    int height   = 0;
    int frames   = 0;
    int channels = 0;

    // stb returns every GIF frame in one allocation, already composited, with the delays in a separate array
    int*           delays = nullptr;
    unsigned char* pixels = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &width, &height, &frames, &channels, 4);
    if (pixels == nullptr) {
        // not a GIF: a still image, which stb returns without the frame machinery
        pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            return std::unexpected(std::string{stbi_failure_reason() == nullptr ? "the image could not be decoded" : stbi_failure_reason()});
        }
        Animation         still{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .frames = {}, .delaysMs = {}};
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ;
        still.frames.emplace_back(pixels, pixels + count);
        still.delaysMs.push_back(0);
        stbi_image_free(pixels);
        return still;
    }

    Animation         animation{.width = static_cast<std::uint32_t>(width), .height = static_cast<std::uint32_t>(height), .frames = {}, .delaysMs = {}};
    const std::size_t frameBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ;
    for (int frame = 0; frame < frames; ++frame) {
        const unsigned char* start = pixels + static_cast<std::size_t>(frame) * frameBytes;
        animation.frames.emplace_back(start, start + frameBytes);
        animation.delaysMs.push_back(delays != nullptr && delays[frame] > 0 ? delays[frame] : kDefaultDelayMs);
    }
    stbi_image_free(pixels);
    stbi_image_free(delays);
    return animation;
}

} // namespace

int Animation::totalMs() const noexcept { return std::accumulate(delaysMs.begin(), delaysMs.end(), 0); }

std::size_t frameAt(std::span<const int> delaysMs, double seconds) noexcept {
    const int total = std::accumulate(delaysMs.begin(), delaysMs.end(), 0);
    if (delaysMs.size() <= 1UZ || total <= 0) {
        return 0UZ;
    }
    // an explicit clock rather than one read from the UI here, so a test can step frames without waiting for them
    const double wrapped = std::fmod(std::max(seconds, 0.0) * 1000.0, static_cast<double>(total));
    int          elapsed = 0;
    for (std::size_t frame = 0UZ; frame < delaysMs.size(); ++frame) {
        elapsed += delaysMs[frame];
        if (wrapped < static_cast<double>(elapsed)) {
            return frame;
        }
    }
    return delaysMs.size() - 1UZ;
}

std::size_t Animation::frameAt(double seconds) const noexcept { return gr::present::frameAt(delaysMs, seconds); }

std::expected<Animation, std::string> decodeAnimation(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::unexpected(std::string{"there are no bytes to decode"});
    }
    return isWebp(bytes) ? decodeWebp(bytes) : decodeWithStb(bytes);
}

} // namespace gr::present
