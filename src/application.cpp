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
#include "ezgl/qt/switchbutton.hpp"

#include <QObject>
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>

#include "ezgl/qt/qtgladeloader.hpp"

namespace ezgl {

// A flag to disable event loop (default is false)
// This allows basic scripted testing even if the GUI is on (return immediately when the event loop is called)
bool disable_event_loop = false;

namespace {

QGridLayout* inner_grid_layout(application const* app)
{
  QWidget* inner_grid = app->find_widget("InnerGrid");
  return_val_if_fail(inner_grid != nullptr, nullptr);

  QGridLayout* layout = qobject_cast<QGridLayout*>(inner_grid->layout());
  return_val_if_fail(layout != nullptr, nullptr);
  return layout;
}

void insert_grid_row(QGridLayout* layout, int insert_row)
{
  return_if_fail(layout != nullptr);

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

void application::startup(Application *gtk_app, void* user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  return_if_fail(ezgl_app != nullptr);

  for(auto &c_pair : ezgl_app->m_canvases) {
    QWidget *drawing_area = ezgl_app->find_widget(c_pair.second->id());
    c_pair.second->initialize(drawing_area);
  }

  q_info("application::startup successful.");
}

void application::activate(Application*, void* user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  return_if_fail(ezgl_app != nullptr);

#ifdef EZGL_RHI
  for (auto &c_pair : ezgl_app->m_canvases)
    c_pair.second->begin_deferred_redraw_cycle();
#endif

  // The main parent window needs to be explicitly added to our GTK application.
  QWidget *window = ezgl_app->find_widget(ezgl_app->m_window_id.c_str());
  window->show();

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

#ifdef EZGL_RHI
  for (auto &c_pair : ezgl_app->m_canvases)
    c_pair.second->end_deferred_redraw_cycle();
#endif

  q_info("application::activate successful.");
}

application::application(application::settings s, int& argc, char** argv)
    : m_main_ui(s.main_ui_resource)
    , m_window_id(s.window_identifier)
    , m_canvas_id(s.canvas_identifier)
    , m_application_id(s.application_identifier)
    , m_application(new Application(argc, argv))
{
  // we moved this to run method

  m_application->setApplicationName(s.application_identifier.c_str());
  m_application->setApp(this);
  
  qInfo() << m_application->applicationName();
  qInfo() << m_application->arguments();
  // NOTE: do NOT load the UI file here. This constructor runs as a static
  // initializer (before main()), so Qt resources registered by the
  // application's .qrc file are not yet available.  Loading is deferred to
  // run(), which is called from main() after all static initializers have
  // completed.

  first_run = true;
  resume_run = false;
}

application::~application()
{
  q_debug("application::~application");
  // Do NOT delete m_application here.  ezgl::application is typically a
  // file-scope static, so its destructor runs during static teardown.
  // Deleting QApplication at that point crashes because Qt's own internal
  // statics (font cache, style engine, etc.) may already be destroyed.
  // The process is exiting; the OS reclaims the memory.
  m_application->quit();
}

canvas *application::get_canvas(const std::string &canvas_id) const
{
  auto it = m_canvases.find(canvas_id);
  if(it != m_canvases.end()) {
    return it->second.get();
  }

  q_warning("Could not find canvas with name %s.", canvas_id.c_str());
  return nullptr;
}

canvas *application::add_canvas(std::string const &canvas_id,
    draw_canvas_fn draw_callback,
    rectangle coordinate_system,
    color background_color)
{
  if(draw_callback == nullptr) {
    // A NULL draw callback means the canvas will never render anything to the screen.
    q_warning("Canvas %s's draw callback is NULL.", canvas_id.c_str());
  }

  // Can't use make_unique with protected constructor without fancy code that will confuse students, so we use new
  // instead.
  std::unique_ptr<canvas> canvas_ptr(new canvas(canvas_id, draw_callback, coordinate_system, background_color));
  auto it = m_canvases.emplace(canvas_id, std::move(canvas_ptr));

  if(!it.second) {
    // std::map's emplace does not insert the value when the key is already present.
    q_warning("Duplicate key (%s) ignored in application::add_canvas.", canvas_id.c_str());
  } else {
    q_info("The %s canvas has been added to the application.", canvas_id.c_str());
  }

  return it.first->second.get();
}

QWidget *application::find_widget(char const *name) const
{
  QWidget* found = nullptr;
  for (QWidget* widget: QApplication::allWidgets()) {
    if (widget->objectName() == name) {
      found = widget;
      break;
    }
  }
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

QPushButton* application::find_push_button(const char *name) const
{
  QPushButton* found = qobject_cast<QPushButton*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

QLineEdit* application::find_line_edit(const char *name) const
{
  QLineEdit* found = qobject_cast<QLineEdit*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

QComboBox* application::find_combo_box(const char *name) const
{
  QComboBox* found = qobject_cast<QComboBox*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

QSpinBox* application::find_spin_box(const char *name) const
{
  QSpinBox* found = qobject_cast<QSpinBox*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

QCheckBox* application::find_check_box(const char *name) const
{
  QCheckBox* found = qobject_cast<QCheckBox*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
}

SwitchButton* application::find_switch_button(const char *name) const
{
  SwitchButton* found = qobject_cast<SwitchButton*>(find_widget(name));
  return_val_if_fail(found != nullptr, nullptr);
  return found;
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
    q_info("The event loop is now starting.");
    return m_application->exec();
  } else {
    // Subsequent stage: reuse the existing window.
    // activate() is NOT called again to avoid double-registering callbacks.
#ifdef EZGL_RHI
    for (auto &c_pair : m_canvases)
      c_pair.second->begin_deferred_redraw_cycle();
#endif
    m_window->show();
    if (initial_setup_callback != nullptr)
      initial_setup_callback(this, false);
#ifdef EZGL_RHI
    for (auto &c_pair : m_canvases)
      c_pair.second->end_deferred_redraw_cycle();
#endif
    resume_run = true;
    q_info("The event loop is now resuming.");
    return m_application->exec();
  }
}

void application::quit()
{
  if(resume_run) {
    // Quit the event loop (exit gtk_main())
    qApp->exec();
  } else {
    // Quit the GTK application (exit g_application_run())
    m_application->exit(0);
  }
}

void application::register_default_events_callbacks(ezgl::application *application)
{
  // Get a pointer to the main window GUI object by using its name.
  std::string main_window_id = application->get_main_window_id();
  QWidget *window = application->find_widget(main_window_id.c_str());

  // Get a pointer to the main canvas GUI object by using its name.
  std::string main_canvas_id = application->get_main_canvas_id();
  QWidget *main_canvas = application->find_widget(main_canvas_id.c_str());
}

void application::register_default_buttons_callbacks(ezgl::application *application)
{
  // Helper: only connect if the button exists in this UI (VPR's main.ui omits
  // several navigation buttons that the basic-application example has).
  auto connect_if_present = [&](const char* name, auto slot) {
    QPushButton* btn = application->find_push_button(name);
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
  QWidget* window = application->find_widget(application->get_main_window_id().c_str());
  if (window) {
    // Prevent Qt from deleting the window on close so it can be reused
    // across stages (m_window->show() in subsequent run() calls).
    window->setAttribute(Qt::WA_DeleteOnClose, false);

    QObject::connect(application->m_application, &QApplication::lastWindowClosed,
                     application->m_application, [application](){
      press_proceed(nullptr, application);
    });
  }
}

void application::update_message(std::string const &message)
{
  // Get the StatusBar Widget
  QStatusBar* status_bar = qobject_cast<QStatusBar*>(find_widget("StatusBar"));

  if (status_bar) {
    // Remove all previous messages from the message stack
    status_bar->clearMessage();

    // Push user message to the message stack
    status_bar->showMessage(QString::fromStdString(message));
  } else {
    qCritical() << "object with name `StatusBar` wasn't found";
  }
}

void application::create_button(const char *button_text,
    int left,
    int top,
    int width,
    int height,
    button_callback_fn button_func)
{
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
}

void application::create_button(const char *button_text,
    int insert_row,
    button_callback_fn button_func)
{
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_button(button_text, 0, insert_row, 3, 1, button_func);
}

void application::create_label(int insert_row, const char *label_text){
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_label(0, insert_row, 3, 1, label_text);
}

void application::create_label(
  int left,
  int top,
  int width,
  int height,
  const char *label_text)
{
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  QString text = QString::fromUtf8(label_text ? label_text : "");
  QLabel* new_label = new QLabel(text);
  new_label->setObjectName(text);
  in_grid->addWidget(new_label, top, left, height, width);
  new_label->show();
}

void application::create_combo_box_text(
  const char* name,
  int insert_row,
  combo_box_callback_fn callback,
  const std::vector<std::string>& options)
{
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  insert_grid_row(in_grid, insert_row);
  create_combo_box_text(name, 0, insert_row, 3, 1, callback, options);
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
}

void application::change_combo_box_text_options(const char* name, const std::vector<std::string>& new_options){
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
}

void application::create_dialog_window(
  dialog_callback_fn cbk_fn,
  const char* dialog_title,
  const char *window_text)
{
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
}

// A default callback function that closes the dialog box when user hits done. DO NOT CALL OR USE EXTERNALLY
static void default_popup_cbk(QDialog* /*self*/, int /*response_id*/, ezgl::application* /*app*/){
  // dialog is destroyed via deleteLater() after this callback returns
}

void application::create_popup_message(const char* title, const char *message)
{
  create_popup_message_with_callback(default_popup_cbk, title, message);
}

void application::create_popup_message_with_callback(dialog_callback_fn cbk_fn, const char* title, const char *message){
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
}

bool application::destroy_widget(const char* widget_name){
  //Searching for widget
  QWidget* widget = find_widget(widget_name);

  //If nothing found, returning false
  if(widget == nullptr){
    return false;
  }

  //Deleting widget if found
  widget->deleteLater();
  return true;
}

bool application::destroy_button(const char *button_text_to_destroy)
{
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
}

void application::change_button_text(const char *button_text, const char *new_button_text)
{
  QGridLayout* in_grid = inner_grid_layout(this);
  if (in_grid == nullptr) {
    return;
  }

  const QString text = QString::fromUtf8(button_text ? button_text : "");
  const QString new_text = QString::fromUtf8(new_button_text ? new_button_text : "");

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
  QWidget *drawing_area = find_widget(m_canvas_id.c_str());

  // queue a redraw of the widget and process pending events immediately
  drawing_area->update();
  QCoreApplication::processEvents();
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
