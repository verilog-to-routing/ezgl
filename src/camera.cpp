#include "ezgl/camera.hpp"

#include <cmath>
#include <cstdio>

namespace ezgl {

camera::camera(rectangle bounds) : m_view(bounds)
{
}

void camera::update_screen(int width, int height)
{
  m_screen_width = width;
  m_screen_height = height;

  // A change in the width/height will impact the view.
  update_view(m_view);
}

void camera::update_view(rectangle view)
{
  double x_scale = m_screen_width / view.width();
  double y_scale = m_screen_height / view.height();

  if(x_scale * view.height() > m_screen_height) {
    m_world_to_screen.x = y_scale;
    m_world_to_screen.y = -y_scale;
  } else {
    m_world_to_screen.x = x_scale;
    m_world_to_screen.y = -x_scale;
  }
}

point2d camera::world_to_screen(point2d world_coordinates) const
{
  double const x = (world_coordinates.x() - m_view.left()) * m_world_to_screen.x;
  double const y = (world_coordinates.y() - m_view.top()) * m_world_to_screen.y;

  return {x, y};
}

rectangle camera::world_to_screen(rectangle wc) const
{
  point2d const bottom_left = world_to_screen({wc.left(), wc.bottom()});
  point2d const top_right = world_to_screen({wc.right(), wc.top()});

  return {bottom_left, top_right};
}
}