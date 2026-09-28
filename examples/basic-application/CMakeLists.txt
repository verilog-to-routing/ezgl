cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  basic-application
  VERSION 0.0.1
  LANGUAGES CXX
)

set(CMAKE_AUTORCC ON)
set(RESOURCE_SRC resource.qrc)

add_executable(
  ${PROJECT_NAME}
  basic_application.cpp
  ${RESOURCE_SRC}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)

# basic_application.cpp loads small_image.png by relative path; copy it next to
# the executable so running from the build directory finds it.
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/small_image.png
          $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
