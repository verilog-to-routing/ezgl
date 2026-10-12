cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  renderer-stress-bench
  VERSION 0.0.1
  LANGUAGES CXX
)

# Embed main.ui as a Qt resource so :/main.ui is accessible at runtime.
qt_add_resources(QT_RESOURCES resource.qrc)
set(EXTRA_SOURCES ${QT_RESOURCES})

# main.ui is embedded through the .qrc and loaded at run time. It is listed as
# a source only so IDEs show it for editing; uic is not needed.
set_source_files_properties(main.ui PROPERTIES SKIP_AUTOUIC ON)

add_executable(
  ${PROJECT_NAME}
  renderer-stress-bench.cpp
  main.ui
  ${EXTRA_SOURCES}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)
