cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

project(
  basic-application
  VERSION 0.0.1
  LANGUAGES CXX
)

add_executable(${PROJECT_NAME} basic_application.cpp)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)

set_target_properties(
  ${PROJECT_NAME} PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
)