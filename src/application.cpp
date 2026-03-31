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
 * Authors: Mario Badr, Sameh Attia, Tanner Young-Schultz,
 * Sebastian Lievano Arzayus and Vaughn Betz
 */

#include "ezgl/application.hpp"

#ifdef EZGL_QT
#include <QObject>
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include "ezgl/qt/qtgladeloader.hpp"
#else // EZGL_QT
// GLib deprecated G_APPLICATION_FLAGS_NONE and replaced it with G_APPLICATION_DEFAULT_FLAGS,
// however, this enum was not introduced until GLib 2.74. These lines of code allow EZGL
// to be backwards compatible with older versions of GLib, while not using the deprecated
// enum.
#if GLIB_CHECK_VERSION(2, 74, 0)
static constexpr GApplicationFlags EZGL_APPLICATION_DEFAULT_FLAGS = G_APPLICATION_DEFAULT_FLAGS;
#else
static constexpr GApplicationFlags EZGL_APPLICATION_DEFAULT_FLAGS = G_APPLICATION_FLAGS_NONE;
#endif
#endif // EZGL_QT

namespace ezgl {

// A flag to disable event loop (default is false)
// This allows basic scripted testing even if the GUI is on (return immediately when the event loop is called)
bool disable_event_loop = false;

#ifdef EZGL_QT
namespace {

QGridLayout* inner_grid_layout(application const* app)
{
  QWidget* inner_grid = app->get_widget("InnerGrid");
  g_return_val_if_fail(inner_grid != nullptr, nullptr);

  QGridLayout* layout = qobject_cast<QGridLayout*>(inner_grid->layout());
  g_return_val_if_fail(layout != nullptr, nullptr);
  return layout;
}

void insert_grid_row(QGridLayout* layout, int insert_row)
{
  g_return_if_fail(layout != nullptr);

  struct Placement {
    QWidget* widget;
    int row;
    int column;
    int row_span;
    int column_span;
    Qt::Alignment alignment;
  };

  std::vector<Placement> moved_widgets;
  for (int i = 0; i < layout->count(); ++i) {
    int row = 0;
    int column = 0;
    int row_span = 0;
    int column_span = 0;
    layout->getItemPosition(i, &row, &column, &row_span, &column_span);

    QWidget* widget = layout->itemAt(i)->widget();
    if (widget == nullptr || row < insert_row) {
      continue;
    }

    moved_widgets.push_back({widget, row, column, row_span, column_span, layout->itemAt(i)->alignment()});
  }

  for (const Placement& placement : moved_widgets) {
    layout->removeWidget(placement.widget);
  }

  for (const Placement& placement : moved_widgets) {
    layout->addWidget(placement.widget,
        placement.row + 1,
        placement.column,
        placement.row_span,
        placement.column_span,
        placement.alignment);
  }
}

} // namespace

void application::startup(GtkApplication *gtk_app, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

#ifdef EZGL_QT

#else // EZGL_QT
  char const *main_ui_resource = ezgl_app->m_main_ui.c_str();
  if (!build_ui_from_file) {
    // Build the main user interface from the XML resource.
    // The XML resource is built from an XML file using the glib-compile-resources tool.
    // This adds an extra compilation step, but it embeds the UI description in the executable.
    GError *error = nullptr;
    if(gtk_builder_add_from_resource(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
      g_error("%s.", error->message);
    }
  }
  else {
    // Build the main user interface from the XML file.
    GError *error = nullptr;
    if(gtk_builder_add_from_file(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
      g_error("%s.", error->message);
    }
  }
#endif // EZGL_QT

  for(auto &c_pair : ezgl_app->m_canvases) {
    GObject *drawing_area = ezgl_app->get_object(c_pair.second->id());
    c_pair.second->initialize(GTK_WIDGET(drawing_area));
  }

  g_info("application::startup successful.");
}
#else // EZGL_QT
void application::startup(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

  char const *main_ui_resource = ezgl_app->m_main_ui.c_str();
#ifndef HIDE_GTK_BUILDER
  if (!build_ui_from_file) {
    // Build the main user interface from the XML resource.
    // The XML resource is built from an XML file using the glib-compile-resources tool.
    // This adds an extra compilation step, but it embeds the UI description in the executable.
    GError *error = nullptr;
    if(gtk_builder_add_from_resource(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
      g_error("%s.", error->message);
    }
  }
  else {
    // Build the main user interface from the XML file.
    GError *error = nullptr;
    if(gtk_builder_add_from_file(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
      g_error("%s.", error->message);
    }
  }
#endif // HIDE_GTK_BUILDER

  for(auto &c_pair : ezgl_app->m_canvases) {
    GObject *drawing_area = ezgl_app->get_object(c_pair.second->id());
    g_debug(c_pair.second->id());
    c_pair.second->initialize(GTK_WIDGET(drawing_area));
  }

  g_info("application::startup successful.");
}
#endif // EZGL_QT

#ifdef EZGL_QT
void application::activate(GtkApplication*, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

  // The main parent window needs to be explicitly added to our GTK application.
#ifdef EZGL_QT
  QWidget *window = ezgl_app->get_widget(ezgl_app->m_window_id.c_str());
  window->show();
#else
  GObject *window = ezgl_app->get_object(ezgl_app->m_window_id.c_str());
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(window));
#endif

  // Setup the default callbacks for the mouse and key events
  register_default_events_callbacks(ezgl_app);

  if(ezgl_app->m_register_callbacks != nullptr) {
    ezgl_app->m_register_callbacks(ezgl_app);
  } else {
    // Setup the default callbacks for the prebuilt buttons
    register_default_buttons_callbacks(ezgl_app);
  }

  if(ezgl_app->initial_setup_callback != nullptr)
    ezgl_app->initial_setup_callback(ezgl_app, true);

  g_info("application::activate successful.");
}
#else // EZGL_QT
void application::activate(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  g_return_if_fail(ezgl_app != nullptr);

  // The main parent window needs to be explicitly added to our GTK application.
  GObject *window = ezgl_app->get_object(ezgl_app->m_window_id.c_str());
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(window));

  // Setup the default callbacks for the mouse and key events
  register_default_events_callbacks(ezgl_app);

  if(ezgl_app->m_register_callbacks != nullptr) {
    ezgl_app->m_register_callbacks(ezgl_app);
  } else {
    // Setup the default callbacks for the prebuilt buttons
    register_default_buttons_callbacks(ezgl_app);
  }

  if(ezgl_app->initial_setup_callback != nullptr)
    ezgl_app->initial_setup_callback(ezgl_app, true);

  g_info("application::activate successful.");
}
#endif // EZGL_QT

#ifdef EZGL_QT
application::application(application::settings s, int& argc, char** argv)
#else
application::application(application::settings s)
#endif
    : m_main_ui(s.main_ui_resource)
    , m_window_id(s.window_identifier)
    , m_canvas_id(s.canvas_identifier)
    , m_application_id(s.application_identifier)
#ifdef EZGL_QT
    , m_application(gtk_application_new(s.application_identifier.c_str(), argc, argv))
#else // EZGL_QT
    , m_application(gtk_application_new(s.application_identifier.c_str(), EZGL_APPLICATION_DEFAULT_FLAGS))
#endif // EZGL_QT
#ifndef HIDE_GTK_BUILDER
    , m_builder(gtk_builder_new())
#endif // HIDE_GTK_BUILDER
#ifndef HIDE_GTK_EVENT
    , m_register_callbacks(s.setup_callbacks)
#endif // HIDE_GTK_EVENT
{
#ifdef EZGL_USE_X11
  // Prefer x11 first, then other backends.
  gdk_set_allowed_backends("x11,*");
#endif

#ifdef EZGL_QT
  // we moved this to run method
#else
  // Connect our static functions application::{startup, activate} to their callbacks. We pass 'this' as the userdata
  // so that we can use it in our static functions.
  g_signal_connect(m_application, "startup", G_CALLBACK(startup), this);
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
#endif

#ifdef EZGL_QT
  m_application->setApp(this);
  qInfo() << m_application->applicationName();
  qInfo() << m_application->arguments();
  // NOTE: do NOT load the UI file here. This constructor runs as a static
  // initializer (before main()), so Qt resources registered by the
  // application's .qrc file are not yet available.  Loading is deferred to
  // run(), which is called from main() after all static initializers have
  // completed.
#endif

  first_run = true;
  resume_run = false;
}

application::~application()
{
#ifdef EZGL_QT
  g_debug("application::~application");
  // Do NOT delete m_application here.  ezgl::application is typically a
  // file-scope static, so its destructor runs during static teardown.
  // Deleting QApplication at that point crashes because Qt's own internal
  // statics (font cache, style engine, etc.) may already be destroyed.
  // The process is exiting; the OS reclaims the memory.
  m_application->quit();
#else
  // GTK uses reference counting to track object lifetime. Since we called *_new() for our application and builder, we
  // need to unreference them. This should set their reference count to 0, letting GTK know that they should be cleaned
  // up in memory.
  g_object_unref(m_builder);
  g_object_unref(m_application);
#endif
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
    rectangle coordinate_system,
    color background_color)
{
  if(draw_callback == nullptr) {
    // A NULL draw callback means the canvas will never render anything to the screen.
    g_warning("Canvas %s's draw callback is NULL.", canvas_id.c_str());
  }

  // Can't use make_unique with protected constructor without fancy code that will confuse students, so we use new
  // instead.
  std::unique_ptr<canvas> canvas_ptr(new canvas(canvas_id, draw_callback, coordinate_system, background_color));
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
#ifdef EZGL_QT
  QObject* object = nullptr;
  //qDebug() <<"~~~ " << "searching" << name;
  for (QWidget* w: QApplication::allWidgets()) {
    //qDebug() <<"~~~ iterate over" << w->objectName();
    if (w->objectName() == name) {
      //qDebug() << "~~~ [+] found" << w->objectName();
      object = w;
      break;
    }
  }
  if (object == nullptr) {
    qDebug() <<"~~~ [-] couldn't find" << name;
  }
#else
  GObject *object = gtk_builder_get_object(m_builder, name);
#endif
  g_return_val_if_fail(object != nullptr, nullptr);

  return object;
}

int application::run(setup_callback_fn initial_setup_user_callback,
    mouse_callback_fn mouse_press_user_callback,
    mouse_callback_fn mouse_move_user_callback,
    key_callback_fn key_press_user_callback)
{
  if(disable_event_loop)
    return 0;

  initial_setup_callback = initial_setup_user_callback;
  mouse_press_callback = mouse_press_user_callback;
  mouse_move_callback = mouse_move_user_callback;
  key_press_callback = key_press_user_callback;

#ifdef EZGL_QT
  // Qt cannot create a second QApplication, so the application object is reused
  // across all stages.  The window is loaded once on the first run and reused
  // (reshown) for every subsequent stage.
  if (first_run) {
    // Load the UI file here, not in the constructor.  The constructor runs as a
    // static initializer before main(), so Qt resources are not yet registered
    // at that point (static initialization order fiasco).  By the time run() is
    // called from main(), all .qrc static initializers have completed.
    if (!m_window) {
      QtGladeLoader uiLoader;
      m_window = uiLoader.loadFile(QString::fromStdString(m_main_ui));
    }
    startup(nullptr, this);
    activate(nullptr, this);
    first_run = false;
    g_info("The event loop is now starting.");
    return g_application_run(m_application, 0, 0);
  } else {
    // Subsequent stage: reuse the existing window.
    // activate() is NOT called again to avoid double-registering callbacks.
    m_window->show();
    if (initial_setup_callback != nullptr)
      initial_setup_callback(this, false);
    resume_run = true;
    g_info("The event loop is now resuming.");
    return g_application_run(m_application, 0, 0);
  }

#else // EZGL_QT

  if(first_run) {
    // set the first_run flag to false
    first_run = false;

    g_info("The event loop is now starting.");

    // see: https://developer.gnome.org/gio/stable/GApplication.html#g-application-run
    return g_application_run(G_APPLICATION(m_application), 0, 0);
  }
  // The result of calling g_application_run() again after it returns is unspecified.
  // So in the subsequent runs instead of calling g_application_run(), we will go back to the event loop using gtk_main()
  else if(!first_run && gtk_application_get_active_window(m_application) != nullptr) {

    // Call user's initial setup call
    if(initial_setup_callback != nullptr)
      initial_setup_callback(this, false);

    // set the resume_run flag to true
    resume_run = true;

    g_info("The event loop is now resuming.");

    // see: https://developer.gnome.org/gtk3/stable/gtk3-General.html#gtk-main
    gtk_main();

    return 0;
  }
  // But if the GTK window is closed, we will have to destruct and reconstruct the GTKApplication
  else {
    // Destroy the GTK application
    g_object_unref(m_application);

#ifndef HIDE_GTK_BUILDER
    g_object_unref(m_builder);
#endif // HIDE_GTK_BUILDER

    // Reconstruct the GTK application
    m_application = (gtk_application_new(m_application_id.c_str(), EZGL_APPLICATION_DEFAULT_FLAGS));

#ifndef HIDE_GTK_BUILDER
    m_builder = (gtk_builder_new());
#endif // HIDE_GTK_BUILDER

#ifndef HIDE_GTK_EVENT
    g_signal_connect(m_application, "startup", G_CALLBACK(startup), this);
    g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
#endif // HIDE_GTK_EVENT

    // set the resume_run flag to false
    resume_run = false;

    g_info("The event loop is now restarting.");

    // see: https://developer.gnome.org/gio/stable/GApplication.html#g-application-run
    return g_application_run(G_APPLICATION(m_application), 0, 0);
  }

#endif // EZGL_QT
}

void application::quit()
{
  if(resume_run) {
    // Quit the event loop (exit gtk_main())
    gtk_main_quit();
  } else {
    // Quit the GTK application (exit g_application_run())
#ifdef EZGL_QT
    g_application_quit(m_application);
#else
    g_application_quit(G_APPLICATION(m_application));
#endif
  }
}

void application::register_default_events_callbacks(ezgl::application *application)
{
  // Get a pointer to the main window GUI object by using its name.
  std::string main_window_id = application->get_main_window_id();
  GObject *window = application->get_object(main_window_id.c_str());

  // Get a pointer to the main canvas GUI object by using its name.
  std::string main_canvas_id = application->get_main_canvas_id();
  GObject *main_canvas = application->get_object(main_canvas_id.c_str());

#ifndef HIDE_GTK_EVENT
  // We want to enable user event handlers for mouse clicks, key presses etc.
  // when they are in the drawing area (MainCanvas).
  // Connect press_key function to keyboard presses in the MainCanvas (drawing area).
  // In the main.ui, this only works if the MainCanvas "can focus" and has 
  // keyboard events selected. Hit tab to move focus from a widget (e.g. a dialog)
  // to the MainCanvas when running the application.
  g_signal_connect(main_canvas, "key_press_event", G_CALLBACK(press_key), application);

  // Connect press_mouse function to mouse presses and releases in the MainCanvas.
  g_signal_connect(main_canvas, "button_press_event", G_CALLBACK(press_mouse), application);

  // Connect release_mouse function to mouse presses and releases in the MainCanvas.
  g_signal_connect(main_canvas, "button_release_event", G_CALLBACK(release_mouse), application);

  // Connect release_mouse function to mouse presses and releases in the MainCanvas.
  g_signal_connect(main_canvas, "motion_notify_event", G_CALLBACK(move_mouse), application);

  // Connect scroll_mouse function to the mouse scroll event (up, down, left and right)
  g_signal_connect(main_canvas, "scroll_event", G_CALLBACK(scroll_mouse), application);

  // Connect press_proceed function to the close button of the MainWindow
  g_signal_connect(window, "destroy", G_CALLBACK(press_proceed), application);
#endif // HIDE_GTK_EVENT
}

void application::register_default_buttons_callbacks(ezgl::application *application)
{
#ifdef EZGL_QT
  // Helper: only connect if the button exists in this UI (VPR's main.ui omits
  // several navigation buttons that the basic-application example has).
  auto connect_if_present = [&](const char* name, auto slot) {
    QPushButton* btn = application->get_push_button(name);
    if (btn) {
      QObject::connect(btn, &QPushButton::clicked, btn, slot);
    }
  };

  connect_if_present("ZoomFitButton", [application](){ press_zoom_fit(nullptr, application); });
  connect_if_present("ZoomInButton",  [application](){ press_zoom_in(nullptr, application); });
  connect_if_present("ZoomOutButton", [application](){ press_zoom_out(nullptr, application); });
  connect_if_present("UpButton",      [application](){ press_up(nullptr, application); });
  connect_if_present("DownButton",    [application](){ press_down(nullptr, application); });
  connect_if_present("LeftButton",    [application](){ press_left(nullptr, application); });
  connect_if_present("RightButton",   [application](){ press_right(nullptr, application); });
  connect_if_present("ProceedButton", [application](){ press_proceed(nullptr, application); });

  // Connect the window's close (X button) to press_proceed so that closing
  // the window exits the event loop, matching the GTK "destroy" signal behaviour.
  // Qt quits the event loop automatically when the last window closes
  // (quitOnLastWindowClosed=true by default), which is equivalent to GTK's
  // "destroy" → press_proceed path.  We just need to ensure press_proceed is
  // also called so VPR's internal state advances to the next stage.
  QWidget* window = application->get_widget(application->get_main_window_id().c_str());
  if (window) {
    // Prevent Qt from deleting the window on close so it can be reused
    // across stages (m_window->show() in subsequent run() calls).
    window->setAttribute(Qt::WA_DeleteOnClose, false);

    QObject::connect(application->m_application, &QApplication::lastWindowClosed,
                     application->m_application, [application](){
      press_proceed(nullptr, application);
    });
  }

#else // EZGL_QT
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

  // Connect press_proceed function to the Proceed button
  GObject *proceed_button = application->get_object("ProceedButton");
  g_signal_connect(proceed_button, "clicked", G_CALLBACK(press_proceed), application);
#endif // EZGL_QT
}

void application::update_message(std::string const &message)
{
#ifdef EZGL_QT
  // Get the StatusBar Object
  QStatusBar* status_bar = qobject_cast<QStatusBar*>(get_object("StatusBar"));

  if (status_bar) {
    // Remove all previous messages from the message stack
    status_bar->clearMessage();

    // Push user message to the message stack
    status_bar->showMessage(QString::fromStdString(message));
  } else {
    qCritical() << "object with name `StatusBar` wasn't found";
  }
#else // EZGL_QT
  // Get the StatusBar Object
  GtkStatusbar *status_bar = (GtkStatusbar *)get_object("StatusBar");

  // Remove all previous messages from the message stack
  gtk_statusbar_remove_all(status_bar, 0);

  // Push user message to the message stack
  gtk_statusbar_push(status_bar, 0, message.c_str());
#endif // EZGL_QT
}

void application::create_button(const char *button_text,
    int left,
    int top,
    int width,
    int height,
    button_callback_fn button_func)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  QString text = QString::fromUtf8(button_text ? button_text : "");
  QPushButton* new_button = new QPushButton(text);
  new_button->setObjectName(text);
  new_button->setFocusPolicy(Qt::NoFocus);
  new_button->setAutoDefault(false);
  new_button->setDefault(false);

  if (button_func != nullptr) {
    QObject::connect(new_button, &QPushButton::clicked, new_button, [this, new_button, button_func]() {
      button_func(new_button, this);
    });
  }

  in_grid->addWidget(new_button, top, left, height, width);
  new_button->show();
#else // EZGL_QT
  // get the internal Gtk grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  // create the new button with the given label
  GtkWidget *new_button = gtk_button_new_with_label(button_text);
  gtk_widget_set_name(new_button, button_text);

  // set can_focus property to false; helps keyboard events go to more useful
  // widgets (e.g. MainCanvas can get them if the user set a callback).
  gtk_widget_set_can_focus(new_button, false);
#if GTK_CHECK_VERSION (3, 20, 0)
  gtk_widget_set_focus_on_click(new_button, false);
#endif

  // connect the buttons clicked event to the callback
  if(button_func != NULL) {
    g_signal_connect(G_OBJECT(new_button), "clicked", G_CALLBACK(button_func), this);
  }

  // add the new button
  gtk_grid_attach(in_grid, new_button, left, top, width, height);

  // show the button
  gtk_widget_show(new_button);
#endif // EZGL_QT
}

void application::create_button(const char *button_text,
    int insert_row,
    button_callback_fn button_func)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_button(button_text, 0, insert_row, 3, 1, button_func);
#else // EZGL_QT
  // get the internal Gtk grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  // add a row where we want to insert
  gtk_grid_insert_row(in_grid, insert_row);

  // create the button
  create_button(button_text, 0, insert_row, 3, 1, button_func);
#endif // EZGL_QT
}

void application::create_label(int insert_row, const char *label_text){
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_label(0, insert_row, 3, 1, label_text);
#else // EZGL_QT
  //Getting grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  //Adding row
  gtk_grid_insert_row(in_grid, insert_row);

  //Creating label
  create_label(0, insert_row, 3, 1, label_text);
#endif // EZGL_QT
}

void application::create_label(
  int left,
  int top,
  int width,
  int height,
  const char *label_text)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  QString text = QString::fromUtf8(label_text ? label_text : "");
  QLabel* new_label = new QLabel(text);
  new_label->setObjectName(text);
  in_grid->addWidget(new_label, top, left, height, width);
  new_label->show();
#else // EZGL_QT
  //Getting grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  GtkWidget* new_label = gtk_label_new(label_text);
  gtk_widget_set_name(new_label, label_text);

