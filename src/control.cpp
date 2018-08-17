#include "ezgl/control.hpp"

#include "ezgl/camera.hpp"
#include "ezgl/canvas.hpp"

namespace ezgl {

rectangle zoom_in_world(point2d zoom_point, rectangle world, double zoom_factor)
{
  double const left = zoom_point.x - (zoom_point.x - world.left()) / zoom_factor;
  double const bottom = zoom_point.y + (world.bottom() - zoom_point.y) / zoom_factor;

  double const right = zoom_point.x + (world.right() - zoom_point.x) / zoom_factor;
  double const top = zoom_point.y - (zoom_point.y - world.top()) / zoom_factor;

  return {{left, bottom}, {right, top}};
}

rectangle zoom_out_world(point2d zoom_point, rectangle world, double zoom_factor)
{
  double const left = zoom_point.x - (zoom_point.x - world.left()) * zoom_factor;
  double const bottom = zoom_point.y + (world.bottom() - zoom_point.y) * zoom_factor;

  double const right = zoom_point.x + (world.right() - zoom_point.x) * zoom_factor;
  double const top = zoom_point.y - (zoom_point.y - world.top()) * zoom_factor;

  return {{left, bottom}, {right, top}};
}

void zoom_in(canvas *cnv, double zoom_factor)
{
  point2d const zoom_point = cnv->get_camera().get_world().centre();
  rectangle const world = cnv->get_camera().get_world();

  cnv->get_camera().set_world(zoom_in_world(zoom_point, world, zoom_factor));
  cnv->redraw();
}

void zoom_in(canvas *cnv, point2d zoom_point, double zoom_factor)
{
  zoom_point = cnv->get_camera().widget_to_world(zoom_point);
  rectangle const world = cnv->get_camera().get_world();

  cnv->get_camera().set_world(zoom_in_world(zoom_point, world, zoom_factor));
  cnv->redraw();
}

void zoom_out(canvas *cnv, double zoom_factor)
{
  point2d const zoom_point = cnv->get_camera().get_world().centre();
  rectangle const world = cnv->get_camera().get_world();

  cnv->get_camera().set_world(zoom_out_world(zoom_point, world, zoom_factor));
  cnv->redraw();
}

void zoom_out(canvas *cnv, point2d zoom_point, double zoom_factor)
{
  zoom_point = cnv->get_camera().widget_to_world(zoom_point);
  rectangle const world = cnv->get_camera().get_world();

  cnv->get_camera().set_world(zoom_out_world(zoom_point, world, zoom_factor));
  cnv->redraw();
}

void zoom_fit(canvas *cnv, rectangle new_world)
{
  cnv->get_camera().set_world(new_world);
  cnv->redraw();
}
}