cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

# create the project
project(
  ezgl
  VERSION 1.0.1
  LANGUAGES CXX
)

# we rely on GTK3 for the GUI, so make sure the system has it
find_package(PkgConfig REQUIRED)

#
# We require c++20 support
#
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) #No compiler specific extensions

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
  include/ezgl/irenderer.hpp
  include/ezgl/graphics.hpp
  include/ezgl/point.hpp
  include/ezgl/rectangle.hpp
  include/ezgl/logutils.hpp
  include/ezgl/main_window.hpp
  include/ezgl/qt/qtutils.hpp
  include/ezgl/qt/qtgladeloader.hpp
  include/ezgl/qt/switchbutton.hpp
  include/ezgl/qt/drawingareawidget.hpp
  include/ezgl/qt/painter.hpp
  include/ezgl/qt/immediate_renderer.hpp
  include/ezgl/qt/deferred_renderer.hpp
  include/ezgl/qt/render_backend.hpp
  include/ezgl/qt/deferred_backend.hpp
  include/ezgl/qt/immediate_backend.hpp
  src/logutils.cpp
  src/application.cpp
  src/main_window.cpp
  src/camera.cpp
  src/canvas.cpp
  src/control.cpp
  src/callback.cpp
  src/qt/qtutils.cpp
  src/qt/qtgladeloader.cpp
  src/qt/switchbutton.cpp
  src/qt/drawingareawidget.cpp
  src/qt/painter.cpp
  src/qt/immediate_renderer.cpp
  src/qt/deferred_renderer.cpp
  src/qt/deferred_backend.cpp
  src/qt/immediate_backend.cpp
  src/qt/irenderer.cpp
)

target_include_directories(
  ${PROJECT_NAME}
  PUBLIC include
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml
    Qt6::Svg)

# ---- Qt RHI GPU renderer (always enabled, requires Qt >= 6.9.3) ----------

# QRhi / QShader live in Qt6::Gui private headers.
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)

# Bake GLSL shaders to .qsb at build time. qsb is REQUIRED — .qsb files
# are NOT committed to the source tree, so the build cannot fall back to
# stale prebuilt shaders if qsb is missing. This makes shader edits flow
# straight to the binary: edit shaders/*.vert / *.frag, rebuild, done.
#
# Install qsb via aqt (https://github.com/miurahr/aqtinstall):
#
#   pip install aqtinstall
#   aqt install-qt linux desktop 6.9.3 linux_gcc_64 -m qtshadertools
#
# This drops `qsb` next to the rest of the Qt 6.9.3 install. The search
# below picks it up from there — same Qt version as the rest of the build.
#
# Qt6_DIR is .../<arch>/lib/cmake/Qt6 — three levels up is the Qt SDK arch
# prefix (e.g. .../Qt/6.9.3/gcc_64/), and `aqt install-qt ... -m
# qtshadertools` lands `qsb` at `<prefix>/bin/qsb`.
get_filename_component(_ezgl_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)

find_program(EZGL_QSB_EXECUTABLE
    NAMES qsb
    HINTS "${_ezgl_qt_prefix}/bin"
    NO_DEFAULT_PATH
    DOC "Path to the Qt Shader Baker executable (qsb)")

if(NOT EZGL_QSB_EXECUTABLE)
    message(FATAL_ERROR
        "ezgl: Qt Shader Baker (qsb) not found in the Qt SDK.\n"
        "Install via aqt (https://github.com/miurahr/aqtinstall):\n"
        "  pip install aqtinstall\n"
        "  aqt install-qt linux desktop 6.9.3 linux_gcc_64 -m qtshadertools\n"
        "Searched: ${_ezgl_qt_prefix}/bin/qsb (derived from Qt6_DIR=${Qt6_DIR})")
endif()

