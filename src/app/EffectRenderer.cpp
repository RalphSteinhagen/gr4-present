#ifndef __EMSCRIPTEN__
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES // framebuffers, samplers and 3-D textures are GL 3 entry points, declared only with the extension prototypes
#endif
#endif

#include "EffectRenderer.hpp"

#include <gr4-present/EffectShaderText.hpp>

#include <gnuradio-4.0/Logger.hpp>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <ranges>

namespace gr::present {

namespace {

constexpr GLint kSlideFromUnit = 4;
constexpr GLint kSlideToUnit   = 5;
constexpr GLint kContentUnit   = 6;
constexpr int   kCubeFaceSize  = 1024;

#ifdef __EMSCRIPTEN__
constexpr GlslDialect kDialect = GlslDialect::es300;
#else
constexpr GlslDialect kDialect = GlslDialect::core330;
#endif

[[nodiscard]] std::string infoLog(GLuint object, bool program) {
    GLint length = 0;
    program ? glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length) : glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    program ? glGetProgramInfoLog(object, length, nullptr, log.data()) : glGetShaderInfoLog(object, length, nullptr, log.data());
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n')) {
        log.pop_back();
    }
    return log;
}

[[nodiscard]] std::expected<GLuint, std::string> compiled(GLenum kind, const std::string& text) {
    const char*  source = text.c_str();
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::string log = infoLog(shader, false);
        glDeleteShader(shader);
        return std::unexpected{std::move(log)};
    }
    return shader;
}

void uniform(GLuint program, const char* uniformName, float value) { glUniform1f(glGetUniformLocation(program, uniformName), value); }
void uniform(GLuint program, const char* uniformName, int value) { glUniform1i(glGetUniformLocation(program, uniformName), value); }
void uniform(GLuint program, const char* uniformName, std::array<float, 2> value) { glUniform2f(glGetUniformLocation(program, uniformName), value[0], value[1]); }
void uniform(GLuint program, const char* uniformName, std::array<float, 4> value) { glUniform4f(glGetUniformLocation(program, uniformName), value[0], value[1], value[2], value[3]); }

void parameterUniform(GLuint program, const EffectParameter& parameter, const std::array<float, 4>& value) {
    const GLint where = glGetUniformLocation(program, parameter.name.c_str());
    switch (parameter.type) {
    case ParameterType::integer:
    case ParameterType::boolean: glUniform1i(where, static_cast<GLint>(value[0])); break;
    case ParameterType::scalar: glUniform1f(where, value[0]); break;
    case ParameterType::vec2: glUniform2f(where, value[0], value[1]); break;
    case ParameterType::vec3: glUniform3f(where, value[0], value[1], value[2]); break;
    case ParameterType::vec4:
    case ParameterType::colour: glUniform4f(where, value[0], value[1], value[2], value[3]); break;
    }
}

[[nodiscard]] std::array<float, 4> clampedToRange(const EffectParameter& parameter, std::array<float, 4> value) {
    if (parameter.range.has_value()) {
        for (float& component : value) {
            component = std::clamp(component, (*parameter.range)[0], (*parameter.range)[1]);
        }
    }
    return value;
}

void flipRows(std::span<std::uint8_t> pixels, std::size_t rowBytes) {
    const std::size_t         rows = rowBytes == 0UZ ? 0UZ : pixels.size() / rowBytes;
    std::vector<std::uint8_t> row(rowBytes);
    for (std::size_t top = 0UZ; top < rows / 2UZ; ++top) {
        std::uint8_t* upper = pixels.data() + top * rowBytes;
        std::uint8_t* lower = pixels.data() + (rows - 1UZ - top) * rowBytes;
        std::memcpy(row.data(), upper, rowBytes);
        std::memcpy(upper, lower, rowBytes);
        std::memcpy(lower, row.data(), rowBytes);
    }
}

struct DecodedImage {
    std::vector<std::uint8_t> pixels; // RGBA8
    int                       width  = 0;
    int                       height = 0;
};

