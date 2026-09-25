# DOM/SVG <-> OpenDigitizer bridge. Links the OpenDigitizer UI library so that the single persistent GR4/OpenDigitizer
# runtime, its dashboards and its ImPlot widgets are reused rather than reimplemented.
if(EMSCRIPTEN)
  set(_od_uilib indexlib) # src/ui names its executable "index" under Emscripten
else()
  set(_od_uilib opendigitizer-uilib)
endif()

if(NOT TARGET ${_od_uilib})
  message(
    FATAL_ERROR
      "expected OpenDigitizer target '${_od_uilib}'; configure with -DGR4_PRESENT_WITH_OPENDIGITIZER=OFF to build without it"
  )
endif()

add_library(gr4-present-viewer STATIC RegionGeometry.cpp RegionGeometry.hpp)

target_include_directories(gr4-present-viewer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(
  gr4-present-viewer
  PRIVATE gr4-present-options
  PUBLIC gr4-present-core ${_od_uilib})

if(GR4_PRESENT_ENABLE_TESTING)
  add_executable(qa_RegionGeometry test/qa_RegionGeometry.cpp)
  target_link_libraries(qa_RegionGeometry PRIVATE ut gr4-present-viewer gr4-present-options)
  add_test(NAME qa_RegionGeometry COMMAND qa_RegionGeometry)
endif()