  // add the new button
  gtk_grid_attach(in_grid, new_label, left, top, width, height);

  // show the button
  gtk_widget_show(new_label);
#endif // EZGL_QT
}

void application::create_combo_box_text(
  const char* name, 
  int insert_row, 
  combo_box_callback_fn callback,
  const std::vector<std::string>& options)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_combo_box_text(name, 0, insert_row, 3, 1, callback, options);
#else // EZGL_QT
  // get the internal Gtk grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  // add a row where we want to insert
  gtk_grid_insert_row(in_grid, insert_row);

  // create the combo box
  create_combo_box_text(name, 0, insert_row, 3, 1, callback, options);
#endif // EZGL_QT
}

void application::create_combo_box_text(
  const char* name,
  int left,
  int top,
  int width,
  int height,
  combo_box_callback_fn combo_box_fn, 
  const std::vector<std::string>& options)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  QString combo_name = QString::fromUtf8(name ? name : "");
  QComboBox* new_combo_box = new QComboBox;
  new_combo_box->setObjectName(combo_name);
  new_combo_box->setFocusPolicy(Qt::NoFocus);

  for (auto const& option : options) {
    new_combo_box->addItem(QString::fromStdString(option));
  }

  if (combo_box_fn != nullptr) {
    QObject::connect(new_combo_box,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        new_combo_box,
        [this, new_combo_box, combo_box_fn](int) {
          combo_box_fn(new_combo_box, this);
        });
  }

  if (new_combo_box->count() > 0) {
    new_combo_box->setCurrentIndex(0);
  }

  in_grid->addWidget(new_combo_box, top, left, height, width);
  new_combo_box->show();