[[nodiscard]] std::optional<DecodedImage> decoded(std::span<const std::uint8_t> bytes, bool vflip) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    int  width    = 0;
    int  height   = 0;
    int  channels = 0;
    auto pixels   = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>{stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4), &stbi_image_free};
    if (pixels == nullptr) {
        return std::nullopt;
    }
    DecodedImage image{.pixels = {pixels.get(), pixels.get() + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ}, .width = width, .height = height};
    if (vflip) {
        flipRows(image.pixels, static_cast<std::size_t>(width) * 4UZ);
    }
    return image;
}

[[nodiscard]] std::uint32_t littleEndian32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept { return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1UZ]) << 8U | static_cast<std::uint32_t>(bytes[at + 2UZ]) << 16U | static_cast<std::uint32_t>(bytes[at + 3UZ]) << 24U; }

} // namespace

SavedTarget SavedTarget::save() {
    SavedTarget saved;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved.drawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved.readFramebuffer);
    glGetIntegerv(GL_VIEWPORT, saved.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, saved.scissor.data());
    saved.scissorTest = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    saved.blend       = glIsEnabled(GL_BLEND) == GL_TRUE;
    return saved;
}

void SavedTarget::restore() const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    for (GLuint unit = 0U; unit <= static_cast<GLuint>(kContentUnit); ++unit) {
        glBindSampler(unit, 0U); // a sampler left bound would override the filtering ImGui's own textures ask for
    }
    glActiveTexture(GL_TEXTURE0);
}

ContentCapture::~ContentCapture() {
    if (_texture != 0U) {
        glDeleteTextures(1, &_texture);
        glDeleteFramebuffers(1, &_framebuffer);
    }
}

