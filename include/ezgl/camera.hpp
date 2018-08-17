#ifndef EZGL_CAMERA_HPP
#define EZGL_CAMERA_HPP

#include <ezgl/rectangle.hpp>

namespace ezgl {

/**
 * A class that manages how a world coordinate system is displayed on an ezgl::canvas.
 *
 * The camera class is maintained by the canvas object, which contains a GTK widget. The widget has its own dimensions,
 * and its aspect ratio may not match the world coordinate system. The camera maintains a "screen" within the widget
 * that keeps the same aspect ratio as the world coordinate system, regardless of the dimensions of the widget.
 */
class camera {
public:
  /**
   * Convert a point in world coordinates to screen coordinates.
   */
  point2d world_to_screen(point2d world_coordinates) const;

  /**
   * Convert a point in widget coordinates to screen coordinates.
   */
  point2d widget_to_screen(point2d widget_coordinates) const;

  /**
   * Convert a point in widget coordinates to world coordinates.
   */
  point2d widget_to_world(point2d widget_coordinates) const;

protected:
  // Only an ezgl::canvas can create a camera.
  friend class canvas;

  /**
   * Create a camera.
   *
   * @param bounds The initial bounds of the coordinate system.
   */
  explicit camera(rectangle bounds);

  /**
   * Update the dimensions of the widget.
   *
   * This will change the screen where the world is projected. The screen will maintain the aspect ratio of the world's
   * coordinate system while being centered within the screen.
   *
   * @see canvas::configure_event
   */
  void update_widget(int width, int height);

  /**
   * Update the scaling factors.
   */
  void update_scale_factors();

private:
  // The dimensions of the parent widget.
  rectangle m_widget = {{0, 0}, 1.0, 1.0};

  // The dimensions of the world (user-defined bounding box).
  rectangle m_world;

  // The dimensions of the screen, which may not match the widget.
  rectangle m_screen;

  // The x and y scaling factors.
  point2d m_world_to_widget = {1.0, 1.0};
  point2d m_widget_to_screen = {1.0, 1.0};
  point2d m_screen_to_world = {1.0, 1.0};
};
}

#endif //EZGL_CAMERA_HPP
