#ifndef EZGL_CAMERA_HPP
#define EZGL_CAMERA_HPP

#include <ezgl/rectangle.hpp>

namespace ezgl {

/**
 * A class that manages how a world coordinate system is displayed on an ezgl::canvas.
 *
 * The camera class is maintained by the canvas object, which contains a GTK widget. The widget, in this case, is
 * considered to be the "screen". The widget may change in dimensions, in which case it is the canvas object's job to
 * update the camera.
 */
class camera {
public:
  /**
   * Convert a point in world coordinates to screen coordinates.
   */
  point2d world_to_screen(point2d world_coordinates) const;

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
   * @see canvas::configure_event
   */
  void update_screen(int width, int height);

private:
  int m_screen_width = 0;
  int m_screen_height = 0;

  rectangle m_coordinate_system;
  rectangle m_view;

  double m_x_scale = 1.0;
  double m_y_scale = 1.0;

private:
  void update_view(rectangle view);
};
}

#endif //EZGL_CAMERA_HPP
