# Build container for gr4-present, derived from the container gnuradio4, gr-digitizers and OpenDigitizer share.
#
# CI publishes this image and runs every job in it while USE_DERIVED_CONTAINER is "true" in .github/workflows/ci.yml;
# that flag is the only trigger, because a diff-based one reports every file as changed on a squashed history.
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

# the base ships Clang 20, whose libc++ predates C++23 library features used here; apt.llvm.org is the only Ubuntu
# questing source of a newer one
RUN apt-get update -y \
    && (apt-get install --no-install-recommends -qqy wget gnupg ca-certificates || true) \
    && wget -qO /usr/share/keyrings/llvm.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.asc] http://apt.llvm.org/questing/ llvm-toolchain-questing-22 main" \
       > /etc/apt/sources.list.d/llvm-22.list \
    && apt-get update -y \
    && (apt-get install --no-install-recommends -qqy clang-22 clang-tools-22 lld-22 libc++-22-dev libc++abi-22-dev libclang-rt-22-dev libomp-22-dev || true) \
    && dpkg-query -W -f='${Status}' clang-22 | grep -q "install ok installed" \
    && dpkg-query -W -f='${Status}' libc++-22-dev | grep -q "install ok installed" \
    && clang++-22 --version \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

USER user
