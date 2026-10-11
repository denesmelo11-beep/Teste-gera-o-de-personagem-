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
  src/generator.cpp
  src/hair.cpp
  src/face.cpp
  src/animation.cpp
  src/image.cpp
  src/render.cpp
  src/gltf.cpp)
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
