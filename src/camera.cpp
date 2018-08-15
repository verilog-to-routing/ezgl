#include "ezgl/camera.hpp"

namespace ezgl {

scale_transform
calculate_scale(int screen_width, double view_width, int screen_height, double view_height)
{
  scale_transform scale{};

  scale.x = screen_width / view_width;
  scale.y = screen_height / view_height;

  return scale;
}

camera::camera(rectangle bounds) : m_bounds(bounds), m_view(bounds)
{
}

void camera::update_screen(int width, int height)
{
  m_screen_width = width;
  m_screen_height = height;
}

void camera::update_bounds(rectangle bounds)
{
  m_bounds = bounds;
  m_view = bounds;

  // Reset scaling to 1.0 for both x and y.
  m_world_to_screen = scale_transform{};
}

void camera::update_view(rectangle view)
{
  scale_transform const scale =
      calculate_scale(m_screen_width, view.width(), m_screen_height, view.height());

  // Maintain the aspect ratio by resizing the view.
  if(scale.x <= scale.y) {
    double const multiplier = scale.y / scale.x;

    double const y_top = view.top() - (view.bottom() - view.top()) * (multiplier - 1) * 0.5;
    double const y_bottom = view.bottom() + (view.bottom() - view.top()) * (multiplier - 1) * 0.5;

    m_view = rectangle({view.left(), y_bottom}, {view.right(), y_top});
  } else {
    double const multiplier = scale.x / scale.y;

    double const x_left = view.left() - (view.right() - view.left()) * (multiplier - 1) * 0.5;
    double const x_right = view.right() + (view.right() - view.left()) * (multiplier - 1) * 0.5;

    m_view = rectangle({x_left, view.bottom()}, {x_right, view.top()});
  }

  m_world_to_screen =
      calculate_scale(m_screen_width, m_view.width(), m_screen_height, m_view.height());
}
}