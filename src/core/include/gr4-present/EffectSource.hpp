#ifndef GR4_PRESENT_EFFECT_SOURCE_HPP
#define GR4_PRESENT_EFFECT_SOURCE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/**
 * An effect shader as its `.glsl` file describes it, before any GPU sees it.
 *
 * An effect is a GLSL ES 3.00 fragment program that defines `void mainImage(out vec4 colour, in vec2 fragCoord)`. A
 * leading comment block may carry `// @key value` lines (name, author, source, licence, the uses it supports, its
 * parameters, its channel bindings, a still time and a default duration). Without `//--- pass: <Name>` markers the
 * whole file is the Image pass; with them the file holds a Common section, Buffer A–D, Cube A and the Image pass,
 * each rendered once per frame in that order, Common prepended to every one.
 *
 * ```glsl
 * // @name   plasma
 * // @uses   background viewport
 * // @param  speed float 1.0 0.1 10.0
 * void mainImage(out vec4 colour, in vec2 fragCoord) {
 *     vec2 uv = fragCoord / iResolution.xy;
 *     colour  = vec4(0.5 + 0.5 * cos(iTime * speed + uv.xyx + vec3(0, 2, 4)), 1.0);
 * }
 * ```
 */
enum class EffectUse : std::uint8_t { transition, background, viewport, overlay, reveal, camera, pointer, pause };

enum class ParameterType : std::uint8_t { scalar, integer, boolean, vec2, vec3, vec4, colour };

struct EffectParameter {
    std::string                         name;
    ParameterType                       type  = ParameterType::scalar;
    std::array<float, 4>                value = {}; // the default; a colour is 0..1 RGBA as written
    std::optional<std::array<float, 2>> range;      // inclusive, applies to every component

    bool operator==(const EffectParameter&) const = default;
};

enum class PassKind : std::uint8_t { common, bufferA, bufferB, bufferC, bufferD, cubeA, image };

enum class ChannelSource : std::uint8_t { image, cubemap, volume, pass };
enum class ChannelFilter : std::uint8_t { nearest, linear, mipmap };
enum class ChannelWrap : std::uint8_t { clamp, repeat };

struct ChannelBinding {
    ChannelSource source = ChannelSource::image;
    std::string   asset;                    // relative to the deck's `effects/` directory; empty for a pass
    PassKind      pass   = PassKind::image; // the buffer read when `source` is `pass`
    ChannelFilter filter = ChannelFilter::mipmap;
    ChannelWrap   wrap   = ChannelWrap::repeat;
    bool          vflip  = true; // images are stored top row first, sampled bottom row first

    bool operator==(const ChannelBinding&) const = default;
};

using ChannelBindings = std::array<std::optional<ChannelBinding>, 4>;

struct EffectPass {
    PassKind        kind = PassKind::image;
    std::string     code;
    std::size_t     firstLine = 1UZ; // the file line `code` starts at, for compiler messages
    ChannelBindings channels  = {};  // the pass's own bindings, over the file's

    bool operator==(const EffectPass&) const = default;
};

struct EffectSource {
    std::string                  name;
    std::string                  author;
    std::string                  source;  // where the effect was adapted from, as an acknowledgement
    std::string                  licence; // empty: the deck's or the project's licence applies
    std::vector<EffectUse>       uses;    // empty: every use
    std::vector<EffectParameter> parameters;
    ChannelBindings              channels = {}; // bindings every pass shares unless it binds the channel itself
    std::vector<EffectPass>      passes;        // in render order, Common first and Image last
    std::optional<float>         still;         // seconds an exported page and a pixel test freeze the effect at
    std::optional<float>         duration;      // seconds a transition or reveal through it lasts unless the slide says

    [[nodiscard]] bool              supports(EffectUse use) const noexcept;
    [[nodiscard]] const EffectPass* pass(PassKind kind) const noexcept;
    /// the binding a pass sees on `channel`: its own, else the file's
    [[nodiscard]] std::optional<ChannelBinding> binding(const EffectPass& pass, std::size_t channel) const;
    [[nodiscard]] const EffectParameter*        parameter(std::string_view parameterName) const noexcept;

    bool operator==(const EffectSource&) const = default;
};

struct ParsedEffect {
    EffectSource             effect;
    std::vector<std::string> warnings; // what was ignored, with the file line
};

[[nodiscard]] std::string_view             name(EffectUse use) noexcept;
[[nodiscard]] std::optional<EffectUse>     parseEffectUse(std::string_view text) noexcept;
[[nodiscard]] std::string_view             name(PassKind kind) noexcept;
[[nodiscard]] std::optional<PassKind>      parsePassKind(std::string_view text) noexcept;
[[nodiscard]] std::optional<ParameterType> parseParameterType(std::string_view text) noexcept;

/// the components of a parameter value as written: `0.5`, `3`, `true`, `0.1,0.2`, `#ff8800` or `#ff880080`
[[nodiscard]] std::optional<std::array<float, 4>> parseParameterValue(ParameterType type, std::string_view text);

/// whether `word` names one of the viewer's own transitions or reveals (cut, fade, push-left, rise, …), which win over
/// an effect file of the same name
[[nodiscard]] bool isBuiltInMotion(std::string_view word) noexcept;

/// the effect a `transition:`, `background:`, `overlay:` or `in=` value names: its first word unless that is a built-in
/// motion; for `camera` and `zoom` the word after it; empty when the value names none
[[nodiscard]] std::string_view effectNamed(std::string_view value) noexcept;

/// the package path of an effect name: `effects/<name>.glsl` for a bare name, the name itself when it holds a slash
[[nodiscard]] std::string effectPath(std::string_view effectName);

/// reads an effect file; fails only when there is no Image pass to draw
[[nodiscard]] std::expected<ParsedEffect, std::string> parseEffect(std::string_view effectName, std::string_view text);

/// the file of one cubemap face: face 0 is `asset` itself, faces 1 … 5 insert `_1` … `_5` before its extension
[[nodiscard]] std::string cubemapFacePath(std::string_view asset, std::size_t face);

/// the asset files an effect reads, relative to the deck's `effects/` directory: images, the six faces of each
/// cubemap (`sky.png`, `sky_1.png` … `sky_5.png`) and volumes
[[nodiscard]] std::vector<std::string> effectAssets(const EffectSource& effect);

/// the assets named by `@channel` lines of an effect file's text, without parsing it as a whole; the loader uses this
/// to fetch an effect's assets as soon as the effect file arrives
[[nodiscard]] std::vector<std::string> effectAssetsOf(std::string_view text);

} // namespace gr::present

#endif // GR4_PRESENT_EFFECT_SOURCE_HPP
