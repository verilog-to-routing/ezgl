#pragma once

#include "ezgl/irenderer.hpp"

#include <string>
#include <vector>

namespace ezgl {

/**
 * Immediate-mode QPainter renderer.
 *
 * Implements the full irenderer draw interface by executing every draw call
 * synchronously against the active QPainter.
 */
class immediate_renderer final : public irenderer {
public:
    void draw_line(const point2d& start, const point2d& end) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void fill_poly(const std::vector<point2d>& points) override;

    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;

    void draw_text(const point2d& point, const std::string& text) override;
    void draw_text(const point2d& point, const std::string& text,
                   double bound_x, double bound_y) override;

    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;

    void update_renderer(Painter* painter, QImage* surface);

    immediate_renderer(Painter* painter, transform_fn transform,
                       camera* cam, QImage* surface);
};

} // namespace ezgl