void ContentCapture::begin(ImDrawList& list, std::array<float, 4> clear) {
    _clear = clear;
    list.AddCallback(&ContentCapture::onBegin, this);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ContentCapture::end(ImDrawList& list) {
    list.AddCallback(&ContentCapture::onEnd, this);
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void ContentCapture::onBegin(const ImDrawList*, const ImDrawCmd* command) {
    auto&          self   = *static_cast<ContentCapture*>(command->UserCallbackData);
    const ImGuiIO& io     = ImGui::GetIO();
    const int      width  = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    const int      height = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    self._saved           = SavedTarget::save();
    if (self._texture == 0U) {
        glGenTextures(1, &self._texture);
        glGenFramebuffers(1, &self._framebuffer);
    }
    if (width != self._width || height != self._height) {
        self._width  = width;
        self._height = height;
        glBindTexture(GL_TEXTURE_2D, self._texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, self._framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, self._texture, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, self._framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(self._clear[0], self._clear[1], self._clear[2], self._clear[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

void ContentCapture::readPixels(std::span<std::uint8_t> rgba) const {
    if (_framebuffer == 0U || rgba.size() < static_cast<std::size_t>(_width) * static_cast<std::size_t>(_height) * 4UZ) {
        return;
    }
    GLint boundFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &boundFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _framebuffer);
    glReadPixels(0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(boundFramebuffer));
}

void ContentCapture::onEnd(const ImDrawList*, const ImDrawCmd* command) { static_cast<const ContentCapture*>(command->UserCallbackData)->_saved.restore(); }

namespace {

/// a clip rectangle (screen points) as framebuffer pixels from the bottom-left, the way GL counts
[[nodiscard]] std::array<int, 4> framebufferBox(const ImVec4& clip, const ImDrawData& data) {
    const ImVec2 scale = data.FramebufferScale;
    const int    left  = static_cast<int>((clip.x - data.DisplayPos.x) * scale.x);
    const int    right = static_cast<int>((clip.z - data.DisplayPos.x) * scale.x);
    const int    top   = static_cast<int>((clip.y - data.DisplayPos.y) * scale.y);
    const int    under = static_cast<int>((clip.w - data.DisplayPos.y) * scale.y);
    const int    tall  = static_cast<int>(data.DisplaySize.y * scale.y);
    return {left, tall - under, std::max(1, right - left), std::max(1, under - top)};
}

void sizedTexture(unsigned& texture, int width, int height) {
    if (texture == 0U) {
        glGenTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/// copies `box` of the framebuffer bound for reading into `texture`, at the box's size
void cropInto(unsigned framebuffer, unsigned texture, const std::array<int, 4>& box) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBlitFramebuffer(box[0], box[1], box[0] + box[2], box[1] + box[3], 0, 0, box[2], box[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

} // namespace

RevealCapture::~RevealCapture() {
    const std::array<GLuint, 3> textures{_before, _after, _capture};
    glDeleteTextures(3, textures.data());
    const std::array<GLuint, 2> framebuffers{_cropFramebuffer, _captureFramebuffer};
    glDeleteFramebuffers(2, framebuffers.data());
}

void RevealCapture::insert(ImDrawList& list, int firstCommand, ImVec2 low, ImVec2 high, EffectRenderer* renderer, const EffectInputs* inputs) {
    _renderer    = renderer;
    _inputs      = inputs;
    firstCommand = std::clamp(firstCommand, 0, list.CmdBuffer.Size);
    // a callback command holds no elements; its clip rectangle carries the box, so a zoom moves it like the rest
    ImDrawCmd opening{};
    opening.ClipRect               = ImVec4{low.x, low.y, high.x, high.y};
    opening.UserCallback           = &RevealCapture::onBegin;
    opening.UserCallbackData       = this;
    opening.UserCallbackDataOffset = -1; // the data is the pointer itself, as `AddCallback` keeps it
    const ImDrawCmd& next          = firstCommand < list.CmdBuffer.Size ? list.CmdBuffer[firstCommand] : list.CmdBuffer.back();
    opening.TexRef                 = next.TexRef;
    opening.VtxOffset              = next.VtxOffset;
    opening.IdxOffset              = next.IdxOffset;
    ImDrawCmd reset                = opening;
    reset.UserCallback             = ImDrawCallback_ResetRenderState;
    reset.UserCallbackData         = nullptr;
    list.CmdBuffer.insert(list.CmdBuffer.Data + firstCommand, reset);
    list.CmdBuffer.insert(list.CmdBuffer.Data + firstCommand, opening);
    list.AddCallback(&RevealCapture::onEnd, this);
    list.CmdBuffer[list.CmdBuffer.Size - 2].ClipRect = ImVec4{low.x, low.y, high.x, high.y}; // a callback opens a fresh command after it
    list.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void RevealCapture::onBegin(const ImDrawList*, const ImDrawCmd* command) {
    auto&             self = *static_cast<RevealCapture*>(command->UserCallbackData);
    const ImDrawData& data = *ImGui::GetDrawData();
    self._saved            = SavedTarget::save();
    self._box              = framebufferBox(command->ClipRect, data);
    const std::array<int, 2> full{static_cast<int>(data.DisplaySize.x * data.FramebufferScale.x), static_cast<int>(data.DisplaySize.y * data.FramebufferScale.y)};
    if (self._cropFramebuffer == 0U) {
        glGenFramebuffers(1, &self._cropFramebuffer);
        glGenFramebuffers(1, &self._captureFramebuffer);
    }
    if (full != self._full) {
        self._full = full;
        sizedTexture(self._capture, full[0], full[1]);
        glBindFramebuffer(GL_FRAMEBUFFER, self._captureFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, self._capture, 0);
    }
    if (std::array{self._box[2], self._box[3]} != self._cropSize) {
        self._cropSize = {self._box[2], self._box[3]};
        sizedTexture(self._before, self._box[2], self._box[3]);
        sizedTexture(self._after, self._box[2], self._box[3]);
    }
    glDisable(GL_SCISSOR_TEST);
    // what is under the box now, cut to it; then the box is drawn over a copy of the whole target
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(self._saved.drawFramebuffer));
    cropInto(self._cropFramebuffer, self._before, self._box);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self._captureFramebuffer);
    glBlitFramebuffer(0, 0, full[0], full[1], 0, 0, full[0], full[1], GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, self._captureFramebuffer);
}

void RevealCapture::onEnd(const ImDrawList*, const ImDrawCmd* command) {
    auto& self = *static_cast<RevealCapture*>(command->UserCallbackData);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, self._captureFramebuffer);
    cropInto(self._cropFramebuffer, self._after, self._box);
    self._saved.restore();
    if (self._renderer == nullptr || self._inputs == nullptr) {
        return;
    }
    EffectInputs inputs = *self._inputs;
    inputs.slideFrom    = self._before;
    inputs.slideTo      = self._after;
    inputs.content      = self._after;
    glDisable(GL_SCISSOR_TEST);
    self._renderer->drawInto(self._box, inputs);
}

EffectRenderer::EffectRenderer(EffectSource effect, AssetBytes assets) : _effect{std::move(effect)}, _assets{std::move(assets)} {}

// its GL objects end with the context, which ends with the program; a renderer replaced while the context lives
// releases what it made
EffectRenderer::~EffectRenderer() {
    if (!_compiled) {
        return;
    }
    for (const Program& program : _programs) {
        glDeleteProgram(program.program);
    }
    for (const Buffer& buffer : _buffers) {
        glDeleteTextures(2, buffer.textures.data());
        glDeleteFramebuffers(2, buffer.framebuffers.data());
    }
    for (const Asset& asset : _assetTextures | std::views::values) {
        glDeleteTextures(1, &asset.texture);
    }
    for (const unsigned sampler : _samplers | std::views::values) {
        glDeleteSamplers(1, &sampler);
    }
    glDeleteTextures(1, &_cubeTexture);
    glDeleteFramebuffers(1, &_cubeFramebuffer);
    glDeleteTextures(1, &_outputTexture);
    glDeleteFramebuffers(1, &_outputFramebuffer);
    glDeleteVertexArrays(1, &_vertexArray);
}

bool EffectRenderer::ready() {
    if (!_compiled) {
        compile();
    }
    return !_failed;
}

void EffectRenderer::compile() {
    _compiled         = true;
    const auto vertex = compiled(GL_VERTEX_SHADER, effectVertexText(kDialect));
    if (!vertex.has_value()) {
        _failed = true;
        _error  = std::format("effect {}: the vertex stage did not compile: {}", _effect.name, vertex.error());
        gr::log::warning("{}", _error);
        return;
    }
    for (const EffectPass& pass : _effect.passes) {
        if (pass.kind == PassKind::common) {
            continue;
        }
        const auto fragment = compiled(GL_FRAGMENT_SHADER, effectProgramText(_effect, pass.kind, kDialect));
        if (!fragment.has_value()) {
            _failed = true;
            _error  = std::format("effect {}, {} pass: {}", _effect.name, name(pass.kind), fragment.error());
            break;
        }
        const GLuint program = glCreateProgram();
        glAttachShader(program, *vertex);
        glAttachShader(program, *fragment);
        glLinkProgram(program);
        glDeleteShader(*fragment);
        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            _failed = true;
            _error  = std::format("effect {}, {} pass did not link: {}", _effect.name, name(pass.kind), infoLog(program, true));
            glDeleteProgram(program);
            break;
        }
        _programs.push_back(Program{.pass = pass.kind, .program = program});
        if (pass.kind != PassKind::image && pass.kind != PassKind::cubeA) {
            _buffers.push_back(Buffer{.pass = pass.kind});
        }
    }
    glDeleteShader(*vertex);
    if (_failed) {
        gr::log::warning("{}", _error);
        return;
    }
    for (const EffectPass& pass : _effect.passes) {
        for (std::size_t channel = 0UZ; channel < 4UZ; ++channel) {
            const auto bound = _effect.binding(pass, channel);
            if (bound.has_value() && bound->source == ChannelSource::pass && bound->filter == ChannelFilter::mipmap) {
                for (Buffer& buffer : _buffers) {
                    buffer.mipmaps = buffer.mipmaps || buffer.pass == bound->pass;
                }
            }
        }
    }
    glGenVertexArrays(1, &_vertexArray); // a core context draws nothing without one, even with no attributes
}

void EffectRenderer::clearBuffers() { _bufferSize = {}; }

void EffectRenderer::ensureBuffers(int width, int height) {
    if (_buffers.empty() || (_bufferSize[0] == width && _bufferSize[1] == height)) {
        return;
    }
    _bufferSize = {width, height};
    for (Buffer& buffer : _buffers) {
        if (buffer.textures[0] == 0U) {
            glGenTextures(2, buffer.textures.data());
            glGenFramebuffers(2, buffer.framebuffers.data());
        }
        for (std::size_t side = 0UZ; side < 2UZ; ++side) {
            const auto allocate = [&](bool floating) {
                glBindTexture(GL_TEXTURE_2D, buffer.textures[side]);
                glTexImage2D(GL_TEXTURE_2D, 0, floating ? GL_RGBA16F : GL_RGBA8, width, height, 0, GL_RGBA, floating ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindFramebuffer(GL_FRAMEBUFFER, buffer.framebuffers[side]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer.textures[side], 0);
                return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            };
            if (_floatBuffers && !allocate(true)) {
                _floatBuffers = false;
                gr::log::warning("effect {}: half-float buffers cannot be drawn into here; its buffers keep 8 bits per channel", _effect.name);
            }
            if (!_floatBuffers) {
                std::ignore = allocate(false);
            }
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        buffer.latest = 0UZ;
    }
}

unsigned EffectRenderer::samplerFor(const ChannelBinding& binding) {
    const std::array<std::uint8_t, 2> key{static_cast<std::uint8_t>(binding.filter), static_cast<std::uint8_t>(binding.wrap)};
    if (const auto found = _samplers.find(key); found != _samplers.end()) {
        return found->second;
    }
    GLuint sampler = 0U;
    glGenSamplers(1, &sampler);
    const GLint minify  = binding.filter == ChannelFilter::nearest ? GL_NEAREST : binding.filter == ChannelFilter::linear ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
    const GLint magnify = binding.filter == ChannelFilter::nearest ? GL_NEAREST : GL_LINEAR;
    const GLint wrap    = binding.wrap == ChannelWrap::repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minify);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magnify);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, wrap);
    _samplers.emplace(key, sampler);
    return sampler;
}

const EffectRenderer::Asset* EffectRenderer::assetFor(const ChannelBinding& binding) {
    if (const auto found = _assetTextures.find(binding.asset); found != _assetTextures.end()) {
        return found->second.texture == 0U ? nullptr : &found->second;
    }
    Asset&     asset   = _assetTextures[binding.asset];
    const auto missing = [&](std::string_view why) {
        gr::log::warning("effect {}: channel asset {} {}; the channel reads black", _effect.name, binding.asset, why);
        _problems.push_back(std::format("channel asset {} {}; the channel reads black", binding.asset, why));
        return nullptr;
    };
    if (binding.source == ChannelSource::image) {
        const auto image = decoded(_assets(binding.asset), binding.vflip);
        if (!image.has_value()) {
            return missing("is missing or not a png/jpg");
        }
        glGenTextures(1, &asset.texture);
        asset.target = GL_TEXTURE_2D;
        asset.size   = {static_cast<float>(image->width), static_cast<float>(image->height), 1.0f};
        glBindTexture(GL_TEXTURE_2D, asset.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        return &asset;
    }
    if (binding.source == ChannelSource::cubemap) {
        std::array<DecodedImage, 6> faces;
        for (std::size_t face = 0UZ; face < 6UZ; ++face) {
            auto image = decoded(_assets(cubemapFacePath(binding.asset, face)), binding.vflip);
            if (!image.has_value() || (face > 0UZ && (image->width != faces[0].width || image->height != faces[0].height))) {
                return missing(std::format("has no usable face {} ({})", face, cubemapFacePath(binding.asset, face)));
            }
            faces[face] = std::move(*image);
        }
        glGenTextures(1, &asset.texture);
        asset.target = GL_TEXTURE_CUBE_MAP;
        asset.size   = {static_cast<float>(faces[0].width), static_cast<float>(faces[0].height), 1.0f};
        glBindTexture(GL_TEXTURE_CUBE_MAP, asset.texture);
        for (std::size_t face = 0UZ; face < 6UZ; ++face) {
            glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA8, faces[face].width, faces[face].height, 0, GL_RGBA, GL_UNSIGNED_BYTE, faces[face].pixels.data());
        }
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        return &asset;
    }
    // a volume: a 20-byte header -- "BIN\0", uint32 width, height, depth, uint8 channels, uint8 layout, uint16
    // format (0 bytes, 10 32-bit floats) -- then the voxels, x fastest, little-endian
    const std::span<const std::uint8_t> bytes = _assets(binding.asset);
    if (bytes.size() < 20UZ || std::memcmp(bytes.data(), "BIN", 3UZ) != 0) {
        return missing("is missing or not a volume");
    }
    const std::uint32_t width    = littleEndian32(bytes, 4UZ);
    const std::uint32_t height   = littleEndian32(bytes, 8UZ);
    const std::uint32_t depth    = littleEndian32(bytes, 12UZ);
    const std::uint8_t  channels = bytes[16UZ];
    const unsigned      format   = static_cast<unsigned>(bytes[18UZ]) | static_cast<unsigned>(bytes[19UZ]) << 8U;
    const std::size_t   element  = format == 10U ? 4UZ : 1UZ;
    const std::size_t   voxels   = static_cast<std::size_t>(width) * height * depth;
    if ((format != 0U && format != 10U) || channels < 1U || channels > 4U || voxels == 0UZ || bytes.size() < 20UZ + voxels * channels * element) {
        return missing(std::format("has an unsupported header ({}×{}×{}, {} channels, format {})", width, height, depth, channels, format));
    }
    constexpr std::array<GLenum, 4> kBytes{GL_R8, GL_RG8, GL_RGB8, GL_RGBA8};
    constexpr std::array<GLenum, 4> kFloats{GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F};
    constexpr std::array<GLenum, 4> kLayouts{GL_RED, GL_RG, GL_RGB, GL_RGBA};
    glGenTextures(1, &asset.texture);
    asset.target = GL_TEXTURE_3D;
    asset.size   = {static_cast<float>(width), static_cast<float>(height), static_cast<float>(depth)};
    glBindTexture(GL_TEXTURE_3D, asset.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, static_cast<GLint>(format == 10U ? kFloats[channels - 1U] : kBytes[channels - 1U]), static_cast<GLsizei>(width), static_cast<GLsizei>(height), static_cast<GLsizei>(depth), 0, kLayouts[channels - 1U], format == 10U ? GL_FLOAT : GL_UNSIGNED_BYTE, bytes.data() + 20UZ);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_3D);
    return &asset;
}

void EffectRenderer::bindChannels(const EffectPass& pass) {
    for (std::size_t channel = 0UZ; channel < 4UZ; ++channel) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + channel));
        const auto bound = _effect.binding(pass, channel);
        if (!bound.has_value()) {
            glBindTexture(GL_TEXTURE_2D, 0U);
            glBindSampler(static_cast<GLuint>(channel), 0U);
            continue;
        }
        glBindSampler(static_cast<GLuint>(channel), samplerFor(*bound));
        if (bound->source == ChannelSource::pass) {
            const auto buffer = std::ranges::find(_buffers, bound->pass, &Buffer::pass);
            glBindTexture(GL_TEXTURE_2D, buffer == _buffers.end() ? 0U : buffer->textures[buffer->latest]);
        } else if (bound->source == ChannelSource::cubemap && bound->asset.empty()) {
            glBindTexture(GL_TEXTURE_CUBE_MAP, _cubeTexture);
        } else if (const Asset* asset = assetFor(*bound); asset != nullptr) {
            glBindTexture(asset->target, asset->texture);
        } else {
            glBindTexture(bound->source == ChannelSource::cubemap ? GL_TEXTURE_CUBE_MAP : bound->source == ChannelSource::volume ? GL_TEXTURE_3D : GL_TEXTURE_2D, 0U);
        }
    }
}

void EffectRenderer::renderPass(const Program& program, std::array<int, 4> viewport, std::array<float, 2> origin, const EffectInputs& inputs, int face) {
    const GLuint      id   = program.program;
    const EffectPass* pass = _effect.pass(program.pass);
    glUseProgram(id);
    glBindVertexArray(_vertexArray);
    bindChannels(*pass);
    const std::array<std::pair<GLint, unsigned>, 3> slides{std::pair{kSlideFromUnit, inputs.slideFrom}, std::pair{kSlideToUnit, inputs.slideTo}, std::pair{kContentUnit, inputs.content}};
    for (const auto& [unit, texture] : slides) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindSampler(static_cast<GLuint>(unit), 0U);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    glActiveTexture(GL_TEXTURE0);

    glUniform3f(glGetUniformLocation(id, "iResolution"), static_cast<float>(viewport[2]), static_cast<float>(viewport[3]), 1.0f);
    uniform(id, "iTime", inputs.time);
    uniform(id, "iTimeDelta", inputs.timeDelta);
    uniform(id, "iFrame", inputs.frame);
    uniform(id, "iFrameRate", inputs.frameRate);
    uniform(id, "iMouse", inputs.mouse);
    uniform(id, "iDate", inputs.date);
    uniform(id, "iSampleRate", 44100.0f);
    const std::array<float, 4> channelTimes{inputs.time, inputs.time, inputs.time, inputs.time};
    glUniform1fv(glGetUniformLocation(id, "iChannelTime"), 4, channelTimes.data());
    std::array<float, 12> resolutions{};
    for (std::size_t channel = 0UZ; channel < 4UZ; ++channel) {
        const auto           bound = _effect.binding(*pass, channel);
        std::array<float, 3> size{};
        if (bound.has_value() && bound->source == ChannelSource::pass) {
            size = {static_cast<float>(_bufferSize[0]), static_cast<float>(_bufferSize[1]), 1.0f};
        } else if (bound.has_value() && bound->source == ChannelSource::cubemap && bound->asset.empty()) {
            size = {static_cast<float>(_cubeSize), static_cast<float>(_cubeSize), 1.0f};
        } else if (bound.has_value()) {
            if (const auto found = _assetTextures.find(bound->asset); found != _assetTextures.end()) {
                size = found->second.size;
            }
        }
        std::ranges::copy(size, resolutions.begin() + static_cast<std::ptrdiff_t>(3UZ * channel));
    }
    glUniform3fv(glGetUniformLocation(id, "iChannelResolution"), 4, resolutions.data());
    for (const auto [index, channelName] : std::array{std::pair{0, "iChannel0"}, std::pair{1, "iChannel1"}, std::pair{2, "iChannel2"}, std::pair{3, "iChannel3"}}) {
        uniform(id, channelName, index);
    }
    uniform(id, "iSlideFrom", kSlideFromUnit);
    uniform(id, "iSlideTo", kSlideToUnit);
    uniform(id, "iContent", kContentUnit);
    uniform(id, "iProgress", std::clamp(inputs.progress, 0.0f, 1.0f));
    uniform(id, "iFocus", inputs.focus);
    uniform(id, "iDark", inputs.dark);
    uniform(id, "iThemeBackground", inputs.themeBackground);
    uniform(id, "iThemeText", inputs.themeText);
    uniform(id, "iThemeAccent", inputs.themeAccent);
    for (const EffectParameter& parameter : _effect.parameters) {
        const auto set = inputs.parameters.find(parameter.name);
        parameterUniform(id, parameter, set == inputs.parameters.end() ? parameter.value : clampedToRange(parameter, set->second));
    }
    uniform(id, "gr4Origin", origin);
    uniform(id, "gr4KeepAlpha", inputs.keepAlpha ? 1.0f : 0.0f);
    uniform(id, "gr4CubeFace", face);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void EffectRenderer::renderBuffers(int width, int height, const EffectInputs& inputs) {
    ensureBuffers(width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    for (const Program& program : _programs) {
        if (program.pass == PassKind::image) {
            continue;
        }
        if (program.pass == PassKind::cubeA) {
            if (_cubeTexture == 0U) {
                _cubeSize = kCubeFaceSize;
                glGenTextures(1, &_cubeTexture);
                glBindTexture(GL_TEXTURE_CUBE_MAP, _cubeTexture);
                for (GLenum face = 0U; face < 6U; ++face) {
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, _floatBuffers ? GL_RGBA16F : GL_RGBA8, _cubeSize, _cubeSize, 0, GL_RGBA, _floatBuffers ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
                }
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glGenFramebuffers(1, &_cubeFramebuffer);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, _cubeFramebuffer);
            for (int face = 0; face < 6; ++face) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), _cubeTexture, 0);
                renderPass(program, {0, 0, _cubeSize, _cubeSize}, {0.0f, 0.0f}, inputs, face);
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, _cubeTexture);
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            continue;
        }
        const auto        buffer  = std::ranges::find(_buffers, program.pass, &Buffer::pass);
        const std::size_t written = 1UZ - buffer->latest;
        glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffers[written]);
        renderPass(program, {0, 0, width, height}, {0.0f, 0.0f}, inputs, 0);
        buffer->latest = written;
        if (buffer->mipmaps) {
            glBindTexture(GL_TEXTURE_2D, buffer->textures[written]);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }
}

void EffectRenderer::drawInto(std::array<int, 4> rectangle, const EffectInputs& inputs) {
    if (!ready() || rectangle[2] <= 0 || rectangle[3] <= 0) {
        return;
    }
    const SavedTarget saved = SavedTarget::save();
    renderBuffers(rectangle[2], rectangle[3], inputs);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(saved.drawFramebuffer));
    glScissor(saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3]);
    saved.scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    if (inputs.keepAlpha) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    const auto image = std::ranges::find(_programs, PassKind::image, &Program::pass);
    renderPass(*image, rectangle, {static_cast<float>(rectangle[0]), static_cast<float>(rectangle[1])}, inputs, 0);
    saved.restore();
}

