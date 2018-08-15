#ifndef EZGL_CAMERA_HPP
#define EZGL_CAMERA_HPP

#include <ezgl/rectangle.hpp>

namespace ezgl {

struct scale_transform {
  double x = 1.0;
  double y = 1.0;
};

class camera {
public:
  explicit camera(rectangle bounds);

  void update_screen(int width, int height);

  void update_view(rectangle view);

  point2d world_to_screen(point2d world_coordinates) const;

  rectangle world_to_screen(rectangle world_coordinates) const;

  rectangle view() const
  {
    return m_view;
  }

private:
  rectangle m_view;

  int m_screen_width = 0;
  int m_screen_height = 0;

  scale_transform m_world_to_screen;
};
}

#endif //EZGL_CAMERA_HPP
