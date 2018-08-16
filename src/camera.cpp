#include "ezgl/camera.hpp"

#include <cmath>
#include <cstdio>

namespace ezgl {

rectangle maintain_aspect_ratio(rectangle const &view, int screen_width, int screen_height)
{
  double const x_scale = screen_width / view.width();
  double const y_scale = screen_height / view.height();

  double x_start = 0.0;
  double y_start = 0.0;
  double new_width;
  double new_height;

  if(x_scale * view.height() > screen_height) {
    // Using x_scale causes the view to be larger than the screen's height.

    // Keep the same height as the screen.
    new_height = screen_height;
    // Scale the width to maintain the aspect ratio.
    new_width = view.width() * y_scale;
    // Keep the view in the centre of the screen.
    x_start = 0.5 * std::fabs(screen_width - new_width);
  } else {
    // Using x_scale keeps the view within the screen's height.

    // Keep the width the same as the screen.
    new_width = screen_width;
    // Scale the height to maintain the aspect ratio.
    new_height = view.height() * x_scale;
    // Keep the view in the centre of the screen.
    y_start = 0.5 * std::fabs(screen_height - new_height);
  }

  return {{x_start, y_start}, new_width, new_height};
}

camera::camera(rectangle bounds) : m_coordinate_system(bounds), m_view(bounds)
{
}

void camera::set_visible_world(rectangle coordinate_system)
{
  m_coordinate_system = coordinate_system;
  m_view = coordinate_system;

  update_view(m_view);
}

point2d camera::world_to_screen(point2d world_coordinates) const
{
  double const x = world_coordinates.x() * m_x_scale + m_view.left();
  double const y = world_coordinates.y() * m_y_scale - m_view.top();

  return {x, -y};
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
  m_view = maintain_aspect_ratio(view, m_screen_width, m_screen_height);

  m_x_scale = m_view.width() / m_coordinate_system.width();
  m_y_scale = m_view.height() / m_coordinate_system.height();
}
}