#else // EZGL_QT
    // get the internal Gtk grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  //Creating new 
  GtkWidget* new_combo_box = gtk_combo_box_text_new();
  gtk_widget_set_name(new_combo_box, name);
  // connect the buttons clicked event to the callback
  if(combo_box_fn != NULL) {
    g_signal_connect(G_OBJECT(new_combo_box), "changed", G_CALLBACK(combo_box_fn), this);
  }

  //Inserting options into combo box
  for(auto it : options){
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(new_combo_box), it.c_str());
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(new_combo_box),0);


  // add the new button
  gtk_grid_attach(in_grid, new_combo_box, left, top, width, height);

  // set can_focus property to false; helps keyboard events go to more useful
  // widgets (e.g. MainCanvas can get them if the user set a callback).
  // Even with this code, seems the combo box can get focus, oddly.
  gtk_widget_set_can_focus(new_combo_box, false);
#if GTK_CHECK_VERSION (3, 20, 0)
  gtk_widget_set_focus_on_click(new_combo_box, false);
#endif

  // show the button
  gtk_widget_show(new_combo_box);
#endif // EZGL_QT
}

void application::change_combo_box_text_options(const char* name, const std::vector<std::string>& new_options){
#ifdef EZGL_QT
  QComboBox* combo_box = qobject_cast<QComboBox*>(find_widget(name));
  if (combo_box == nullptr) {
    return;
  }

  combo_box->clear();
  for (const std::string& new_option: new_options) {
    combo_box->addItem(QString::fromStdString(new_option));
  }
  if (combo_box->count() > 0) {
    combo_box->setCurrentIndex(0);
  }
#else // EZGL_QT
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");
  // the text to delete, in c++ string form
  std::string name_to_find = std::string(name);

  // iterate over all of the children in the grid and find the button by it's text
  GList *children, *iter;
  children = gtk_container_get_children(GTK_CONTAINER(in_grid));
  for(iter = children; iter != NULL; iter = g_list_next(iter)){
    GtkWidget* widget = GTK_WIDGET(iter->data);

    if(GTK_IS_COMBO_BOX_TEXT(widget)) {
      if(gtk_widget_get_name(widget) == name_to_find){
        auto combo_box = GTK_COMBO_BOX_TEXT(widget);
        gtk_combo_box_text_remove_all(combo_box);
        std::cout << "REMOVED ALL" << std::endl;
        for(auto it : new_options){
          gtk_combo_box_text_append_text(combo_box, it.c_str());
        }
        if(new_options.size()){
          gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 0);
        }
      }
    }
  }