set(EZGL_RHI_SHADER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ezgl_rhi_shaders")
file(MAKE_DIRECTORY "${EZGL_RHI_SHADER_BUILD_DIR}")

set(EZGL_FILL_RECT_VERT_QSB   "${EZGL_RHI_SHADER_BUILD_DIR}/fill_rect.vert.qsb")
set(EZGL_BASE_VERT_QSB        "${EZGL_RHI_SHADER_BUILD_DIR}/base.vert.qsb")
set(EZGL_BASE_FRAG_QSB        "${EZGL_RHI_SHADER_BUILD_DIR}/base.frag.qsb")
set(EZGL_OVERLAY_VERT_QSB     "${EZGL_RHI_SHADER_BUILD_DIR}/overlay.vert.qsb")
set(EZGL_OVERLAY_FRAG_QSB     "${EZGL_RHI_SHADER_BUILD_DIR}/overlay.frag.qsb")
set(EZGL_THICK_LINE_VERT_QSB  "${EZGL_RHI_SHADER_BUILD_DIR}/thick_line.vert.qsb")
set(EZGL_DASHED_LINE_VERT_QSB "${EZGL_RHI_SHADER_BUILD_DIR}/dashed_line.vert.qsb")
set(EZGL_DASHED_LINE_FRAG_QSB "${EZGL_RHI_SHADER_BUILD_DIR}/dashed_line.frag.qsb")
set(EZGL_ARROW_VERT_QSB       "${EZGL_RHI_SHADER_BUILD_DIR}/arrow.vert.qsb")

set(EZGL_RHI_SHADER_SOURCES
    "fill_rect.vert"
    "base.vert"
    "base.frag"
    "overlay.vert"
    "overlay.frag"
    "thick_line.vert"
    "dashed_line.vert"
    "dashed_line.frag"
    "arrow.vert"
)
set(EZGL_RHI_SHADER_OUTPUTS
    "${EZGL_FILL_RECT_VERT_QSB}"
    "${EZGL_BASE_VERT_QSB}"
    "${EZGL_BASE_FRAG_QSB}"
    "${EZGL_OVERLAY_VERT_QSB}"
    "${EZGL_OVERLAY_FRAG_QSB}"
    "${EZGL_THICK_LINE_VERT_QSB}"
    "${EZGL_DASHED_LINE_VERT_QSB}"
    "${EZGL_DASHED_LINE_FRAG_QSB}"
    "${EZGL_ARROW_VERT_QSB}"
)

list(LENGTH EZGL_RHI_SHADER_SOURCES _ezgl_shader_count)
math(EXPR _ezgl_shader_last_index "${_ezgl_shader_count} - 1")
foreach(_ezgl_shader_index RANGE ${_ezgl_shader_last_index})
    list(GET EZGL_RHI_SHADER_SOURCES ${_ezgl_shader_index} _ezgl_shader_src)
    list(GET EZGL_RHI_SHADER_OUTPUTS ${_ezgl_shader_index} _ezgl_shader_out)
    # --qt6 alone bakes GLSL 100es/120/150 + HLSL 50 + MSL 12. We add
    # GLSL 330 explicitly so QRhi can pick a variant that supports the
    # `layout(location = N)` qualifier on vertex inputs — without it the
    # GLSL 150 variant strips the qualifiers and QRhi has to resolve
    # attributes by name, which silently aliases the two `vec2 inMin/
    # inMax` inputs of fill_rect (and the equivalent inputs in the thick
    # / dashed line vertex shaders), producing stray-quad artefacts in
    # the headless RHI readback.
    add_custom_command(
        OUTPUT "${_ezgl_shader_out}"
        COMMAND "${EZGL_QSB_EXECUTABLE}"
                --glsl "100 es,120,150,330"
                --hlsl 50 --msl 12
                -o "${_ezgl_shader_out}"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_ezgl_shader_src}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_ezgl_shader_src}"
        COMMENT "Baking Qt shader ${_ezgl_shader_src}"
        VERBATIM
    )
endforeach()

add_custom_target(ezgl_rhi_shaders DEPENDS ${EZGL_RHI_SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} ezgl_rhi_shaders)
set_source_files_properties(${EZGL_RHI_SHADER_OUTPUTS} PROPERTIES GENERATED TRUE)

set(EZGL_RHI_SHADER_QRC "${CMAKE_CURRENT_BINARY_DIR}/ezgl_rhi_shaders.qrc")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shaders.qrc.in"
    "${EZGL_RHI_SHADER_QRC}"
    @ONLY
)

target_sources(${PROJECT_NAME} PRIVATE
    include/ezgl/qt/rhi_types.hpp
    include/ezgl/qt/rhi_scene_renderer.hpp
    include/ezgl/qt/rhi_canvas_widget.hpp
    include/ezgl/qt/rhi_renderer.hpp
    include/ezgl/qt/rhi_backend.hpp
    src/qt/rhi_scene_renderer.cpp
    src/qt/rhi_canvas_widget.cpp
    src/qt/rhi_renderer.cpp
    src/qt/rhi_backend.cpp
    ${EZGL_RHI_SHADER_QRC}
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    $<$<CONFIG:Debug>:EZGL_RENDERER_DEBUG>
)

# Use target property to enforce C++20 — raw -std= flag gets overridden by Qt6's gnu++17
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(EZGL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(EZGL_BUILD_DOCS)
  add_subdirectory(doc)
endif()
