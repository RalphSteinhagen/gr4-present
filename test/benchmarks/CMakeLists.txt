# hot paths, measured in instructions per run (GR4's harness reads the CPU counters); meaningful in a Release build
foreach(_benchmark IN ITEMS bm_Markdown bm_Highlight)
  add_executable(${_benchmark} ${_benchmark}.cpp)
  target_link_libraries(
    ${_benchmark}
    PRIVATE ut
            ut-benchmark
            gr4-present-core
            gr4-present-options)
  target_compile_definitions(${_benchmark}
                             PRIVATE GR4_PRESENT_DEFAULT_PRESENTATION="${CMAKE_SOURCE_DIR}/presentations/default")
endforeach()