#endif // EZGL_QT
}

void application::create_dialog_window(
  dialog_callback_fn cbk_fn, 
  const char* dialog_title, 
  const char *window_text)
{
#ifdef EZGL_QT
  QDialog* dialog = new QDialog(m_window);
  dialog->setWindowTitle(dialog_title);
  dialog->setModal(true);

  QVBoxLayout* layout = new QVBoxLayout(dialog);

  QLabel* label = new QLabel(window_text, dialog);
  layout->addWidget(label);

  QDialogButtonBox* buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttonBox);

  QObject::connect(buttonBox, &QDialogButtonBox::accepted,
      dialog, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected,
      dialog, &QDialog::reject);

  QObject::connect(dialog, &QDialog::finished, dialog,
      [this, dialog, cbk_fn](int result) {
        cbk_fn(dialog, result, this);
        dialog->deleteLater();
      });

  dialog->exec();
#else // EZGL_QT
  //getting window ptr
  GtkWindow* window = GTK_WINDOW(get_object(m_window_id.c_str()));
  GtkWidget* dialog = gtk_dialog_new_with_buttons(
    dialog_title,   //title
    window,         //window
    GTK_DIALOG_MODAL, //Button and return_id pairs
    ("OK"),
    GTK_RESPONSE_ACCEPT,
    ("CANCEL"),
    GTK_RESPONSE_REJECT, 
    NULL
  );

  //Adding the label to the content window
  auto content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget* label = gtk_label_new(window_text);
  gtk_container_add(GTK_CONTAINER(content_area), label);

  //Connecting Callback Function
  g_signal_connect(
    GTK_DIALOG(dialog), 
    "response", 
    G_CALLBACK(cbk_fn), 
    this
  );

  //Showing the dialog window
  gtk_widget_show_all(dialog);
