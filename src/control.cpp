#include "ezgl/control.hpp"

#include "ezgl/camera.hpp"
#include "ezgl/canvas.hpp"

namespace ezgl {

void zoom_in(canvas *cnv, point2d zoom_point, double zoom_factor)
{
  zoom_point = cnv->get_camera().widget_to_world(zoom_point);
  rectangle world = cnv->get_camera().get_world();

  double const left = zoom_point.x - (zoom_point.x - world.left()) / zoom_factor;
  double const bottom = zoom_point.y + (world.bottom() - zoom_point.y) / zoom_factor;

  double const right = zoom_point.x + (world.right() - zoom_point.x) / zoom_factor;
  double const top = zoom_point.y - (zoom_point.y - world.top()) / zoom_factor;

  cnv->get_camera().set_world({{left, bottom}, {right, top}});
  cnv->redraw();
}

void zoom_out(canvas *cnv, point2d zoom_point, double zoom_factor)
{
  zoom_point = cnv->get_camera().widget_to_world(zoom_point);
  rectangle world = cnv->get_camera().get_world();

  double const left = zoom_point.x - (zoom_point.x - world.left()) * zoom_factor;
  double const bottom = zoom_point.y + (world.bottom() - zoom_point.y) * zoom_factor;

  double const right = zoom_point.x + (world.right() - zoom_point.x) * zoom_factor;
  double const top = zoom_point.y - (zoom_point.y - world.top()) * zoom_factor;

  cnv->get_camera().set_world({{left, bottom}, {right, top}});
  cnv->redraw();
}

void zoom_fit(canvas *cnv, rectangle new_world)
{
  cnv->get_camera().set_world(new_world);
  cnv->redraw();
}
}