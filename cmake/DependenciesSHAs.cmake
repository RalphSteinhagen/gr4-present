# main (2026-11-13), as a full SHA: GitHub serves a commit by its full id only. Its own name: OpenDigitizer's dependency
# file force-sets GIT_SHA_GNURADIO4 to its pin, and would win otherwise.
set(GR4_PRESENT_GIT_REF_GNURADIO4 53796de6d16a2b6474895f6c637099744a3fdae5 CACHE STRING "" FORCE)

# main (2026-11-13) past the FM radio demo and the browser microphone pairs' alignment, on the GR4 pin above. A merged
# branch's head is unreachable once the branch is deleted, so the pin follows main.
set(GIT_REF_OPENDIGITIZER b44a9083fe483c6c03ddc0b7ba25a865831f9a3b CACHE STRING "" FORCE)

set(GIT_SHA_UT v2.3.1 CACHE STRING "" FORCE) # latest release as of 2024-04-03

# only used when GR4_PRESENT_WITH_OPENDIGITIZER=OFF; with it ON, OpenDigitizer's own pins supply ImGui and SDL3
set(GIT_TAG_IMGUI v1.92.6-docking CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_TAG_SDL3 release-3.2.16 CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_TAG_OGG v1.3.6 CACHE STRING "") # latest release as of 2026-09-26

set(GIT_TAG_LIBVPX v1.14.1 CACHE STRING "") # latest release as of 2026-09-29

set(GIT_TAG_LIBWEBM libwebm-1.0.0.31 CACHE STRING "") # latest release as of 2026-09-29

# SVG is a primary layout mechanism and Inkscape text must stay text, which rules out the single-header rasterisers
set(GIT_TAG_LUNASVG v3.5.0 CACHE STRING "") # latest release as of 2026-09-25

set(GIT_SHA_STB 8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55 CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

set(GIT_TAG_IMGUI_TEST_ENGINE v1.92.5 CACHE STRING "") # matches OpenDigitizer's pin as of 2026-09-12

# LaTeX: the openmath branch is the OpenType MATH rewrite and is newer than the Xrysnow fork. Compiled with
# GLYPH_RENDER_TYPE=1 it draws every glyph from outlines, so no typeface is loaded and only the .clm2 metrics vendored
# in assets/firamath are needed at runtime.
set(GIT_SHA_MICROTEX 086f4eb740270b28bd0c61a0a359aea9300d61ae CACHE STRING "") # openmath as of 2024-08-06

# WebP: the only still format the browsers all take that stb cannot decode; the demux half is what steps an animated
# file. AVIF is deliberately absent -- it needs an AV1 decoder, which is an order of magnitude more code.
set(GIT_TAG_LIBWEBP v1.6.0 CACHE STRING "") # latest release as of 2026-09-26

# Vorbis: the audio track of an Ogg Theora film is multiplexed into the same stream, so stb_vorbis cannot be used -- it
# consumes Ogg framing, whereas libogg hands us demuxed packets and vorbis_synthesis takes exactly those.
set(GIT_TAG_VORBIS v1.3.7 CACHE STRING "") # latest release as of 2026-09-26

# PDF export: ToUnicode maps for embedded TrueType, clipping, URI links, text annotations, fonts and output in memory
set(GIT_TAG_LIBHARU v2.4.6 CACHE STRING "") # latest release as of 2026-10-02
