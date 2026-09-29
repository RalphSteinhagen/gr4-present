#ifndef GR4_PRESENT_VIDEO_STREAM_HPP
#define GR4_PRESENT_VIDEO_STREAM_HPP

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gr::present {

/**
 * Plays the video track of a WebM file: VP8 pictures and Vorbis sound in a Matroska container.
 *
 * Decoded a frame at a time rather than up front. A four-second clip at 480x270 is 900 frames' worth of RGBA if it
 * is all held at once, and a talk's clip is usually larger than that; one frame in flight is the only shape that
 * scales.
 *
 * Time is an input. `frameAt` is given the seconds since the clip started and decodes forward to that point, so a
 * test can step through a clip as fast as it likes and a dropped frame on a slow machine is a skipped frame rather
 * than a clip that runs slow. Seeking backwards means starting again, which is what `restart` does: seeking a
 * Matroska segment means finding the keyframe before the target and decoding from there, and a slide's clip is
 * short enough that starting over costs less than the machinery would.
 *
 * Only `yuv420p` is handled, which is what `devtools/encode-video.sh` produces and the only format VP8 encodes.
 */
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

    /// the interleaved float samples decoded since this was last called, and empties the buffer. Audio is decoded
    /// as a side effect of decoding video, because both tracks are interleaved in the clusters of one segment.
    [[nodiscard]] std::vector<float> takeAudio();
    /// true once the last frame has been decoded and the clip has nothing further to show
    [[nodiscard]] bool finished() const noexcept;

    /**
     * The RGBA frame that should be showing `seconds` into the clip, or nullptr when the clip has ended.
     *
     * The pointer stays valid until the next call. Going backwards restarts the clip rather than seeking back to a
     * keyframe.
     */
    [[nodiscard]] const std::vector<std::uint8_t>* frameAt(double seconds);

    void restart();

private:
    struct State;
    std::unique_ptr<State> _state;

    explicit VideoStream(std::unique_ptr<State> state) noexcept;
};

} // namespace gr::present

#endif // GR4_PRESENT_VIDEO_STREAM_HPP
