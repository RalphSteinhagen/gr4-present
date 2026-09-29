#include <boost/ut.hpp>

#include <gr4-present/EffectShaderText.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr std::string_view kImageCode  = "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(1.0); }";
constexpr std::string_view kBufferCode = "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(0.5); }";
constexpr std::string_view kCubeCode   = "void mainCubemap(out vec4 c, in vec2 f, in vec3 o, in vec3 d) { c = vec4(d, 1.0); }";

[[nodiscard]] std::string joined(std::initializer_list<std::string_view> lines) {
    std::string text;
    for (const std::string_view line : lines) {
        text += line;
        text += '\n';
    }
    return text;
}

[[nodiscard]] EffectSource effectOf(std::string_view text) {
    auto result = parseEffect("fx", text);
    expect(result.has_value()) << "the effect text should parse";
    return result.has_value() ? std::move(result->effect) : EffectSource{};
}

[[nodiscard]] EffectSource singlePassEffect(std::initializer_list<std::string_view> headerLines = {}) {
    std::string text = joined(headerLines);
    text += kImageCode;
    text += '\n';
    return effectOf(text);
}

// line 1 header, 2 Common marker, 3 Common code, 4 BufferA marker, 5 BufferA code, 6 Image marker, 7 Image code
[[nodiscard]] EffectSource multiPassEffect() { return effectOf(joined({"// @name multi", "//--- pass: Common", "float commonHelper(float x) { return x * 2.0; }", "//--- pass: BufferA", kBufferCode, "//--- pass: Image", kImageCode})); }

[[nodiscard]] bool declares(const std::string& text, std::string_view type, std::string_view uniformName) { return text.contains(std::string{"uniform "} + std::string{type} + ' ' + std::string{uniformName}); }

[[nodiscard]] std::size_t countOf(const std::string& text, std::string_view fragment) {
    std::size_t count = 0UZ;
    for (std::size_t at = text.find(fragment); at != std::string::npos; at = text.find(fragment, at + fragment.size())) {
        ++count;
    }
    return count;
}

} // namespace

