cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

project(
  draw-shapes
  VERSION 0.0.1
  LANGUAGES CXX
)

add_executable(${PROJECT_NAME} draw_shapes.cpp)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)

