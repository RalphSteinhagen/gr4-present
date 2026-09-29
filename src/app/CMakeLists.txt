# minimal Dear ImGui viewer shell, built from one source for native and Emscripten. It deliberately links only the
# presentation core and ImGui. The OpenDigitizer runtime joins the application later, filling the region rectangles the
# layout already records when the live-region adapter lands. The target keeps its own name in every configuration:
# OpenDigitizer's src/ui also defines an executable called `index`, and CMake target names are global. Only the output
# file is called index, which is what Emscripten needs.
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
  kFontAwesomeSolidOtf=${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.subset.otf)

# the built-in faces, gzipped once by hand (assets/fonts/README.md) and inflated at start
gr4_present_embed_binaries(
  ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFonts.hpp
  kLiberationSansRegularTtfGz=${CMAKE_SOURCE_DIR}/assets/liberation/LiberationSans-Regular.ttf.gz
  kLiberationSansBoldTtfGz=${CMAKE_SOURCE_DIR}/assets/liberation/LiberationSans-Bold.ttf.gz
  kLiberationSansItalicTtfGz=${CMAKE_SOURCE_DIR}/assets/liberation/LiberationSans-Italic.ttf.gz
  kLiberationSansBoldItalicTtfGz=${CMAKE_SOURCE_DIR}/assets/liberation/LiberationSans-BoldItalic.ttf.gz
  kLiberationMonoRegularTtfGz=${CMAKE_SOURCE_DIR}/assets/liberation/LiberationMono-Regular.ttf.gz)