#endif // EZGL_QT
}

// A default callback function that closes the dialog box when user hits done. DO NOT CALL OR USE EXTERNALLY
static void default_popup_cbk(GtkDialog* self, gint /*response_id*/, ezgl::application* /*app*/){
  gtk_widget_destroy(GTK_WIDGET(self));
}

void application::create_popup_message(const char* title, const char *message)
{
  create_popup_message_with_callback(default_popup_cbk, title, message);
}

void application::create_popup_message_with_callback(dialog_callback_fn cbk_fn, const char* title, const char *message){
#ifdef EZGL_QT
  QDialog* popup_msg = new QDialog(m_window);
  popup_msg->setWindowTitle(title);
  popup_msg->setModal(true);

  QVBoxLayout* layout = new QVBoxLayout(popup_msg);

  QLabel* label = new QLabel(message, popup_msg);
  layout->addWidget(label);

  QDialogButtonBox* buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Ok, popup_msg);

  // change default text
  QPushButton* okButton = buttonBox->button(QDialogButtonBox::Ok);
  okButton->setText("DONE");

  layout->addWidget(buttonBox);

  QObject::connect(buttonBox, &QDialogButtonBox::accepted,
      popup_msg, &QDialog::accept);

  QObject::connect(popup_msg, &QDialog::finished, popup_msg,
      [this, popup_msg, cbk_fn](int result) {
        cbk_fn(popup_msg, result, this);
        popup_msg->deleteLater();
      });

  popup_msg->exec();
