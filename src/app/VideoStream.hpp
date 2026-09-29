#ifndef GR4_PRESENT_VIDEO_STREAM_HPP
#define GR4_PRESENT_VIDEO_STREAM_HPP

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/// plays a WebM file (VP8 and Vorbis in Matroska), decoding one frame at a time as whole clips would not fit in memory
/// time is an input: `frameAt` decodes forward to the seconds since the start, so tests step freely and a slow machine
/// skips frames; going backwards restarts, as seeking needs the preceding keyframe and slide clips are short
/// only `yuv420p` is handled, as produced by `devtools/encode-video.sh` and the only format VP8 encodes
class VideoStream {
public:
    VideoStream()                              = delete;
    VideoStream(const VideoStream&)            = delete;
    VideoStream& operator=(const VideoStream&) = delete;
    VideoStream(VideoStream&&) noexcept;
    VideoStream& operator=(VideoStream&&) noexcept;
    ~VideoStream();

    /// `bytes` must outlive the stream: it is read from where it lies rather than copied
    [[nodiscard]] static std::expected<VideoStream, std::string> open(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    /// the clip's length in seconds, from the granule position of its last video page; 0 when the file does not say
    [[nodiscard]] double duration() const noexcept;
    [[nodiscard]] bool   hasAudio() const noexcept;
    [[nodiscard]] int    audioRate() const noexcept;
    [[nodiscard]] int    audioChannels() const noexcept;

    /// the interleaved float samples decoded since the last call; audio is decoded along with video in shared clusters
    [[nodiscard]] std::vector<float> takeAudio();
    /// true once the last frame has been decoded and the clip has nothing further to show
    [[nodiscard]] bool finished() const noexcept;

    /// the RGBA frame showing `seconds` into the clip, or nullptr when it has ended; valid until the next call
    [[nodiscard]] const std::vector<std::uint8_t>* frameAt(double seconds);
    /// the presentation time, in seconds, of the frame `frameAt` last returned; negative when there is none
    [[nodiscard]] double frameTime() const noexcept;

    void restart();

private:
    struct State;
    std::unique_ptr<State> _state;

    explicit VideoStream(std::unique_ptr<State> state) noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_VIDEO_STREAM_HPP