# the maths metrics and glyph outlines, so a formula needs no file at run time in either backend
gr4_present_embed_binaries(${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedMathFont.hpp
                           kFiraMathClm2=${CMAKE_SOURCE_DIR}/assets/firamath/FiraMath-Regular.clm2)

set(GR4_PRESENT_APP_SOURCES
    BuiltinFace.cpp
    BuiltinFace.hpp
    DiagnosticsPanel.cpp
    DiagnosticsPanel.hpp
    Animation.cpp
    Animation.hpp
    Canvas.cpp
    Canvas.hpp
    DocumentView.cpp
    DocumentView.hpp
    FigureCache.cpp
    FigureCache.hpp
    FallbackScene.cpp
    FallbackScene.hpp
    Fonts.cpp
    Fonts.hpp
    FormulaCache.cpp
    FormulaCache.hpp
    Grid.cpp
    Grid.hpp
    Layout.cpp
    Layout.hpp
    LoadingScreen.cpp
    LoadingScreen.hpp
    MathRender.cpp
    MathRender.hpp
    NotesOverlay.cpp
    NotesOverlay.hpp
    QrCache.cpp
    RasterCamera.cpp
    RasterCamera.hpp
    QrCode.cpp
    QrCode.hpp
    PlotView.cpp
    PlotView.hpp
    Texture.cpp
    Texture.hpp
    VideoStream.cpp
    VideoStream.hpp
    VideoCache.cpp
    VideoCache.hpp
    ImGuiScoped.hpp
    Morph.cpp
    Morph.hpp
    StbImage.cpp
    SvgImage.cpp
    SvgImage.hpp
    SwipeNavigation.cpp
    SwipeNavigation.hpp
    TextRuns.cpp
    TextRuns.hpp
    SideMenu.cpp
    ShaderTransition.cpp
    ShaderTransition.hpp
    SideMenu.hpp
    Theme.cpp
    Transition.cpp
    Transition.hpp
    Theme.hpp
    WindowModeControl.cpp
    WindowModeControl.hpp
    ZoomPan.cpp
    ZoomPan.hpp)
set(GR4_PRESENT_APP_SOURCES
    ${GR4_PRESENT_APP_SOURCES}
    PARENT_SCOPE) # test/app compiles them again, against the instrumented ImGui

add_library(qrcodegen STATIC ${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp)
target_include_directories(qrcodegen SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/qrcodegen)
target_compile_options(qrcodegen PRIVATE -w) # vendored upstream, not ours to make warning-clean

# LiveGraph is on the application rather than in GR4_PRESENT_APP_SOURCES on purpose: the test binaries compile that list
# and link the instrumented ImGui, and this translation unit reaches ImGui through ImPlot. Two of them in one binary is
# an ABI clash, not a link error, so it would fail at run time and in a way nobody would trace back here.
add_executable(
  ${target_name}
  main.cpp
  Viewer.hpp
  ViewerBrowser.cpp
  ViewerExport.cpp
  ViewerInput.cpp
  ViewerLive.cpp
  ViewerLoading.cpp
  ViewerPresenter.cpp
  ViewerSlides.cpp
  LiveGraph.cpp
  LiveGraph.hpp
  ${GR4_PRESENT_APP_SOURCES})

target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(
  ${target_name}
  PRIVATE gr4-present-core
          gr4-present-options
          gr4-present-recording
          lunasvg
          microtex
          plutovg::plutovg
          qrcodegen
          webpdecoder
          webpdemux
          vpxdec
          webmparser
          vorbisdec
          stb)

# Touch comes from OpenDigitizer's TouchHandler rather than a second implementation of finger tracking. `ui-common` is
# the layer that carries it and nothing of the dashboard: ImGui, ImPlot, SDL3 and the RAII wrapper, which is all the
# header needs. The hand-built include paths and object list this replaced were what a consumer had to do before
# OpenDigitizer published targets. ImGui and the RAII wrapper arrive through this layer rather than being named again
# here: ImGui is an object library, and naming it twice puts every object on the link line twice and fails with several
# hundred multiple definitions. Without OpenDigitizer the same two are taken directly.
if(GR4_PRESENT_WITH_OPENDIGITIZER AND GR4_PRESENT_ENABLE_EXPORT)
  # writing a PDF needs GR4's fileio to deliver it, so the export comes with the runtime
  target_compile_definitions(${target_name} PRIVATE GR4_PRESENT_HAS_EXPORT)
  target_link_libraries(${target_name} PRIVATE gr4-present-export)
endif()
if(GR4_PRESENT_WITH_OPENDIGITIZER)
  target_compile_definitions(${target_name} PRIVATE GR4_PRESENT_HAS_OPENDIGITIZER)
  # `charts` rather than `ui-common`: it is the layer that carries OpenDigitizer's own blocks, `ImPlotSink` among them,
  # and it brings `ui-common` with it, so TouchHandler still arrives. It also brings GNU Radio's block libraries, which
  # is what lets a workflow name `gr::basic::SignalGenerator` and get one.
  target_link_libraries(
    ${target_name}
    PRIVATE opendigitizer::flowgraph-editor
            opendigitizer::dashboard
            opendigitizer::charts
            gnuradio4::gnuradio-core
            gnuradio4::gr-audio
            gnuradio4::gr-fileio
            gnuradio4::gr-trigger)
else()
  target_link_libraries(${target_name} PRIVATE imgui raii_wrapper)
endif()

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
    # MicroTeX throws on a formula it cannot parse and the viewer catches it. Emscripten needs -fexceptions on the link
    # as well as the compile, or the catch is not generated and the throw takes the whole thread with it -- which is
    # what happened: one unsupported command froze the application on that slide.
    -fexceptions
    "SHELL:-s PTHREAD_POOL_SIZE=${GR_MAX_WASM_THREAD_COUNT}"
    -pthread
    --shell-file
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
  set_target_properties(${target_name} PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html OUTPUT_NAME index)

  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/coi-serviceworker.js ${EXECUTABLE_OUTPUT_PATH}/coi-serviceworker.js
                 COPYONLY)

  add_custom_target(
    serve-viewer
    COMMAND node ${CMAKE_SOURCE_DIR}/devtools/serve.mjs --directory ${EXECUTABLE_OUTPUT_PATH}
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
# Each package is removed before being copied, because copy_directory only adds and overwrites: a figure deleted from a
# package would otherwise go on being served. A target rather than a POST_BUILD step, because editing a presentation
# does not relink the viewer and the staged copy must follow the source regardless.
file(
  GLOB _presentation_packages
  RELATIVE ${CMAKE_SOURCE_DIR}/presentations
  ${CMAKE_SOURCE_DIR}/presentations/*)
set(_stage_commands)
foreach(_package IN LISTS _presentation_packages)
  if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/presentations/${_package})
    list(
      APPEND
      _stage_commands
      COMMAND
      ${CMAKE_COMMAND}
      -E
      rm
      -rf
      ${_presentation_destination}/${_package})
  endif()
endforeach()

add_custom_target(
  stage-presentations ALL
  ${_stage_commands}
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/presentations ${_presentation_destination}
  DEPENDS ${target_name}
  COMMENT "stage the presentation packages beside the viewer")
