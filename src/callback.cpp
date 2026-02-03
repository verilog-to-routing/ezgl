/*
 * Copyright 2019-2022 University of Toronto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors: Mario Badr, Sameh Attia, Tanner Young-Schultz and Vaughn Betz
 */

#include "ezgl/callback.hpp"

#ifdef EZGL_QT
#include "ezgl/qt/_qtcompat.hpp"
#endif

namespace ezgl {

/**
 * Provides file wide variables to support mouse panning. We store some 
 * state about mouse panning so we can determine when click & drag mouse
 * panning (handled by ezgl) is happening vs. simple mouse clicks (sent to
 * user mouse click callback).
 */
struct mouse_pan {
  /**
   * Tracks whether the mouse button used for panning is currently pressed
   */
  bool panning_mouse_button_pressed = false;
  /**
   * Holds the timestamp of the last panning event
   */
  int last_panning_event_time = 0;
  /**
   * The old x and y positions of the mouse pointer, in the previous pan 
   * event.
   */
  double prev_x = 0;
  double prev_y = 0;

  /* Has any panning happened since the mouse button was held down?
   */
  bool has_panned = false; 
} g_mouse_pan;

#ifdef EZGL_QT
bool press_key(QWidget*, QKeyEvent* event, void* data)
{
  auto application = static_cast<ezgl::application*>(data);

  // Call the user-defined key press callback if defined
  if(application->key_press_callback != nullptr) {
    QString keyName = QKeySequence(event->key()).toString();
    application->key_press_callback(application, event, keyName.toStdString().c_str());
  }

  // Returning FALSE to indicate this event should be propagated on to other
  // gtk widgets. This is important since we're grabbing keyboard events
  // for the whole main window. It can have unexpected effects though, such
  // as Enter/Space being treated as press any highlighted button.
  // return TRUE (event consumed) if you want to avoid that, and don't have
  // any widgets that need keyboard events.
  return false;
}
#else // EZGL_QT
gboolean press_key(GtkWidget *, GdkEventKey *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  // Call the user-defined key press callback if defined
  if(application->key_press_callback != nullptr) {
    // see: https://developer.gnome.org/gdk3/stable/gdk3-Keyboard-Handling.html
    application->key_press_callback(application, event, gdk_keyval_name(event->keyval));
  }

  // Returning FALSE to indicate this event should be propagated on to other
  // gtk widgets. This is important since we're grabbing keyboard events
  // for the whole main window. It can have unexpected effects though, such
  // as Enter/Space being treated as press any highlighted button.
  // return TRUE (event consumed) if you want to avoid that, and don't have
  // any widgets that need keyboard events.
  return FALSE;
}
#endif // EZGL_QT

#ifdef EZGL_QT
bool press_mouse(QWidget*, QMouseEvent* event, void* data)
{
  auto application = static_cast<ezgl::application *>(data);
  const QPointF pos = event->position();

  if(event->type() == QEvent::MouseButtonPress) {

    // Check for mouse press to support dragging.
    if(event->button() == PANNING_MOUSE_BUTTON) {
      g_mouse_pan.panning_mouse_button_pressed = true;
      g_mouse_pan.prev_x = pos.x();
      g_mouse_pan.prev_y = pos.y();
      g_mouse_pan.has_panned = false;  /* Haven't shifted the view yet */
    }
    // Call the user-defined mouse press callback if defined
    // The user-defined callback is called for mouse buttons other than
    // the PANNING_MOUSE_BUTTON button. If the user pressed the PANNING_MOUSE_BUTTON button,
    // the callback will be called at mouse release only if no panning occurs
    else if(application->mouse_press_callback != nullptr) {
      ezgl::point2d const widget_coordinates(pos.x(), pos.y());

      std::string main_canvas_id = application->get_main_canvas_id();
      ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

      ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
      application->mouse_press_callback(application, event, world.x, world.y);
    }
  }
  event->accept();
  return true; // consume the event
}
#else // EZGL_QT
gboolean press_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_BUTTON_PRESS) {

    // Check for mouse press to support dragging. 
    if(event->button == PANNING_MOUSE_BUTTON) {
      g_mouse_pan.panning_mouse_button_pressed = true;
      g_mouse_pan.prev_x = event->x;
      g_mouse_pan.prev_y = event->y;
      g_mouse_pan.has_panned = false;  /* Haven't shifted the view yet */
    }
    // Call the user-defined mouse press callback if defined
    // The user-defined callback is called for mouse buttons other than
    // the PANNING_MOUSE_BUTTON button. If the user pressed the PANNING_MOUSE_BUTTON button,
    // the callback will be called at mouse release only if no panning occurs
    else if(application->mouse_press_callback != nullptr) {
      ezgl::point2d const widget_coordinates(event->x, event->y);

      std::string main_canvas_id = application->get_main_canvas_id();
      ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

      ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
      application->mouse_press_callback(application, event, world.x, world.y);
    }
  }

  return TRUE; // consume the event
}
#endif // EZGL_QT

