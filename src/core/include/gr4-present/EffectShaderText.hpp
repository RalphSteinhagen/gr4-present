#ifndef GR4_PRESENT_EFFECT_SHADER_TEXT_HPP
#define GR4_PRESENT_EFFECT_SHADER_TEXT_HPP

#include <gr4-present/EffectSource.hpp>

#include <string>
#include <string_view>

namespace gr::present {

/// GLSL ES 3.00 in the browser (WebGL 2), GLSL 3.30 core natively; effects are written once, in ES 3.00
enum class GlslDialect : std::uint8_t { es300, core330 };

/// the vertex stage every pass shares: one triangle covering the viewport, from the vertex index alone
[[nodiscard]] std::string effectVertexText(GlslDialect dialect);

/**
 * The fragment program of one pass: version and precision lines, the uniforms every effect may read, the effect's own
 * parameters as uniforms, the Common section, the pass, and a `main()` that calls `mainImage` (or, for Cube A,
 * `mainCubemap` once per face). `#line` directives make the compiler report the effect file's own line numbers.
 *
 * Uniforms an effect may read:
 * - `vec3 iResolution` (pixels of what it draws), `float iTime`, `float iTimeDelta`, `int iFrame`, `float iFrameRate`,
 *   `vec4 iMouse`, `vec4 iDate`, `float iChannelTime[4]`, `vec3 iChannelResolution[4]`, `float iSampleRate`,
 *   `iChannel0` … `iChannel3` (a `sampler2D`, `samplerCube` or `sampler3D` as bound);
 * - the slides a transition or reveal mixes, `sampler2D iSlideFrom`, `iSlideTo`; the content under an overlay or a
 *   reveal, `sampler2D iContent`; `float iProgress` (0 … 1); `vec2 iFocus` (the last click, 0 … 1); `float iDark`
 *   (1 in the dark scheme); `vec4 iThemeBackground`, `iThemeText`, `iThemeAccent`.
 */
[[nodiscard]] std::string effectProgramText(const EffectSource& effect, PassKind pass, GlslDialect dialect);

/// GLSL type of a parameter's uniform
[[nodiscard]] std::string_view glslTypeOf(ParameterType type) noexcept;

} // namespace gr::present

#endif // GR4_PRESENT_EFFECT_SHADER_TEXT_HPP
