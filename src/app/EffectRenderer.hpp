#ifndef GR4_PRESENT_EFFECT_RENDERER_HPP
#define GR4_PRESENT_EFFECT_RENDERER_HPP

#include <gr4-present/EffectSource.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::present {

/// what an effect's uniforms hold for one frame
struct EffectInputs {
    float                                                    time      = 0.0f; // seconds
    float                                                    timeDelta = 0.0f;
    int                                                      frame     = 0;
    float                                                    frameRate = 60.0f;
    std::array<float, 4>                                     mouse{};        // pixels of the effect's rectangle, bottom-left origin
    std::array<float, 4>                                     date{};         // year, month from 0, day, seconds since midnight
    unsigned                                                 slideFrom = 0U; // GL textures; 0 when the use has none
    unsigned                                                 slideTo   = 0U;
    unsigned                                                 content   = 0U;
    float                                                    progress  = 0.0f;
    std::array<float, 2>                                     focus{0.5f, 0.5f}; // 0 … 1, bottom-left origin
    float                                                    dark = 0.0f;
    std::array<float, 4>                                     themeBackground{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4>                                     themeText{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4>                                     themeAccent{1.0f, 0.7f, 0.26f, 1.0f};
    std::map<std::string, std::array<float, 4>, std::less<>> parameters;        // set by the slide, over the effect's defaults
    bool                                                     keepAlpha = false; // blend by the alpha the effect wrote instead of covering
};

/// the framebuffers and viewport a callback found, restored when it is done, so effects drawn inside a capture land in
/// the capture and not on the screen
/// what the pointer did this frame, in the effect's pixels (origin bottom-left)
struct PointerFrame {
    float x       = 0.0f;
    float y       = 0.0f;
    bool  inside  = false; // over the effect's rectangle
    bool  clicked = false; // the button went down this frame
    bool  down    = false; // the button is held
};

/// `iMouse` and whether a press that began inside is still held
struct MouseState {
    std::array<float, 4> mouse{};
    bool                 pressed = false;
};

/// `iMouse` one frame on: a press inside sets `xy` and `zw` to where it is, `z` and `w` positive; while held, `xy`
/// follows the pointer, `z` stays positive and `w` turns negative, so it is positive on the frame of the press only;
/// released, `z` turns negative too and `xy` stays where it was
[[nodiscard]] constexpr MouseState nextMouse(MouseState state, PointerFrame frame) noexcept {
    const auto negative = [](float value) { return value > 0.0f ? -value : value; };
    const auto positive = [](float value) { return value < 0.0f ? -value : value; };
    if (frame.clicked && frame.inside) {
        return MouseState{.mouse = {frame.x, frame.y, frame.x, frame.y}, .pressed = true};
    }
    if (state.pressed && frame.down) {
        return MouseState{.mouse = {frame.x, frame.y, positive(state.mouse[2]), negative(state.mouse[3])}, .pressed = true};
    }
    if (state.pressed) {
        return MouseState{.mouse = {state.mouse[0], state.mouse[1], negative(state.mouse[2]), negative(state.mouse[3])}, .pressed = false};
    }
    return state;
}

struct SavedTarget {
    int                drawFramebuffer = 0;
    int                readFramebuffer = 0;
    std::array<int, 4> viewport{};
    std::array<int, 4> scissor{};
    bool               scissorTest = false;
    bool               blend       = false;

    [[nodiscard]] static SavedTarget save();
    void                             restore() const;
};

/**
 * What a run of draw commands drew, kept as a texture of the framebuffer's size instead of reaching its target: the
 * slide under an overlay, or a box arriving through an effect. `begin` and `end` bracket the commands as callbacks;
 * `end` restores the framebuffer `begin` found, so captures nest.
 */
class ContentCapture {
public:
    ContentCapture()                                 = default;
    ContentCapture(const ContentCapture&)            = delete;
    ContentCapture& operator=(const ContentCapture&) = delete;
    ~ContentCapture();

    void begin(ImDrawList& list, std::array<float, 4> clear);
    void end(ImDrawList& list);

    [[nodiscard]] unsigned           texture() const noexcept { return _texture; }
    [[nodiscard]] std::array<int, 2> size() const noexcept { return {_width, _height}; }
    /// what the last capture holds, `size()` RGBA, rows from the bottom; the bound framebuffer is kept
    void readPixels(std::span<std::uint8_t> rgba) const;

private:
    unsigned             _framebuffer = 0U;
    unsigned             _texture     = 0U;
    int                  _width       = 0;
    int                  _height      = 0;
    std::array<float, 4> _clear{};
    SavedTarget          _saved{};

    static void onBegin(const ImDrawList* list, const ImDrawCmd* command);
    static void onEnd(const ImDrawList* list, const ImDrawCmd* command);
};

class EffectRenderer;

/**
 * A box drawn through an effect, arriving or overlaid: what was under it before its commands and what it looks like
 * with them, each cut to the box, then the effect draws from the two in its place. `insert` puts the bracketing callbacks around commands a
 * draw list already holds; the box's rectangle travels in the callbacks' clip rectangles, so a zoom applied to the
 * list afterwards moves it with everything else.
 */
class RevealCapture {
public:
    RevealCapture()                                = default;
    RevealCapture(const RevealCapture&)            = delete;
    RevealCapture& operator=(const RevealCapture&) = delete;
    ~RevealCapture();

    /// brackets `list`'s commands from `firstCommand` on; `low`, `high` the box on screen; `renderer` then draws where
    /// the box was going with `inputs` as they stand when the frame is rendered
    void insert(ImDrawList& list, int firstCommand, ImVec2 low, ImVec2 high, EffectRenderer* renderer, const EffectInputs* inputs);

private:
    unsigned            _before             = 0U; // the box's rectangle before its commands
    unsigned            _after              = 0U; // and after them
    unsigned            _cropFramebuffer    = 0U;
    unsigned            _captureFramebuffer = 0U;
    unsigned            _capture            = 0U; // the whole target, the box drawn over what was there
    std::array<int, 4>  _box{};                   // framebuffer pixels, bottom-left origin
    std::array<int, 2>  _full{};
    std::array<int, 2>  _cropSize{};
    SavedTarget         _saved{};
    EffectRenderer*     _renderer = nullptr;
    const EffectInputs* _inputs   = nullptr;

    static void onBegin(const ImDrawList* list, const ImDrawCmd* command);
    static void onEnd(const ImDrawList* list, const ImDrawCmd* command);
};

/**
 * One effect on the GPU: its passes compiled, its buffers, its channel textures, and the image it draws.
 *
 * Everything here runs where a GL context is current, which in this viewer means inside an ImGui draw callback. A pass
 * that does not compile or link leaves the renderer `failed()`; the message names the effect file and the line within
 * it. Buffers keep two textures each so a pass can read its own previous frame while writing the next.
 */
class EffectRenderer {
public:
    using AssetBytes = std::function<std::span<const std::uint8_t>(std::string_view asset)>;

    EffectRenderer(EffectSource effect, AssetBytes assets);
    EffectRenderer(const EffectRenderer&)            = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;
    ~EffectRenderer();

    [[nodiscard]] bool             failed() const noexcept { return _failed; }
    [[nodiscard]] std::string_view error() const noexcept { return _error; }
    /// what went wrong with a channel's asset since the last call -- the channel reads black -- for the deck's problems list
    [[nodiscard]] std::vector<std::string> takeProblems() { return std::exchange(_problems, {}); }
    [[nodiscard]] const EffectSource&      effect() const noexcept { return _effect; }

    /// compiles every pass on first use; false when the effect cannot be drawn
    [[nodiscard]] bool ready();

    /// renders the buffers, then the Image pass into the bound framebuffer at `rectangle` (x, y from the bottom-left,
    /// width, height, in framebuffer pixels); the caller's framebuffer, viewport and scissor are kept
    void drawInto(std::array<int, 4> rectangle, const EffectInputs& inputs);

    /// the renderer's own texture of `width` × `height`, made now so a draw list can name it before it is drawn into
    [[nodiscard]] unsigned outputTexture(int width, int height);

    /// renders the buffers, then the Image pass into the renderer's own texture of `width` × `height`; returns it
    [[nodiscard]] unsigned drawToTexture(int width, int height, const EffectInputs& inputs);

    /// copies what the last `drawToTexture` of `width` × `height` rendered into `rgba`, rows from the bottom; the bound
    /// framebuffer is kept
    void readOutput(int width, int height, std::span<std::uint8_t> rgba) const;

    /// forgets what the buffers hold, so the next frame starts from cleared buffers
    void clearBuffers();

private:
    struct Program {
        PassKind pass    = PassKind::image;
        unsigned program = 0U;
    };
    struct Buffer {
        PassKind                pass = PassKind::bufferA;
        std::array<unsigned, 2> textures{};
        std::array<unsigned, 2> framebuffers{};
        std::size_t             latest  = 0UZ; // the texture holding the last completed frame
        bool                    mipmaps = false;
    };
    struct Asset {
        unsigned             texture = 0U;
        unsigned             target  = 0U; // GL_TEXTURE_2D, _CUBE_MAP or _3D
        std::array<float, 3> size{};
    };

    EffectSource                                    _effect;
    AssetBytes                                      _assets;
    std::vector<std::string>                        _problems;
    std::vector<Program>                            _programs;
    std::vector<Buffer>                             _buffers;
    std::map<std::string, Asset, std::less<>>       _assetTextures;
    std::map<std::array<std::uint8_t, 2>, unsigned> _samplers; // by filter and wrap
    unsigned                                        _vertexArray       = 0U;
    unsigned                                        _cubeTexture       = 0U;
    unsigned                                        _cubeFramebuffer   = 0U;
    int                                             _cubeSize          = 0;
    unsigned                                        _outputTexture     = 0U;
    unsigned                                        _outputFramebuffer = 0U;
    std::array<int, 2>                              _outputSize{};
    std::array<int, 2>                              _bufferSize{};
    bool                                            _compiled     = false;
    bool                                            _failed       = false;
    bool                                            _floatBuffers = true;
    std::string                                     _error;

    void                       compile();
    void                       ensureBuffers(int width, int height);
    void                       renderBuffers(int width, int height, const EffectInputs& inputs);
    void                       bindChannels(const EffectPass& pass);
    void                       renderPass(const Program& program, std::array<int, 4> viewport, std::array<float, 2> origin, const EffectInputs& inputs, int face);
    [[nodiscard]] const Asset* assetFor(const ChannelBinding& binding);
    [[nodiscard]] unsigned     samplerFor(const ChannelBinding& binding);
};

} // namespace gr::present

#endif // GR4_PRESENT_EFFECT_RENDERER_HPP
