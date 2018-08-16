#include "ezgl/camera.hpp"

#include <cmath>
#include <cstdio>

namespace ezgl {

rectangle maintain_aspect_ratio(rectangle const &view, double screen_width, double screen_height)
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

camera::camera(rectangle bounds) : m_world(bounds), m_view(bounds)
{
}

point2d camera::world_to_screen(point2d world_coordinates) const
{
  point2d const world_origin(m_world.left(), m_world.bottom());

  // Project the world coordinates to screen coordinates.
  point2d screen_coordinates = (world_coordinates - world_origin) * m_scale;

  // Translate the screen coordinates so that they fit within the view. Note that cairo uses a flipped y-axis.
  screen_coordinates += point2d{m_view.left(), -m_view.top()};
  screen_coordinates.y = -screen_coordinates.y;

  return screen_coordinates;
}

point2d camera::screen_to_world(point2d screen_coordinates) const
{
  point2d const view_origin(m_view.left(), m_view.bottom());

  // Project the screen coordinates to the world coordinates.
  point2d world_coordinates = (screen_coordinates - view_origin) * m_inverse_scale;

  // Translate the world coordinates -- needs to match what we did in world_to_screen.
  world_coordinates += point2d{m_world.left(), -m_world.top()};
  world_coordinates.y = -world_coordinates.y;

  return world_coordinates;
}

void camera::update_screen(int width, int height)
{
  m_screen = rectangle{{0, 0}, static_cast<double>(width), static_cast<double>(height)};

  m_view = maintain_aspect_ratio(m_view, m_screen.width(), m_screen.height());
  update_scale_factor(m_view, m_world);
}

void camera::update_scale_factor(rectangle view, rectangle world)
{
  m_scale.x = view.width() / world.width();
  m_scale.y = view.height() / world.height();

  m_inverse_scale.x = 1 / m_scale.x;
  m_inverse_scale.y = 1 / m_scale.y;
}
}