set(_app_source_dir ${PROJECT_SOURCE_DIR}/src/app) # the sources under test
set(_app_generated_dir ${PROJECT_BINARY_DIR}/src/app/generated) # the headers the application's build embeds

# The sources under test are compiled into each test rather than linked from the application: the test binaries use the
# instrumented ImGui, whose ABI differs from the one the application links.
add_library(
  gr4-present-test-support STATIC
  ImGuiTestHarness.cpp
  ImGuiTestHarness.hpp
  ScreenshotDiff.cpp
  ScreenshotDiff.hpp
  ${_app_source_dir}/StbImage.cpp) # the stb decoder lives here so each test binary carries exactly one copy
target_include_directories(gr4-present-test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${_app_source_dir})
target_compile_definitions(
  gr4-present-test-support PUBLIC GR4_PRESENT_BUILD_DIRECTORY="${CMAKE_BINARY_DIR}"
                                  GR4_PRESENT_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/reference")
target_link_libraries(
  gr4-present-test-support
  PRIVATE gr4-present-options
  PUBLIC imgui-instrumented
         lunasvg
         microtex
         plutovg::plutovg
         qrcodegen
         webpdecoder
         webpdemux
         vpxdec
         webmparser
         vorbisdec
         raii_wrapper
         $<TARGET_OBJECTS:imgui-instrumented>
         stb
         SDL3::SDL3)

add_executable(
  qa_SvgImage
  qa_SvgImage.cpp
  ${_app_source_dir}/SvgImage.cpp
  ${_app_source_dir}/BuiltinFace.cpp
  ${_app_source_dir}/Layout.cpp)
target_include_directories(qa_SvgImage PRIVATE ${_app_source_dir} ${_app_generated_dir})
target_compile_definitions(
  qa_SvgImage PRIVATE GR4_PRESENT_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets"
                      GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_SvgImage
  PRIVATE ut
          gnuradio4::gnuradio-core
          lunasvg
          plutovg::plutovg
          gr4-present-options)
add_test(NAME qa_SvgImage COMMAND qa_SvgImage)

add_executable(qa_VideoStream qa_VideoStream.cpp ${_app_source_dir}/VideoStream.cpp)
target_include_directories(qa_VideoStream PRIVATE ${_app_source_dir})
target_compile_definitions(qa_VideoStream
                           PRIVATE GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_VideoStream
  PRIVATE ut
          gr4-present-test-support
          vpxdec
          webmparser
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_VideoStream COMMAND qa_VideoStream)

add_executable(qa_Animation qa_Animation.cpp ${_app_source_dir}/Animation.cpp ${_app_source_dir}/StbImage.cpp)
target_include_directories(qa_Animation PRIVATE ${_app_source_dir})
target_compile_definitions(qa_Animation
                           PRIVATE GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_Animation
  PRIVATE ut
          webpdecoder
          webpdemux
          stb
          imgui
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_Animation COMMAND qa_Animation)

add_executable(qa_QrCode qa_QrCode.cpp ${_app_source_dir}/QrCode.cpp)
target_include_directories(qa_QrCode PRIVATE ${_app_source_dir})
target_link_libraries(
  qa_QrCode
  PRIVATE ut
          qrcodegen
          imgui
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_QrCode COMMAND qa_QrCode)

add_executable(qa_MathRender qa_MathRender.cpp ${_app_source_dir}/MathRender.cpp)
target_include_directories(qa_MathRender PRIVATE ${_app_source_dir} ${_app_generated_dir})
target_link_libraries(
  qa_MathRender
  PRIVATE ut
          microtex
          plutovg::plutovg
          imgui
          gr4-present-options)

add_test(NAME qa_MathRender COMMAND qa_MathRender)

# The one test that links OpenDigitizer's charts, and so the one that must not link the instrumented ImGui: it compiles
# LiveGraph.cpp directly and nothing from the test support library, which carries the other one.
if(GR4_PRESENT_WITH_OPENDIGITIZER)
  add_executable(
    qa_LiveGraph
    qa_LiveGraph.cpp
    ${_app_source_dir}/LiveGraph.cpp
    ${_app_source_dir}/Fonts.cpp
    ${_app_source_dir}/BuiltinFace.cpp)
  target_compile_definitions(
    qa_LiveGraph PRIVATE GR4_PRESENT_HAS_OPENDIGITIZER
                         GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
  target_include_directories(qa_LiveGraph PRIVATE ${_app_source_dir} ${_app_generated_dir})
  target_link_libraries(
    qa_LiveGraph
    PRIVATE ut
            gr4-present-core
            gr4-present-options
            opendigitizer::flowgraph-editor
            opendigitizer::dashboard
            opendigitizer::charts
            gnuradio4::gnuradio-core
            gnuradio4::gr-audio
            gnuradio4::gr-fileio
            gnuradio4::gr-trigger)
  add_test(NAME qa_LiveGraph COMMAND qa_LiveGraph)
  set_tests_properties(qa_LiveGraph PROPERTIES TIMEOUT 120)
endif()

add_executable(qa_SwipeNavigation qa_SwipeNavigation.cpp ${_app_source_dir}/SwipeNavigation.cpp)
target_include_directories(qa_SwipeNavigation PRIVATE ${_app_source_dir})
target_link_libraries(qa_SwipeNavigation PRIVATE ut gr4-present-options)
add_test(NAME qa_SwipeNavigation COMMAND qa_SwipeNavigation)

add_executable(qa_TextRuns qa_TextRuns.cpp ${_app_source_dir}/TextRuns.cpp)
target_include_directories(qa_TextRuns PRIVATE ${_app_source_dir})
target_link_libraries(qa_TextRuns PRIVATE ut gr4-present-core gr4-present-options)
add_test(NAME qa_TextRuns COMMAND qa_TextRuns)

# the GR4 blocks this deck brings for its workflows, run in a graph between GR4's own test source and sink
foreach(_block_test IN ITEMS qa_Deinterleave qa_PeakDecimator)
  add_executable(${_block_test} ${_block_test}.cpp)
  target_include_directories(${_block_test} PRIVATE ${_app_source_dir})
  target_link_libraries(
    ${_block_test}
    PRIVATE ut
            gr4-present-options
            gnuradio4::gnuradio-core
            gnuradio4::gnuradio-blocklib-core
            gnuradio4::gr-basic
            gnuradio4::gr-testing)
  add_test(NAME ${_block_test} COMMAND ${_block_test})
endforeach()
# the clap the live audio slide plays when it has no microphone, through that slide's trigger chain
target_link_libraries(qa_Deinterleave PRIVATE gnuradio4::gr-fileio gnuradio4::gr-trigger)
target_compile_definitions(qa_Deinterleave
                           PRIVATE GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")

add_executable(qa_ZoomPan qa_ZoomPan.cpp ${_app_source_dir}/ZoomPan.cpp)
target_include_directories(qa_ZoomPan PRIVATE ${_app_source_dir})
target_link_libraries(qa_ZoomPan PRIVATE ut gr4-present-options)
add_test(NAME qa_ZoomPan COMMAND qa_ZoomPan)

add_executable(
  qa_Grid
  qa_Grid.cpp
  ${_app_source_dir}/Grid.cpp
  ${_app_source_dir}/Layout.cpp
  ${_app_source_dir}/RasterCamera.cpp
  ${_app_source_dir}/Transition.cpp
  ${_app_source_dir}/SvgImage.cpp
  ${_app_source_dir}/BuiltinFace.cpp)
# Fonts.hpp only lends this test one inline function, the slide's type scale, but it includes imgui.h to do it. The
# headers are enough: nothing here calls into ImGui, so its objects are not linked.
target_include_directories(qa_Grid SYSTEM PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
target_include_directories(qa_Grid PRIVATE ${_app_source_dir} ${_app_generated_dir})
target_link_libraries(
  qa_Grid
  PRIVATE ut
          gr4-present-core
          gr4-present-options
          gr4-present-recording
          lunasvg
          plutovg::plutovg)
add_test(NAME qa_Grid COMMAND qa_Grid)

add_executable(qa_RasterCamera qa_RasterCamera.cpp ${_app_source_dir}/RasterCamera.cpp
                               ${_app_source_dir}/Transition.cpp)
target_include_directories(qa_RasterCamera PRIVATE ${_app_source_dir})
target_link_libraries(qa_RasterCamera PRIVATE ut gr4-present-core gr4-present-options)
add_test(NAME qa_RasterCamera COMMAND qa_RasterCamera)

add_executable(
  qa_SideMenu
  qa_SideMenu.cpp
  ${_app_source_dir}/SideMenu.cpp
  ${_app_source_dir}/Fonts.cpp
  ${_app_source_dir}/BuiltinFace.cpp)
target_include_directories(qa_SideMenu PRIVATE ${_app_source_dir} ${_app_generated_dir})
target_link_libraries(
  qa_SideMenu
  PRIVATE ut
          gr4-present-core
          gr4-present-options
          imgui
          raii_wrapper)
add_test(NAME qa_SideMenu COMMAND qa_SideMenu)

add_executable(qa_Morph qa_Morph.cpp ${_app_source_dir}/Morph.cpp ${_app_source_dir}/Transition.cpp)
# imgui.h lends this test its vertex type only; nothing here calls into ImGui, so its objects are not linked
target_include_directories(qa_Morph SYSTEM PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
target_include_directories(qa_Morph PRIVATE ${_app_source_dir})
target_link_libraries(qa_Morph PRIVATE ut gr4-present-core gr4-present-options)
add_test(NAME qa_Morph COMMAND qa_Morph)

add_executable(qa_Transition qa_Transition.cpp ${_app_source_dir}/Transition.cpp)
target_include_directories(qa_Transition PRIVATE ${_app_source_dir})
target_link_libraries(qa_Transition PRIVATE ut gr4-present-core gr4-present-options)
add_test(NAME qa_Transition COMMAND qa_Transition)

add_executable(qa_ScreenshotDiff qa_ScreenshotDiff.cpp)
target_link_libraries(qa_ScreenshotDiff PRIVATE ut gr4-present-test-support gr4-present-options)
add_test(NAME qa_ScreenshotDiff COMMAND qa_ScreenshotDiff)

list(TRANSFORM GR4_PRESENT_APP_SOURCES PREPEND "${_app_source_dir}/")
list(
  FILTER
  GR4_PRESENT_APP_SOURCES
  EXCLUDE
  REGEX
  "StbImage") # provided by gr4-present-test-support
add_executable(qa_LaunchScreen qa_LaunchScreen.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_LaunchScreen PRIVATE ${_app_generated_dir})
target_link_libraries(
  qa_LaunchScreen
  PRIVATE ut
          gr4-present-test-support
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_LaunchScreen COMMAND qa_LaunchScreen)
# renders offscreen through SDL's "offscreen" video driver, so no display server is needed
set_tests_properties(
  qa_LaunchScreen
  PROPERTIES LABELS
             "gui"
             TIMEOUT
             120)

add_executable(qa_Fonts qa_Fonts.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_Fonts PRIVATE ${_app_generated_dir})
target_compile_definitions(qa_Fonts PRIVATE GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_Fonts
  PRIVATE ut
          gr4-present-test-support
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_Fonts COMMAND qa_Fonts)
set_tests_properties(
  qa_Fonts
  PROPERTIES LABELS
             "gui"
             TIMEOUT
             120)

add_executable(qa_DocumentView qa_DocumentView.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_DocumentView PRIVATE ${_app_generated_dir})
target_compile_definitions(qa_DocumentView
                           PRIVATE GR4_PRESENT_PACKAGE_DIRECTORY="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_DocumentView
  PRIVATE ut
          gr4-present-test-support
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_DocumentView COMMAND qa_DocumentView)
set_tests_properties(
  qa_DocumentView
  PROPERTIES LABELS
             "gui"
             TIMEOUT
             120)

add_executable(qa_MainScreen qa_MainScreen.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_MainScreen PRIVATE ${_app_generated_dir})
target_compile_definitions(qa_MainScreen
                           PRIVATE GR4_PRESENT_DEFAULT_PRESENTATION="${CMAKE_SOURCE_DIR}/presentations/default")
target_link_libraries(
  qa_MainScreen
  PRIVATE ut
          gr4-present-test-support
          gr4-present-core
          gr4-present-options)
add_test(NAME qa_MainScreen COMMAND qa_MainScreen)
set_tests_properties(
  qa_MainScreen
  PROPERTIES LABELS
             "gui"
             TIMEOUT
             120)

# the targets that compile the viewer's own sources record what a slide draws
foreach(
  _viewer_test IN
  ITEMS qa_LaunchScreen
        qa_Fonts
        qa_DocumentView
        qa_MainScreen
        qa_MathRender
        qa_SvgImage)
  target_link_libraries(${_viewer_test} PRIVATE gr4-present-recording)
endforeach()
