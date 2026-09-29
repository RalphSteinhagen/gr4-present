include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/DependenciesSHAs.cmake)

# `source_subdirectory` empty takes the whole project, which its root now supports; naming one takes only that part.
# SOURCE_SUBDIR cannot simply be given an empty value -- the keyword would swallow whatever follows it.
function(gr4_present_fetch_opendigitizer source_subdirectory)
  # following a branch means re-checking it on every configure; pinned dependencies do not need this and are not given
  # it, because refetching what cannot have moved is only slower
  set(FETCHCONTENT_UPDATES_DISCONNECTED_OPENDIGITIZER OFF)
  if(source_subdirectory STREQUAL "")
    FetchContent_Declare(
      opendigitizer
      GIT_REPOSITORY https://github.com/fair-acc/opendigitizer.git
      GIT_TAG ${GIT_REF_OPENDIGITIZER}
      SYSTEM EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(
      opendigitizer
      GIT_REPOSITORY https://github.com/fair-acc/opendigitizer.git
      GIT_TAG ${GIT_REF_OPENDIGITIZER}
      SOURCE_SUBDIR
      ${source_subdirectory}
      SYSTEM
      EXCLUDE_FROM_ALL)
  endif()
  FetchContent_MakeAvailable(opendigitizer)
  set(opendigitizer_SOURCE_DIR
      "${opendigitizer_SOURCE_DIR}"
      PARENT_SCOPE)
endfunction()

# a branch rather than a commit, so every configure checks for its newest commit, as OpenDigitizer's does
set(FETCHCONTENT_UPDATES_DISCONNECTED_GNURADIO4 OFF)

# declared before OpenDigitizer is fetched: the first declaration of a name wins, so this pin holds over OpenDigitizer's
FetchContent_Declare(
  gnuradio4
  GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
  GIT_TAG ${GR4_PRESENT_GIT_REF_GNURADIO4}
  OVERRIDE_FIND_PACKAGE SYSTEM EXCLUDE_FROM_ALL)

# OpenDigitizer publishes no CMake package, so it is consumed as a source dependency. src/utils is added in every
# configuration for `raii_wrapper` (stdex::c_resource), which wraps the C APIs this project uses; src/ui, which carries
# the dashboard and ImPlot code and drags in imgui, implot, imgui-node-editor, opencmw-cpp and SDL3, is added only when
# the live-region viewer is wanted. src/ui re-adds src/utils itself unless raii_wrapper already exists.
if(GR4_PRESENT_WITH_OPENDIGITIZER)
  set(OPENDIGITIZER_ENABLE_TESTING
      OFF
      CACHE BOOL "Build the OpenDigitizer unit tests" FORCE)
  set(OPENDIGITIZER_WARNINGS_AS_ERRORS
      OFF
      CACHE BOOL "Treat OpenDigitizer compiler warnings as errors" FORCE)

  # OpenDigitizer's root is consumable now: taken whole, it skips its own tests, service, application and the WASM
  # sub-build because it is not the top-level project, and publishes its user interface as layered targets. Adding
  # src/ui by hand was only ever what worked while that was not true.
  gr4_present_fetch_opendigitizer("")

else()
  gr4_present_fetch_opendigitizer(src/utils)

  find_package(gnuradio4 4.0.0 QUIET)
  if(NOT gnuradio4_FOUND)
    message(STATUS "Pre-built gnuradio4 not found, fetching and building from source...")
    FetchContent_MakeAvailable(gnuradio4)
  endif()
endif()

# gnuradio4 only defines `ut` when built as the top-level project, and OpenDigitizer declares it in its root CMakeLists
# rather than in src/ui.
if(GR4_PRESENT_ENABLE_TESTING AND NOT TARGET ut)
  FetchContent_Declare(
    ut
    GIT_REPOSITORY https://github.com/boost-ext/ut.git
    GIT_TAG ${GIT_SHA_UT}
    SYSTEM EXCLUDE_FROM_ALL)
  set(BOOST_UT_DISABLE_MODULE
      ON
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ut)
endif()

# ut.hpp is third-party and cannot be fixed here, so it is consumed as SYSTEM regardless -- a dependency must not break
# the -Werror build.
if(TARGET ut)
  get_target_property(_ut_includes ut INTERFACE_INCLUDE_DIRECTORIES)
  if(_ut_includes)
    set_target_properties(ut PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ut_includes}")
  endif()
endif()

# Boost.UT's interface asks for -fwasm-exceptions, but gnuradio4 compiles with -fexceptions (the JS exception model,
# which Asyncify needs). Mixing the two models leaves __cxa_find_matching_catch_3 undefined in any test that reaches a
# gnuradio4 catch. gnuradio4 applies the same correction when it owns the ut target; it must be repeated here because we
# fetch ut ourselves.
if(EMSCRIPTEN AND TARGET ut)
  foreach(_property IN ITEMS INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
    get_target_property(_ut_options ut ${_property})
    if(_ut_options)
      list(REMOVE_ITEM _ut_options -fwasm-exceptions)
      list(APPEND _ut_options -fexceptions)
      set_target_properties(ut PROPERTIES ${_property} "${_ut_options}")
    endif()
  endforeach()
endif()

# The viewer shell needs ImGui and SDL3. With OpenDigitizer enabled both already exist as targets of its src/ui, and
# reusing them keeps one ImGui in the build; without it they are declared here so that the application does not depend
# on OpenDigitizer being enabled. Under Emscripten SDL3 comes from the emscripten ports (-s USE_SDL=3).
if(NOT TARGET imgui)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${GIT_TAG_IMGUI}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(imgui)

  if(NOT EMSCRIPTEN)
    find_package(SDL3 QUIET)
    if(NOT SDL3_FOUND)
      set(SDL3_DISABLE_SDL3MAIN
          ON
          CACHE BOOL "" FORCE)
      set(SDL_TEST
          OFF
          CACHE BOOL "" FORCE)
      FetchContent_Declare(
        sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${GIT_TAG_SDL3}
        OVERRIDE_FIND_PACKAGE SYSTEM EXCLUDE_FROM_ALL)
      FetchContent_MakeAvailable(sdl3)
    endif()
    find_package(SDL3 REQUIRED)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL)
  endif()

  # imgui ships no CMake project of its own
  add_library(
    imgui OBJECT
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
  target_include_directories(imgui SYSTEM BEFORE PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(imgui PUBLIC "ImDrawIdx=unsigned int" IMGUI_USE_WCHAR32 IMGUI_DEFINE_MATH_OPERATORS)
  if(EMSCRIPTEN)
    target_compile_options(imgui PUBLIC "SHELL:-s USE_SDL=3" -pthread)
  else()
    target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)
  endif()
endif()

# With OpenDigitizer enabled, imgui is populated inside its subdirectory, so its path is only reachable through
# FetchContent's global properties rather than the variable MakeAvailable set in that scope.
if(NOT imgui_SOURCE_DIR)
  FetchContent_GetProperties(imgui SOURCE_DIR imgui_SOURCE_DIR)
endif()

# stb_image decodes the embedded splash textures. OpenDigitizer already provides the target when it is enabled.
if(NOT TARGET stb)
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG ${GIT_SHA_STB}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(stb)
  add_library(stb INTERFACE)
  target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif()

# Screenshot and interaction tests need an ImGui instrumented with imgui_test_engine. That instrumentation changes
# ImGui's ABI, so it cannot be the same target the application links; and OpenDigitizer's own switch for it insists on
# OPENDIGITIZER_ENABLE_TESTING, which would build its whole suite into ours. A separate target avoids both: the sources
# under test are compiled into each test binary rather than shared through a library.
if(GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE)
  # find_package imports targets into the directory that called it, and OpenDigitizer called it in its own
  if(NOT TARGET OpenGL::GL)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL)
  endif()
  if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 REQUIRED)
  endif()

  FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG ${GIT_TAG_IMGUI_TEST_ENGINE}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(imgui_test_engine)

  add_library(
    imgui-instrumented OBJECT
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp)
  target_include_directories(imgui-instrumented SYSTEM BEFORE PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                                                     ${imgui_test_engine_SOURCE_DIR})
  target_compile_definitions(
    imgui-instrumented
    PUBLIC IMGUI_ENABLE_TEST_ENGINE
           IMGUI_TEST_ENGINE_ENABLE_CAPTURE
           IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
           "ImDrawIdx=unsigned int"
           IMGUI_USE_WCHAR32
           IMGUI_DEFINE_MATH_OPERATORS)
  target_compile_options(imgui-instrumented PRIVATE -Wno-old-style-cast -Wno-double-promotion
                                                    -Wno-deprecated-enum-enum-conversion)
  target_link_libraries(imgui-instrumented PUBLIC SDL3::SDL3 OpenGL::GL)
