#include "VideoStream.hpp"

#include <mkvparser/mkvparser.h>

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace gr::present {

namespace {

// Matroska's own numbering for the two track types this viewer plays; the specification, not a library's header
constexpr long   kVideoTrackType       = 1;
constexpr long   kAudioTrackType       = 2;
constexpr double kNanosecondsPerSecond = 1e9;

/// BT.601, the coefficients VP8 defines for its 4:2:0 planes, in 16.16 fixed point
[[nodiscard]] std::uint8_t clampToByte(int value) noexcept { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

/**
 * Reads for mkvparser out of bytes that are already here.
 *
 * The reader that ships beside the parser reads through a `FILE*`, and a package's media arrives as a buffer, so
 * this hands the parser the span instead of writing it to a temporary file to read it back.
 */
class SpanReader final : public mkvparser::IMkvReader {
public:
    explicit SpanReader(std::span<const std::uint8_t> bytes) noexcept : _bytes(bytes) {}

    int Read(long long position, long length, unsigned char* into) override {
        if (position < 0 || length < 0 || into == nullptr) {
            return -1;
        }
        const auto at     = static_cast<std::size_t>(position);
        const auto wanted = static_cast<std::size_t>(length);
        if (at > _bytes.size() || wanted > _bytes.size() - at) {
            return -1;
        }
        std::memcpy(into, _bytes.data() + at, wanted);
        return 0;
    }

    int Length(long long* total, long long* available) override {
        if (total == nullptr || available == nullptr) {
            return -1;
        }
        *total     = static_cast<long long>(_bytes.size());
        *available = *total;
        return 0;
    }

private:
    std::span<const std::uint8_t> _bytes;
};

/**
 * The three Vorbis headers, which Matroska keeps packed in the track's CodecPrivate.
 *
 * Xiph's lacing: a count of packets less one, then the length of all but the last as a run of 255s and a remainder,
 * then the packets end to end. Returns empty when the layout is not that, which is the only validation a container
 * can do before the codec sees them.
 */
[[nodiscard]] std::vector<std::span<const std::uint8_t>> xiphPackets(std::span<const std::uint8_t> priv) {
    if (priv.size() < 3UZ || priv[0] != 2U) {
        return {};
    }
    std::size_t                at = 1UZ;
    std::array<std::size_t, 2> lengths{};
    for (std::size_t which = 0UZ; which < lengths.size(); ++which) {
        std::size_t length = 0UZ;
        while (at < priv.size() && priv[at] == 255U) {
            length += 255UZ;
            ++at;
        }
        if (at >= priv.size()) {
            return {};
        }
        length += priv[at];
        ++at;
        lengths[which] = length;
    }
    if (lengths[0] > priv.size() - at || lengths[1] > priv.size() - at - lengths[0]) {
        return {};
    }
    std::vector<std::span<const std::uint8_t>> packets;
    packets.push_back(priv.subspan(at, lengths[0]));
    at += lengths[0];
    packets.push_back(priv.subspan(at, lengths[1]));
    at += lengths[1];
    packets.push_back(priv.subspan(at));
    return packets;
}

} // namespace

struct VideoStream::State {
    std::span<const std::uint8_t> source;
    SpanReader                    reader;
    mkvparser::Segment*           segment = nullptr;

    long long     videoTrack    = -1;
    long long     audioTrack    = -1;
    std::uint32_t pictureWidth  = 0U;
    std::uint32_t pictureHeight = 0U;
    double        totalSeconds  = 0.0;

    vpx_codec_ctx_t codec{};
    bool            codecReady = false;

    bool               foundVorbis = false;
    vorbis_info        audioInfo{};
    vorbis_comment     audioComment{};
    vorbis_dsp_state   audioDsp{};
    vorbis_block       audioBlock{};
    bool               audioReady  = false;
    long long          audioPacket = 0; // vorbis tracks its own position by packet number, so it has to keep counting
    std::vector<float> audioSamples;    // interleaved, taken by the caller

    const mkvparser::Cluster*    cluster = nullptr;
    const mkvparser::BlockEntry* entry   = nullptr;
    bool                         held    = false; // `entry`'s block has been read but not yet decoded
    bool                         ended   = false;

    std::vector<std::uint8_t> rgba;
    double                    frameTime = -1.0; // presentation time of the frame in `rgba`, negative when none

    explicit State(std::span<const std::uint8_t> bytes) noexcept : source(bytes), reader(bytes) {}

    ~State() {
        if (audioReady) {
            vorbis_block_clear(&audioBlock);
            vorbis_dsp_clear(&audioDsp);
        }
        if (foundVorbis) {
            vorbis_comment_clear(&audioComment);
            vorbis_info_clear(&audioInfo);
        }
        if (codecReady) {
            vpx_codec_destroy(&codec);
        }
        delete segment;
    }

    State(const State&)            = delete;
    State& operator=(const State&) = delete;

    /// decodes one Vorbis packet into interleaved floats
    void decodeAudio(std::span<const std::uint8_t> payload) {
        if (!audioReady) {
            return;
        }
        ogg_packet packet{};
        packet.packet     = const_cast<unsigned char*>(payload.data());
        packet.bytes      = static_cast<long>(payload.size());
        packet.granulepos = -1;
        packet.packetno   = audioPacket++;

        if (vorbis_synthesis(&audioBlock, &packet) != 0) {
            return;
        }
        vorbis_synthesis_blockin(&audioDsp, &audioBlock);

        float** channels  = nullptr;
        int     available = 0;
        while ((available = vorbis_synthesis_pcmout(&audioDsp, &channels)) > 0) {
            const int count = audioInfo.channels;
            audioSamples.reserve(audioSamples.size() + static_cast<std::size_t>(available) * static_cast<std::size_t>(count));
            // vorbis hands back one plane per channel; every sink this project has wants them interleaved
            for (int sample = 0; sample < available; ++sample) {
                for (int channel = 0; channel < count; ++channel) {
                    audioSamples.push_back(channels[channel][sample]);
                }
            }
            vorbis_synthesis_read(&audioDsp, available);
        }
    }

    /// the next block of the segment, in file order, whichever track it belongs to
    [[nodiscard]] const mkvparser::Block* nextBlock() {
        while (true) {
            if (cluster == nullptr || cluster->EOS()) {
                ended = true;
                return nullptr;
            }
            const long status = entry == nullptr ? cluster->GetFirst(entry) : cluster->GetNext(entry, entry);
            if (status < 0) {
                ended = true;
                return nullptr;
            }
            if (entry == nullptr || entry->EOS()) {
                cluster = segment->GetNext(cluster);
                entry   = nullptr;
                continue;
            }
            if (const mkvparser::Block* block = entry->GetBlock(); block != nullptr) {
                return block;
            }
        }
    }

    void convertToRgba(const vpx_image_t& image) {
        const int width  = static_cast<int>(image.d_w);
        const int height = static_cast<int>(image.d_h);
        rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // 4:2:0, so the two chroma planes are half resolution in both directions
                const int luma   = image.planes[VPX_PLANE_Y][y * image.stride[VPX_PLANE_Y] + x];
                const int blue   = image.planes[VPX_PLANE_U][(y / 2) * image.stride[VPX_PLANE_U] + x / 2];
                const int red    = image.planes[VPX_PLANE_V][(y / 2) * image.stride[VPX_PLANE_V] + x / 2];
                const int scaled = 298 * (luma - 16);
                const int cb     = blue - 128;
                const int cr     = red - 128;

                const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4UZ;
                rgba[offset]             = clampToByte((scaled + 409 * cr + 128) >> 8);
                rgba[offset + 1UZ]       = clampToByte((scaled - 100 * cb - 208 * cr + 128) >> 8);
                rgba[offset + 2UZ]       = clampToByte((scaled + 516 * cb + 128) >> 8);
                rgba[offset + 3UZ]       = 0xFFU;
            }
        }
    }

    /// decodes one block's payload, video or audio, and says whether it produced a picture
    bool decodeBlock(const mkvparser::Block& block, long long track) {
        bool decoded = false;
        for (int index = 0; index < block.GetFrameCount(); ++index) {
            const mkvparser::Block::Frame& frame = block.GetFrame(index);
            if (frame.pos < 0 || frame.len < 0 || static_cast<std::size_t>(frame.pos) + static_cast<std::size_t>(frame.len) > source.size()) {
                continue;
            }
            // the file is already in memory, so a block's payload is a view of it rather than a copy
            const std::span<const std::uint8_t> payload = source.subspan(static_cast<std::size_t>(frame.pos), static_cast<std::size_t>(frame.len));

            if (track == audioTrack) {
                decodeAudio(payload);
                continue;
            }
            if (vpx_codec_decode(&codec, payload.data(), static_cast<unsigned int>(payload.size()), nullptr, 0) != VPX_CODEC_OK) {
                continue;
            }
            vpx_codec_iter_t iterator = nullptr;
            if (const vpx_image_t* image = vpx_codec_get_frame(&codec, &iterator); image != nullptr) {
                convertToRgba(*image);
                decoded = true;
            }
        }
        return decoded;
    }

    /**
     * Decodes forward until the picture on screen is the one that belongs at `seconds`.
     *
     * A frame is on screen from its own timestamp until the next one's, so decoding must stop *before* a frame
     * that belongs in the future rather than after it. A block's time is known from the container without
     * decoding it, so the one that comes too early is left where it is and decoded on the next call; decoding it
     * and keeping the frame before would mean holding two pictures instead.
     */
    void decodeUpTo(double seconds) {
        while (true) {
            if (!held) {
                if (nextBlock() == nullptr) {
                    return;
                }
                held = true;
            }
            const mkvparser::Block* block = entry->GetBlock();
            if (block == nullptr) {
                held = false;
                continue;
            }
            const long long track = block->GetTrackNumber();
            if (track == videoTrack) {
                const double when = static_cast<double>(block->GetTime(cluster)) / kNanosecondsPerSecond;
                // the first frame is always decoded: until there is one, there is nothing to show
                if (frameTime >= 0.0 && when > seconds) {
                    return;
                }
                if (decodeBlock(*block, track)) {
                    frameTime = when;
                }
                held = false;
                continue;
            }
            if (track == audioTrack) {
                std::ignore = decodeBlock(*block, track);
            }
            held = false; // a track this viewer does not play, such as a subtitle or a second language
        }
    }
};

