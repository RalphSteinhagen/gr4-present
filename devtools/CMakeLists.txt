# libclang is under /usr/lib64 on openSUSE and under /usr/lib/llvm-<version> from apt.llvm.org (the CI image)
find_library(
  GR4_PRESENT_LIBCLANG
  NAMES clang clang-22 libclang
  HINTS /usr/lib/llvm-22/lib REQUIRED)
find_path(GR4_PRESENT_LIBCLANG_INCLUDE clang-c/Index.h HINTS /usr/lib/llvm-22/include REQUIRED)
foreach(_tool IN ITEMS code-metrics naming-check)
  add_executable(${_tool} ${_tool}.cpp)
  target_include_directories(${_tool} SYSTEM PRIVATE ${GR4_PRESENT_LIBCLANG_INCLUDE})
  target_link_libraries(${_tool} PRIVATE gr4-present-options ${GR4_PRESENT_LIBCLANG})
endforeach()

# the quality bots' reports (.github/workflows): reporting coverage, the bot is not itself measured by it, so the
# instrumented build leaves it out and CI compiles it alone (g++-15 "$PWD/devtools/qualitybot.cpp")
if(NOT GR4_PRESENT_COVERAGE)
  add_executable(qualitybot qualitybot.cpp)
  target_link_libraries(qualitybot PRIVATE gr4-present-options)
endif()

find_program(GR4_PRESENT_CLANG_TIDY NAMES clang-tidy-22 clang-tidy)
if(GR4_PRESENT_CLANG_TIDY AND CMAKE_CXX_COMPILER_ID MATCHES "Clang") # the fixture is parsed as the Clang build parses
                                                                     # its code
  set(FIXTURE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/test/code-metrics)
  configure_file(test/code-metrics/compile_commands.json.in test/code-metrics/compile_commands.json @ONLY)
  add_test(NAME code-metrics-fixture COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/code-metrics/check-fixture.sh
                                             $<TARGET_FILE:code-metrics> ${CMAKE_CURRENT_BINARY_DIR}/test/code-metrics)
  set(FIXTURE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/test/naming-check)
  configure_file(test/naming-check/compile_commands.json.in test/naming-check/compile_commands.json @ONLY)
  add_test(NAME naming-check-fixture COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/naming-check/check-fixture.sh
                                             $<TARGET_FILE:naming-check> ${CMAKE_CURRENT_BINARY_DIR}/test/naming-check)
endif()

# the tools' pure parts against their ground truths; the fixtures above check them end to end
if(TARGET ut)
  foreach(_test IN ITEMS qa_NamingCheck qa_CodeMetrics qa_QualityBot)
    add_executable(${_test} test/${_test}.cpp)
    target_include_directories(${_test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${_test} PRIVATE ut gr4-present-options)
    add_test(NAME ${_test} COMMAND ${_test})
  endforeach()
endif()
