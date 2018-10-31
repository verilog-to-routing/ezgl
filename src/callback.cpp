#include "ezgl/callback.hpp"

namespace ezgl {

// File wide static variables to track whether the middle mouse
// button is currently pressed AND the old x and y positions of the mouse pointer
bool middle_mouse_button_pressed = false;
double prev_x = 0, prev_y = 0;

gboolean press_key(GtkWidget *, GdkEventKey *event, gpointer)
{
  // see: https://developer.gnome.org/gdk3/stable/gdk3-Keyboard-Handling.html
  std::cout << gdk_keyval_name(event->keyval) << " was pressed.\n";

  return FALSE; // propagate the event
}

gboolean press_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_BUTTON_PRESS) {
    std::cout << "Click (widget): " << event->x << ", " << event->y << "\n";

    ezgl::point2d const widget_coordinates(event->x, event->y);

    std::string main_canvas_id = application->get_main_canvas_id();
    ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

    ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
    std::cout << "Click (world): " << world.x << ", " << world.y << "\n";

    if(event->button == 1) {  // Left mouse click
      ezgl::zoom_in(canvas, widget_coordinates, 5.0 / 3.0);
    } else if(event->button == 2) {  // Middle mouse click
      middle_mouse_button_pressed = true;
      prev_x = event->x;
      prev_y = event->y;
    } else if(event->button == 3) {  // Right mouse click
      ezgl::zoom_out(canvas, widget_coordinates, 5.0 / 3.0);
    }
  }

  return TRUE; // consume the event
}

gboolean release_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_BUTTON_RELEASE) {
    if(event->button == 2) {  // Middle mouse release
      middle_mouse_button_pressed = false;
    }
  }

  return TRUE; // consume the event
}

gboolean move_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_MOTION_NOTIFY) {
    if(middle_mouse_button_pressed) {
      GdkEventMotion *motion_event = (GdkEventMotion *)event;

      std::string main_canvas_id = application->get_main_canvas_id();
      auto canvas = application->get_canvas(main_canvas_id);

      double dx = motion_event->x - prev_x;
      double dy = motion_event->y - prev_y;

      prev_x = motion_event->x;
      prev_y = motion_event->y;

      // Flip the delta x to avoid inverted dragging
      translate(canvas, -dx, dy);
    }
  }

  return TRUE; // consume the event
}

gboolean scroll_mouse(GtkWidget *widget, GdkEvent *event, gpointer data) {

  if(event->type == GDK_SCROLL) {
    auto application = static_cast<ezgl::application *>(data);

    std::string main_canvas_id = application->get_main_canvas_id();
    auto canvas = application->get_canvas(main_canvas_id);

    GdkEventScroll *scroll_event = (GdkEventScroll *)event;

    ezgl::point2d scroll_point(scroll_event->x, scroll_event->y);

    if(scroll_event->direction == GDK_SCROLL_UP) {
      // Zoom in at the scroll point
      ezgl::zoom_in(canvas, scroll_point, 5.0 / 3.0);
    } else if(scroll_event->direction == GDK_SCROLL_DOWN) {
      // Zoom out at the scroll point
      ezgl::zoom_out(canvas, scroll_point, 5.0 / 3.0);
    } else if(scroll_event->direction == GDK_SCROLL_SMOOTH) {
      // Doesn't seem to be happening
    }  // NOTE: We ignore scroll GDK_SCROLL_LEFT and GDK_SCROLL_RIGHT
  }
  return TRUE;
}

gboolean press_zoom_fit(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_fit(canvas, canvas->get_camera().get_initial_world());

  return TRUE;
}

gboolean press_zoom_in(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_in(canvas, 5.0 / 3.0);

  return TRUE;
}

gboolean press_zoom_out(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_out(canvas, 5.0 / 3.0);

  return TRUE;
}

gboolean press_up(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_up(canvas, 5.0);

  return TRUE;
}

gboolean press_down(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_down(canvas, 5.0);

  return TRUE;
}

gboolean press_left(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_left(canvas, 5.0);

  return TRUE;
}

gboolean press_right(GtkWidget *widget, GdkEvent *event, gpointer data) {

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_right(canvas, 5.0);

  return TRUE;
}

}
