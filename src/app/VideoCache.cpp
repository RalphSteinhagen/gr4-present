#include "VideoCache.hpp"

#include <utility>

namespace gr::present {

namespace {
/// how far ahead of the device the decoder is allowed to run: enough not to starve between frames, little
/// enough that the sound does not drift away from the picture
constexpr float kAudioLeadSeconds = 0.25f;
} // namespace

const PlayingVideo* VideoCache::get(std::string_view reference, bool autoplay, bool loop, bool sound) {
    auto entry = _playing.find(reference);
    if (entry == _playing.end()) {
        const std::span<const std::uint8_t> bytes = bytesFor ? bytesFor(reference) : std::span<const std::uint8_t>{};
        if (bytes.empty()) {
            return nullptr; // not fetched yet; it is not this cache's business to retry
        }

        Entry fresh{.stream = nullptr, .state = {}, .startedAt = clock, .autoplay = autoplay, .loop = loop};
        if (auto opened = VideoStream::open(bytes); opened.has_value()) {
            fresh.stream = std::make_unique<VideoStream>(std::move(*opened));
        } else {
            fresh.state.problem = std::move(opened.error());
        }
        fresh.state.hasSound = fresh.stream != nullptr && fresh.stream->hasAudio();
        fresh.state.muted    = !sound;
        entry                = _playing.emplace(std::string{reference}, std::move(fresh)).first;
        if (!entry->second.state.muted) {
            openAudio(entry->second);
        }
        entry->second.state.playing = autoplay;
    }

    Entry& playing = entry->second;
    if (playing.stream == nullptr) {
        return &playing.state;
    }
    playing.state.length = playing.stream->duration();

    // a paused clip holds its position; a playing one follows the clock from where it was started
    if (playing.state.playing) {
        playing.state.position = clock - playing.startedAt;
    } else {
        playing.startedAt = clock - playing.state.position;
    }

    if (playing.stream->finished() && playing.state.position > 0.0) {
        if (playing.loop) {
            playing.startedAt      = clock;
            playing.state.position = 0.0;
            if (playing.audio != nullptr) {
                SDL_ClearAudioStream(playing.audio); // whatever is still queued belongs to the run that just ended
            }
        } else {
            playing.state.ended = true;
        }
    }

    if (const std::vector<std::uint8_t>* frame = playing.stream->frameAt(playing.state.position); frame != nullptr) {
        playing.state.texture = Texture::loadRgba(std::span<const unsigned char>{*frame}, static_cast<int>(playing.stream->width()), static_cast<int>(playing.stream->height()));
    }

    // Decoding the picture also decodes whatever audio shared those pages. It arrives far faster than it plays:
    // a clip decodes ahead, and a looping one decodes its whole length again on every pass, so handing every
    // sample straight to the device grew the queue without bound -- tens of megabytes, minutes of audio, for a
    // clip four seconds long. The device then plays something older every pass and the seams are audible.
    //
    // So the queue is kept to a lead of a fraction of a second: enough that it cannot run dry between frames,
    // little enough that the sound stays with the picture. Samples past that are dropped rather than queued,
    // because they are samples the clip has already moved beyond.
    if (const std::vector<float> samples = playing.stream->takeAudio(); playing.audio != nullptr && !samples.empty() && playing.state.playing && !playing.state.muted) {
        const int rate = std::max(playing.stream->audioRate(), 1) * std::max(playing.stream->audioChannels(), 1) * static_cast<int>(sizeof(float));
        const int lead = static_cast<int>(static_cast<float>(rate) * kAudioLeadSeconds);
        if (SDL_GetAudioStreamQueued(playing.audio) < lead) {
            SDL_PutAudioStreamData(playing.audio, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
        }
    }
    return &playing.state;
}

void VideoCache::openAudio(Entry& entry) {
    if (entry.audio != nullptr || entry.stream == nullptr || !entry.stream->hasAudio()) {
        return;
    }
    // the wall clock stays the master and the samples are fed to follow it. Slaving the picture to the
    // device's own played-sample count is the better scheme for a film; for a clip on a slide it buys
    // nothing and costs a second clock to keep honest.
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = entry.stream->audioChannels(), .freq = entry.stream->audioRate()};
    entry.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (entry.audio != nullptr) {
        SDL_ResumeAudioStreamDevice(entry.audio);
    } else {
        // said rather than swallowed: a clip that asks for sound and gets none is a fault somebody should see
        entry.state.problem = std::string{"no audio device: "} + SDL_GetError();
    }
}

void VideoCache::rewindEntry(Entry& entry, double clock) {
    if (entry.audio != nullptr) {
        SDL_ClearAudioStream(entry.audio); // what was queued belongs to the part of the clip being left behind
    }
    if (entry.stream != nullptr) {
        entry.stream->restart();
    }
    entry.startedAt      = clock;
    entry.state.position = 0.0;
    entry.state.ended    = false;
}

void VideoCache::rewind() {
    for (auto& [reference, entry] : _playing) {
        rewindEntry(entry, clock);
        entry.state.playing = entry.autoplay;
    }
}

void VideoCache::toggle() {
    for (auto& [reference, entry] : _playing) {
        entry.state.playing = !entry.state.playing;
    }
}

void VideoCache::toggle(std::string_view reference) {
    if (auto entry = _playing.find(reference); entry != _playing.end()) {
        entry->second.state.playing = !entry->second.state.playing;
    }
}

void VideoCache::restart(std::string_view reference) {
    if (auto entry = _playing.find(reference); entry != _playing.end()) {
        rewindEntry(entry->second, clock);
    }
}

double VideoCache::positionOf(std::string_view reference) const {
    const auto entry = _playing.find(reference);
    return entry == _playing.end() ? 0.0 : entry->second.state.position;
}

void VideoCache::toggleMute(std::string_view reference) {
    auto entry = _playing.find(reference);
    if (entry == _playing.end()) {
        return;
    }
    Entry& clip      = entry->second;
    clip.state.muted = !clip.state.muted;
    if (clip.state.muted) {
        if (clip.audio != nullptr) {
            SDL_ClearAudioStream(clip.audio);
            SDL_PauseAudioStreamDevice(clip.audio); // what the device already holds would otherwise play out
        }
    } else if (clip.audio != nullptr) {
        SDL_ResumeAudioStreamDevice(clip.audio);
    } else {
        openAudio(clip);
    }
}

VideoCache::~VideoCache() { release(); }

void VideoCache::release() {
    for (auto& [reference, entry] : _playing) {
        if (entry.audio != nullptr) {
            SDL_DestroyAudioStream(entry.audio);
        }
    }
    _playing.clear();
}

} // namespace gr::present