const suite<"EffectShaderText program"> programTests = [] {
    "the Common pass and a missing pass have no program"_test = [] {
        const EffectSource effect = multiPassEffect();
        expect(effectProgramText(effect, PassKind::common, GlslDialect::es300).empty());
        expect(effectProgramText(effect, PassKind::bufferB, GlslDialect::es300).empty());
        expect(effectProgramText(effect, PassKind::cubeA, GlslDialect::core330).empty());
        expect(effectProgramText(EffectSource{}, PassKind::image, GlslDialect::es300).empty());
    };

    "an es300 program starts with the ES 3.00 version line and declares highp defaults"_test = [] {
        const std::string text = effectProgramText(singlePassEffect(), PassKind::image, GlslDialect::es300);
        expect(text.starts_with("#version 300 es\n"));
        expect(text.contains("precision highp float;"));
        expect(text.contains("precision highp int;"));
        expect(text.contains("precision highp sampler2D;"));
        expect(text.contains("precision highp samplerCube;"));
        expect(text.contains("precision highp sampler3D;"));
    };

    "a core330 program starts with the 3.30 core version line and has no precision statements"_test = [] {
        const std::string text = effectProgramText(singlePassEffect(), PassKind::image, GlslDialect::core330);
        expect(text.starts_with("#version 330 core\n"));
        expect(!text.contains("precision"));
        expect(!text.contains("#version 300"));
    };

    "the version line appears once and the precision lines follow it in es300"_test = [] {
        const std::string text = effectProgramText(singlePassEffect(), PassKind::image, GlslDialect::es300);
        expect(eq(countOf(text, "#version"), 1UZ));
        expect(lt(text.find("#version"), text.find("precision highp float;")));
    };

    "every standard uniform is declared with its type in both dialects"_test = [] {
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectProgramText(singlePassEffect(), PassKind::image, dialect);
            expect(declares(text, "vec3", "iResolution"));
            expect(declares(text, "float", "iTime"));
            expect(declares(text, "float", "iTimeDelta"));
            expect(declares(text, "int", "iFrame"));
            expect(declares(text, "float", "iFrameRate"));
            expect(declares(text, "vec4", "iMouse"));
            expect(declares(text, "vec4", "iDate"));
            expect(declares(text, "float", "iChannelTime[4]"));
            expect(declares(text, "vec3", "iChannelResolution[4]"));
            expect(declares(text, "float", "iSampleRate"));
            expect(declares(text, "sampler2D", "iSlideFrom"));
            expect(declares(text, "sampler2D", "iSlideTo"));
            expect(declares(text, "sampler2D", "iContent"));
            expect(declares(text, "float", "iProgress"));
            expect(declares(text, "vec2", "iFocus"));
            expect(declares(text, "float", "iDark"));
            expect(declares(text, "vec4", "iThemeBackground"));
            expect(declares(text, "vec4", "iThemeText"));
            expect(declares(text, "vec4", "iThemeAccent"));
        }
    };

    "unbound channels are 2D samplers"_test = [] {
        const std::string text = effectProgramText(singlePassEffect(), PassKind::image, GlslDialect::es300);
        for (const std::string_view channel : {"iChannel0", "iChannel1", "iChannel2", "iChannel3"}) {
            expect(declares(text, "sampler2D", channel)) << channel;
        }
    };

    "each channel gets the sampler type of its binding"_test = [] {
        const EffectSource effect = effectOf(joined({
            "// @channel0 wood.png",
            "// @channel1 sky.png type=cubemap",
            "// @channel2 noise.bin",
            "// @channel3 BufferA",
            "//--- pass: BufferA",
            kBufferCode,
            "//--- pass: Image",
            kImageCode,
        }));
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectProgramText(effect, PassKind::image, dialect);
            expect(declares(text, "sampler2D", "iChannel0"));
            expect(declares(text, "samplerCube", "iChannel1"));
            expect(declares(text, "sampler3D", "iChannel2"));
            expect(declares(text, "sampler2D", "iChannel3"));
        }
    };

    "a CubeA binding is a samplerCube"_test = [] {
        const EffectSource effect = effectOf(joined({"// @channel2 CubeA", "//--- pass: CubeA", kCubeCode, "//--- pass: Image", kImageCode}));
        const std::string  text   = effectProgramText(effect, PassKind::image, GlslDialect::es300);
        expect(declares(text, "samplerCube", "iChannel2"));
        expect(declares(text, "sampler2D", "iChannel0"));
    };

    "a pass binding decides the sampler of that pass and the file binding serves the other passes"_test = [] {
        const EffectSource effect = effectOf(joined({
            "// @channel0 sky.png type=cubemap",
            "//--- pass: BufferA",
            "// @channel0 flat.png",
            kBufferCode,
            "//--- pass: Image",
            kImageCode,
        }));
        expect(declares(effectProgramText(effect, PassKind::bufferA, GlslDialect::es300), "sampler2D", "iChannel0"));
        expect(declares(effectProgramText(effect, PassKind::image, GlslDialect::es300), "samplerCube", "iChannel0"));
    };

    "every parameter type becomes a uniform of its GLSL type and a colour is a vec4"_test = [] {
        const EffectSource effect = singlePassEffect({"// @param pFloat float 1.5", "// @param pInt int 3", "// @param pBool bool true", "// @param pVec2 vec2 0.1,0.2", "// @param pVec3 vec3 1,2,3", "// @param pVec4 vec4 1,2,3,4", "// @param pColour colour #ff8800"});
        expect(eq(effect.parameters.size(), 7UZ));
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectProgramText(effect, PassKind::image, dialect);
            expect(declares(text, "float", "pFloat;"));
            expect(declares(text, "int", "pInt;"));
            expect(declares(text, "bool", "pBool;"));
            expect(declares(text, "vec2", "pVec2;"));
            expect(declares(text, "vec3", "pVec3;"));
            expect(declares(text, "vec4", "pVec4;"));
            expect(declares(text, "vec4", "pColour;"));
        }
    };

    "parameter uniforms are declared in every pass program"_test = [] {
        const EffectSource effect = effectOf(joined({"// @param speed float 1.0", "//--- pass: BufferA", kBufferCode, "//--- pass: Image", kImageCode}));
        expect(declares(effectProgramText(effect, PassKind::bufferA, GlslDialect::es300), "float", "speed;"));
        expect(declares(effectProgramText(effect, PassKind::image, GlslDialect::es300), "float", "speed;"));
    };

    "glslTypeOf maps each parameter type"_test = [] {
        expect(eq(glslTypeOf(ParameterType::scalar), std::string_view{"float"}));
        expect(eq(glslTypeOf(ParameterType::integer), std::string_view{"int"}));
        expect(eq(glslTypeOf(ParameterType::boolean), std::string_view{"bool"}));
        expect(eq(glslTypeOf(ParameterType::vec2), std::string_view{"vec2"}));
        expect(eq(glslTypeOf(ParameterType::vec3), std::string_view{"vec3"}));
        expect(eq(glslTypeOf(ParameterType::vec4), std::string_view{"vec4"}));
        expect(eq(glslTypeOf(ParameterType::colour), std::string_view{"vec4"}));
    };

    "an unmarked effect carries its code after a line directive for line one"_test = [] {
        const std::string text = effectProgramText(singlePassEffect(), PassKind::image, GlslDialect::es300);
        expect(eq(countOf(text, "#line"), 1UZ));
        expect(text.contains(std::string{"#line 1\n"} + std::string{kImageCode}));
    };

    "Common code precedes the pass code and each is preceded by its file line"_test = [] {
        const std::string text       = effectProgramText(multiPassEffect(), PassKind::image, GlslDialect::es300);
        const std::size_t commonLine = text.find("#line 3\nfloat commonHelper(");
        const std::size_t imageLine  = text.find(std::string{"#line 7\n"} + std::string{kImageCode});
        expect(commonLine != std::string::npos);
        expect(imageLine != std::string::npos);
        expect(lt(commonLine, imageLine));
        expect(eq(countOf(text, "#line"), 2UZ));
        expect(!text.contains(kBufferCode)) << "another pass's code is not part of this program";
    };

    "a buffer program holds the Common code and its own pass but not the Image pass"_test = [] {
        const std::string text = effectProgramText(multiPassEffect(), PassKind::bufferA, GlslDialect::es300);
        expect(lt(text.find("#line 3\nfloat commonHelper("), text.find(std::string{"#line 5\n"} + std::string{kBufferCode})));
        expect(!text.contains(kImageCode));
    };

    "uniforms and precision come before the effect code"_test = [] {
        const std::string text      = effectProgramText(multiPassEffect(), PassKind::image, GlslDialect::es300);
        const std::size_t firstLine = text.find("#line");
        expect(lt(text.find("uniform vec3 iResolution"), firstLine));
        expect(lt(text.find("uniform sampler2D iChannel0"), firstLine));
        expect(lt(text.find("precision highp float;"), firstLine));
    };

    "the Image pass ends with a main that calls mainImage relative to the origin"_test = [] {
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectProgramText(singlePassEffect(), PassKind::image, dialect);
            expect(text.contains("void main()"));
            expect(text.contains("mainImage(colour, gl_FragCoord.xy - gr4Origin)"));
            expect(lt(text.find(kImageCode), text.find("void main()"))) << "main follows the effect code";
        }
    };

    "a buffer pass calls mainImage with the plain fragment coordinate"_test = [] {
        const std::string text = effectProgramText(multiPassEffect(), PassKind::bufferA, GlslDialect::es300);
        expect(text.contains("void main()"));
        expect(text.contains("mainImage(colour, gl_FragCoord.xy)"));
        expect(!text.contains("gr4Origin"));
        expect(lt(text.find(kBufferCode), text.find("void main()")));
    };

    "the CubeA pass calls mainCubemap and not mainImage"_test = [] {
        const EffectSource effect = effectOf(joined({"//--- pass: CubeA", kCubeCode, "//--- pass: Image", kImageCode}));
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectProgramText(effect, PassKind::cubeA, dialect);
            expect(text.contains("void main()"));
            expect(text.contains("mainCubemap("));
            expect(!text.contains("mainImage"));
            expect(lt(text.find(kCubeCode), text.find("void main()")));
        }
    };

    "the Image program does not call mainCubemap"_test = [] {
        const EffectSource effect = effectOf(joined({"//--- pass: CubeA", kCubeCode, "//--- pass: Image", kImageCode}));
        expect(!effectProgramText(effect, PassKind::image, GlslDialect::es300).contains("mainCubemap"));
    };

    "the same effect gives different version lines but the same body in both dialects"_test = [] {
        const EffectSource effect = multiPassEffect();
        const std::string  es     = effectProgramText(effect, PassKind::image, GlslDialect::es300);
        const std::string  core   = effectProgramText(effect, PassKind::image, GlslDialect::core330);
        expect(es != core);
        expect(es.contains(kImageCode));
        expect(core.contains(kImageCode));
        expect(es.contains("#line 7\n"));
        expect(core.contains("#line 7\n"));
    };
};

const suite<"EffectShaderText vertex"> vertexTests = [] {
    "the vertex stage carries the version line of its dialect"_test = [] {
        const std::string es   = effectVertexText(GlslDialect::es300);
        const std::string core = effectVertexText(GlslDialect::core330);
        expect(es.starts_with("#version 300 es\n"));
        expect(core.starts_with("#version 330 core\n"));
        expect(eq(countOf(es, "#version"), 1UZ));
        expect(eq(countOf(core, "#version"), 1UZ));
    };

    "the vertex stage defines main and writes gl_Position from the vertex index"_test = [] {
        for (const GlslDialect dialect : {GlslDialect::es300, GlslDialect::core330}) {
            const std::string text = effectVertexText(dialect);
            expect(text.contains("void main()"));
            expect(text.contains("gl_Position"));
            expect(text.contains("gl_VertexID"));
        }
    };
};

int main() { return 0; }
