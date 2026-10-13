# One CI flavour's build image: the toolchain image (docker/Dockerfile) plus that flavour's prewarmed dependencies,
# made by devtools/ci/prewarm.sh at the paths the CI jobs use. Built by .github/workflows/ci.yml only when its inputs
# change; tagged <preset>-<hash of those inputs>.
ARG TOOLCHAIN
FROM ${TOOLCHAIN}

COPY --chown=user:user prewarm/ /opt/gr4-present/

# empty for a native flavour, which leaves Emscripten's default cache alone
ARG EM_CACHE=""
ENV EM_CACHE=${EM_CACHE}
