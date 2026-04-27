/*
 * Copyright 2019-2022 University of Toronto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors: Mario Badr, Sameh Attia, Tanner Young-Schultz and Vaughn Betz
 */

#include "ezgl/canvas.hpp"
#include "ezgl/qt/deferred_backend.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/qt/immediate_backend.hpp"
#include "ezgl/qt/rhi_backend.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QWidget>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QSvgGenerator>
#include "ezgl/logutils.hpp"
#include "ezgl/qt/painter.hpp"

#include <cassert>
#include <cmath>

namespace {

// Create the appropriate rendering backend for a given renderer type and
// (optional) display widget.
//
// Probe logic lives here — called once per backend creation, cached by
// probe_rhi(). Passing widget == nullptr produces a headless backend.
// For a RhiCanvasWidget the cast inside rhi_backend selects the display path;
// for a DrawingAreaWidget (or nullptr) the non-RHI path is used.
//
// If the requested type is rhi but no GPU is available, falls back to
// immediate (QPainter) transparently.
std::unique_ptr<ezgl::render_backend> make_backend(
    ezgl::renderer_type  type,
    QWidget*             widget,   // RhiCanvasWidget*, DrawingAreaWidget*, or nullptr
    ezgl::draw_canvas_fn callback,
    ezgl::camera*        cam,
    ezgl::color          bg)
{
    ezgl::renderer_type effective = type;
    if (effective == ezgl::renderer_type::rhi && !ezgl::probe_rhi()) {
        ezgl::q_warning("canvas: no GPU backend available, falling back to immediate renderer.");
        effective = ezgl::renderer_type::immediate;
    }

    if (effective == ezgl::renderer_type::rhi) {
        // qobject_cast returns nullptr for non-RhiCanvasWidget widgets and for
        // nullptr itself — rhi_backend interprets nullptr as headless mode.
        return std::make_unique<ezgl::rhi_backend>(
            qobject_cast<ezgl::RhiCanvasWidget*>(widget), callback, cam, bg);
    }
    if (effective == ezgl::renderer_type::immediate)
        return std::make_unique<ezgl::immediate_backend>(widget, callback, cam, bg);
    return std::make_unique<ezgl::deferred_backend>(widget, callback, cam, bg);
}

} // anonymous namespace

namespace ezgl {

// Renders the canvas into an off-screen QImage of the given dimensions,
// shared by print_pdf / print_svg / print_png / draw_offscreen.
QImage canvas::render_to_image(int surface_width, int surface_height)
{
  if (!m_backend)
    m_backend = make_backend(m_renderer_type, nullptr, m_draw_callback, &m_camera, m_background_color);

  // Pre-configure the camera for the target dimensions (canvas is a friend of camera).
  m_camera.update_widget(surface_width, surface_height);
  return m_backend->render_to_image(surface_width, surface_height);
}

bool canvas::print_pdf(const char *file_name, int output_width, int output_height)
{
  const int w = (output_width == 0 && output_height == 0) ? m_drawing_area->width()  : output_width;
  const int h = (output_width == 0 && output_height == 0) ? m_drawing_area->height() : output_height;

  const QImage surface = render_to_image(w, h);

  QPdfWriter writer(file_name);
  writer.setResolution(72);
  writer.setPageSize(QPageSize(QSizeF(w, h), QPageSize::Point));
  writer.setPageMargins(QMarginsF(0, 0, 0, 0));

  QPainter pdfPainter(&writer);
  if (!pdfPainter.isActive())
    return false;
  pdfPainter.drawImage(pdfPainter.window(), surface);
  pdfPainter.end();

  return true;
}

bool canvas::print_svg(const char *file_name, int output_width, int output_height)
{
  const int w = (output_width == 0 && output_height == 0) ? m_drawing_area->width()  : output_width;
  const int h = (output_width == 0 && output_height == 0) ? m_drawing_area->height() : output_height;

  const QImage surface = render_to_image(w, h);

  QSvgGenerator generator;
  generator.setFileName(file_name);
  generator.setSize(QSize(w, h));
  generator.setViewBox(QRect(0, 0, w, h));

  QPainter svgPainter(&generator);
  if (!svgPainter.isActive())
    return false;
  svgPainter.drawImage(0, 0, surface);
  svgPainter.end();

  return true;
}

bool canvas::print_png(const char *file_name, int output_width, int output_height)
{
  const int w = (output_width == 0 && output_height == 0) ? m_drawing_area->width()  : output_width;
  const int h = (output_width == 0 && output_height == 0) ? m_drawing_area->height() : output_height;

  const QImage surface = render_to_image(w, h);
  return surface.save(file_name, "PNG");
}

void canvas::draw_offscreen(int output_width, int output_height)
{
  render_to_image(output_width, output_height);
}

canvas::canvas(std::string canvas_id,
               draw_canvas_fn draw_callback,
               rectangle coordinate_system,
               color background_color)
    : m_canvas_id(std::move(canvas_id))
    , m_draw_callback(draw_callback)
    , m_camera(coordinate_system)
    , m_background_color(background_color)
{
}

void canvas::initialize(QWidget *drawing_area)
{
  return_if_fail("initialize drawing_area", drawing_area != nullptr);

  m_drawing_area = drawing_area;
  m_backend = make_backend(m_renderer_type, drawing_area, m_draw_callback, &m_camera, m_background_color);

  // Wire up widget-specific signals after backend creation.
  if (RhiCanvasWidget* rw = qobject_cast<RhiCanvasWidget*>(drawing_area)) {
    if (dynamic_cast<rhi_backend*>(m_backend.get())) {
      QObject::connect(rw, &QRhiWidget::renderFailed, [this]() {
        q_warning("RHI render failed — disabling renderer.");
        m_backend.reset();
      });
    }
    QObject::connect(rw, &RhiCanvasWidget::resized, [this](int w, int h) {
      m_camera.update_widget(w, h);
      if (m_backend) m_backend->on_resize(w, h);
    });
    if (rw->width() > 0 && rw->height() > 0)
      m_camera.update_widget(rw->width(), rw->height());
  } else if (DrawingAreaWidget* daw = qobject_cast<DrawingAreaWidget*>(drawing_area)) {
    QObject::connect(daw, &DrawingAreaWidget::resized, [this](int w, int h) {
      m_camera.update_widget(w, h);
      m_backend->on_resize(w, h);
    });
    if (width() > 0 && height() > 0) {
      m_camera.update_widget(width(), height());
      m_backend->redraw();
    }
  }

  q_info("canvas::initialize successful.");
}

int canvas::width() const
{
  return m_drawing_area->width();
}

int canvas::height() const
{
  return m_drawing_area->height();
}

void canvas::redraw()
{
  if (!m_backend)
    return;
  if (m_frame_timing_fn) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    m_backend->redraw();
    const auto t1 = std::chrono::high_resolution_clock::now();
    m_frame_timing_fn(std::chrono::duration<double, std::milli>(t1 - t0).count());
  } else {
    m_backend->redraw();
  }
}

void canvas::redraw_camera_only()
{
  if (m_backend)
    m_backend->redraw_camera_only();
}

renderer *canvas::create_animation_renderer()
{
  if (m_backend)
    return m_backend->create_animation_renderer();
  return nullptr;
}

void canvas::begin_deferred_redraw_cycle()
{
  if (m_backend)
    m_backend->begin_deferred_redraw_cycle();
}

void canvas::end_deferred_redraw_cycle()
{
  if (m_backend)
    m_backend->end_deferred_redraw_cycle();
}

} // namespace ezgl
