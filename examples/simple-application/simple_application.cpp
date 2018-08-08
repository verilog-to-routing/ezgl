/**
 * @file
 *
 * This example shows you how to create an application using the EZGL library.
 */

#include <ezgl/application.hpp>
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
 * React to <a href = "https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-draw">requests from GTK</a> to
 * render graphics.
 *
 * @param widget The GUI widget where this event came from.
 * @param cairo The current state of the rendering device.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean draw_canvas(GtkWidget *widget, cairo_t *cairo, gpointer data);

/**
 * Connect the press_key(), click_mouse(), and draw_canvas() functions to signals emitted by different GUI objects.
 *
 * @param application The application gives access to the GUI objects.
 */
void setup_callbacks(ezgl::application *application);

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
  // Path to the resource that contains an XML description of the UI.
  // Note: this is not a file path, it is a resource path.
  char const *main_ui = "/edu/toronto/eecg/ezgl/ece297/cd000/main.ui";

  // Create our EZGL application.
  // Note: the "main.ui" file has a GtkWindow called "MainWindow" and
  // a GtkDrawingArea called "MainCanvas".
  ezgl::application application(main_ui, "MainWindow", "MainCanvas");

  // Tell the EZGL application which function to call when it is time
  // to connect GUI objects to our own custom callbacks.
  application.register_callbacks_with(setup_callbacks);

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
  GObject *canvas = application->get_object("MainCanvas");

  // Connect our draw_canvas function to the MainCanvas so we can draw graphics.
  g_signal_connect(canvas, "draw", G_CALLBACK(draw_canvas), nullptr);
  // Connect our click_mouse function to mouse clicks in the MainWindow.
  g_signal_connect(canvas, "button_press_event", G_CALLBACK(click_mouse), nullptr);
}

gboolean press_key(GtkWidget *, GdkEventKey *event, gpointer)
{
  // see: https://developer.gnome.org/gdk3/stable/gdk3-Keyboard-Handling.html
  std::cout << gdk_keyval_name(event->keyval) << " was pressed.\n";

  return FALSE; // propagate the event
}

gboolean click_mouse(GtkWidget *, GdkEventButton *event, gpointer)
{
  if(event->type == GDK_BUTTON_PRESS) {
    std::cout << "User clicked mouse at " << event->x << ", " << event->y << "\n";
  } else if(event->type == GDK_BUTTON_RELEASE) {
    std::cout << "User released mouse button at " << event->x << ", " << event->y << "\n";
  }

  return TRUE; // consume the event
}

gboolean draw_canvas(GtkWidget *, cairo_t *cairo, gpointer)
{
  ezgl::graphics g(cairo);

  // Change the next draw calls to use the colour red.
  g.set_colour(ezgl::colour{255, 0, 0});

  // Draw rectangle outlines...
  g.draw_rectangle({100, 100}, {400, 300}); // from one point to another
  g.draw_rectangle({10, 10}, 50, 50);       // from one point with a width and height

  // Draw a triangle.
  g.fill_poly({{500, 400}, {440, 480}, {560, 480}});

  // Draw semi-transparent blue text.
  g.set_colour(ezgl::colour{0, 0, 255, 153});
  g.format_font("monospace", ezgl::font_slant::oblique, ezgl::font_weight::normal, 24);
  g.draw_text({100, 100}, "Hello World!");

  g.set_line_cap(ezgl::line_cap::butt);
  g.set_line_dash(ezgl::line_dash::asymmetric_5_3);
  g.set_line_width(5);
  g.draw_line({128, 128}, {256, 256});

  // Change the next draw calls to use green with half transparency.
  g.set_colour(0, 255, 0, 128);
  // Draw filled in rectangles...
  g.fill_rectangle({500, 50}, {600, 300}); // from one point to another
  g.fill_rectangle({500, 50}, 50, 50);     // from one point with a width and height

  return FALSE; // propagate event
}