#else // EZGL_QT
  //getting window ptr
  GtkWindow* window = GTK_WINDOW(get_object(m_window_id.c_str()));
  GtkWidget* popup_mssg = gtk_dialog_new_with_buttons(
    title,   //title
    window,         //window
    GTK_DIALOG_MODAL, //Button and return_id pairs
    ("DONE"),
    GTK_RESPONSE_ACCEPT,
    NULL
  );

  //Adding the label to the content window
  auto content_area = gtk_dialog_get_content_area(GTK_DIALOG(popup_mssg));
  GtkWidget* label = gtk_label_new(message);
  gtk_container_add(GTK_CONTAINER(content_area), label);

  //Connecting Callback Function
  g_signal_connect(
    GTK_DIALOG(popup_mssg), 
    "response",
    G_CALLBACK(cbk_fn), 
    this
  );

  //Showing the dialog window
  gtk_widget_show_all(popup_mssg);
#endif // EZGL_QT
}

bool application::destroy_widget(const char* widget_name){
  //Searching for widget
  GtkWidget* widget = find_widget(widget_name);

  //If nothing found, returning false
  if(widget == nullptr){
    return false;
  }

  //Deleting widget if found
  gtk_widget_destroy(widget);
  return true;
}

GtkWidget* application::find_widget(const char* widget_name){
#ifdef EZGL_QT
  QWidget* widget = qobject_cast<QWidget*>(get_object(widget_name));
  assert(widget);
  return widget;
#else
  GtkWidget* widget = nullptr;

  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");
  // the text to delete, in c++ string form
  std::string name_to_find = std::string(widget_name);

  // iterate over all of the children in the grid and find the widget by name
  GList *children, *iter;
  children = gtk_container_get_children(GTK_CONTAINER(in_grid));
  for(iter = children; iter != NULL; iter = g_list_next(iter)){
    GtkWidget* current_widget = GTK_WIDGET(iter->data);

    //If found, set widget to that value and break loop
    if(gtk_widget_get_name(current_widget) == name_to_find){
      widget = current_widget;
      update_message("Found widget through name");
      break;
    }
  }
  //If widget is still null (found nothing), running get_object to search glade/main.ui
  if(widget == nullptr){
    widget = GTK_WIDGET(get_object(widget_name));
  }
  
  return widget;
#endif
}


