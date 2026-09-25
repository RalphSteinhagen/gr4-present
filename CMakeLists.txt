cmake_minimum_required(VERSION 3.27)

project(
  gr4-present
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# template-heavy GR4/OpenDigitizer translation units have measured over 7 GB each: the binding constraint is memory, not
# cores. Raise only after measuring peak RSS.
set(GR_BUILD_PARALLEL_LEVEL
    "4"
    CACHE STRING "Maximum parallel compile/link jobs")

# gnuradio4 sets its release flags as a directory-scoped variable, so they never reach the cache and our own targets
# fall through to CMake's default -O3. -O2 is what the dependencies are built and measured with.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g0 -DNDEBUG -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g1 -gz -DNDEBUG -ffunction-sections -fdata-sections")
if(CMAKE_CXX_COMPILER MATCHES "/em\\+\\+(-[a-zA-Z0-9.])?$") # unless already set via emcmake
  message(STATUS "Transpiling to WASM: using Emscripten (${CMAKE_CXX_COMPILER})")
  set(EMSCRIPTEN ON)
endif()

set(GR_MAX_WASM_THREAD_COUNT
    "16"
    CACHE STRING "Size of the Emscripten pthread pool")

include(CMakeDependentOption)

option(GR4_PRESENT_ENABLE_TESTING "Enable unit tests" ON)
option(GR4_PRESENT_WARNINGS_AS_ERRORS "Treat gr4-present compiler warnings as errors" ON)
option(GR4_PRESENT_WITH_OPENDIGITIZER "Build the OpenDigitizer/GR4 live-region viewer" ON)

# the test engine instruments ImGui, so it is built only where tests are
cmake_dependent_option(
  GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE
  "Build the ImGui screenshot and interaction tests"
  ON
  "GR4_PRESENT_ENABLE_TESTING;NOT EMSCRIPTEN"
  OFF)

if(GR4_PRESENT_ENABLE_TESTING)
  enable_testing()
endif()

include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/Cache.cmake)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT EMSCRIPTEN)
  set(GR4_USE_LIBCXX
      ON
      CACHE BOOL "Force libc++ when compiling with Clang" FORCE)
  add_compile_options(-stdlib=libc++ -Wno-unused-command-line-argument)
  add_link_options(-stdlib=libc++)
endif()

add_library(gr4-present-options INTERFACE)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(gr4-present-options)

if(EMSCRIPTEN)
  # every translation unit must agree about atomics: the application links with shared memory, so a library compiled
  # without -pthread is rejected by wasm-ld the moment it uses std::atomic, as gr::algorithm::fileio does
  target_compile_options(gr4-present-options INTERFACE -pthread)
  target_link_options(gr4-present-options INTERFACE -pthread)
endif()

# Selecting the pools has to happen before the dependencies are added, otherwise OpenDigitizer's own UI translation
# units -- the heaviest in the build -- are generated without one and ignore the memory bound. Declaring the pools is
# deferred until afterwards because gnuradio4 declares the same two when it is built as a subproject, and ninja rejects
# a duplicate pool. A pool declared anywhere is global to the generated build file.
if(CMAKE_GENERATOR MATCHES "Ninja")
  set(CMAKE_JOB_POOL_COMPILE compile_pool)
  set(CMAKE_JOB_POOL_LINK link_pool)
endif()

include(cmake/Dependencies.cmake)

if(CMAKE_GENERATOR MATCHES "Ninja")
  get_property(_job_pools GLOBAL PROPERTY JOB_POOLS)
  if(NOT
     _job_pools
     MATCHES
     "compile_pool=")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS compile_pool=${GR_BUILD_PARALLEL_LEVEL} link_pool=2)
  endif()
endif()

# applied after the dependencies are added so it binds our own sources only: -std=c++23 rather than -std=gnu++23, so a
# GNU extension is a compile error instead of a silent portability debt for the Clang and Emscripten builds
set(CMAKE_CXX_EXTENSIONS OFF)

option(GR4_PRESENT_ENABLE_CLANG_TIDY "Run clang-tidy over gr4-present sources" OFF)
if(GR4_PRESENT_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_COMMAND NAMES clang-tidy-20 clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_COMMAND})
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)
if(GR4_PRESENT_WITH_OPENDIGITIZER)
  add_subdirectory(src/viewer)
endif()

message(STATUS "GR4_PRESENT_ENABLE_TESTING       : ${GR4_PRESENT_ENABLE_TESTING}")
message(STATUS "GR4_PRESENT_ENABLE_CLANG_TIDY    : ${GR4_PRESENT_ENABLE_CLANG_TIDY}")
message(STATUS "GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE : ${GR4_PRESENT_ENABLE_IMGUI_TEST_ENGINE}")
message(STATUS "GR4_PRESENT_WARNINGS_AS_ERRORS   : ${GR4_PRESENT_WARNINGS_AS_ERRORS}")
message(STATUS "GR4_PRESENT_WITH_OPENDIGITIZER   : ${GR4_PRESENT_WITH_OPENDIGITIZER}")
message(STATUS "GR_BUILD_PARALLEL_LEVEL          : ${GR_BUILD_PARALLEL_LEVEL}")
