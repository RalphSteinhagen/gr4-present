#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif

#include <boost/ut.hpp>

#include "EffectRenderer.hpp"

#include <gr4-present/EffectSource.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

constexpr int kWidth  = 320;
constexpr int kHeight = 180;

[[nodiscard]] std::string fileText(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// two slides with structure at every scale: smooth gradients for wipes and folds, hard edges for grains and tears
[[nodiscard]] std::vector<std::uint8_t> slidePixels(bool arriving) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth * kHeight * 4));
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t at      = static_cast<std::size_t>((y * kWidth + x) * 4);
            const bool        checker = ((x / 16) + (y / 16)) % 2 == 0;
            const float       dx      = static_cast<float>(x - kWidth / 2);
            const float       dy      = static_cast<float>(y - kHeight / 2);
            const bool        ring    = static_cast<int>(std::sqrt(dx * dx + dy * dy)) / 12 % 2 == 0;
            pixels[at + 0UZ]          = static_cast<std::uint8_t>(arriving ? (ring ? 230 : 40) : 255 * x / kWidth);
            pixels[at + 1UZ]          = static_cast<std::uint8_t>(arriving ? 255 * y / kHeight : (checker ? 200 : 60));
            pixels[at + 2UZ]          = static_cast<std::uint8_t>(arriving ? (checker ? 90 : 170) : 255 * (kWidth - x) / kWidth);
            pixels[at + 3UZ]          = 255U;
        }
    }
    return pixels;
}

[[nodiscard]] GLuint texture(const std::vector<std::uint8_t>& pixels) {
    GLuint id = 0U;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

[[nodiscard]] std::vector<std::uint8_t> readBack(GLuint framebuffer) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth * kHeight * 4));
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

[[nodiscard]] GLuint compiled(GLenum kind, const std::string& text) {
    const char*  source = text.c_str();
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 4096> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "the snapshot did not compile: {}", log.data());
    }
    return shader;
}

/// the transition shader as it was before effects became files, drawn into its own framebuffer
struct Legacy {
    GLuint program     = 0U;
    GLuint array       = 0U;
    GLuint framebuffer = 0U;
    GLuint output      = 0U;

    Legacy() {
        const std::filesystem::path reference{GR4_PRESENT_REFERENCE_DIRECTORY};
        const GLuint                vertex   = compiled(GL_VERTEX_SHADER, "#version 330 core\n" + fileText(reference / "effects/legacy_transition.vert"));
        const GLuint                fragment = compiled(GL_FRAGMENT_SHADER, "#version 330 core\n" + fileText(reference / "effects/legacy_transition.frag"));
        program                              = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glGenVertexArrays(1, &array);
        glGenTextures(1, &output);
        glBindTexture(GL_TEXTURE_2D, output);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    }

    [[nodiscard]] std::vector<std::uint8_t> draw(int effect, GLuint leaving, GLuint arriving, float progress, std::array<float, 2> focus, float time) const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, kWidth, kHeight);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glUseProgram(program);
        glBindVertexArray(array);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, leaving);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, arriving);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(program, "leaving"), 0);
        glUniform1i(glGetUniformLocation(program, "arriving"), 1);
        glUniform1f(glGetUniformLocation(program, "progress"), progress);
        glUniform1i(glGetUniformLocation(program, "effect"), effect);
        glUniform2f(glGetUniformLocation(program, "focus"), focus[0], focus[1]);
        glUniform1f(glGetUniformLocation(program, "aspect"), static_cast<float>(kWidth) / static_cast<float>(kHeight));
        glUniform1f(glGetUniformLocation(program, "time"), time);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return readBack(framebuffer);
    }
};

struct Difference {
    double meanAbsolute = 0.0; // per channel, in 1/255
    double farOff       = 0.0; // fraction of pixels with a channel more than 8/255 off
};

