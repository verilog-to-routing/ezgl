cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

# create the project
project(
  ezgl
  VERSION 1.0.1
  LANGUAGES CXX
)

# we rely on GTK3 for the GUI, so make sure the system has it
find_package(PkgConfig REQUIRED)

# --------------------------------------------------------------------------
# EZGL_USE_RHI — opt-in GPU renderer via Qt RHI (requires Qt >= 6.7).
# Default: ON when Qt 6.7 or newer is detected.  Set -DEZGL_USE_RHI=OFF to
# force the QPainter (deferred_renderer) path on all platforms.
# --------------------------------------------------------------------------
option(EZGL_USE_RHI "Enable Qt RHI GPU renderer (requires Qt >= 6.7)" ON)

set(USE_QT 1)
if(USE_QT)
    # Enable Qt automatic code generation
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml Svg)

    if(NOT Qt6_FOUND)
      message(FATAL_ERROR "EZGL: Failed to find required Qt library (on debian/ubuntu try 'sudo apt-get install qt6-base-dev' to install)")
    endif()

else(USE_QT)
    pkg_check_modules(GTK3 QUIET gtk+-3.0)
    pkg_check_modules(X11 QUIET x11)

    if(NOT GTK3_FOUND)
      message(WARNING "EZGL: Failed to find required GTK3 library (on debian/ubuntu try 'sudo apt-get install libgtk-3-dev' to install)")
    endif()

    # we also rely on glib to compile the GTK resource files
    # a set of macros has been developed by Makman2 on GitHub to help with this
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/gcr-cmake/macros)
endif(USE_QT)

#Is ezgl the root cmake project?
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set(IS_ROOT_PROJECT TRUE)
else()
    set(IS_ROOT_PROJECT FALSE)
endif()

# include the configuration/compile time options for this library
include(options.cmake)

# create a library that can be linked by executables
add_library(
  ${PROJECT_NAME}
  include/ezgl/application.hpp
  include/ezgl/camera.hpp
  include/ezgl/canvas.hpp
  include/ezgl/color.hpp
  include/ezgl/control.hpp
  include/ezgl/callback.hpp
  include/ezgl/graphics.hpp
  include/ezgl/point.hpp
  include/ezgl/rectangle.hpp
  include/ezgl/qt/ezgl_qtcompat.hpp
  include/ezgl/qt/qtgladeloader.hpp
  include/ezgl/qt/switchbutton.hpp
  include/ezgl/qt/drawingareawidget.hpp
  include/ezgl/qt/painter.hpp
  include/ezgl/qt/deferred_renderer.hpp
  src/application.cpp
  src/camera.cpp
  src/canvas.cpp
  src/control.cpp
  src/callback.cpp
  src/graphics.cpp
  src/spatial_grid.cpp
  src/qt/ezgl_qtcompat.cpp
  src/qt/qtgladeloader.cpp
  src/qt/switchbutton.cpp
  src/qt/drawingareawidget.cpp
  src/qt/painter.cpp
  src/qt/deferred_renderer.cpp
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC include
)

if(USE_QT)

    target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Xml
        Qt6::Svg)

    # QT_MIGRATION_TODO, ASSERT_QT_MIGRATION_TODO
    target_compile_definitions(
        ${PROJECT_NAME}
        PUBLIC
        EZGL_QT
        HIDE_GTK_EVENT
        HIDE_GTK_BUILDER
    )

    # ---- Qt RHI GPU renderer (optional, Qt >= 6.7) --------------------------
    if(EZGL_USE_RHI)
        if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7.0")
            message(STATUS "ezgl: Enabling Qt RHI renderer (Qt ${Qt6_VERSION})")

            # QRhi / QShader live in Qt6::Gui private headers.
            find_package(Qt6 QUIET COMPONENTS Gui)
            target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)

            # RHI sources
            target_sources(${PROJECT_NAME} PRIVATE
                include/ezgl/qt/rhi_canvas_widget.hpp
                include/ezgl/qt/rhi_renderer.hpp
                src/qt/rhi_canvas_widget.cpp
                src/qt/rhi_renderer.cpp
                shaders/shaders.qrc
            )

            target_compile_definitions(${PROJECT_NAME} PUBLIC EZGL_RHI)

            # Qt 6.7 ships QRhiWidget inside Qt6::Widgets (no extra module needed).
            # QRhi itself lives in Qt6::Gui — already linked above.
        else()
            message(WARNING
                "ezgl: EZGL_USE_RHI=ON but Qt ${Qt6_VERSION} < 6.7 — "
                "QRhiWidget unavailable.  Falling back to deferred_renderer.")
        endif()
    endif()

else(USE_QT)

    #Treat GTK/X11 headers as system headers so they
    #do not generate compilation warnings
    target_include_directories(
      ${PROJECT_NAME}
      SYSTEM
      PUBLIC ${GTK3_INCLUDE_DIRS}
      PUBLIC ${X11_INCLUDE_DIRS}
    )

    target_link_libraries(
      ${PROJECT_NAME}
      PUBLIC ${GTK3_LIBRARIES}
      PUBLIC ${X11_LIBRARIES}
    )

endif(USE_QT)

# add_compile_options does not seem to be working on the UG machines,
# and we cannot set target properties in version 3.0.2
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

if(EZGL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(EZGL_BUILD_DOCS)
  add_subdirectory(doc)
endif()
