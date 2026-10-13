# every test may read a whole file through FileContents.hpp
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(core)
if(NOT EMSCRIPTEN)
  add_subdirectory(benchmarks)
endif()
if(GR4_PRESENT_ENABLE_EXPORT)
  add_subdirectory(export)
endif()
if(GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE)
  add_subdirectory(app)
endif()
