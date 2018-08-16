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

  point2d screen_to_world(point2d screen_coordinates) const;

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
   * This will change the view where the world is projected. The view will maintain the aspect ratio of the world's
   * coordinate system while being centered within the screen.
   *
   * @see canvas::configure_event
   */
  void update_screen(int width, int height);

  /**
   * Update the scaling factors.
   */
  void update_scale_factor(rectangle view, rectangle world);

private:
  // The dimensions of the parent widget.
  rectangle m_screen = {{0, 0}, 1.0, 1.0};

  // The dimensions of the world (user-defined bounding box).
  rectangle m_world;

  // The dimensions of the view.
  rectangle m_view;

  // The x and y scaling factors.
  point2d m_scale = {1.0, 1.0};
  point2d m_inverse_scale = {1.0, 1.0};
};
}

#endif //EZGL_CAMERA_HPP
