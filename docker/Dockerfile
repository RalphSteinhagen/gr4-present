# syntax=docker/dockerfile:1
# Toolchain image for gr4-present, derived from the container gnuradio4, gr-digitizers and OpenDigitizer share: GR4's
# Ubuntu 26.04 image (GCC 15 and 16, Clang 21 and 22, Emscripten, the formatters' Python, npm and curl) plus
# gr-digitizers' PicoScope and timing libraries.
#
# CI tags it by the hash of this file and builds it only when no image of that tag exists; each CI flavour then runs in
# docker/dependencies.Dockerfile on top of it, with its dependencies prewarmed. The base's `latest` moves underneath,
# so each step verifies what it installs.
#
# test locally with: docker build -f docker/Dockerfile -t gr4-present-build-container:test docker/
FROM ghcr.io/fair-acc/gr-digitizers-build-container:latest

LABEL org.opencontainers.image.source=https://github.com/RalphSteinhagen/gr4-present
LABEL org.opencontainers.image.description="Build container for gr4-present"

USER root

# A PicoScope package in the base image has a failing postinst, so apt-get exits non-zero whatever it installed: every
# step below is verified with dpkg-query instead of by apt's exit status.

# the offscreen screenshot tests need a rasteriser; the base carries only the headers and link targets
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy libgl1-mesa-dri libegl-mesa0 || true) \
    && dpkg-query -W -f='${Status}' libgl1-mesa-dri | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# The native application tests drive the viewer under Xvfb with XTest (qa_NativeToolbar, qa_NativePresenter,
# qa_ExportPdf*), and qa_PdfWriter reads its pages back with poppler; without them CMake leaves those tests out. The
# probe opens the viewer's window under an Xvfb of the tests' depth through the SDL3 the base ships, asking for the
# context main.cpp asks for: the offscreen suites draw through EGL and pass on an X server whose GLX offers no visual.
# When it fails, glxinfo lists what the server offers.
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy xvfb libxtst-dev poppler-utils mesa-utils || true) \
    && dpkg-query -W -f='${Status}' xvfb | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libxtst-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' poppler-utils | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' mesa-utils | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
RUN <<'PROBE'
set -eu
cat > /tmp/probe-window.cpp <<'CPP'
#include <SDL3/SDL.h>
#include <cstdio>
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow("probe", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_GL_CreateContext(window) == nullptr) {
        std::printf("SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    using GetString         = const unsigned char* (*)(unsigned int);
    const auto  glGetString = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    const auto* renderer    = glGetString(0x1F01); // GL_RENDERER
    const auto* version     = glGetString(0x1F02); // GL_VERSION
    std::printf("%s, OpenGL %s\n", renderer, version);
    return 0;
}
CPP
g++-15 -std=c++23 $(pkg-config --cflags sdl3) -o /tmp/probe-window /tmp/probe-window.cpp $(pkg-config --libs sdl3)
Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp &
for _ in $(seq 1 50); do [ -S /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
export DISPLAY=:99 SDL_VIDEO_DRIVER=x11
if ! /tmp/probe-window; then
    glxinfo || true
    exit 1
fi
glxinfo -B
kill $!
rm -f /tmp/probe-window /tmp/probe-window.cpp
PROBE

# ./formatFiles.sh --install needs a Python venv, npm and curl
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy python3-venv python3-pip npm curl || true) \
    && dpkg-query -W -f='${Status}' python3-venv | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' python3-pip | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' npm | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' curl | grep -q "install ok installed" \
    && python3 -m venv /tmp/venv-check && rm -rf /tmp/venv-check \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# The base ships libc++ 21 only, and Ubuntu's libc++ versions exclude each other; the Clang lanes and libclang 22 need
# 22's. The probe compiles C++23 with both toolchains.
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy libc++-22-dev libc++abi-22-dev libomp-22-dev || true) \
    && dpkg-query -W -f='${Status}' clang-22 | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' clang-tidy-22 | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libclang-22-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' lld-22 | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libc++-22-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libc++abi-22-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' g++-15 | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
RUN <<'PROBE'
set -eu
cat > /tmp/probe.cpp <<'CPP'
#include <print>
#include <ranges>
#include <string>
#include <vector>
int main() {
    const std::vector<std::string> words{"gr4", "present"};
    std::println("{}", words | std::views::join_with('-') | std::ranges::to<std::string>());
}
CPP
clang++-22 -std=c++23 -stdlib=libc++ -o /tmp/probe-clang /tmp/probe.cpp && /tmp/probe-clang
g++-15 -std=c++23 -o /tmp/probe-gcc /tmp/probe.cpp && /tmp/probe-gcc
rm -f /tmp/probe.cpp /tmp/probe-clang /tmp/probe-gcc
PROBE

# callgrind counts the hot paths' instructions where a hosted runner grants no hardware counters to perf
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy valgrind || true) \
    && dpkg-query -W -f='${Status}' valgrind | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# CI reads the prewarmed dependencies through ccache's remote storage, which needs ccache 4.8 or later
RUN ccache --version | head -n 1

USER user