VideoStream::VideoStream(std::unique_ptr<State> state) noexcept : _state(std::move(state)) {}
VideoStream::VideoStream(VideoStream&&) noexcept            = default;
VideoStream& VideoStream::operator=(VideoStream&&) noexcept = default;
VideoStream::~VideoStream()                                 = default;

std::expected<VideoStream, std::string> VideoStream::open(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return std::unexpected(std::string{"there are no bytes to play"});
    }

    auto                  state = std::make_unique<State>(bytes);
    long long             at    = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(&state->reader, at) < 0) {
        return std::unexpected(std::string{"this file is not WebM"});
    }

    mkvparser::Segment* segment = nullptr;
    if (mkvparser::Segment::CreateInstance(&state->reader, at, segment) < 0 || segment == nullptr) {
        return std::unexpected(std::string{"this WebM file has no segment"});
    }
    state->segment = segment;
    if (segment->Load() < 0) {
        return std::unexpected(std::string{"this WebM file could not be read"});
    }

    const mkvparser::Tracks* tracks = segment->GetTracks();
    if (tracks == nullptr) {
        return std::unexpected(std::string{"this WebM file declares no tracks"});
    }

    std::span<const std::uint8_t> vorbisPrivate;
    for (unsigned long index = 0UL; index < tracks->GetTracksCount(); ++index) {
        const mkvparser::Track* track = tracks->GetTrackByIndex(index);
        if (track == nullptr || track->GetCodecId() == nullptr) {
            continue;
        }
        const std::string_view codec{track->GetCodecId()};

        if (state->videoTrack < 0 && track->GetType() == kVideoTrackType && codec == "V_VP8") {
            const auto* video    = static_cast<const mkvparser::VideoTrack*>(track);
            state->videoTrack    = track->GetNumber();
            state->pictureWidth  = static_cast<std::uint32_t>(video->GetWidth());
            state->pictureHeight = static_cast<std::uint32_t>(video->GetHeight());
            continue;
        }
        if (state->audioTrack < 0 && track->GetType() == kAudioTrackType && codec == "A_VORBIS") {
            std::size_t          length      = 0UZ;
            const unsigned char* privateData = track->GetCodecPrivate(length);
            if (privateData != nullptr && length > 0UZ) {
                state->audioTrack = track->GetNumber();
                vorbisPrivate     = std::span<const std::uint8_t>{privateData, length};
            }
        }
    }

    if (state->videoTrack < 0) {
        return std::unexpected(std::string{"this file carries no VP8 video"});
    }

    vpx_codec_dec_cfg_t configuration{};
    if (vpx_codec_dec_init(&state->codec, vpx_codec_vp8_dx(), &configuration, 0) != VPX_CODEC_OK) {
        return std::unexpected(std::string{"the VP8 decoder could not be started"});
    }
    state->codecReady = true;

    // A broken audio track is not a reason to refuse the film, so every failure here leaves the video playing
    // silently rather than returning an error.
    if (const std::vector<std::span<const std::uint8_t>> headers = xiphPackets(vorbisPrivate); headers.size() == 3UZ) {
        vorbis_info_init(&state->audioInfo);
        vorbis_comment_init(&state->audioComment);
        state->foundVorbis = true;

        for (std::size_t which = 0UZ; which < headers.size(); ++which) {
            ogg_packet packet{};
            packet.packet     = const_cast<unsigned char*>(headers[which].data());
            packet.bytes      = static_cast<long>(headers[which].size());
            packet.b_o_s      = which == 0UZ ? 1 : 0; // vorbis refuses an identification header that does not say so
            packet.granulepos = -1;
            packet.packetno   = static_cast<long long>(which);
            if (vorbis_synthesis_headerin(&state->audioInfo, &state->audioComment, &packet) < 0) {
                state->foundVorbis = false;
                break;
            }
        }
        if (state->foundVorbis && vorbis_synthesis_init(&state->audioDsp, &state->audioInfo) == 0) {
            vorbis_block_init(&state->audioDsp, &state->audioBlock);
            state->audioReady  = true;
            state->audioPacket = static_cast<long long>(headers.size());
        }
    }

    if (const mkvparser::SegmentInfo* info = segment->GetInfo(); info != nullptr) {
        state->totalSeconds = static_cast<double>(info->GetDuration()) / kNanosecondsPerSecond;
    }
    state->cluster = segment->GetFirst();
    return VideoStream{std::move(state)};
}

