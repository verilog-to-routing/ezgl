cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

project(
  basic-window
  VERSION 0.0.1
  LANGUAGES CXX
)

add_executable(${PROJECT_NAME} basic_window.cpp)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)