#ifdef EZGL_QT
bool release_mouse(GtkWidget*, QMouseEvent* event, void* data)
{
  auto application = static_cast<ezgl::application*>(data);
  const QPointF pos = event->position();

  if(event->type() == QEvent::MouseButtonRelease) {
    // Check for mouse release to support dragging
    if(event->button() == PANNING_MOUSE_BUTTON) {
      g_mouse_pan.panning_mouse_button_pressed = false;

      // Call the user-defined mouse press callback for the PANNING_MOUSE_BUTTON button only if no panning occurs.
      // This lets the user use one mouse button for both click-and-drag
      // panning and simple clicking.
      if (!g_mouse_pan.has_panned && application->mouse_press_callback != nullptr) {
        ezgl::point2d const widget_coordinates(pos.x(), pos.y());

        std::string main_canvas_id = application->get_main_canvas_id();
        ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

        ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
        application->mouse_press_callback(application, event, world.x, world.y);
      }
      g_mouse_pan.has_panned = false;  /* Done pan; reset for next time */
    }
  }
  event->accept();
  return true; // consume the event
}
#else // EZGL_QT
gboolean release_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_BUTTON_RELEASE) {
    // Check for mouse release to support dragging
    if(event->button == PANNING_MOUSE_BUTTON) {
      g_mouse_pan.panning_mouse_button_pressed = false;

      // Call the user-defined mouse press callback for the PANNING_MOUSE_BUTTON button only if no panning occurs. 
      // This lets the user use one mouse button for both click-and-drag 
      // panning and simple clicking.
      if (!g_mouse_pan.has_panned && application->mouse_press_callback != nullptr) {
        ezgl::point2d const widget_coordinates(event->x, event->y);

        std::string main_canvas_id = application->get_main_canvas_id();
        ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

        ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
        application->mouse_press_callback(application, event, world.x, world.y);
      }
      g_mouse_pan.has_panned = false;  /* Done pan; reset for next time */
    }
  }

  return TRUE; // consume the event
}
#endif // EZGL_QT

#ifdef EZGL_QT
bool move_mouse(QWidget*, QMouseEvent* event, void* data)
{
  auto application = static_cast<ezgl::application *>(data);
  const QPointF pos = event->position();

  if(event->type() == QEvent::MouseMove) {

    // Check if the mouse button is pressed to support dragging
    if(g_mouse_pan.panning_mouse_button_pressed) {
      // Code below drops a panning event if we served anothe one
      // less than 100 ms. I believe it was intended to avoid having panning
      // fall behind and queue up many events if redraws were slow. However,
      // it is not necessary on the UG machines (debian) in person, or over
      // VNC or on a VM and it has the bad effect of limiting refresh to 10 Hz.
      // Commenting it out for now and will delete if there
      // are no reported issues. - VB
      // if(event->timestamp() - g_mouse_pan.last_panning_event_time < 100)
      // return true;

      g_mouse_pan.last_panning_event_time = event->timestamp();

      std::string main_canvas_id = application->get_main_canvas_id();
      auto canvas = application->get_canvas(main_canvas_id);

      point2d curr_trans = canvas->get_camera().widget_to_world({pos.x(), pos.y()});
      point2d prev_trans = canvas->get_camera().widget_to_world({g_mouse_pan.prev_x, g_mouse_pan.prev_y});

      double dx = curr_trans.x - prev_trans.x;
      double dy = curr_trans.y - prev_trans.y;

      g_mouse_pan.prev_x = pos.x();
      g_mouse_pan.prev_y = pos.y();

      // Flip the delta x to avoid inverted dragging
      translate(canvas, -dx, -dy);
      g_mouse_pan.has_panned = true;
    }
    // Else call the user-defined mouse move callback if defined
    else if(application->mouse_move_callback != nullptr) {
      ezgl::point2d const widget_coordinates(pos.x(), pos.y());

      std::string main_canvas_id = application->get_main_canvas_id();
      ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

      ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
      application->mouse_move_callback(application, event, world.x, world.y);
    }
  }
  event->accept();
  return true; // consume the event
}
#else // EZGL_QT
gboolean move_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_MOTION_NOTIFY) {

    // Check if the mouse button is pressed to support dragging
    if(g_mouse_pan.panning_mouse_button_pressed) {
      // Code below drops a panning event if we served anothe one 
      // less than 100 ms. I believe it was intended to avoid having panning
      // fall behind and queue up many events if redraws were slow. However,
      // it is not necessary on the UG machines (debian) in person, or over 
      // VNC or on a VM and it has the bad effect of limiting refresh to 10 Hz.
      // Commenting it out for now and will delete if there 
      // are no reported issues. - VB
      // if(gtk_get_current_event_time() - g_mouse_pan.last_panning_event_time < 100)
       // return true;

      g_mouse_pan.last_panning_event_time = gtk_get_current_event_time();

      GdkEventMotion *motion_event = (GdkEventMotion *)event;

      std::string main_canvas_id = application->get_main_canvas_id();
      auto canvas = application->get_canvas(main_canvas_id);

      point2d curr_trans = canvas->get_camera().widget_to_world({motion_event->x, motion_event->y});
      point2d prev_trans = canvas->get_camera().widget_to_world({g_mouse_pan.prev_x, g_mouse_pan.prev_y});

      double dx = curr_trans.x - prev_trans.x;
      double dy = curr_trans.y - prev_trans.y;

      g_mouse_pan.prev_x = motion_event->x;
      g_mouse_pan.prev_y = motion_event->y;

      // Flip the delta x to avoid inverted dragging
      translate(canvas, -dx, -dy);
      g_mouse_pan.has_panned = true;
    }
    // Else call the user-defined mouse move callback if defined
    else if(application->mouse_move_callback != nullptr) {
      ezgl::point2d const widget_coordinates(event->x, event->y);

      std::string main_canvas_id = application->get_main_canvas_id();
      ezgl::canvas *canvas = application->get_canvas(main_canvas_id);

      ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
      application->mouse_move_callback(application, event, world.x, world.y);
    }
  }

  return TRUE; // consume the event
}
#endif // EZGL_QT

