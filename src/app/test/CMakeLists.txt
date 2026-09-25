# The sources under test are compiled into each test rather than linked from the application: the test binaries use the
# instrumented ImGui, whose ABI differs from the one the application links.
add_library(
  gr4-present-test-support STATIC
  ImGuiTestHarness.cpp
  ImGuiTestHarness.hpp
  ScreenshotDiff.cpp
  ScreenshotDiff.hpp
  ../StbImage.cpp) # the stb decoder lives here so each test binary carries exactly one copy
target_include_directories(gr4-present-test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(
  gr4-present-test-support PUBLIC GR4_PRESENT_BUILD_DIRECTORY="${CMAKE_BINARY_DIR}"
                                  GR4_PRESENT_REFERENCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/reference")
target_link_libraries(
  gr4-present-test-support
  PRIVATE gr4-present-options
  PUBLIC imgui-instrumented
         raii_wrapper
         $<TARGET_OBJECTS:imgui-instrumented>
         stb
         SDL3::SDL3)

add_executable(qa_ScreenshotDiff qa_ScreenshotDiff.cpp)
target_link_libraries(qa_ScreenshotDiff PRIVATE ut gr4-present-test-support gr4-present-options)
add_test(NAME qa_ScreenshotDiff COMMAND qa_ScreenshotDiff)

list(TRANSFORM GR4_PRESENT_APP_SOURCES PREPEND "../")
list(
  FILTER
  GR4_PRESENT_APP_SOURCES
  EXCLUDE
  REGEX
  "StbImage") # provided by gr4-present-test-support
add_executable(qa_LaunchScreen qa_LaunchScreen.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_LaunchScreen PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated)
target_include_directories(qa_LaunchScreen PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated)
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

add_executable(qa_MainScreen qa_MainScreen.cpp ${GR4_PRESENT_APP_SOURCES})
target_include_directories(qa_MainScreen PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../generated)
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
