# require at least CMake version 3.0.2
cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

# create the project
project(
  ezgl
  VERSION 0.0.1
  LANGUAGES CXX
)

# we rely on GTK2 for the GUI, so make sure the system has it
find_package(GTK2 REQUIRED)

# include the configuration/compile time options for this library
include(options.cmake)

# create a library that can be linked by executables
add_library(
  ${PROJECT_NAME}
  include/ezgl/ezgl.hpp
  include/ezgl/application.hpp
  src/ezgl.cpp
  src/application.cpp
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC include
  PUBLIC ${GTK2_INCLUDE_DIRS}
)

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC ${GTK2_LIBRARIES}
)

if(EZGL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