bool application::destroy_button(const char *button_text_to_destroy)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return false;
  }

  QString text_to_del = QString::fromUtf8(button_text_to_destroy ? button_text_to_destroy : "");
  for (int i = 0; i < in_grid->count(); ++i) {
    QWidget* widget = in_grid->itemAt(i)->widget();
    QPushButton* button = qobject_cast<QPushButton*>(widget);
    if (button == nullptr) {
      continue;
    }
    if (button->text() != text_to_del) {
      continue;
    }

    in_grid->removeWidget(button);
    button->deleteLater();
    return true;
  }

  return false;
#else
  // get the inner grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  // the text to delete, in c++ string form
  std::string text_to_del = std::string(button_text_to_destroy);

  // iterate over all of the children in the grid and find the button by it's text
  GList *children, *iter;
  children = gtk_container_get_children(GTK_CONTAINER(in_grid));
  for(iter = children; iter != NULL; iter = g_list_next(iter)) {
    // iterator to widget
    GtkWidget *widget = GTK_WIDGET(iter->data);

    // check if widget is a button
    if(GTK_IS_BUTTON(widget)) {
      // convert to button
      GtkButton *button = GTK_BUTTON(widget);

      // get the button label
      const char *button_label = gtk_button_get_label(button);
      if(button_label != nullptr) {
        std::string button_text = std::string(button_label);

        // does the label of the button match the one we want to delete?
        if(button_text == text_to_del) {
          // destroy the button (widget) and return true
          gtk_widget_destroy(widget);
          // free the children list
          g_list_free (children);
          return true;
        }
      }
    }
  }

  // free the children list
  g_list_free (children);
  // couldn't find the button with the label 'button_text_to_destroy'
  return false;
