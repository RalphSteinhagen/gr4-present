#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif

#include <boost/ut.hpp>

#include "EffectRenderer.hpp"

#include <gr4-present/EffectSource.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

// imgui_test_engine bundles its own stb_image_write with the same external symbols, so this copy stays internal
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <vector>

using namespace boost::ut;
using namespace gr::present;

namespace {

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
        window  = SDL_CreateWindow("qa_EffectRenderer", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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

using Files = std::map<std::string, std::vector<std::uint8_t>, std::less<>>;

[[nodiscard]] std::vector<std::uint8_t> onePixelPng(std::array<std::uint8_t, 4> rgba) {
    int                       length = 0;
    unsigned char*            png    = stbi_write_png_to_mem(rgba.data(), 4, 1, 1, 4, &length);
    std::vector<std::uint8_t> bytes(png, png + length);
    STBIW_FREE(png);
    return bytes;
}

/// what `effect` draws into a `width` × 1 texture, RGBA, read back
[[nodiscard]] std::vector<std::uint8_t> drawn(std::string_view effect, const Files& files, int width) {
    auto parsed = parseEffect("fx", effect);
    expect(fatal(parsed.has_value())) << (parsed ? std::string{} : parsed.error());
    EffectRenderer renderer{parsed->effect, [&files](std::string_view asset) {
                                const auto found = files.find(asset);
                                return found == files.end() ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>{found->second};
                            }};
    expect(fatal(renderer.ready())) << renderer.error();
    const GLuint texture     = renderer.drawToTexture(width, 1, EffectInputs{});
    GLuint       framebuffer = 0U;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * 4UZ);
    glReadPixels(0, 0, width, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glDeleteFramebuffers(1, &framebuffer);
    return pixels;
}

/// a 2 × 2 × 2 single-channel volume (FR-5): "BIN\0", uint32 x, y, z, uint8 channels, uint8 layout, uint16 format,
/// then the voxels with x running fastest, little-endian
template<typename T>
[[nodiscard]] std::vector<std::uint8_t> volume(std::uint16_t format, const std::array<T, 8>& voxels) {
    std::vector<std::uint8_t> bytes{'B', 'I', 'N', 0U};
    const auto                append = [&bytes](auto value) {
        const auto raw = std::bit_cast<std::array<std::uint8_t, sizeof(value)>>(value);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    };
    append(std::uint32_t{2U});
    append(std::uint32_t{2U});
    append(std::uint32_t{2U});
    append(std::uint8_t{1U});
    append(std::uint8_t{0U});
    append(format);
    for (const T voxel : voxels) {
        append(voxel);
    }
    return bytes;
}

// pixel i reads voxel (i & 1, i >> 1 & 1, i >> 2) at its centre
constexpr std::string_view kVolumeProbe = "// @channel0 cloud.bin filter=nearest\n"
                                          "void mainImage(out vec4 colour, in vec2 fragCoord) {\n"
                                          "    int i = int(fragCoord.x);\n"
                                          "    vec3 at = (vec3(float(i & 1), float((i >> 1) & 1), float(i >> 2)) + 0.5) / 2.0;\n"
                                          "    colour = vec4(texture(iChannel0, at).r, 0.0, 0.0, 1.0);\n"
                                          "}\n";

} // namespace

int main() {
    const Window window;
    expect(fatal(window.context != nullptr)) << "no GL 3.3 core context:" << SDL_GetError();

    // Ground truth: the GL cube-map face selection -- a direction along +x reads the +X face, and so on -- and the
    // six-file convention: the file named, then `_1` … `_5`, in the order +X, -X, +Y, -Y, +Z, -Z
    "a cubemap's six files are its faces in GL order, each read along its own axis"_test = [] {
        constexpr std::array<std::array<std::uint8_t, 4>, 6> kFaces{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}}};
        Files                                                files;
        for (std::size_t face = 0UZ; face < kFaces.size(); ++face) {
            files.emplace(cubemapFacePath("sky.png", face), onePixelPng(kFaces[face]));
        }
        const std::vector<std::uint8_t> pixels = drawn("// @channel0 sky.png type=cubemap\n"
                                                       "void mainImage(out vec4 colour, in vec2 fragCoord) {\n"
                                                       "    vec3 axes[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));\n"
                                                       "    colour = texture(iChannel0, axes[min(int(fragCoord.x), 5)]);\n"
                                                       "}\n",
            files, 6);
        for (std::size_t face = 0UZ; face < kFaces.size(); ++face) {
            const std::array<std::uint8_t, 3> got{pixels[face * 4UZ], pixels[face * 4UZ + 1UZ], pixels[face * 4UZ + 2UZ]};
            expect(got == std::array{kFaces[face][0], kFaces[face][1], kFaces[face][2]}) << std::format("face {}: {} {} {}", face, got[0], got[1], got[2]);
        }
    };

    // Ground truth: the voxels this test wrote
    "a byte volume reads back voxel for voxel, x fastest"_test = [] {
        constexpr std::array<std::uint8_t, 8> kVoxels{16, 48, 80, 112, 144, 176, 208, 240};
        const std::vector<std::uint8_t>       pixels = drawn(kVolumeProbe, Files{{"cloud.bin", volume<std::uint8_t>(0U, kVoxels)}}, 8);
        for (std::size_t voxel = 0UZ; voxel < kVoxels.size(); ++voxel) {
            expect(eq(static_cast<int>(pixels[voxel * 4UZ]), static_cast<int>(kVoxels[voxel]))) << std::format("voxel {}", voxel);
        }
    };

    "a float volume (format 10) reads back as its values"_test = [] {
        constexpr std::array<float, 8>  kVoxels{0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 1.0f};
        const std::vector<std::uint8_t> pixels = drawn(kVolumeProbe, Files{{"cloud.bin", volume<float>(10U, kVoxels)}}, 8);
        for (std::size_t voxel = 0UZ; voxel < kVoxels.size(); ++voxel) {
            expect(std::abs(static_cast<int>(pixels[voxel * 4UZ]) - static_cast<int>(std::lround(255.0f * kVoxels[voxel]))) <= 1) << std::format("voxel {}: {}", voxel, pixels[voxel * 4UZ]);
        }
    };

    // Ground truth: a channel whose asset cannot be read reads black, and the author is told which file of which
    // channel, rather than the effect failing as a whole
    "a volume whose header does not say BIN reads black and is reported with the file's name"_test = [] {
        auto           parsed = parseEffect("fx", kVolumeProbe);
        Files          files{{"cloud.bin", std::vector<std::uint8_t>(40UZ, 7U)}};
        EffectRenderer renderer{parsed->effect, [&files](std::string_view asset) { return std::span<const std::uint8_t>{files.at(std::string{asset})}; }};
        expect(fatal(renderer.ready())) << renderer.error();
        const GLuint texture     = renderer.drawToTexture(8, 1, EffectInputs{});
        GLuint       framebuffer = 0U;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        std::array<std::uint8_t, 4> first{};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, first.data());
        glDeleteFramebuffers(1, &framebuffer);
        expect(eq(static_cast<int>(first[0]), 0)) << "the channel reads black";
        const std::vector<std::string> problems = renderer.takeProblems();
        expect(eq(problems.size(), 1UZ) && problems.front().contains("cloud.bin")) << (problems.empty() ? std::string{"nothing reported"} : problems.front());
        expect(renderer.takeProblems().empty()) << "reported once";
    };
    // Ground truth: the line the author typed the mistake on, counted in the effect file (FR-9); the driver's message
    // is its own, so only the line number is held to, as the driver pairs it with the source string
    "a shader that does not compile says which pass and which line of the file"_test = [] {
        auto parsed = parseEffect("fx", "// @name fx\n\nvoid mainImage(out vec4 colour, in vec2 fragCoord) {\n    colour = vec4(undeclared);\n}\n");
        expect(fatal(parsed.has_value()));
        EffectRenderer renderer{parsed->effect, [](std::string_view) { return std::span<const std::uint8_t>{}; }};
        expect(!renderer.ready()) << "an undeclared name compiles";
        const std::string_view error = renderer.error();
        expect(error.contains("Image") && (error.contains("0:4") || error.contains("0(4)"))) << error; // Mesa and ANGLE write 0:4, NVIDIA 0(4)
    };
    return 0;
}
