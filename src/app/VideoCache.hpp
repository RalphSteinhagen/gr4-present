#ifndef GR4_PRESENT_VIDEO_CACHE_HPP
#define GR4_PRESENT_VIDEO_CACHE_HPP

#include "Texture.hpp"
#include "VideoStream.hpp"

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

struct PlayingVideo {
    Texture     texture;
    double      position = 0.0; // seconds into the clip
    double      length   = 0.0; // the clip's own length; 0 when the file does not state one
    bool        playing  = true;
    bool        ended    = false;
    bool        hasSound = false; // the clip carries an audio track, so a mute button means something
    bool        muted    = true;
    std::string problem; // when set, nothing plays and this says why
};

/**
 * Keeps one decoder per clip and hands the renderer the frame that should be showing.
 *
 * A clip starts again whenever its view is entered, which is what `rewind` is for: coming back to a slide replays
 * from the beginning rather than resuming in the middle of a sentence.
 *
 * `clock` is an input, as it is for animated images, so a test can step a clip without waiting for it.
 */
struct VideoCache {
    ~VideoCache();

    using ByteSource = std::function<std::span<const std::uint8_t>(std::string_view)>;

    ByteSource bytesFor;
    double     clock = 0.0;

    /// `autoplay` false holds the first frame until something calls `toggle`; `loop` restarts at the end; `sound`
    /// opens an audio device for the clip, which is off unless the author asked for it
    [[nodiscard]] const PlayingVideo* get(std::string_view reference, bool autoplay, bool loop, bool sound = false);

    /// every clip back to its first frame, paused or playing as its author asked; called when the view changes
    void rewind();

    /// starts or stops every clip that is loaded, which is what the presenter's key does
    void toggle();

    /// the same for one clip, which is what the buttons under it do
    void toggle(std::string_view reference);

    /// one clip back to its first frame, playing or paused as it was
    void restart(std::string_view reference);

    /// silences or restores one clip's sound; a clip that was never meant to be heard opens its device on first use
    void toggleMute(std::string_view reference);

    /// seconds into one clip, 0 for a clip that is not loaded
    [[nodiscard]] double positionOf(std::string_view reference) const;

    /// closes every clip and its audio stream; before SDL shuts down, since the cache itself may outlive it
    void release();

private:
    struct Entry {
        std::unique_ptr<VideoStream> stream;
        PlayingVideo                 state;
        double                       startedAt = 0.0; // the clock reading the clip's own time is measured from
        bool                         autoplay  = true;
        bool                         loop      = true;
        SDL_AudioStream*             audio     = nullptr; // null until the clip is first heard
    };

    static void openAudio(Entry& entry);
    static void rewindEntry(Entry& entry, double clock);

    std::map<std::string, Entry, std::less<>> _playing;
};

} // namespace gr::present

#endif // GR4_PRESENT_VIDEO_CACHE_HPP
