#include "Texture.hpp"

#include "EmbeddedLogos.hpp"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <stb_image.h>

#include <span>
#include <utility>

namespace gr::present {

Texture Texture::loadLogo(ColourScheme scheme) { return load(scheme == ColourScheme::dark ? std::span<const unsigned char>{kLogoDarkPng} : std::span<const unsigned char>{kLogoLightPng}); }

Texture Texture::load(std::span<const unsigned char> encoded) {
    int            width    = 0;
    int            height   = 0;
    int            channels = 0;
    unsigned char* pixels   = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return {};
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);

    Texture loaded;
    loaded.id     = static_cast<ImTextureID>(texture);
    loaded.width  = static_cast<float>(width);
    loaded.height = static_cast<float>(height);
    return loaded;
}

void Texture::swap(Texture& other) noexcept {
    std::swap(id, other.id);
    std::swap(width, other.width);
    std::swap(height, other.height);
}

void Texture::release() noexcept {
    if (id != 0) {
        const GLuint texture = static_cast<GLuint>(id);
        glDeleteTextures(1, &texture);
        id = 0;
    }
}

} // namespace gr::present
