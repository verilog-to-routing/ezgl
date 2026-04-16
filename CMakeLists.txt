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

    # Qt 6.8 contains a bug in the RHI backend where geometry buffers are not
    # properly invalidated/refreshed after the MVP matrix changes, causing
    # incorrect rendering artifacts. The bug is not seen in Qt 6.9.3.
    if(Qt6_VERSION VERSION_LESS "6.9.3")
        message(FATAL_ERROR "EZGL: Qt 6.9.3 or higher is required (found ${Qt6_VERSION})")
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
  include/ezgl/logutils.hpp
  include/ezgl/qt/qtutils.hpp
  include/ezgl/qt/qtgladeloader.hpp
  include/ezgl/qt/switchbutton.hpp
  include/ezgl/qt/drawingareawidget.hpp
  include/ezgl/qt/painter.hpp
  include/ezgl/qt/deferred_renderer.hpp
  src/logutils.cpp
  src/application.cpp
  src/camera.cpp
  src/canvas.cpp
  src/control.cpp
  src/callback.cpp
  src/graphics.cpp

  src/qt/qtutils.cpp
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

    # ---- Qt RHI GPU renderer (optional, Qt >= 6.7) --------------------------
    if(EZGL_USE_RHI)
        if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7.0")
            message(STATUS "ezgl: Enabling Qt RHI renderer (Qt ${Qt6_VERSION})")

            # QRhi / QShader live in Qt6::Gui private headers.
            #find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
            find_package(Qt6 QUIET COMPONENTS Gui)
            target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)

            # Bake GLSL shaders to .qsb as part of the build when qsb is available.
            set(EZGL_QSB_EXECUTABLE "${EZGL_QSB_EXECUTABLE}" CACHE FILEPATH
                "Path to the Qt Shader Baker executable (qsb)")
            set(EZGL_RHI_SHADER_QRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shaders.qrc")

            if(NOT EZGL_QSB_EXECUTABLE)
                get_filename_component(_ezgl_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
                get_filename_component(_ezgl_qt_sdk_root "${_ezgl_qt_prefix}/.." ABSOLUTE)
                get_filename_component(_ezgl_qt_root "${_ezgl_qt_sdk_root}/.." ABSOLUTE)

                set(_ezgl_qsb_hints
                    "${_ezgl_qt_prefix}/bin"
                    "${_ezgl_qt_prefix}/libexec"
                    "${_ezgl_qt_sdk_root}/bin"
                    "${_ezgl_qt_sdk_root}/libexec"
                    "${_ezgl_qt_root}/Tools/QtCreator/lib/Qt/bin"
                    "${_ezgl_qt_root}/Tools/QtCreator/lib/Qt/libexec"
                    "${_ezgl_qt_root}/Tools/QtDesignStudio/lib/Qt/bin"
                    "${_ezgl_qt_root}/Tools/QtDesignStudio/lib/Qt/libexec"
                    "${_ezgl_qt_root}/Tools/QtDesignStudio/qt6_design_studio_reduced_version/bin"
                )
                if(DEFINED QT_HOST_PATH AND NOT "${QT_HOST_PATH}" STREQUAL "")
                    list(PREPEND _ezgl_qsb_hints
                        "${QT_HOST_PATH}/bin"
                        "${QT_HOST_PATH}/libexec")
                endif()
                if(DEFINED ENV{QT_HOST_PATH} AND NOT "$ENV{QT_HOST_PATH}" STREQUAL "")
                    list(PREPEND _ezgl_qsb_hints
                        "$ENV{QT_HOST_PATH}/bin"
                        "$ENV{QT_HOST_PATH}/libexec")
                endif()

                find_program(_ezgl_qsb_executable
                    NAMES qsb qsb-qt6
                    HINTS ${_ezgl_qsb_hints}
                    DOC "Path to the Qt Shader Baker executable (qsb)")
                if(_ezgl_qsb_executable)
                    set(EZGL_QSB_EXECUTABLE "${_ezgl_qsb_executable}" CACHE FILEPATH
                        "Path to the Qt Shader Baker executable (qsb)" FORCE)
                endif()
            endif()

            if(EZGL_QSB_EXECUTABLE)
                set(EZGL_RHI_SHADER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ezgl_rhi_shaders")
                file(MAKE_DIRECTORY "${EZGL_RHI_SHADER_BUILD_DIR}")

                set(EZGL_FILL_RECT_VERT_QSB   "${EZGL_RHI_SHADER_BUILD_DIR}/fill_rect.vert.qsb")
                set(EZGL_LINE_VERT_QSB        "${EZGL_RHI_SHADER_BUILD_DIR}/line.vert.qsb")
                set(EZGL_LINE_FRAG_QSB        "${EZGL_RHI_SHADER_BUILD_DIR}/line.frag.qsb")
                set(EZGL_OVERLAY_VERT_QSB     "${EZGL_RHI_SHADER_BUILD_DIR}/overlay.vert.qsb")
                set(EZGL_OVERLAY_FRAG_QSB     "${EZGL_RHI_SHADER_BUILD_DIR}/overlay.frag.qsb")
                set(EZGL_THICK_LINE_VERT_QSB  "${EZGL_RHI_SHADER_BUILD_DIR}/thick_line.vert.qsb")
                set(EZGL_DASHED_LINE_VERT_QSB "${EZGL_RHI_SHADER_BUILD_DIR}/dashed_line.vert.qsb")
                set(EZGL_DASHED_LINE_FRAG_QSB "${EZGL_RHI_SHADER_BUILD_DIR}/dashed_line.frag.qsb")

                set(EZGL_RHI_SHADER_SOURCES
                    "fill_rect.vert"
                    "line.vert"
                    "line.frag"
                    "overlay.vert"
                    "overlay.frag"
                    "thick_line.vert"
                    "dashed_line.vert"
                    "dashed_line.frag"
                )
                set(EZGL_RHI_SHADER_OUTPUTS
                    "${EZGL_FILL_RECT_VERT_QSB}"
                    "${EZGL_LINE_VERT_QSB}"
                    "${EZGL_LINE_FRAG_QSB}"
                    "${EZGL_OVERLAY_VERT_QSB}"
                    "${EZGL_OVERLAY_FRAG_QSB}"
                    "${EZGL_THICK_LINE_VERT_QSB}"
                    "${EZGL_DASHED_LINE_VERT_QSB}"
                    "${EZGL_DASHED_LINE_FRAG_QSB}"
                )

                list(LENGTH EZGL_RHI_SHADER_SOURCES _ezgl_shader_count)
                math(EXPR _ezgl_shader_last_index "${_ezgl_shader_count} - 1")
                foreach(_ezgl_shader_index RANGE ${_ezgl_shader_last_index})
                    list(GET EZGL_RHI_SHADER_SOURCES ${_ezgl_shader_index} _ezgl_shader_src)
                    list(GET EZGL_RHI_SHADER_OUTPUTS ${_ezgl_shader_index} _ezgl_shader_out)
                    add_custom_command(
                        OUTPUT "${_ezgl_shader_out}"
                        COMMAND "${EZGL_QSB_EXECUTABLE}" --qt6 -o "${_ezgl_shader_out}"
                                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_ezgl_shader_src}"
                        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_ezgl_shader_src}"
                        COMMENT "Baking Qt shader ${_ezgl_shader_src}"
                        VERBATIM
                    )
                endforeach()

                add_custom_target(ezgl_rhi_shaders DEPENDS ${EZGL_RHI_SHADER_OUTPUTS})
                add_dependencies(${PROJECT_NAME} ezgl_rhi_shaders)
                set_source_files_properties(${EZGL_RHI_SHADER_OUTPUTS} PROPERTIES GENERATED TRUE)
                target_compile_definitions(${PROJECT_NAME} PRIVATE EZGL_RHI_GENERATED_SHADERS)

                set(EZGL_RHI_SHADER_QRC "${CMAKE_CURRENT_BINARY_DIR}/ezgl_rhi_shaders.qrc")
                configure_file(
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shaders.qrc.in"
                    "${EZGL_RHI_SHADER_QRC}"
                    @ONLY
                )
            else()
                message(WARNING
                    "ezgl: qsb not found; using checked-in shaders/*.qsb. "
                    "Set EZGL_QSB_EXECUTABLE to enable automatic shader baking.")
            endif()

            # RHI sources
            target_sources(${PROJECT_NAME} PRIVATE
                include/ezgl/qt/rhi_canvas_widget.hpp
                include/ezgl/qt/rhi_renderer.hpp
                src/qt/rhi_canvas_widget.cpp
                src/qt/rhi_renderer.cpp
                ${EZGL_RHI_SHADER_QRC}
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
