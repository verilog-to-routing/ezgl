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

#include "ezgl/graphics.hpp"

#include <QWidget>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QSvgGenerator>
#include "ezgl/logutils.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/qt/deferred_renderer.hpp"
#ifdef EZGL_RHI
#include "ezgl/qt/rhi_canvas_widget.hpp"
#include "ezgl/qt/rhi_renderer.hpp"   // full type needed for unique_ptr destruction
#endif

#include <cassert>
#include <cmath>
#include <functional>

namespace ezgl {

QImage *create_surface(QWidget* widget)
{
  ezgl::DrawingAreaWidget* drawableAreaWidget = qobject_cast<ezgl::DrawingAreaWidget*>(widget);
  if (drawableAreaWidget) {
    return drawableAreaWidget->createSurface();
  }
  return nullptr;
}

static Painter *create_painter(QImage *p_surface)
{
  Painter *painter = new Painter(p_surface);

  // Equivalent to CAIRO_ANTIALIAS_NONE
  painter->setAntialias(false);
  painter->setSmoothPixmap(false);

  return painter;
}

// Renders the canvas into an off-screen QImage of the given dimensions,
// shared by print_pdf / print_svg / print_png.
QImage canvas::render_to_image(int surface_width, int surface_height)
{
  QImage surface(surface_width, surface_height, QImage::Format_ARGB32);
  Painter painter(&surface);

  painter.set_source_rgb(m_background_color.red / 255.0,
                         m_background_color.green / 255.0,
                         m_background_color.blue / 255.0);
  painter.paint();

  using namespace std::placeholders;
  camera cam = m_camera;
  cam.update_widget(surface_width, surface_height);
  deferred_renderer g(&painter, std::bind(&camera::world_to_screen, cam, _1), &cam, &surface);
  m_draw_callback(&g);
  g.flush();

  return surface;
}

bool canvas::print_pdf(const char *file_name, int output_width, int output_height)
{
  const int w = (output_width == 0 && output_height == 0) ? m_drawing_area->width()  : output_width;
  const int h = (output_width == 0 && output_height == 0) ? m_drawing_area->height() : output_height;

  const QImage surface = render_to_image(w, h);

  // QPdfWriter lives in Qt::Gui — no PrintSupport module needed.
  // At 72 DPI, 1 point == 1 pixel, so pixel dimensions map directly to page points.
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
  // Qt path: render_to_image already does draw-only; just discard the result.
  render_to_image(output_width, output_height);
}

bool canvas::draw_surface(QWidget *, Painter *painter, void* data)
{
  // Assume context and data are non-null.
  auto &p_surface = static_cast<canvas *>(data)->m_surface;

  // Assume surface is non-null.
  painter->set_source_surface(p_surface, 0, 0);
  painter->paint();

  return false;
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

canvas::~canvas()
{
  // Painter must be destroyed before the surface it is painting on.
  if(m_painter != nullptr) {
    delete m_painter;
    m_painter = nullptr;
  }

  if(m_surface != nullptr) {
    delete m_surface;
  }

  if(m_animation_renderer != nullptr) {
    delete m_animation_renderer;
  }
}

#ifdef EZGL_RHI
void canvas::begin_deferred_redraw_cycle()
{
  if (!m_rhi_widget)
    return;

  m_rhi_defer_redraw = true;
  m_rhi_pending_redraw = false;
  m_rhi_pending_camera_only = false;
}

void canvas::end_deferred_redraw_cycle()
{
  if (!m_rhi_widget || !m_rhi_defer_redraw)
    return;

  m_rhi_defer_redraw = false;
  if (m_rhi_pending_redraw || !m_rhi_has_drawn_frame)
    redraw();
  else if (m_rhi_pending_camera_only)
    redraw_camera_only();
  else if (m_rhi_renderer)
    redraw();
}
#endif

int canvas::width() const
{
  return m_drawing_area->width();
}

int canvas::height() const
{
  return m_drawing_area->height();
}

void canvas::initialize(QWidget *drawing_area)
{
  q_debug("~~~ canvas::initialize");
  return_if_fail(drawing_area != nullptr);

  m_drawing_area = drawing_area;

#ifdef EZGL_RHI
  // ---- RHI path: RhiCanvasWidget takes over from DrawingAreaWidget ----------
  if (RhiCanvasWidget* rw = qobject_cast<RhiCanvasWidget*>(drawing_area)) {
    m_rhi_widget = rw;

    // Connect renderFailed → fall back to the QPainter path.
    QObject::connect(rw, &QRhiWidget::renderFailed, [this]() {
      q_warning("RHI render failed — falling back to deferred_renderer (QPainter).");
      m_rhi_widget   = nullptr;
      m_rhi_renderer.reset();
    });

    rw->setPreResizeCallback([this]() {
      // Nothing to end on the RHI path; kept for API symmetry.
    });
    rw->setResizeCallback([this](int w, int h) {
      const rectangle old_widget = m_camera.get_widget();
      const bool size_changed = old_widget.width() != double(w) || old_widget.height() != double(h);
      m_camera.update_widget(w, h);

      const bool can_reuse_geometry = size_changed && m_rhi_renderer && m_rhi_has_drawn_frame;
      if (m_rhi_defer_redraw) {
        if (can_reuse_geometry) {
          m_rhi_pending_camera_only = true;
        } else {
          m_rhi_pending_redraw = true;
          m_rhi_pending_camera_only = false;
        }
      } else if (can_reuse_geometry) {
        redraw_camera_only();
      } else {
        redraw();
      }
    });

    if (rw->width() > 0 && rw->height() > 0) {
      m_camera.update_widget(rw->width(), rw->height());
    }
    q_info("canvas::initialize using RHI path.");
    return;
  }
#endif // EZGL_RHI

  m_surface = create_surface(m_drawing_area);
  m_painter = new Painter(m_surface);

  // Before show(), the widget may have zero size (layout not yet resolved).
  // Guard against division-by-zero in camera::update_scale_factors().
  // The resize callback below fires as soon as the widget receives its real
  // size (first QResizeEvent after show()), which mirrors GTK's configure-event.
  if (width() > 0 && height() > 0) {
    m_camera.update_widget(width(), height());
    redraw();
  }

  // Register a resize callback — the Qt equivalent of GTK's configure-event.
  // It recreates the backing surface/context and updates the camera every time
  // the DrawingAreaWidget is resized (including the initial show()).
  if (DrawingAreaWidget* daw = qobject_cast<DrawingAreaWidget*>(drawing_area)) {
    // End the painter before the backing image is deleted on resize (resizeEvent path).
    daw->setPreResizeCallback([this]() {
      if (m_painter != nullptr) {
        delete m_painter;
        m_painter = nullptr;
      }
    });
    daw->setResizeCallback([this](int /*w*/, int /*h*/) {
      // For showEvent (no pre-resize): painter is still alive, end it first.
      if (m_painter != nullptr) {
        delete m_painter;
        m_painter = nullptr;
      }
      m_surface = create_surface(m_drawing_area);
      m_painter = new Painter(m_surface);
      m_camera.update_widget(width(), height());
      redraw();
      if (m_animation_renderer != nullptr)
        m_animation_renderer->update_renderer(m_painter, m_surface);
    });
  }

  q_info("canvas::initialize successful.");
}

void canvas::redraw()
{
#ifdef EZGL_RHI
  if (m_rhi_widget) {
    using namespace std::placeholders;
    QColor bg(m_background_color.red,
               m_background_color.green,
               m_background_color.blue,
               m_background_color.alpha);

    if (!m_rhi_renderer) {
      // First draw: create the persistent renderer.
      m_rhi_renderer = std::make_unique<rhi_renderer>(
          m_rhi_widget,
          std::bind(&camera::world_to_screen, &m_camera, _1),
          &m_camera,
          m_draw_callback,
          bg);
    } else {
      // Subsequent draws: reset per-frame state and reuse GPU resources.
      m_rhi_renderer->begin_frame();
    }

    m_draw_callback(m_rhi_renderer.get());
    m_rhi_renderer->flush();  // uploads geometry + MVP, calls widget->update()
    m_rhi_defer_redraw = false;
    m_rhi_pending_redraw = false;
    m_rhi_pending_camera_only = false;
    m_rhi_has_drawn_frame = true;
    q_info("The canvas will be redrawn (RHI path).");
    return;
  }
#endif // EZGL_RHI

  // Clear the screen and set the background color
  m_painter->set_source_rgb(m_background_color.red / 255.0,
      m_background_color.green / 255.0,
      m_background_color.blue / 255.0);
  m_painter->paint();

  using namespace std::placeholders;
  deferred_renderer g(m_painter, std::bind(&camera::world_to_screen, &m_camera, _1), &m_camera, m_surface);
  m_draw_callback(&g);
  g.flush();

  m_drawing_area->update();

  q_info("The canvas will be redrawn.");
}

void canvas::redraw_camera_only()
{
#ifdef EZGL_RHI
  if (m_rhi_widget && m_rhi_renderer) {
    // Geometry is unchanged — reuse the cached GPU buffers and rebuild only
    // the overlay for the new camera transform.
    m_rhi_renderer->flush_mvp_only();
    m_rhi_pending_redraw = false;
    m_rhi_pending_camera_only = false;
    m_rhi_has_drawn_frame = true;
    q_info("The canvas overlay+MVP will be updated (camera-only RHI path).");
    return;
  }
#endif
  // No cached GPU geometry yet, or non-RHI path: fall back to full redraw.
  redraw();
}

renderer *canvas::create_animation_renderer()
{
  if(m_animation_renderer == nullptr) {
    using namespace std::placeholders;
    m_animation_renderer = new renderer(m_painter, std::bind(&camera::world_to_screen, &m_camera, _1), &m_camera, m_surface);
  }

  return m_animation_renderer;
}
} // namespace ezgl
