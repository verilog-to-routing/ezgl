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

  /**
   * Update the world coordinate system of the camera.
   *
   * Your coordinate system's aspect ratio (i.e., width to height ratio) will be maintained according to the size of
   * the GTK widget.
   *
   * If you call this function, it will reset the camera's view (undoing any previous transformations, like zooming).
   */
  void set_visible_world(rectangle coordinate_system);

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
  rectangle m_screen;
  rectangle m_coordinate_system;
  rectangle m_view;

  point2d m_scale;

private:
  void update_view(rectangle view);
};
}

#endif //EZGL_CAMERA_HPP
