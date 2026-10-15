cmake_minimum_required(VERSION 3.20)
project(chargen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
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
  src/studies.cpp)
target_include_directories(chargen PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(chargen PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(chargen PRIVATE /W4)
else()
  target_compile_options(chargen PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_executable(chargen_cli apps/chargen_cli.cpp)
target_link_libraries(chargen_cli PRIVATE chargen)

enable_testing()
add_executable(chargen_tests tests/tests.cpp)
target_link_libraries(chargen_tests PRIVATE chargen)
add_test(NAME chargen_tests COMMAND chargen_tests)

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