void EffectRenderer::readOutput(int width, int height, std::span<std::uint8_t> rgba) const {
    if (_outputFramebuffer == 0U || _outputSize != std::array{width, height} || rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4UZ) {
        return;
    }
    GLint boundFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &boundFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _outputFramebuffer);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(boundFramebuffer));
}

unsigned EffectRenderer::outputTexture(int width, int height) {
    if (_outputTexture != 0U && _outputSize[0] == width && _outputSize[1] == height) {
        return _outputTexture;
    }
    GLint boundTexture     = 0;
    GLint boundFramebuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFramebuffer);
    if (_outputTexture == 0U) {
        glGenTextures(1, &_outputTexture);
        glGenFramebuffers(1, &_outputFramebuffer);
    }
    _outputSize = {width, height};
    glBindTexture(GL_TEXTURE_2D, _outputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, _outputFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _outputTexture, 0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundTexture));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(boundFramebuffer));
    return _outputTexture;
}

unsigned EffectRenderer::drawToTexture(int width, int height, const EffectInputs& inputs) {
    if (!ready() || width <= 0 || height <= 0) {
        return 0U;
    }
    const SavedTarget saved = SavedTarget::save();
    std::ignore             = outputTexture(width, height);
    renderBuffers(width, height, inputs);
    glBindFramebuffer(GL_FRAMEBUFFER, _outputFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto image = std::ranges::find(_programs, PassKind::image, &Program::pass);
    renderPass(*image, {0, 0, width, height}, {0.0f, 0.0f}, inputs, 0);
    saved.restore();
    return _outputTexture;
}

} // namespace gr::present
