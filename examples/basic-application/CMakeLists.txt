cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  basic-application
  VERSION 0.0.1
  LANGUAGES CXX
)

set(CMAKE_AUTORCC ON)
set(RESOURCE_SRC resource.qrc)

# main.ui is embedded through the .qrc and loaded at run time. It is listed as
# a source only so IDEs show it for editing; uic is not needed.
set_source_files_properties(main.ui PROPERTIES SKIP_AUTOUIC ON)

add_executable(
  ${PROJECT_NAME}
  basic_application.cpp
  main.ui
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