#ifdef EZGL_QT
bool scroll_mouse(QWidget*, QWheelEvent* event, void* data)
{
  auto application = static_cast<ezgl::application*>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  const QPointF pos = event->position();

  ezgl::point2d scroll_point(pos.x(), pos.y());

  const QPoint angle = event->angleDelta();
  const QPoint pixel = event->pixelDelta();

  constexpr double zoomFactor = 5.0 / 3.0;

  if (!angle.isNull()) {
    // Standard wheel mouse: sign of angle.y()
    if (angle.y() > 0) {
      ezgl::zoom_in(canvas, scroll_point, zoomFactor);
    } else if (angle.y() < 0) {
      ezgl::zoom_out(canvas, scroll_point, zoomFactor);
    }
    // ignore horizontal: angle.x()
  } else if (!pixel.isNull()) {
    // Smooth scrolling (trackpad). GTK would call this GDK_SCROLL_SMOOTH.
    // Decide direction by pixel.y()
    if (pixel.y() > 0) {
      ezgl::zoom_in(canvas, scroll_point, zoomFactor);
    } else if (pixel.y() < 0) {
      ezgl::zoom_out(canvas, scroll_point, zoomFactor);
    }
  }
  event->accept();
  return true;
}
#else // EZGL_QT
gboolean scroll_mouse(GtkWidget *, GdkEvent *event, gpointer data)
{

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
    } // NOTE: We ignore scroll GDK_SCROLL_LEFT and GDK_SCROLL_RIGHT
  }
  return TRUE;
}
#endif // EZGL_QT

gboolean press_zoom_fit(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_fit(canvas, canvas->get_camera().get_initial_world());

  return TRUE;
}

gboolean press_zoom_in(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_in(canvas, 5.0 / 3.0);

  return TRUE;
}

gboolean press_zoom_out(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::zoom_out(canvas, 5.0 / 3.0);

  return TRUE;
}

gboolean press_up(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_up(canvas, 5.0);

  return TRUE;
}

gboolean press_down(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_down(canvas, 5.0);

  return TRUE;
}

gboolean press_left(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_left(canvas, 5.0);

  return TRUE;
}

gboolean press_right(GtkWidget *, gpointer data)
{

  auto application = static_cast<ezgl::application *>(data);

  std::string main_canvas_id = application->get_main_canvas_id();
  auto canvas = application->get_canvas(main_canvas_id);

  ezgl::translate_right(canvas, 5.0);

  return TRUE;
}

gboolean press_proceed(GtkWidget *, gpointer data)
{
  auto ezgl_app = static_cast<ezgl::application *>(data);
  ezgl_app->quit();

  return TRUE;
}
}
