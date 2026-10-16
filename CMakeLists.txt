cmake_minimum_required(VERSION 3.20)
project(chargen VERSION 1.0.0 LANGUAGES CXX)

# Used from a game (add_subdirectory / FetchContent) only the library is built by default;
# the CLI, tests and viewer are for working on chargen itself.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(CHARGEN_TOP_LEVEL ON)
else()
  set(CHARGEN_TOP_LEVEL OFF)
endif()
option(CHARGEN_BUILD_TOOLS "Build chargen_cli and the tests" ${CHARGEN_TOP_LEVEL})

if(CHARGEN_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
  endif()
endif()

add_library(chargen
  src/genome.cpp
  src/builder.cpp
  src/bones.cpp
  src/generator.cpp
  src/hair.cpp
  src/head.cpp
  src/physics.cpp
  src/face.cpp
  src/animation.cpp
  src/motion.cpp
  src/image.cpp
  src/render.cpp
  src/gltf.cpp
  src/engine.cpp
  src/studies.cpp
  src/traits.cpp)
add_library(chargen::chargen ALIAS chargen)
target_compile_features(chargen PUBLIC cxx_std_20)
target_include_directories(chargen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(chargen PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(chargen PRIVATE /W4)
else()
  target_compile_options(chargen PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(CHARGEN_BUILD_TOOLS)
  add_executable(chargen_cli apps/chargen_cli.cpp)
  target_link_libraries(chargen_cli PRIVATE chargen)

  enable_testing()
  add_executable(chargen_tests tests/tests.cpp)
  target_link_libraries(chargen_tests PRIVATE chargen)
  add_test(NAME chargen_tests COMMAND chargen_tests)
endif()

# ---- optional raylib viewer ----
option(CHARGEN_VIEWER "Build the raylib/OpenGL viewer" OFF)
set(RAYLIB_SOURCE_DIR "" CACHE PATH "Path to a raylib source tree (optional)")
if(CHARGEN_VIEWER)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    if(RAYLIB_SOURCE_DIR)
      set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      add_subdirectory(${RAYLIB_SOURCE_DIR} ${CMAKE_BINARY_DIR}/raylib EXCLUDE_FROM_ALL)
    else()
      include(FetchContent)
      set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
      FetchContent_MakeAvailable(raylib)
    endif()
  endif()
  add_executable(chargen_viewer apps/viewer.cpp)
  target_link_libraries(chargen_viewer PRIVATE chargen raylib)
endif()
