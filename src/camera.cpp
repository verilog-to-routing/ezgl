#include "ezgl/camera.hpp"

namespace ezgl {

scale_transform
calculate_scale(int screen_width, double view_width, int screen_height, double view_height)
{
  scale_transform scale{};

  scale.x = screen_width / view_width;

  // Cartesian coordinates has y increasing from the origin, but Cairo is the opposite, so we use a negative height.
  scale.y = screen_height / -view_height;

  return scale;
}

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
  scale_transform const scale =
      calculate_scale(m_screen_width, view.width(), m_screen_height, view.height());

  // Maintain the aspect ratio by resizing the view.
  if(scale.x <= scale.y) {
    double const multiplier = scale.y / scale.x;
    double const height = view.height();

    double const y_top = view.top() - height * (multiplier - 1) * 0.5;
    double const y_bottom = view.bottom() + height * (multiplier - 1) * 0.5;

    m_view = rectangle({view.left(), y_bottom}, {view.right(), y_top});
  } else {
    double const multiplier = scale.x / scale.y;
    double const width = view.width();

    double const x_left = view.left() - width * (multiplier - 1) * 0.5;
    double const x_right = view.right() + width * (multiplier - 1) * 0.5;

    m_view = rectangle({x_left, view.bottom()}, {x_right, view.top()});
  }

  m_world_to_screen =
      calculate_scale(m_screen_width, m_view.width(), m_screen_height, m_view.height());
}
}