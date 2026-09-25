# minimal Dear ImGui viewer shell, built from one source for native and Emscripten. It deliberately links only the
# presentation core and ImGui: the OpenDigitizer runtime is wired through gr4-present-viewer and joins the application
# when the live-region adapter lands. The target keeps its own name in every configuration: OpenDigitizer's src/ui also
# defines an executable called `index`, and CMake target names are global. Only the output file is called index, which
# is what Emscripten needs.
set(target_name "gr4-present")
if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/viewer-web)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake)
# the launch screen must draw before any resource loading works, so its pixels and glyphs live in the binary
gr4_present_embed_binaries(
  ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedLogos.hpp
  kLogoLightPng=${CMAKE_SOURCE_DIR}/assets/GSI_FAIR_Logo_light.png
  kLogoDarkPng=${CMAKE_SOURCE_DIR}/assets/GSI_FAIR_Logo_dark.png
  kRobotoMediumTtf=${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf)

set(GR4_PRESENT_APP_SOURCES
    FallbackScene.cpp
    FallbackScene.hpp
    Fonts.cpp
    Fonts.hpp
    LoadingScreen.cpp
    LoadingScreen.hpp
    Texture.cpp
    Texture.hpp
    ImGuiScoped.hpp
    StbImage.cpp
    SideMenu.cpp
    SideMenu.hpp
    ViewScene.cpp
    ViewScene.hpp
    Theme.cpp
    Theme.hpp
    WindowModeControl.cpp
    WindowModeControl.hpp)

add_executable(${target_name} main.cpp ${GR4_PRESENT_APP_SOURCES})

target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(
  ${target_name}
  PRIVATE gr4-present-core
          gr4-present-options
          imgui
          raii_wrapper
          stb)

if(EMSCRIPTEN)
  target_compile_options(${target_name} PRIVATE "SHELL:-s USE_SDL=3" -pthread)
  target_link_options(
    ${target_name}
    PRIVATE
    "SHELL:-s USE_SDL=3"
    "SHELL:-s WASM=1"
    "SHELL:-s FULL_ES3=1"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s NO_EXIT_RUNTIME=1"
    "SHELL:-s MINIFY_HTML=0"
    "-sFETCH=1" # gr::algorithm::fileio reads http(s) through emscripten_fetch
    "SHELL:-s PTHREAD_POOL_SIZE=${GR_MAX_WASM_THREAD_COUNT}"
    -pthread
    --shell-file
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
  set_target_properties(${target_name} PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html OUTPUT_NAME index)

  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/coi-serviceworker.js ${EXECUTABLE_OUTPUT_PATH}/coi-serviceworker.js
                 COPYONLY)

  add_custom_target(
    serve-viewer
    COMMAND python3 ${CMAKE_SOURCE_DIR}/devtools/serve.py --directory ${EXECUTABLE_OUTPUT_PATH}
    DEPENDS ${target_name}
    USES_TERMINAL
    COMMENT "serve the WASM viewer on http://localhost:8000 with COOP/COEP set")
else()
  target_link_libraries(${target_name} PRIVATE SDL3::SDL3)
endif()

# staged at the root of what is served, so the source layout does not leak into the addresses people type
if(EMSCRIPTEN)
  set(_presentation_destination ${EXECUTABLE_OUTPUT_PATH})
else()
  set(_presentation_destination $<TARGET_FILE_DIR:${target_name}>)
endif()
add_custom_command(
  TARGET ${target_name}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/presentations ${_presentation_destination}
  COMMENT "stage the presentation packages beside the viewer")

if(GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE)
  add_subdirectory(test)
endif()