endif()

if(NOT TARGET lunasvg)
  set(LUNASVG_BUILD_EXAMPLES
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_Declare(
    lunasvg
    GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
    GIT_TAG ${GIT_TAG_LUNASVG}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(lunasvg)

  # every translation unit in a shared-memory build must agree about atomics, and lunasvg does not know it is being
  # linked into one; wasm-ld rejects the whole link otherwise
  if(EMSCRIPTEN)
    foreach(_svg_target IN ITEMS lunasvg plutovg)
      if(TARGET ${_svg_target})
        target_compile_options(${_svg_target} PRIVATE -pthread)
      endif()
    endforeach()
  endif()
endif()

# WebM: VP8 video and Vorbis audio in a Matroska container, which is what browsers decode natively. libogg ships CMake
# and is still required -- Vorbis packets are framed as `ogg_packet` whatever container carried them. libvpx ships
# autotools only, and libwebm's parser is two files, so both are given targets here.
if(NOT TARGET vpxdec)
  set(BUILD_TESTING
      OFF
      CACHE BOOL "" FORCE)
  set(INSTALL_DOCS
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_Declare(
    ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG ${GIT_TAG_OGG}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(ogg)

  # SOURCE_SUBDIR names a directory with no CMakeLists.txt, so the content is fetched but never added as a subproject:
  # libwebm's own build produces tools and tests this project has no use for, and only the parser is wanted
  FetchContent_Declare(
    libwebm
    GIT_REPOSITORY https://github.com/webmproject/libwebm.git
    GIT_TAG ${GIT_TAG_LIBWEBM}
    SOURCE_SUBDIR
    no-cmake-project-here
    SYSTEM
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(libwebm)

  # mkvparser alone: the reader that comes beside it reads through a FILE*, and a package's bytes are already in memory
  # by the time a video is played, so VideoStream implements mkvparser::IMkvReader over a span instead.
  add_library(webmparser STATIC ${libwebm_SOURCE_DIR}/mkvparser/mkvparser.cc)
  target_include_directories(webmparser SYSTEM PUBLIC ${libwebm_SOURCE_DIR})
  # upstream C++ that we do not maintain: its warnings are not ours to fix, and -Werror would stop the build dead
  target_compile_options(webmparser PRIVATE -w)
  if(EMSCRIPTEN)
    target_compile_options(webmparser PRIVATE -pthread)
  endif()

  # libvpx configures and builds with autotools, so it is built as an external project and imported. `generic-gnu` is
  # the pure-C target: it needs no yasm and no per-architecture assembly, which is what lets one recipe serve both the
  # native and the Emscripten build. The decoder is 312 kB native and 214 kB of wasm, measured.
  include(ExternalProject)
  # an empty generator expression is still passed as an empty argument, which configure rejects outright
  set(_vpx_extra_flags "")
  if(EMSCRIPTEN)
    set(_vpx_extra_flags --extra-cflags=-pthread)
  endif()
  set(_vpx_install ${CMAKE_BINARY_DIR}/_deps/libvpx-install)
  set(_vpx_library ${_vpx_install}/lib/libvpx.a)
  ExternalProject_Add(
    libvpx_external
    GIT_REPOSITORY https://chromium.googlesource.com/webm/libvpx
    GIT_TAG ${GIT_TAG_LIBVPX}
    GIT_SHALLOW TRUE
    UPDATE_DISCONNECTED TRUE
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}" "AR=${CMAKE_AR}"
      "RANLIB=${CMAKE_RANLIB}" "LD=${CMAKE_C_COMPILER}" <SOURCE_DIR>/configure --target=generic-gnu
      --prefix=${_vpx_install} --enable-vp8-decoder --enable-static --enable-pic --disable-shared --disable-vp8-encoder
      --disable-vp9 --disable-examples --disable-tools --disable-docs --disable-unit-tests --disable-webm-io
      --disable-libyuv --disable-runtime-cpu-detect --disable-multithread ${_vpx_extra_flags}
    BUILD_COMMAND ${CMAKE_COMMAND} -E env make -j2
    INSTALL_COMMAND ${CMAKE_COMMAND} -E env make install
    BUILD_BYPRODUCTS ${_vpx_library})

  add_library(
    vpxdec
    STATIC
    IMPORTED
    GLOBAL)
  add_dependencies(vpxdec libvpx_external)
  # the include directory must exist at configure time or INTERFACE_INCLUDE_DIRECTORIES is rejected
  file(MAKE_DIRECTORY ${_vpx_install}/include)
  set_target_properties(vpxdec PROPERTIES IMPORTED_LOCATION ${_vpx_library} INTERFACE_INCLUDE_DIRECTORIES
                                                                            ${_vpx_install}/include)
endif()

# zeromq's polling_util.hpp uses std::nothrow without including <new>; libstdc++ and libc++ 20 pull it in transitively,
# libc++ 22 does not. The target is opencmw's, fetched two levels down, so the include is forced on the command line.
foreach(_zeromq_target IN ITEMS objects libzmq libzmq-static)
  if(TARGET ${_zeromq_target} AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${_zeromq_target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include;new>)
  endif()
endforeach()

if(NOT TARGET microtex)
  set(BUILD_STATIC
      ON
      CACHE BOOL "" FORCE)
  set(HAVE_LOG
      OFF
      CACHE BOOL "" FORCE)
  # 1 is GLYPH_RENDER_TYPE_PATH: glyphs arrive as outlines, never as font ids, so no typeface is ever loaded and plutovg
  # -- already here for lunasvg -- can fill them with their holes intact, which ImDrawList cannot
  set(GLYPH_RENDER_TYPE
      1
      CACHE STRING "" FORCE)
  FetchContent_Declare(
    microtex
    GIT_REPOSITORY https://github.com/NanoMichael/MicroTeX.git
    GIT_TAG ${GIT_SHA_MICROTEX}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(microtex)
  # upstream is not warning-clean and is not ours to fix; -Werror would stop the build on code we do not maintain
  target_compile_options(microtex PRIVATE -w)
  target_include_directories(microtex SYSTEM PUBLIC $<BUILD_INTERFACE:${microtex_SOURCE_DIR}/lib>)
  if(EMSCRIPTEN)
    target_compile_options(microtex PRIVATE -pthread)
  endif()
endif()

if(NOT TARGET webp)
  foreach(
    _webp_tool IN
    ITEMS WEBP_BUILD_ANIM_UTILS
          WEBP_BUILD_CWEBP
          WEBP_BUILD_DWEBP
          WEBP_BUILD_GIF2WEBP
          WEBP_BUILD_IMG2WEBP
          WEBP_BUILD_VWEBP
          WEBP_BUILD_WEBPINFO
          WEBP_BUILD_LIBWEBPMUX
          WEBP_BUILD_WEBPMUX
          WEBP_BUILD_EXTRAS
          WEBP_BUILD_FUZZTEST)
    set(${_webp_tool}
        OFF
        CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG ${GIT_TAG_LIBWEBP}
    SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(libwebp)
  if(EMSCRIPTEN)
    # the same trap lunasvg and libvpx sprang: a dependency compiled without -pthread cannot link against a target that
    # was
    foreach(
      _webp_target IN
      ITEMS webp
            webpdecoder
            webpdemux
            sharpyuv)
      if(TARGET ${_webp_target})
        target_compile_options(${_webp_target} PRIVATE -pthread)
      endif()
    endforeach()
  endif()
endif()

if(NOT TARGET vorbisdec)
  # libvorbis still declares cmake_minimum_required(VERSION 2.8), which CMake 4 refuses, and overriding the policy would
  # change how every other subproject is configured. Its decoder sources are compiled here instead, exactly as libvpx's
  # are; vorbisenc.c and the standalone tools are left out, so no encoder is built.
  FetchContent_Declare(
    vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG ${GIT_TAG_VORBIS}
    SOURCE_SUBDIR
    no-cmake-project-here
    SYSTEM
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(vorbis)

  add_library(
    vorbisdec STATIC
    ${vorbis_SOURCE_DIR}/lib/bitrate.c
    ${vorbis_SOURCE_DIR}/lib/block.c
    ${vorbis_SOURCE_DIR}/lib/codebook.c
    ${vorbis_SOURCE_DIR}/lib/envelope.c
    ${vorbis_SOURCE_DIR}/lib/floor0.c
    ${vorbis_SOURCE_DIR}/lib/floor1.c
    ${vorbis_SOURCE_DIR}/lib/info.c
    ${vorbis_SOURCE_DIR}/lib/lookup.c
    ${vorbis_SOURCE_DIR}/lib/lpc.c
    ${vorbis_SOURCE_DIR}/lib/lsp.c
    ${vorbis_SOURCE_DIR}/lib/mapping0.c
    ${vorbis_SOURCE_DIR}/lib/mdct.c
    ${vorbis_SOURCE_DIR}/lib/psy.c
    ${vorbis_SOURCE_DIR}/lib/registry.c
    ${vorbis_SOURCE_DIR}/lib/res0.c
    ${vorbis_SOURCE_DIR}/lib/sharedbook.c
    ${vorbis_SOURCE_DIR}/lib/smallft.c
    ${vorbis_SOURCE_DIR}/lib/synthesis.c
    ${vorbis_SOURCE_DIR}/lib/window.c)
  target_include_directories(vorbisdec SYSTEM PUBLIC $<BUILD_INTERFACE:${vorbis_SOURCE_DIR}/include>)
  target_include_directories(vorbisdec PRIVATE ${vorbis_SOURCE_DIR}/lib)
  target_link_libraries(vorbisdec PUBLIC ogg)
  target_compile_options(vorbisdec PRIVATE -w)
  if(EMSCRIPTEN)
    target_compile_options(vorbisdec PRIVATE -pthread)
  endif()
endif()

if(GR4_PRESENT_ENABLE_EXPORT AND NOT TARGET hpdf)
  # static, and without libpng: pages embed JPEG and raw pixels, never PNG files
  set(_shared_before ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_DISABLE_FIND_PACKAGE_PNG ON)
  if(EMSCRIPTEN)
    # Emscripten's zlib is a port: ask for it, then point libharu's find_package(ZLIB) at what the port installed
    execute_process(COMMAND embuilder build zlib OUTPUT_QUIET)
    set(ZLIB_INCLUDE_DIR ${EMSCRIPTEN_SYSROOT}/include)
    set(ZLIB_LIBRARY ${EMSCRIPTEN_SYSROOT}/lib/wasm32-emscripten/libz.a)
  endif()
  FetchContent_Declare(
    libharu
    GIT_REPOSITORY https://github.com/libharu/libharu.git
    GIT_TAG ${GIT_TAG_LIBHARU}
    # two fixes, both to be offered upstream: the ToUnicode map for UTF-8 TrueType used `cidrange`, which readers reject
    # or warn about; and a 128 kB map kept on the stack overflowed the browser's, which hung the export
    PATCH_COMMAND
      ${CMAKE_COMMAND} -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/libharu-tounicode-bfrange.patch -P
      ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake COMMAND ${CMAKE_COMMAND}
      -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/libharu-cid-map-off-stack.patch -P
      ${CMAKE_SOURCE_DIR}/cmake/ApplyPatch.cmake SYSTEM EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(libharu)
  set(BUILD_SHARED_LIBS ${_shared_before})
  target_compile_options(hpdf PRIVATE -w)
  target_include_directories(hpdf SYSTEM INTERFACE $<BUILD_INTERFACE:${libharu_SOURCE_DIR}/include>
                                                   $<BUILD_INTERFACE:${libharu_BINARY_DIR}/include>)
  if(EMSCRIPTEN)
    target_compile_options(hpdf PRIVATE -pthread)
  endif()
endif()
