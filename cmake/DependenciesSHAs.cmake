# With GR4_PRESENT_WITH_OPENDIGITIZER=ON, OpenDigitizer's own DependenciesSHAs.cmake is included later and re-forces
# GIT_SHA_GNURADIO4 and GIT_SHA_UT, so OpenDigitizer owns both pins -- its src/ui is written against a specific GR4
# revision and skewing it is not worth the drift. The gnuradio4 value below therefore only takes effect in the
# -DGR4_PRESENT_WITH_OPENDIGITIZER=OFF build.
set(GIT_SHA_GNURADIO4
    5a38e0605af4cb6ea86a494e1cb5a71987e995c6
    CACHE STRING "" FORCE) # origin/main as of 2026-09-24

set(GIT_SHA_OPENDIGITIZER
    99832feb62c89a092d3ea343d1828b9fd90a09ae
    CACHE STRING "" FORCE) # origin/main as of 2026-09-12

set(GIT_SHA_UT
    v2.3.1
    CACHE STRING "" FORCE) # latest release as of 2024-04-03

# only used when GR4_PRESENT_WITH_OPENDIGITIZER=OFF; with it ON, OpenDigitizer's own pins supply ImGui and SDL3
set(GIT_TAG_IMGUI
    v1.92.6-docking
    CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_TAG_SDL3
    release-3.2.16
    CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_SHA_STB
    8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55
    CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_TAG_IMGUI_TEST_ENGINE
    v1.92.5
    CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12
