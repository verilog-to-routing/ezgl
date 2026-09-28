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
    // irenderer interface — each call paints synchronously to the active
    // QPainter (no recording or batching). Semantics otherwise match the
    // irenderer base declarations.

    /// @name Paint a line immediately to the QPainter.
    /// @{
    void draw_line(const point2d& start, const point2d& end) override;
    /// @}

    /// @name Paint a rectangle outline immediately to the QPainter.
    /// @{
    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;
    /// @}

    /// @name Paint a filled rectangle immediately to the QPainter.
    /// @{
    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;
    /// @}

    /// @name Paint a filled polygon immediately to the QPainter.
    /// @{
    void fill_poly(const std::vector<point2d>& points) override;
    /// @}
    /// @name Paint a filled triangle immediately to the QPainter.
    /// @{
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    /// @}

    /// @name Paint an elliptic-arc outline immediately to the QPainter.
    /// @{
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    /// @}
    /// @name Paint a circular-arc outline immediately to the QPainter.
    /// @{
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    /// @}
    /// @name Paint a filled elliptic arc immediately to the QPainter.
    /// @{
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    /// @}
    /// @name Paint a filled circular arc immediately to the QPainter.
    /// @{
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    /// @}

    /// @name Paint a text label immediately to the QPainter.
    /// @{
    void draw_text(const point2d& point, const std::string& text) override;
    void draw_text(const point2d& point, const std::string& text,
                   double bound_x, double bound_y) override;
    /// @}

    /// @name Paint a surface (image) blit immediately to the QPainter.
    /// @{
    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;
    /// @}

    /// Construct an immediate renderer over the given painter and camera.
    immediate_renderer(Painter* painter, camera* cam);
};

} // namespace ezgl
