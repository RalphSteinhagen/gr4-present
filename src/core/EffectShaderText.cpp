#include <gr4-present/EffectShaderText.hpp>

#include <format>

namespace gr::present {

namespace {

[[nodiscard]] std::string_view versionOf(GlslDialect dialect) noexcept {
    // ES 3.00 leaves sampler3D without a default precision and gives samplerCube a low one
    return dialect == GlslDialect::es300 ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp samplerCube;\nprecision highp sampler3D;\n" : "#version 330 core\n";
}

[[nodiscard]] std::string_view samplerOf(const std::optional<ChannelBinding>& binding) noexcept {
    if (!binding.has_value()) {
        return "sampler2D";
    }
    switch (binding->source) {
    case ChannelSource::cubemap: return "samplerCube";
    case ChannelSource::volume: return "sampler3D";
    default: return "sampler2D";
    }
}

// The direction a cubemap texel looks along, the inverse of the OpenGL cube-map face selection table: face order
// +X, -X, +Y, -Y, +Z, -Z, and (s, t) the texel's position on its face, 0 … 1.
constexpr std::string_view kCubeWrapper = R"(
uniform int gr4CubeFace;
out vec4 gr4Output;
void main() {
    vec2 st = gl_FragCoord.xy / iResolution.xy * 2.0 - 1.0;
    vec3 direction;
    if (gr4CubeFace == 0) direction = vec3(1.0, -st.y, -st.x);
    else if (gr4CubeFace == 1) direction = vec3(-1.0, -st.y, st.x);
    else if (gr4CubeFace == 2) direction = vec3(st.x, 1.0, st.y);
    else if (gr4CubeFace == 3) direction = vec3(st.x, -1.0, -st.y);
    else if (gr4CubeFace == 4) direction = vec3(st.x, -st.y, 1.0);
    else direction = vec3(-st.x, -st.y, -1.0);
    vec4 colour = vec4(0.0, 0.0, 0.0, 1.0);
    mainCubemap(colour, gl_FragCoord.xy, vec3(0.0), normalize(direction));
    gr4Output = colour;
}
)";

// gr4Origin moves the window coordinates of a pass drawn into part of the screen to the effect's own; gr4KeepAlpha is
// 0 where what an effect draws covers what is under it, whatever alpha it wrote
constexpr std::string_view kImageWrapper = R"(
uniform vec2 gr4Origin;
uniform float gr4KeepAlpha;
out vec4 gr4Output;
void main() {
    vec4 colour = vec4(0.0, 0.0, 0.0, 1.0);
    mainImage(colour, gl_FragCoord.xy - gr4Origin);
    gr4Output = vec4(colour.rgb, mix(1.0, colour.a, gr4KeepAlpha));
}
)";

constexpr std::string_view kBufferWrapper = R"(
out vec4 gr4Output;
void main() {
    vec4 colour = vec4(0.0, 0.0, 0.0, 1.0);
    mainImage(colour, gl_FragCoord.xy);
    gr4Output = colour;
}
)";

} // namespace

std::string_view glslTypeOf(ParameterType type) noexcept {
    switch (type) {
    case ParameterType::scalar: return "float";
    case ParameterType::integer: return "int";
    case ParameterType::boolean: return "bool";
    case ParameterType::vec2: return "vec2";
    case ParameterType::vec3: return "vec3";
    case ParameterType::vec4:
    case ParameterType::colour: return "vec4";
    }
    return "float";
}

std::string effectVertexText(GlslDialect dialect) {
    return std::format("{}{}", versionOf(dialect), R"(
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)");
}

std::string effectProgramText(const EffectSource& effect, PassKind passKind, GlslDialect dialect) {
    const EffectPass* pass = effect.pass(passKind);
    if (pass == nullptr || passKind == PassKind::common) {
        return {};
    }
    std::string text{versionOf(dialect)};
    text += "uniform vec3 iResolution;\nuniform float iTime;\nuniform float iTimeDelta;\nuniform int iFrame;\nuniform float iFrameRate;\nuniform vec4 iMouse;\nuniform vec4 iDate;\n"
            "uniform float iChannelTime[4];\nuniform vec3 iChannelResolution[4];\nuniform float iSampleRate;\n";
    for (std::size_t channel = 0UZ; channel < 4UZ; ++channel) {
        text += std::format("uniform {} iChannel{};\n", samplerOf(effect.binding(*pass, channel)), channel);
    }
    text += "uniform sampler2D iSlideFrom;\nuniform sampler2D iSlideTo;\nuniform sampler2D iContent;\nuniform float iProgress;\nuniform vec2 iFocus;\nuniform float iDark;\n"
            "uniform vec4 iThemeBackground;\nuniform vec4 iThemeText;\nuniform vec4 iThemeAccent;\n";
    for (const EffectParameter& parameter : effect.parameters) {
        text += std::format("uniform {} {};\n", glslTypeOf(parameter.type), parameter.name);
    }
    if (const EffectPass* common = effect.pass(PassKind::common); common != nullptr) {
        text += std::format("#line {}\n{}\n", common->firstLine, common->code);
    }
    text += std::format("#line {}\n{}\n", pass->firstLine, pass->code);
    text += passKind == PassKind::cubeA ? kCubeWrapper : passKind == PassKind::image ? kImageWrapper : kBufferWrapper;
    return text;
}

} // namespace gr::present
