# The viewer as a person uses it: the native build under Xvfb, driven with XTest, and the WASM build in a headless
# browser. Each is skipped rather than failed where the display, the browser or the reader it needs is absent.

if(EMSCRIPTEN)
  # The DOM half of the viewer -- layering, layout, event routing -- cannot be reached by the ImGui test engine, so it
  # is asserted in a real headless browser instead.
  find_program(GR4_PRESENT_NODE node)
  find_program(GR4_PRESENT_BROWSER NAMES google-chrome chromium chromium-browser google-chrome-stable)
  if(GR4_PRESENT_NODE AND GR4_PRESENT_BROWSER)
    file(GLOB _browser_tests RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
                                                                  ${CMAKE_CURRENT_SOURCE_DIR}/qa_Browser*.mjs)
    # CI's consumer job runs it on a deck site it builds from a release archive, as a deck's repository would
    list(REMOVE_ITEM _browser_tests qa_BrowserConsumerSite.mjs)
    # wholly about live graphs, which come with GR4's runtime and OpenDigitizer (not in WASM-Minimal)
    set(_runtime_browser_tests
        qa_BrowserDashboard
        qa_BrowserLayer
        qa_BrowserLiveAudio
        qa_BrowserLiveRapid
        qa_BrowserLiveWalk
        qa_BrowserToolbar)
    # the other suites skip only their checks of live graphs; without the export, qa_BrowserExport checks the refusal
    # instead
    set(_with_opendigitizer 0)
    if(GR4_PRESENT_WITH_OPENDIGITIZER)
      set(_with_opendigitizer 1)
    endif()
    set(_with_export 0)
    if(GR4_PRESENT_HAS_EXPORT)
      set(_with_export 1)
    endif()
    foreach(_browser_test_file IN LISTS _browser_tests)
      get_filename_component(_browser_test ${_browser_test_file} NAME_WE)
      if(NOT _with_opendigitizer AND _browser_test IN_LIST _runtime_browser_tests)
        continue()
      endif()
      add_test(NAME ${_browser_test} COMMAND ${GR4_PRESENT_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/${_browser_test_file}
                                             $<TARGET_FILE_DIR:gr4-present>)
      set_tests_properties(
        ${_browser_test}
        PROPERTIES LABELS
                   "browser"
                   TIMEOUT
                   600
                   RUN_SERIAL
                   TRUE
                   ENVIRONMENT
                   "GR4_PRESENT_WITH_OPENDIGITIZER=${_with_opendigitizer};GR4_PRESENT_HAS_EXPORT=${_with_export}")
    endforeach()
  else()
    message(STATUS "browser tests disabled (node: ${GR4_PRESENT_NODE}, browser: ${GR4_PRESENT_BROWSER})")
  endif()
  return()
endif()

# the native viewer exports the demo deck and poppler reads it back
find_program(GR4_PRESENT_PDFINFO pdfinfo)
if(GR4_PRESENT_PDFINFO AND GR4_PRESENT_HAS_EXPORT)
  add_test(
    NAME qa_ExportPdf
    COMMAND
      ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:gr4-present> -DDECK=${CMAKE_SOURCE_DIR}/presentations/default/talk.md
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/qa_ExportPdf.pdf -P ${CMAKE_CURRENT_SOURCE_DIR}/qa_ExportPdf.cmake)
  # each renders the whole deck with its live charts; side by side, one ran past its timeout
  set_tests_properties(
    qa_ExportPdf
    PROPERTIES LABELS
               "gui"
               TIMEOUT
               300
               RESOURCE_LOCK
               pdf-export)
  # a driver without OpenGL 3.3 core: effects off, every page still exported
  add_test(
    NAME qa_ExportPdfGl32
    COMMAND
      ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:gr4-present> -DDECK=${CMAKE_SOURCE_DIR}/presentations/default/talk.md
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/qa_ExportPdfGl32.pdf "-DEXPECT_LOG=effect shaders are switched off" -P
      ${CMAKE_CURRENT_SOURCE_DIR}/qa_ExportPdf.cmake)
  set_tests_properties(
    qa_ExportPdfGl32
    PROPERTIES LABELS
               "gui"
               TIMEOUT
               300
               ENVIRONMENT
               "GR4_PRESENT_GL32=1"
               RESOURCE_LOCK
               pdf-export)
  # a master whose clip is in its element's own box cannot be outlined: it is a picture, one per step, as each step
  # hides other elements
  add_test(
    NAME qa_ExportPdfClippedMaster
    COMMAND
      ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:gr4-present>
      -DDECK=${CMAKE_SOURCE_DIR}/test/fixtures/clipped-master/talk.md
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/qa_ExportPdfClippedMaster.pdf -DOPEN_DECK=ON -DMODE=steps -DEXPECT_PAGES=2
      -DEXPECT_IMAGES=2 "-DEXPECT_LOG=stays a picture in the PDF" -P ${CMAKE_CURRENT_SOURCE_DIR}/qa_ExportPdf.cmake)
  set_tests_properties(
    qa_ExportPdfClippedMaster
    PROPERTIES LABELS
               "gui"
               TIMEOUT
               120
               RESOURCE_LOCK
               pdf-export)
endif()

# The toolbar's controls and the presenter's buttons, clicked as a person would: the native viewer under Xvfb, driven
# with XTest. A browser's synthesised clicks do not reliably reach an ImGui widget, so this is the only automated check
# of that path.
find_program(GR4_PRESENT_XVFB Xvfb)
find_package(X11 COMPONENTS Xtst)
if(GR4_PRESENT_WITH_OPENDIGITIZER AND GR4_PRESENT_XVFB AND TARGET X11::Xtst)
  add_library(gr4-present-native-display STATIC NativeDisplay.cpp NativeDisplay.hpp)
  target_link_libraries(gr4-present-native-display PUBLIC X11::X11 X11::Xtst PRIVATE gr4-present-options)
  foreach(_native_test IN ITEMS qa_NativeFollowEdits qa_NativeLoadSave qa_NativeToolbar qa_NativePresenter)
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
  foreach(_test IN ITEMS qa_NativeFollowEdits qa_NativeLoadSave)
    target_compile_definitions(${_test} PRIVATE GR4_PRESENT_REMOTE_DECK="${CMAKE_SOURCE_DIR}/test/fixtures/remote-deck")
  endforeach()
  target_include_directories(qa_NativeLoadSave PRIVATE ${CMAKE_SOURCE_DIR}/test) # FileContents.hpp
else()
  message(STATUS "native GUI tests disabled (Xvfb: ${GR4_PRESENT_XVFB}, XTest found: ${X11_Xtst_FOUND})")
endif()
