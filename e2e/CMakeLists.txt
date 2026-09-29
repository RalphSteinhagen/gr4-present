# The viewer as a person uses it: the native build under Xvfb, driven with XTest, and the WASM build in a headless
# browser. Each is skipped rather than failed where the display, the browser or the reader it needs is absent.

if(EMSCRIPTEN)
  # The DOM half of the viewer -- layering, layout, event routing -- cannot be reached by the ImGui test engine, so it
  # is asserted in a real headless browser instead.
  find_program(GR4_PRESENT_NODE node)
  find_program(
    GR4_PRESENT_BROWSER
    NAMES google-chrome
          chromium
          chromium-browser
          google-chrome-stable)
  if(GR4_PRESENT_NODE AND GR4_PRESENT_BROWSER)
    file(
      GLOB _browser_tests
      RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
      CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qa_Browser*.mjs)
    foreach(_browser_test_file IN LISTS _browser_tests)
      get_filename_component(_browser_test ${_browser_test_file} NAME_WE)
      add_test(NAME ${_browser_test} COMMAND ${GR4_PRESENT_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/${_browser_test_file}
                                             $<TARGET_FILE_DIR:gr4-present>)
      set_tests_properties(
        ${_browser_test}
        PROPERTIES LABELS
                   "browser"
                   TIMEOUT
                   600
                   RUN_SERIAL
                   TRUE)
    endforeach()
  else()
    message(STATUS "browser tests disabled (node: ${GR4_PRESENT_NODE}, browser: ${GR4_PRESENT_BROWSER})")
  endif()
  return()
endif()

# the native viewer exports the demo deck and poppler reads it back
find_program(GR4_PRESENT_PDFINFO pdfinfo)
if(GR4_PRESENT_PDFINFO
   AND GR4_PRESENT_WITH_OPENDIGITIZER
   AND GR4_PRESENT_ENABLE_EXPORT)
  add_test(
    NAME qa_ExportPdf
    COMMAND
      ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:gr4-present> -DDECK=${CMAKE_SOURCE_DIR}/presentations/default/talk.md
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/qa_ExportPdf.pdf -P ${CMAKE_CURRENT_SOURCE_DIR}/qa_ExportPdf.cmake)
  set_tests_properties(
    qa_ExportPdf
    PROPERTIES LABELS
               "gui"
               TIMEOUT
               300)
endif()

# The toolbar's controls and the presenter's buttons, clicked as a person would: the native viewer under Xvfb, driven
# with XTest. A browser's synthesised clicks do not reliably reach an ImGui widget, so this is the only automated check
# of that path.
find_program(GR4_PRESENT_XVFB Xvfb)
find_package(X11 COMPONENTS Xtst)
if(GR4_PRESENT_WITH_OPENDIGITIZER
   AND GR4_PRESENT_XVFB
   AND TARGET X11::Xtst)
  add_library(gr4-present-native-display STATIC NativeDisplay.cpp NativeDisplay.hpp)
  target_link_libraries(
    gr4-present-native-display
    PUBLIC X11::X11 X11::Xtst
    PRIVATE gr4-present-options)
  foreach(_native_test IN ITEMS qa_NativeToolbar qa_NativePresenter)
    add_executable(${_native_test} ${_native_test}.cpp)
    target_link_libraries(${_native_test} PRIVATE ut gr4-present-native-display gr4-present-options)
    add_test(NAME ${_native_test} COMMAND ${_native_test} $<TARGET_FILE:gr4-present>)
    set_tests_properties(
      ${_native_test}
      PROPERTIES LABELS
                 "gui"
                 TIMEOUT
                 180
                 RUN_SERIAL
                 TRUE)
  endforeach()
else()
  message(STATUS "native GUI tests disabled (Xvfb: ${GR4_PRESENT_XVFB}, XTest found: ${X11_Xtst_FOUND})")
endif()
