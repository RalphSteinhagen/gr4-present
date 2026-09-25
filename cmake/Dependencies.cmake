include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/DependenciesSHAs.cmake)

function(gr4_present_fetch_opendigitizer source_subdirectory)
  FetchContent_Declare(
    opendigitizer
    GIT_REPOSITORY https://github.com/fair-acc/opendigitizer.git
    GIT_TAG ${GIT_SHA_OPENDIGITIZER}
    SOURCE_SUBDIR
    ${source_subdirectory}
    SYSTEM
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(opendigitizer)
  set(opendigitizer_SOURCE_DIR
      "${opendigitizer_SOURCE_DIR}"
      PARENT_SCOPE)
endfunction()

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

  # src/ui resolves `DependenciesSHAs` and `CompileGr4Release` through CMAKE_MODULE_PATH; the entry it appends itself
  # (${CMAKE_SOURCE_DIR}/../../cmake) is relative to the consuming project and does not find them from here. The path is
  # added before FetchContent_MakeAvailable() because that call runs src/ui's add_subdirectory() internally.
  if(FETCHCONTENT_SOURCE_DIR_OPENDIGITIZER)
    list(APPEND CMAKE_MODULE_PATH "${FETCHCONTENT_SOURCE_DIR_OPENDIGITIZER}/cmake")
  else()
    if(NOT FETCHCONTENT_BASE_DIR)
      set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
    endif()
    list(APPEND CMAKE_MODULE_PATH "${FETCHCONTENT_BASE_DIR}/opendigitizer-src/cmake")
  endif()

  gr4_present_fetch_opendigitizer(src/utils)
  add_subdirectory(${opendigitizer_SOURCE_DIR}/src/ui ${CMAKE_BINARY_DIR}/_deps/opendigitizer-ui-build EXCLUDE_FROM_ALL)

  # OpenDigitizer applies ImGui's ABI-affecting macros with add_compile_definitions() inside src/ui, so they reach its
  # own translation units but not a consumer outside that directory. ImGui's headers then disagree with the compiled
  # library about ImDrawIdx and every consumer aborts in DebugCheckVersionAndDataLayout() at start-up. Carrying them on
  # the target makes the setting travel with the dependency.
  if(TARGET imgui)
    target_compile_definitions(imgui PUBLIC "ImDrawIdx=unsigned int" IMGUI_USE_WCHAR32 IMGUI_DEFINE_MATH_OPERATORS)
  endif()
else()
  gr4_present_fetch_opendigitizer(src/utils)

  find_package(gnuradio4 4.0.0 QUIET)
  if(NOT gnuradio4_FOUND)
    message(STATUS "Pre-built gnuradio4 not found, fetching and building from source...")
    FetchContent_Declare(
      gnuradio4
      GIT_REPOSITORY https://github.com/fair-acc/gnuradio4.git
      GIT_TAG ${GIT_SHA_GNURADIO4}
      OVERRIDE_FIND_PACKAGE SYSTEM EXCLUDE_FROM_ALL)
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

# zeromq's polling_util.hpp uses std::nothrow without including <new>; libstdc++ and libc++ 20 pull it in transitively,
# libc++ 22 does not. The target is opencmw's, fetched two levels down, so the include is forced on the command line.
foreach(_zeromq_target IN ITEMS objects libzmq libzmq-static)
  if(TARGET ${_zeromq_target} AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${_zeromq_target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include;new>)
  endif()
endforeach()