#endif
}

void application::change_button_text(const char *button_text, const char *new_button_text)
{
#ifdef EZGL_QT
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  const QString text = QString::fromUtf8(button_text ? button_text : "");
  const QString new_text = QString::fromUtf8(new_button_text ? new_button_text : "");

  const QString current_text = QString::fromUtf8(button_text);
  for (int i = 0; i < in_grid->count(); ++i) {
    QWidget* widget = in_grid->itemAt(i)->widget();
    QPushButton* button = qobject_cast<QPushButton*>(widget);
    if (button == nullptr) {
      continue;
    }
    if (button->text() == text) {
      button->setText(new_text);
      break;
    }
  }
#else
  // get the inner grid
  GtkGrid *in_grid = (GtkGrid *)get_object("InnerGrid");

  // the text to change, in c++ string form
  std::string text_to_change = std::string(button_text);

  // iterate over all of the children in the grid and find the button by it's text
  GList *children, *iter;
  children = gtk_container_get_children(GTK_CONTAINER(in_grid));
  for(iter = children; iter != NULL; iter = g_list_next(iter)) {
    // iterator to widget
    GtkWidget *widget = GTK_WIDGET(iter->data);

    // check if widget is a button
    if(GTK_IS_BUTTON(widget)) {
      // convert to button
      GtkButton *button = GTK_BUTTON(widget);

      // get the button label
      const char *button_label = gtk_button_get_label(button);
      if(button_label != nullptr) {
        std::string button_text_str = std::string(button_label);

        // does the label of the button match the one we want to change?
        if(button_text_str == text_to_change) {
          // change button label
          gtk_button_set_label(button, new_button_text);
        }
      }
    }
  }

  // free the children list
  g_list_free (children);
#endif
}

void application::change_canvas_world_coordinates(std::string const &canvas_id,
    rectangle coordinate_system)
{
  // get the canvas
  canvas *cnv = get_canvas(canvas_id);

  // reset the camera system with the new coordinates
  if (cnv != nullptr) {
    cnv->get_camera().reset_world(coordinate_system);
  }
}

void application::refresh_drawing()
{
  // get the main canvas
  canvas *cnv = get_canvas(m_canvas_id);

  // force redrawing
  cnv->redraw();
}

void application::flush_drawing()
{
  // get the main drawing area widget
  GtkWidget *drawing_area = (GtkWidget *)get_object(m_canvas_id.c_str());

  // queue a redraw of the GtkWidget
  gtk_widget_queue_draw(drawing_area);

#ifdef EZGL_QT
  QCoreApplication::processEvents();
#else
  // run the main loop on pending events
  while(gtk_events_pending())
    gtk_main_iteration();
#endif
}

renderer *application::get_renderer()
{
  // get the main canvas
  canvas *cnv = get_canvas(m_canvas_id);

  return cnv->create_animation_renderer();
}

void set_disable_event_loop(bool new_setting)
{
  disable_event_loop = new_setting;
}
}