[[nodiscard]] Difference compare(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual) {
    double      sum = 0.0;
    std::size_t far = 0UZ;
    for (std::size_t pixel = 0UZ; pixel < expected.size() / 4UZ; ++pixel) {
        int worst = 0;
        for (std::size_t channel = 0UZ; channel < 3UZ; ++channel) {
            const int off = std::abs(static_cast<int>(expected[pixel * 4UZ + channel]) - static_cast<int>(actual[pixel * 4UZ + channel]));
            sum += off;
            worst = std::max(worst, off);
        }
        far += worst > 8 ? 1UZ : 0UZ;
    }
    const double pixels = static_cast<double>(expected.size() / 4UZ);
    return {.meanAbsolute = sum / (3.0 * pixels), .farOff = static_cast<double>(far) / pixels};
}

struct Window {
    SDL_Window*   window  = nullptr;
    SDL_GLContext context = nullptr;

    Window() {
        if (std::getenv("SDL_VIDEODRIVER") == nullptr) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        }
        SDL_Init(SDL_INIT_VIDEO);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        window  = SDL_CreateWindow("qa_EffectTransitions", kWidth, kHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        context = window == nullptr ? nullptr : SDL_GL_CreateContext(window);
    }
    ~Window() {
        if (context != nullptr) {
            SDL_GL_DestroyContext(context);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

struct Effect {
    std::string_view      name;
    int                   legacyIndex = 0;
    std::filesystem::path file;
};

} // namespace

int main() {
    const Window window;
    expect(fatal(window.context != nullptr)) << "no GL 3.3 core context:" << SDL_GetError();

    const std::filesystem::path source{GR4_PRESENT_SOURCE_DIRECTORY};
    const std::filesystem::path bundled = source / "assets/effects";
    const std::filesystem::path deck    = source / "presentations/default/effects";
    // the order of the effect enumeration the snapshot dispatches on
    const std::array<Effect, 11> effects{Effect{"fire", 0, deck / "fire.glsl"}, Effect{"crt", 1, bundled / "crt.glsl"}, Effect{"glitch", 2, bundled / "glitch.glsl"}, Effect{"waterfall", 3, deck / "waterfall.glsl"}, Effect{"sine", 4, bundled / "sine.glsl"}, Effect{"disintegrate", 5, bundled / "disintegrate.glsl"}, Effect{"ripple", 6, bundled / "ripple.glsl"}, Effect{"curl", 7, bundled / "curl.glsl"}, Effect{"cube", 8, bundled / "cube.glsl"}, Effect{"flip", 9, bundled / "flip.glsl"}, Effect{"fire2", 10, deck / "fire2.glsl"}};

    const Legacy                   legacy;
    const GLuint                   leaving  = texture(slidePixels(false));
    const GLuint                   arriving = texture(slidePixels(true));
    constexpr std::array<float, 2> kFocus{0.3f, 0.6f};
    constexpr float                kTime = 1.25f;

    for (const Effect& effect : effects) {
        test(std::format("the {} effect file draws what the transition shader before it drew", effect.name)) = [&] {
            auto parsed = parseEffect(effect.name, fileText(effect.file));
            expect(fatal(parsed.has_value())) << effect.file.string() << "does not parse";
            expect(parsed->warnings.empty()) << effect.file.string() << "warns";
            EffectRenderer renderer{parsed->effect, [](std::string_view) { return std::span<const std::uint8_t>{}; }};
            expect(fatal(renderer.ready())) << renderer.error();
            for (const float progress : {0.25f, 0.5f, 0.75f}) {
                const std::vector<std::uint8_t> before = legacy.draw(effect.legacyIndex, leaving, arriving, progress, kFocus, kTime);
                EffectInputs                    inputs;
                inputs.time              = kTime;
                inputs.slideFrom         = leaving;
                inputs.slideTo           = arriving;
                inputs.progress          = progress;
                inputs.focus             = kFocus;
                const GLuint drawn       = renderer.drawToTexture(kWidth, kHeight, inputs);
                GLuint       framebuffer = 0U;
                glGenFramebuffers(1, &framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, drawn, 0);
                const Difference difference = compare(before, readBack(framebuffer));
                glDeleteFramebuffers(1, &framebuffer);
                expect(difference.meanAbsolute <= 1.0) << std::format("{} at {}: mean difference {:.3f}/255", effect.name, progress, difference.meanAbsolute);
                expect(difference.farOff <= 0.005) << std::format("{} at {}: {:.2f} % of pixels more than 8/255 off", effect.name, progress, 100.0 * difference.farOff);
            }
        };
    }
    return 0;
}
