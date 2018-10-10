/**
 * @file
 *
 * This example shows you how to create an application using the EZGL library.
 */

#include <ezgl/application.hpp>
#include <ezgl/camera.hpp>
#include <ezgl/canvas.hpp>
#include <ezgl/control.hpp>
#include <ezgl/graphics.hpp>

#include <iostream>

/**
 * React to a <a href = "https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-key-press-event">keyboard
 * press event</a>.
 *
 * @param widget The GUI widget where this event came from.
 * @param event The keyboard event.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean press_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

/**
 * React to <a href = "https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-button-press-event">mouse click
 * event</a>
 *
 * @param widget The GUI widget where this event came from.
 * @param event The click event.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean click_mouse(GtkWidget *widget, GdkEventButton *event, gpointer data);

/**
 * Draw to the main canvas using the provided graphics object.
 *
 * The graphics object expects that x and y values will be in the main canvas' world coordinate system.
 */
void draw_main_canvas(ezgl::renderer &g);

/**
 * Connect the press_key(), click_mouse(), and draw_canvas() functions to signals emitted by different GUI objects.
 *
 * @param application The application gives access to the GUI objects.
 */
void setup_callbacks(ezgl::application *application);

static ezgl::rectangle initial_world{{0, 0}, 1100, 1150};

/**
 * The start point of the program.
 *
 * This function initializes an ezgl application and runs it.
 *
 * @param argc The number of arguments provided.
 * @param argv The arguments as an array of c-strings.
 *
 * @return the exit status of the application run.
 */
int main(int argc, char **argv)
{
  ezgl::application::settings settings;

  // Path to the resource that contains an XML description of the UI.
  // Note: this is not a file path, it is a resource path.
  settings.main_ui_resource = "/edu/toronto/eecg/ezgl/ece297/cd000/main.ui";

  // Note: the "main.ui" file has a GtkWindow called "MainWindow".
  settings.window_identifier = "MainWindow";

  // Tell the EZGL application which function to call when it is time
  // to connect GUI objects to our own custom callbacks.
  settings.setup_callbacks = setup_callbacks;

  // Create our EZGL application.
  ezgl::application application(settings);

  application.add_canvas("MainCanvas", draw_main_canvas, initial_world);

  // Run the application until the user quits.
  // This hands over all control to the GTK runtime---after this point
  // you will only regain control based on callbacks you have setup.
  return application.run(argc, argv);
}

void setup_callbacks(ezgl::application *application)
{
  // Get a pointer to the MainWindow GUI object by using its name.
  GObject *window = application->get_object("MainWindow");

  // Connect our press_key function to keyboard presses in the MainWindow.
  g_signal_connect(window, "key_press_event", G_CALLBACK(press_key), nullptr);

  // Get a pointer to the MainCanvas GUI object by using its name.
  GObject *main_canvas = application->get_object("MainCanvas");

  // Connect our click_mouse function to mouse presses and releases in the MainWindow.
  g_signal_connect(main_canvas, "button_press_event", G_CALLBACK(click_mouse), application);

  GObject *zoom_fit_button = application->get_object("ZoomFitButton");
  g_signal_connect(zoom_fit_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    ezgl::zoom_fit(app->get_canvas("MainCanvas"), initial_world);
  }),
      application);

  GObject *zoom_in_button = application->get_object("ZoomInButton");
  g_signal_connect(zoom_in_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::zoom_in(canvas, 5.0 / 3.0);
  }),
      application);

  GObject *zoom_out_button = application->get_object("ZoomOutButton");
  g_signal_connect(zoom_out_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::zoom_out(canvas, 5.0 / 3.0);
  }),
      application);

  GObject *shift_up_button = application->get_object("UpButton");
  g_signal_connect(shift_up_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::translate_up(canvas, 5.0);
  }),
      application);

  GObject *shift_down_button = application->get_object("DownButton");
  g_signal_connect(shift_down_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::translate_down(canvas, 5.0);
  }),
      application);

  GObject *shift_left_button = application->get_object("LeftButton");
  g_signal_connect(shift_left_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::translate_left(canvas, 5.0);
  }),
      application);

  GObject *shift_right_button = application->get_object("RightButton");
  g_signal_connect(shift_right_button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
    auto app = static_cast<ezgl::application *>(data);
    auto canvas = app->get_canvas("MainCanvas");

    ezgl::translate_right(canvas, 5.0);
  }),
      application);
}

gboolean press_key(GtkWidget *, GdkEventKey *event, gpointer)
{
  // see: https://developer.gnome.org/gdk3/stable/gdk3-Keyboard-Handling.html
  std::cout << gdk_keyval_name(event->keyval) << " was pressed.\n";

  return FALSE; // propagate the event
}

gboolean click_mouse(GtkWidget *, GdkEventButton *event, gpointer data)
{
  auto application = static_cast<ezgl::application *>(data);

  if(event->type == GDK_BUTTON_PRESS) {
    std::cout << "Click (widget): " << event->x << ", " << event->y << "\n";

    ezgl::point2d const widget_coordinates(event->x, event->y);
    ezgl::canvas *canvas = application->get_canvas("MainCanvas");

    ezgl::point2d const world = canvas->get_camera().widget_to_world(widget_coordinates);
    std::cout << "Click (world): " << world.x << ", " << world.y << "\n";

    if(event->button == 1) {
      ezgl::zoom_in(canvas, widget_coordinates, 5.0 / 3.0);
    } else if(event->button == 3) {
      ezgl::zoom_out(canvas, widget_coordinates, 5.0 / 3.0);
    }
  }

  return TRUE; // consume the event
}

void draw_main_canvas(ezgl::renderer &g)
{
  // Change the next draw calls to use the colour red.
  g.set_colour(ezgl::RED);

  // Draw rectangle functions.
  g.draw_rectangle({100, 100}, {400, 300});
  g.draw_rectangle({10, 10}, 50, 50);
  g.draw_rectangle({{1000, 1000}, {1100, 1150}});

  // Draw a filled, green triangle.
  g.set_colour(ezgl::GREEN);
  g.fill_poly({{500, 400}, {440, 480}, {560, 480}});

  // Draw blue text.
  g.set_colour(ezgl::BLUE);
  g.format_font("monospace", ezgl::font_slant::normal, ezgl::font_weight::normal, 24);
  g.draw_text({100, 100}, "Hello World!");

  // Draw a thick, dashed semi-transparent line.
  g.set_colour(ezgl::DARK_GREEN, 64);

  g.set_line_cap(ezgl::line_cap::butt);
  g.set_line_dash(ezgl::line_dash::asymmetric_5_3);
  g.set_line_width(5);

  g.draw_line({0, 0}, {1100, 1150});

  // Draw filled in rectangles...
  g.set_colour(ezgl::DARK_SLATE_BLUE, 128);
  g.fill_rectangle({500, 50}, {600, 300}); // from one point to another
  g.fill_rectangle({500, 50}, 50, 50);     // from one point with a width and height

  g.set_colour(ezgl::BLACK);
  g.set_line_dash(ezgl::line_dash::none);
  g.set_line_width(1);
  g.draw_rectangle(initial_world);
}
