set(_core_tests
    qa_LaunchOptions
    qa_PackagePath
    qa_PresentationLoader
    qa_Navigation
    qa_Manifest
    qa_LiveRegion)
if(EMSCRIPTEN)
  list(REMOVE_ITEM _core_tests qa_PresentationLoader) # reads a real filesystem; MEMFS has no package staged
endif()

foreach(_test IN LISTS _core_tests)
  add_executable(${_test} ${_test}.cpp)
  target_link_libraries(${_test} PRIVATE ut gr4-present-core gr4-present-options)
  target_compile_definitions(${_test}
                             PRIVATE GR4_PRESENT_DEFAULT_PRESENTATION="${CMAKE_SOURCE_DIR}/presentations/default")
  add_test(NAME ${_test} COMMAND ${_test})
endforeach()
