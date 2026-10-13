# Toolchain image for gr4-present, derived from the container gnuradio4, gr-digitizers and OpenDigitizer share.
#
# CI tags it by the hash of this file and builds it only when no image of that tag exists; each CI flavour then runs in
# docker/dependencies.Dockerfile on top of it, with its dependencies prewarmed.
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

# the native application tests drive the viewer under Xvfb with XTest (qa_NativeToolbar, qa_NativePresenter,
# qa_ExportPdf*), and qa_PdfWriter reads its pages back with poppler; without them CMake leaves those tests out
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy xvfb libxtst-dev poppler-utils || true) \
    && dpkg-query -W -f='${Status}' xvfb | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libxtst-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' poppler-utils | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# the base ships Clang 20, whose libc++ predates C++23 library features used here; apt.llvm.org is the only Ubuntu
# questing source of a newer one
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy wget gnupg ca-certificates || true) \
    && wget -qO /usr/share/keyrings/llvm.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.asc] http://apt.llvm.org/questing/ llvm-toolchain-questing-22 main" \
       > /etc/apt/sources.list.d/llvm-22.list \
    && apt-get update -y \
    && (apt-get install --no-install-recommends -qqy clang-22 clang-tools-22 clang-tidy-22 libclang-22-dev lld-22 libc++-22-dev libc++abi-22-dev libclang-rt-22-dev libomp-22-dev llvm-22 || true) \
    && dpkg-query -W -f='${Status}' clang-22 | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libc++-22-dev | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' llvm-22 | grep -q "install ok installed" \
    && clang++-22 --version \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# callgrind counts the hot paths' instructions where a hosted runner grants no hardware counters to perf
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy valgrind || true) \
    && dpkg-query -W -f='${Status}' valgrind | grep -q "install ok installed" \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# CI reads the prewarmed dependencies through ccache's remote storage, which needs ccache 4.8 or later
RUN ccache --version | head -n 1

USER user
