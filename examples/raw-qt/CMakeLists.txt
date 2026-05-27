cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  raw-qt
  VERSION 0.0.1
  LANGUAGES CXX
)

set(CMAKE_AUTORCC ON)
set(RESOURCE_SRC resource.qrc)

add_executable(
  ${PROJECT_NAME}
  raw-qt.cpp
  ${RESOURCE_SRC}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)
