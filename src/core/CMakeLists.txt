# presentation model, navigation/staging, manifest and package-path handling. Free of any UI toolkit: it must stay
# buildable and testable without OpenDigitizer, ImGui or a browser.
add_library(
  gr4-present-core STATIC
  LaunchOptions.cpp
  LiveRegion.cpp
  Manifest.cpp
  Navigation.cpp
  PresentationLoader.cpp
  PackagePath.cpp)

target_include_directories(gr4-present-core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                                   $<INSTALL_INTERFACE:include>)
target_link_libraries(
  gr4-present-core
  PRIVATE gr4-present-options
  PUBLIC gnuradio4::gnuradio-algorithm gnuradio4::gnuradio-core)

if(GR4_PRESENT_ENABLE_TESTING)
  add_subdirectory(test)
endif()
