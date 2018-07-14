/**
 * @file
 *
 * This example shows you how to create an XML-Based application using the library.
 */

#include <ezgl/application.hpp>

#include <iostream>

/**
 * React to a keyboard press event.
 *
 * See: https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-key-press-event
 *
 * @param widget The GUI object where this event came from.
 * @param event The keyboard event.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean press_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

/**
 * React to mouse click event.
 *
 * See: https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-button-press-event
 *
 * @param widget The GUI object where this event came from.
 * @param event The click event.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean click_mouse(GtkWidget *, GdkEventButton *event, gpointer);

/**
 * React to requests from GTK to render graphics.
 *
 * See: https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget-draw
 *
 * @param widget The GUI object where this event came from.
 * @param cairo
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
gboolean draw_canvas(GtkWidget *, cairo_t *cairo, gpointer);

/**
 * Connect the functions above to events from different GUI objects.
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
  // Note: the "main.ui" file has a GtkWindow called "MainWindow"
  ezgl::application application(main_ui, "MainWindow");

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
  // Connect our click_mouse function to mouse clicks in the MainWindow.
  g_signal_connect(window, "button_press_event", G_CALLBACK(click_mouse), nullptr);

  // Get a pointer to the MainCanvas GUI object by using its name.
  GObject *canvas = application->get_object("MainCanvas");
  
  // Connect our draw_canvas function to the MainCanvas so we can draw graphics.
  g_signal_connect(canvas, "draw", G_CALLBACK(draw_canvas), nullptr);
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

  return FALSE; // propagate the event
}

gboolean draw_canvas(GtkWidget *, cairo_t *cairo, gpointer)
{
  ezgl::graphics g(cairo);

  // Change the next draw calls to use the colour red.
  g.set_colour(ezgl::RED);

  // Draw rectangle outlines...
  g.draw_rectangle({100, 100}, {400, 300}); // from one point to another
  g.draw_rectangle({10, 10}, 50, 50);       // from one point with a width and height

  // Draw semi-transparent blue text.
  g.set_colour(ezgl::BLUE, 0.6);
  g.format_font("monospace", ezgl::font_slant::oblique, ezgl::font_weight::normal, 24);
  g.draw_text({100, 100}, "Hello World!");

  // Change the next draw calls to use green with half transparency.
  g.set_colour(0, 1, 0, 0.5);
  // Draw filled in rectangles...
  g.fill_rectangle({500, 50}, {600, 300}); // from one point to another
  g.fill_rectangle({500, 50}, 50, 50);     // from one point with a width and height

  return FALSE; // propagate event
}
