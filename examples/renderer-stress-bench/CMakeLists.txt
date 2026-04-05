cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  renderer-stress-bench
  VERSION 0.0.1
  LANGUAGES CXX
)

add_executable(
  ${PROJECT_NAME}
  renderer-stress-bench.cpp
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)
