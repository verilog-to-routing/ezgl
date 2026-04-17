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

#ifndef EZGL_CANVAS_HPP
#define EZGL_CANVAS_HPP

#include "ezgl/camera.hpp"
#include "ezgl/rectangle.hpp"
#include "ezgl/graphics.hpp"
#include "ezgl/color.hpp"
#include "ezgl/qt/qtutils.hpp"
#include "ezgl/qt/render_backend.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace ezgl {

/**** Functions in this class are for ezgl internal use; application code doesn't need to call them ****/

/**
 * Responsible for creating, destroying, and maintaining the rendering context of a QWidget.
 *
 * Underneath, the class relies on a GtkDrawingArea as its GUI widget along with cairo to provide the rendering context.
 * The class connects to the relevant GTK signals, namely configure and draw events, to remain responsive.
 *
 * Each canvas is double-buffered. A draw callback (see: ezgl::draw_canvas_fn) is invoked each time the canvas needs to
 * be redrawn. This may be caused by the user (e.g., resizing the screen), but can also be forced by the programmer.
 */
class canvas {
public:
  /**
   * Destructor.
   */
  ~canvas() = default;

  /**
   * Get the name (identifier) of the canvas.
   */
  char const *id() const
  {
    return m_canvas_id.c_str();
  }

  /**
   * Get the width of the canvas in pixels.
   */
  int width() const;

  /**
   * Get the height of the canvas in pixels.
   */
  int height() const;

  /**
   * Force the canvas to redraw itself.
   *
   * This will invoke the ezgl::draw_canvas_fn callback and queue a redraw of the QWidget.
   */
  void redraw();

  /**
   * Redraw using only a camera (MVP) update — no geometry re-upload.
   *
   * On the RHI path this reuses the existing vertex buffers, rebuilds the
   * cached overlay for the new camera, and avoids re-running the draw callback.
   * Falls back to a full redraw on non-RHI paths or before the first frame.
   */
  void redraw_camera_only();

  /**
   * Get an immutable reference to this canvas' camera.
   */
  camera const &get_camera() const
  {
    return m_camera;
  }

  /**
   * Get a mutable reference to this canvas' camera.
   */
  camera &get_camera()
  {
    return m_camera;
  }

  /**
   * Set the rendering backend type. Must be called before application::run().
   */
  void set_renderer_type(renderer_type t)
  {
    m_renderer_type = t;
  }

  renderer_type get_renderer_type() const
  {
    return m_renderer_type;
  }

  /**
   * Register a callback invoked after each canvas::redraw() completes.
   * Receives the total CPU time of the redraw in milliseconds — for RHI this
   * includes both command recording and flush(); for QPainter backends it
   * covers the full draw callback execution.
   */
  void set_frame_timing_callback(std::function<void(double /*ms*/)> fn)
  {
    m_frame_timing_fn = std::move(fn);
  }

  /**
   * Create an animation renderer that can be used to draw on top of the current canvas
   */
  renderer *create_animation_renderer();

  /**
   * print_pdf, print_svg, and print_png generate a PDF, SVG, or PNG output file showing
   * all the graphical content of the current canvas.
   *
   * @param file_name   name of the output file
   * @return            returns true if the function has successfully generated the output file, otherwise
   *                    failed due to errors such as out of memory occurs.
   */
  bool print_pdf(const char *file_name, int width = 0, int height = 0);
  bool print_svg(const char *file_name, int width = 0, int height = 0);
  bool print_png(const char *file_name, int width = 0, int height = 0);

  /**
   * Run the draw callback on an offscreen surface of the given size without
   * saving any file. Use this to measure pure render time, separate from
   * PNG/PDF encoding overhead.
   */
  void draw_offscreen(int width, int height);

protected:
  // Only the ezgl::application can create and initialize a canvas object.
  friend class application;

  /**
   * Create a canvas that can be drawn to.
   */
  canvas(std::string canvas_id, draw_canvas_fn draw_callback, rectangle coordinate_system, color background_color);

  /**
   * Lazy initialization of the canvas class.
   *
   * This function is required because GTK will not send activate/startup signals to an ezgl::application until control
   * of the program has been reliquished. The GUI is not built until ezgl::application receives an activate signal.
   */
  void initialize(QWidget *drawing_area);

private:
  // Name of the canvas in XML.
  std::string m_canvas_id;

  // The function to call when the widget needs to be redrawn.
  draw_canvas_fn m_draw_callback;

  // The transformations between the GUI and the world.
  camera m_camera;

  // The background color of the drawing area
  color m_background_color;

  // A non-owning pointer to the drawing area inside a window.
  QWidget *m_drawing_area = nullptr;

  // Requested backend type — set before run(), used by initialize() to pick the backend.
  renderer_type m_renderer_type = renderer_type::rhi;

  // Optional post-redraw timing callback.
  std::function<void(double)> m_frame_timing_fn;

  // Active rendering backend — selected at initialize() time based on widget type.
  std::unique_ptr<render_backend> m_backend;

  // Renders the canvas into an off-screen QImage; shared by print_pdf/print_svg/print_png.
  QImage render_to_image(int surface_width, int surface_height);

  void begin_deferred_redraw_cycle();
  void end_deferred_redraw_cycle();
};
}

#endif //EZGL_CANVAS_HPP