std::uint32_t VideoStream::width() const noexcept { return _state->pictureWidth; }
std::uint32_t VideoStream::height() const noexcept { return _state->pictureHeight; }
double        VideoStream::duration() const noexcept { return _state->totalSeconds; }
bool          VideoStream::hasAudio() const noexcept { return _state->audioReady; }
int           VideoStream::audioRate() const noexcept { return _state->audioReady ? static_cast<int>(_state->audioInfo.rate) : 0; }
int           VideoStream::audioChannels() const noexcept { return _state->audioReady ? _state->audioInfo.channels : 0; }

std::vector<float> VideoStream::takeAudio() { return std::exchange(_state->audioSamples, {}); }
bool               VideoStream::finished() const noexcept { return _state->ended; }

const std::vector<std::uint8_t>* VideoStream::frameAt(double seconds) {
    if (seconds < _state->frameTime) {
        restart();
    }
    // decoding forward rather than seeking means a slow machine skips frames rather than running the clip late
    _state->decodeUpTo(seconds);
    return _state->rgba.empty() ? nullptr : &_state->rgba;
}

void VideoStream::restart() {
    std::span<const std::uint8_t> source = _state->source;
    if (auto reopened = open(source); reopened.has_value()) {
        _state = std::move(reopened->_state);
    }
}

} // namespace gr::present
