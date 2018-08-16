#ifndef EZGL_CAMERA_HPP
#define EZGL_CAMERA_HPP

#include <ezgl/rectangle.hpp>

namespace ezgl {

class camera {
public:
  void update_screen(int width, int height);

  void update_view(rectangle view);

  point2d world_to_screen(point2d world_coordinates) const;

  rectangle world_to_screen(rectangle world_coordinates) const;

protected:
  // Only an ezgl::canvas can create a camera.
  friend class canvas;

  /**
   * Create a camera.
   *
   * @param bounds The initial bounds of the coordinate system.
   */
  explicit camera(rectangle bounds);

private:
  int m_screen_width = 0;
  int m_screen_height = 0;

  rectangle m_coordinate_system;
  rectangle m_view;

  double m_x_scale = 1.0;
  double m_y_scale = 1.0;
};
}

#endif //EZGL_CAMERA_HPP
