#include "ezgl/application.hpp"

namespace ezgl {

void application::startup(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

  char const *main_ui_resource = ezgl_app->m_main_ui.c_str();

  // Build the main user interface from the XML resource.
  GError *error = nullptr;
  if(gtk_builder_add_from_resource(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
    g_error("%s.", error->message);
  }

  for(auto &c_pair : ezgl_app->m_canvases) {
    GObject *drawing_area = ezgl_app->get_object(c_pair.second->id());
    c_pair.second->initialize(GTK_WIDGET(drawing_area));
  }

  g_info("application::startup successful.");
}

void application::activate(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

  // The main parent window needs to be explicitly added to our GTK application.
  GObject *window = ezgl_app->get_object(ezgl_app->m_window_id.c_str());
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(window));

  if(ezgl_app->m_register_callbacks != nullptr) {
    ezgl_app->m_register_callbacks(ezgl_app);
  } else {
    register_default_buttons_callbacks(ezgl_app);
    register_default_events_callbacks(ezgl_app);
  }

  g_info("application::activate successful.");
}

application::application(application::settings s)
    : m_main_ui(s.main_ui_resource)
    , m_window_id(s.window_identifier)
    , m_canvas_id(s.canvas_identifier)
    , m_application(gtk_application_new("edu.toronto.eecg.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_builder(gtk_builder_new())
    , m_register_callbacks(s.setup_callbacks)
{
  // Connect our static functions application::{startup, activate} to their callbacks. We pass 'this' as the userdata
  // so that we can use it in our static functions.
  g_signal_connect(m_application, "startup", G_CALLBACK(startup), this);
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
}

application::~application()
{
  // GTK uses reference counting to track object lifetime. Since we called *_new() for our application and builder, we
  // need to unreference them. This should set their reference count to 0, letting GTK know that they should be cleaned
  // up in memory.
  g_object_unref(m_builder);
  g_object_unref(m_application);
}

canvas *application::get_canvas(const std::string &canvas_id) const
{
  auto it = m_canvases.find(canvas_id);
  if(it != m_canvases.end()) {
    return it->second.get();
  }

  g_warning("Could not find canvas with name %s.", canvas_id.c_str());
  return nullptr;
}

canvas *application::add_canvas(std::string const &canvas_id,
    draw_canvas_fn draw_callback,
    rectangle coordinate_system)
{
  if(draw_callback == nullptr) {
    // A NULL draw callback means the canvas will never render anything to the screen.
    g_warning("Canvas %s's draw callback is NULL.", canvas_id.c_str());
  }

  // Can't use make_unique with protected constructor without fancy code that will confuse students, so we use new
  // instead.
  std::unique_ptr<canvas> canvas_ptr(new canvas(canvas_id, draw_callback, coordinate_system));
  auto it = m_canvases.emplace(canvas_id, std::move(canvas_ptr));

  if(!it.second) {
    // std::map's emplace does not insert the value when the key is already present.
    g_warning("Duplicate key (%s) ignored in application::add_canvas.", canvas_id.c_str());
  } else {
    g_info("The %s canvas has been added to the application.", canvas_id.c_str());
  }

  return it.first->second.get();
}

GObject *application::get_object(gchar const *name) const
{
  // Getting an object from the GTK builder does not increase its reference count.
  GObject *object = gtk_builder_get_object(m_builder, name);
  g_return_val_if_fail(object != nullptr, nullptr);

  return object;
}

int application::run(int argc, char **argv, mouse_callback_fn mouse_press_user_callback,
    mouse_callback_fn mouse_move_user_callback, key_callback_fn key_press_user_callback)
{
  mouse_press_callback = mouse_press_user_callback;
  mouse_move_callback = mouse_move_user_callback;
  key_press_callback = key_press_user_callback;

  g_info("The event loop is now starting.");

  // see: https://developer.gnome.org/gio/unstable/GApplication.html#g-application-run
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}

void application::register_default_events_callbacks(ezgl::application *application)
{
  // Get a pointer to the main window GUI object by using its name.
  std::string main_window_id =  application->get_main_window_id();
  GObject *window = application->get_object(main_window_id.c_str());

  // Get a pointer to the main canvas GUI object by using its name.
  std::string main_canvas_id = application->get_main_canvas_id();
  GObject *main_canvas = application->get_object(main_canvas_id.c_str());

  // Connect press_key function to keyboard presses in the MainWindow.
  g_signal_connect(window, "key_press_event", G_CALLBACK(press_key), application);

  // Connect press_mouse function to mouse presses and releases in the MainWindow.
  g_signal_connect(main_canvas, "button_press_event", G_CALLBACK(press_mouse), application);

  // Connect release_mouse function to mouse presses and releases in the MainWindow.
  g_signal_connect(main_canvas, "button_release_event", G_CALLBACK(release_mouse), application);

  // Connect release_mouse function to mouse presses and releases in the MainWindow.
  g_signal_connect(main_canvas, "motion_notify_event", G_CALLBACK(move_mouse), application);

  // Connect scroll_mouse function to the mouse scroll event (up, down, left and right)
  g_signal_connect(main_canvas, "scroll_event", G_CALLBACK(scroll_mouse), application);
}

void application::register_default_buttons_callbacks(ezgl::application *application)
{
  // Connect press_zoom_fit function to the Zoom-fit button
  GObject *zoom_fit_button = application->get_object("ZoomFitButton");
  g_signal_connect(zoom_fit_button, "clicked", G_CALLBACK(press_zoom_fit), application);

  // Connect press_zoom_in function to the Zoom-in button
  GObject *zoom_in_button = application->get_object("ZoomInButton");
  g_signal_connect(zoom_in_button, "clicked", G_CALLBACK(press_zoom_in), application);

  // Connect press_zoom_out function to the Zoom-out button
  GObject *zoom_out_button = application->get_object("ZoomOutButton");
  g_signal_connect(zoom_out_button, "clicked", G_CALLBACK(press_zoom_out), application);

  // Connect press_up function to the Up button
  GObject *shift_up_button = application->get_object("UpButton");
  g_signal_connect(shift_up_button, "clicked", G_CALLBACK(press_up), application);

  // Connect press_down function to the Down button
  GObject *shift_down_button = application->get_object("DownButton");
  g_signal_connect(shift_down_button, "clicked", G_CALLBACK(press_down), application);

  // Connect press_left function to the Left button
  GObject *shift_left_button = application->get_object("LeftButton");
  g_signal_connect(shift_left_button, "clicked", G_CALLBACK(press_left), application);

  // Connect press_right function to the Right button
  GObject *shift_right_button = application->get_object("RightButton");
  g_signal_connect(shift_right_button, "clicked", G_CALLBACK(press_right), application);
}

void application::update_message(std::string const &message)
{
  // Get the StatusBar Object
  GtkStatusbar *status_bar = (GtkStatusbar *) get_object("StatusBar");

  // Remove all previous messages from the message stack
  gtk_statusbar_remove_all(status_bar, 0);

  // Push user message to the message stack
  gtk_statusbar_push (status_bar, 0, message.c_str());
}
}
