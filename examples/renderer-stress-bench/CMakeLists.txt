cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
  renderer-stress-bench
  VERSION 0.0.1
  LANGUAGES CXX
)

if(TARGET Qt6::Widgets)
  # Qt build: embed main.ui as a Qt resource so :/main.ui is accessible at runtime.
  qt_add_resources(QT_RESOURCES resource.qrc)
  set(EXTRA_SOURCES ${QT_RESOURCES})
else()
  # GTK build: compile main.ui into a GLib resource so /ezgl/main.ui is accessible at runtime.
  include(GlibCompileResourcesSupport)
  compile_gresources(
    RESOURCE_FILE
    XML_OUT
    TYPE    EMBED_C
    TARGET  resources.C
    PREFIX  /ezgl
    RESOURCES STRIPBLANKS main.ui
  )
  add_custom_target(resource-bench ALL DEPENDS ${RESOURCE_FILE})
  set(EXTRA_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/resources.C)
endif()

add_executable(
  ${PROJECT_NAME}
  renderer-stress-bench.cpp
  ${EXTRA_SOURCES}
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ezgl
)
