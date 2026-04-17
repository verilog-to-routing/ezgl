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

#ifndef EZGL_GRAPHICS_HPP
#define EZGL_GRAPHICS_HPP

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/renderer_base.hpp"

#include <cfloat>
#include <string>
#include <vector>

namespace ezgl {

/**
 * Immediate-mode QPainter renderer.
 *
 * Implements the full irenderer interface by executing every draw call
 * synchronously against the active QPainter.  Retained for animation
 * overlays and unit testing; not used as a base class for any other renderer.
 */
class immediate_renderer final : public irenderer, protected RendererBase {
public:
    // ---- irenderer: coordinate system / viewport ----------------------------

    void set_coordinate_system(t_coordinate_system cs) override;
    void set_visible_world(rectangle new_world) override;
    rectangle get_visible_world() override;
    rectangle get_visible_screen() const override;
    rectangle world_to_screen(const rectangle& box) override;

    // ---- irenderer: state setters -------------------------------------------

    void set_color(color new_color) override;
    void set_color(color new_color, uint_fast8_t alpha) override;
    void set_color(uint_fast8_t red, uint_fast8_t green, uint_fast8_t blue,
                   uint_fast8_t alpha = 255) override;
    void set_line_cap(line_cap cap) override;
    void set_line_dash(line_dash dash) override;
    void set_line_width(int width) override;
    void set_font_size(double new_size) override;
    void format_font(std::string const& family, font_slant slant,
                     font_weight weight) override;
    void format_font(std::string const& family, font_slant slant,
                     font_weight weight, double new_size) override;
    void set_text_rotation(double degrees) override;
    void set_horiz_justification(justification horiz_just) override;
    void set_vert_justification(justification vert_just) override;

    // ---- irenderer: draw calls ----------------------------------------------

    void draw_line(const point2d& start, const point2d& end) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void fill_poly(std::vector<point2d> const& points) override;

    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;

    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;

    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;

    // ---- Update painter after surface resize --------------------------------
    void update_renderer(Painter* painter, QImage* surface);

protected:
    friend class canvas;

    using transform_fn = RendererBase::transform_fn;

    immediate_renderer(Painter* painter, transform_fn transform,
                       camera* cam, QImage* surface);
};

} // namespace ezgl

#endif // EZGL_GRAPHICS_HPP
