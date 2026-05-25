/*
 * Copyright 2019-2024 University of Toronto
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

#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include "ezgl/canvas.hpp"
#include "ezgl/control.hpp"
#include "ezgl/callback.hpp"
#include "ezgl/graphics.hpp"
#include "ezgl/color.hpp"
#include "ezgl/qt/switchbutton.hpp"

#include <QApplication>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <ctime>
#include <vector>

class QWidget;
class QAbstractButton;
class QPushButton;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;

/**
 * A library for creating a graphical user interface.
 */
namespace ezgl {

// Controls whether the GUI layout is loaded from a loose XML file on disk (true) or from a
// compiled-in GResource (false). Defined externally by course infrastructure (ECE297) which
// needs to swap in custom UI files at runtime; all other consumers use the compiled resource.
#ifndef ECE297
const bool build_ui_from_file = false;
#else
const bool build_ui_from_file = true;
#endif

class application;

/**
 * The signature of a function that connects QObject to functions via signals.
 *
 * @see application::get_object.
 */
using connect_g_objects_fn = void (*)(application *app);

/**
 * The signature of a setup callback function
 */
using setup_callback_fn = void (*)(application *app, bool new_window);

/**
 * The signature of a button callback function
 */
using button_callback_fn = void (*)(QWidget *widget, application *app);

/**
 * The signature of a user-defined callback function for mouse events
 */
using mouse_callback_fn = void (*)(application *app, QMouseEvent *event, double x, double y);
/**
 * The signature of a user-defined callback function for keyboard events
 */
using key_callback_fn = void (*)(application *app, QKeyEvent *event, const char *key_name);

/**
 * The signature of a user-defined callback function for the combo-box "changed" signal
 */
using combo_box_callback_fn = void (*)(QComboBox* self, application* app);

/**
 * The signature of a user-defined callback function for a dialog window
 */
using dialog_callback_fn = void (*)(QDialog* self, int response_id, application* app);

/**
 * The core application.
 *
 * The GUI of an application is created from a Glade-format .ui XML file (loaded via ezgl::QtGladeLoader, which
 * materialises the described widgets as Qt widgets). Widgets created in the .ui file can be retrieved from an
 * application object via find_widget(), but only after application::run() has loaded the .ui file (UI loading is
 * deferred from the constructor to run() so that Qt resources from .qrc are registered).
 * application is a singleton class: only create one.
 */
class application : public QApplication {
  Q_OBJECT
public:
  /**
   * Configuration settings for the application.
   *
   * The GUI will be built from the XML description given by main_ui_resource.
   * The .ui file must contain a top-level window widget with the name in
   * window_identifier.
   */
  struct settings {
    /**
     * The resource/file path that contains the XML file, which describes the GUI.
     */
    std::string main_ui_resource;

    /**
     * The name of the main window in the XML file.
     */
    std::string window_identifier;

    /**
     * The name of the main canvas in the XML file. This is where renderer drawing calls appear.
     */
    std::string canvas_identifier;

    /**
     * A user-defined name of the application. Used to make each
     * application instance distinguishable to the desktop environment.
     */
    std::string application_identifier;

    /**
     * Specify the function that will connect GUI objects to user-defined callbacks.
     *
     * GUI objects (QObject instances) can be retrieved from this application object. These objects can then be
     * connected to specific Qt signals via QObject::connect.
     *
     * If not provided, application::register_default_buttons_callbacks function will be used, which assumes that the
     * UI has QPushButton widgets named "ZoomFitButton", "ZoomInButton", "ZoomOutButton", "UpButton", "DownButton",
     * "LeftButton", "RightButton", "ProceedButton"
     */
    connect_g_objects_fn setup_callbacks;

    /**
     * Create the settings structure with default values
     */
    settings()
    : main_ui_resource(build_ui_from_file ? "main_ui" : "/ezgl/main.ui"), window_identifier("MainWindow"), canvas_identifier("MainCanvas"), application_identifier("ezgl.app"),
      setup_callbacks(nullptr)
    {
      // Uniquify the application_identifier by appending a time stamp,
      // so that each instance of the same program has a different application ID.
      // This allows multiple instances of the program to run independently.
      application_identifier += ".t" + std::to_string(std::time(nullptr));
    }

    /**
     * Create the settings structure with user-defined values
     */
    settings(std::string m_resource, std::string w_identifier, std::string c_identifier, std::string a_identifier = "ezgl.app",
        connect_g_objects_fn s_callbacks = nullptr)
    : main_ui_resource(m_resource), window_identifier(w_identifier), canvas_identifier(c_identifier), application_identifier(a_identifier),
      setup_callbacks(s_callbacks)
    {
      // Uniquify the application_identifier by appending a time stamp,
      // so that each instance of the same program has a different application ID.
      // This allows multiple instance of the program to run independently.
      application_identifier += ".t" + std::to_string(std::time(nullptr));
    }
  };

public:
  /**
   * Create an application.
   *
   * @param s The preconfigured settings.
   */
  explicit application(application::settings s, int& argc, char** argv);

  /**
   * Add a canvas to the application.
   *
   * If the canvas has already been added, it will not be overwritten and a warning will be displayed.
   *
   * @param canvas_id The id of the DrawingAreaWidget (or RhiCanvasWidget under the rhi backend) in the .ui XML file.
   * @param draw_callback The function to call that draws to this canvas.
   * @param coordinate_system The initial coordinate system of this canvas. 
   *            coordinate_system.first gives the (x,y) world coordinates of the lower left corner, 
   *            and coordinate_system.second gives the (x,y) coordinates of the upper right corner.
   * @param background_color (OPTIONAL) The color of the canvas background. Default is WHITE.
   *
   * @return A pointer to the newly created canvas.
   */
  canvas *add_canvas(std::string const &canvas_id,
      draw_canvas_fn draw_callback,
      rectangle coordinate_system,
      color background_color = WHITE);

  /**
   * @note The following functions create UI Elements and add them to the grid "InnerGrid".
   * The example main.ui file already includes a grid called "InnerGrid", as well as the Zoom and pan buttons.
   * As long as a grid called "InnerGrid" exists, the functions will work and add the UI elements to that grid.
   */

  /**
   * Add a button that you can click on to call its callback function.
   *
   * @param button_text the new button text
   * @param left the column number to attach the left side of the new button to
   * @param top the row number to attach the top side of the new button to
   * @param width the number of columns that the button will span
   * @param height the number of rows that the button will span
   * @param button_func callback function for the button
   *
   * The function assumes that the UI has a grid named "InnerGrid"
   */
  void create_button(const char *button_text,
      int left,
      int top,
      int width,
      int height,
      button_callback_fn button_func);

  /**
   * Add a button convenience
   * Adds a button at a given row index (assuming buttons in the right bar use 1 row each, with the top button at row 0)
   * by inserting a row in the grid and adding the button. Uses the default width of 3 and height of 1
   * 
   * @param button_text the new button text
   * @param insert_row the row in the right bar to insert the button.
   *         If there is already a button there, it and the following buttons shift down 1 row.
   * @param button_func callback function for the button
   *          fn prototype: void fn_name(QPushButton* self, ezgl::application* app);
   * The function assumes that the UI has a grid named "InnerGrid"
   */
  void create_button(const char *button_text, int insert_row, button_callback_fn button_func);

  /**
   * Deletes a button by its label (displayed text)
   * 
   * @param the text of the button to delete
   * @return whether the button was found and deleted
   *
   * The function assumes that the UI has a grid named "InnerGrid"
   */
  bool destroy_button(const char *button_text_to_destroy);


  /**
   * Change the label of the button (displayed text)
   *
   * @param button_text the old text of the button
   * @param new_button_text the new button text
   *
   * The function assumes that the UI has a grid named "InnerGrid"
   */
  void change_button_text(const char *button_text, const char *new_button_text);


  /**
   * @brief Creates a label object (a text label) in the Inner Grid
   * 
   * Label convenience function. Assumes default height of 1 and width of 3. 
   * Creates Label object at insert_row in Inner Grid. Also sets name of label to text.
   * If you ever need to delete or find the widget, use find_widget with the label_text
   * 
   * @param insert_row Row where label will be placed
   * @param label_text Text of Label
   */
  void create_label(int insert_row, const char *label_text);

  /**
   * @brief Create a label object in Inner Grid at specified position/dimensions
   * 
   * Creates a label and sets its name to given text, which can be used with find_widget to access it
   * @param left the column number to attach the left side of the new button to
   * @param top the row number to attach the top side of the new button to
   * @param width the number of columns that the button will span
   * @param height the number of rows that the button will span
   * @param label_text Text of Label
   */
  void create_label(
    int left,
    int top,
    int width,
    int height,
    const char *label_text
  );

  /**
   * @brief Creates a combo box (QComboBox) in Inner Grid
   * A combo box is a dropdown menu with different options.
   * EZGL provides functions to modify the options in your combo box, and
   * you can connect a callback function to the signal sent when the
   * selected option is changed.
   *
   * Creates a combo box at the row id given by insert_row. Assumes
   * default height of 1 and width of 3.
   * 
   * @param id_string A id string used to track combo box. Can be any UNIQUE string, not a label/not visible
   *              used to identify widget to destroy/modify it.
   * @param insert_row  the row in the right bar to insert the button.
   *         If there is already a button there, it and the following buttons shift down 1 row.
   * @param combo_box_fn Callback function for "changed" signal, emmitted when a new option is selected.
   *              fn prototype: void fn_name(QComboBox* self, ezgl::application* app);
   * @param options A string vector containing the options to be contained in the combo box. String at index 0 is set as default
   */
  void create_combo_box_text(
    const char* id_string,
    int insert_row,
    combo_box_callback_fn combo_box_fn,
    const std::vector<std::string>& options);

  /**
   * @brief Create a combo box text object
   * 
   * 
   * Creates a QComboBox at the given location. 
   * A combo box is a dropdown menu with different options. 
   * EZGL provides functions to modify the options in your combo box, and 
   * you can connect a callback function to the signal sent when the 
   * selected option is changed
   * 
   * @param id_string A id string used to track combo box. Can be any UNIQUE string, not a label/not visible
   *              used to identify widget to destroy/modify it.
   * @param left the column number to attach the left side of the new button to
   * @param top the row number to attach the top side of the new button to
   * @param width the number of columns that the button will span
   * @param height the number of rows that the button will span
   * @param combo_box_fn Callback function for "changed" signal, emmitted when a new option is selected.
   *              fn prototype: void fn_name(QComboBox* self, ezgl::application* app);
   * @param options A string vector containing the options to be contained in the combo box. String at index 0 is set as default
   */
  void create_combo_box_text(
    const char* id_string,
    int left,
    int top,
    int width,
    int height,
    combo_box_callback_fn combo_box_fn, 
    const std::vector<std::string>& options);

  /**
   * @brief changes list of options to new given vector. Erases all old options. 
   * 
   * This will call your callback function. Make sure you have some check that returns/ends the function if
   * your combo box has no active id (this occurs while erasing the old options)

   * @param id_string identifying string of QComboBox, given in creation
   * @param new_options new string vector of options
   */
  void change_combo_box_text_options(const char* name, const std::vector<std::string>& new_options);

  /**
   * @brief Creates a simple dialog window with "OK" and "CANCEL" buttons. 
   *
   * This function creates a dialog window with three buttons that send the following response_ids
   * (the values are Qt's QDialog::DialogCode plus QDialog::Rejected for the close (X) button):
   * OK - QDialog::Accepted
   * CANCEL - QDialog::Rejected
   * X - QDialog::Rejected
   * It is dynamically created and shown through this function. Hitting any option in the dialog will
   * run the attached cbk fn. Follow the given fn prototype and use the response_id to act accordingly.
   * The dialog deletes itself on close (Qt::WA_DeleteOnClose); no manual destroy is required.
   *
   * @param cbk_fn Dialog callback function. Function prototype:
   *              void dialog_cbk(QDialog* self, int response_id, application* app);
   * @param dialog_title Title of the window to be created
   * @param window_text Message to be contained in a label in the window
   */
  void create_dialog_window(dialog_callback_fn cbk_fn, const char* dialog_title, const char *window_text);

  /**
   * @brief Creates a popup message with a "DONE" button. This version has a default callback
   * 
   * Creates a popup window that will hold focus until user hits done button. This version has a default
   * callback function that will just close the dialog window. popup is destroyed when user presses "DONE"
   * 
   * @param title Popup Message Title
   * @param message Popup Message Body
   */
  void create_popup_message(const char* title, const char *message);

  /**
   * @brief Creates a popup message with a "DONE" button. This version takes a callback function
   * 
   * Creates a popup window that will hold focus until user hits done button. You can pass
   * a callback function, which is called when user hits DONE. This dialog window only has one button.
   * The popup deletes itself on close (Qt::WA_DeleteOnClose); no manual destroy is required.
   * 
   * @param cbk_fn Popup Callback Function
   * @param title Popup Message Title
   * @param message Popup Message Body
   */
  void create_popup_message_with_callback(dialog_callback_fn cbk_fn, const char* title, const char *message);

  /**
   * @brief Destroys widget.
   * 
   * @param widget_name The ID assigned in the .ui file / name set in the creation function
   * @return true if widget found and destroyed, false if not found
   */
  bool destroy_widget(const char* widget_name);

  /**
   * @brief Searches inner grid for widget with given name
   * 
   * This function will search the inner grid (sidebar) for the widget with the given name/id.
   * It will return a Widget ptr to it. This function is powerful; it will search through, in this order:
   * String IDs assigned to widgets in the .ui file
   * Names set using ezgl::application method functions that make widgets (i.e create_combo_box)
   * Button labels set using application::create_button
   * 
   * @param widget_name string to be searched for
   * @return QWidget* Pointer to QWidget. Can be cast to appropriate type
   */
  QWidget* find_widget(const char* widget_name, bool skip_notfound_report = false) const;

  /**
   * Update the message in the status bar
   *
   * @param message The message that will be displayed on the status bar
   *
   * The function assumes that the UI has a QStatusBar named "StatusBar"
   */
  void update_message(std::string const &message);

  /**
   * Change the coordinate system of a previously created canvas
   * 
   * This changes the current visible world (as set_visible_world would) and also changes 
   * the saved initial coordinate_system so that Zoom Fit shows the proper area.
   *
   * @param canvas_id The id of the DrawingAreaWidget (or RhiCanvasWidget under the rhi backend) in the .ui XML file, e.g. "MainCanvas"
   * @param coordinate_system The new coordinate system of this canvas.
   */
  void change_canvas_world_coordinates(std::string const &canvas_id, rectangle coordinate_system);

  /**
   * redraw the main canvas 
   * 
   * Useful to force an immediate redraw when you want a different graphics display
   */
  void refresh_drawing();

  /**
   * Get a renderer that can be used to draw on top of the main canvas
   * 
   * Most common usage is to get a renderer in an animation callback.
   */
  renderer *get_renderer();

  /**
   * Flush the drawing done by the renderer to the on-screen buffer
   *
   * The flushing is done immediately. Useful when you are drawing an animation and need the graphics
   * to update immediatey, instead of the usual allowing them to be buffered until user in put is requested.
   */
  void flush_drawing();

  /**
   * Run the application.
   *
   * Once this is called, the application will be initialized first. Initialization will build the GUI based on the XML
   * resource given in the constructor. Once the GUI has been created, the function initial_setup_user_callback will be
   * called; you can use that callback to create additional widgets and/or connect additional signals.
   *
   * After initialization, control of the program will be given to the Qt event loop. You will only regain control
   * for the signals that you have registered callbacks for.
   *
   * @param initial_setup_user_callback A user-defined function that is called before application activation
   * @param mouse_press_user_callback The user-defined callback function for mouse press
   * @param mouse_move_user_callback The user-defined callback function for mouse move
   * @param key_press_user_callback The user-defined callback function for keyboard press
   *
   * @return The exit status.
   */
  int run(setup_callback_fn initial_setup_user_callback,
      mouse_callback_fn mouse_press_user_callback,
      mouse_callback_fn mouse_move_user_callback,
      key_callback_fn key_press_user_callback);

  /**
   * Destructor.
   */
  ~application();

  /**
   * Copies are disabled.
   */
  application(application const &) = delete;

  /**
   * Copies are disabled.
   */
  application &operator=(application const &) = delete;

  /**
   * Ownership of an application is transferrable.
   */
  application(application &&) = default;

  /**
   * Ownership of an application is transferrable.
   */
  application &operator=(application &&) = default;

  /**
   * Retrieve a pointer to a canvas that was previously added to the application.
   *
   * Calling this function before application::run results in undefined behaviour.
   *
   * @param canvas_id The key used when the canvas was added (e.g. "MainCanvas")
   *
   * @return A non-owning pointer, or nullptr if not found.
   *
   * @see application::get_object
   */
  canvas *get_canvas(std::string const &canvas_id) const;

  QPushButton* find_push_button(const char *name, bool skip_notfound_report = false) const;
  QLineEdit* find_line_edit(const char *name) const;
  QComboBox* find_combo_box(const char *name) const;
  QSpinBox* find_spin_box(const char *name) const;
  QCheckBox* find_check_box(const char *name) const;
  SwitchButton* find_switch_button(const char *name) const;

  void hide_widget(const std::string& widgetName);
  void show_widget(const std::string& widgetName);

  /**
   * Get the ID of the main window
   */
  std::string get_main_window_id() const
  {
    return m_window_id;
  }

  /**
   * Get the ID of the main canvas 
   */
  std::string get_main_canvas_id() const
  {
    return m_canvas_id;
  }

  /**
   * Quit the application
   */
  void quit();

  /**
   * Schedule a callback to run as soon as the Qt event loop starts pumping.
   *
   * Posts the callback via Qt::QueuedConnection so it fires after exec()
   * begins, regardless of whether platform input events arrive. Used to drive
   * scripted --graphics_commands under --disp on + QT_QPA_PLATFORM=offscreen,
   * where no user input is delivered to wake the loop.
   */
  void schedule_initial_callback(std::function<void()> callback);

protected:
  bool notify(QObject* receiver, QEvent* event) override;

private:
  // The package path to the XML file that describes the UI.
  std::string m_main_ui;

  // The ID of the main window in the .ui XML file.
  std::string m_window_id;

  // The ID of the main canvas. This canvas is where ezgl renderer calls (e.g. draw_line) display
  std::string m_canvas_id;

  // The application identifier (used to make each application instance distinguishable).
  std::string m_application_id;

  QWidget* m_window{nullptr};

  // The function to call when the application is starting up.
  connect_g_objects_fn m_register_callbacks{nullptr};

  // The collection of canvases added to the application.
  std::map<std::string, std::unique_ptr<canvas>> m_canvases;

  // A flag that indicates if the run() was called before or not to allow multiple reruns
  bool first_run;

  // Holds the most recent status-bar message pushed before the StatusBar
  // widget existed (i.e. before run() loaded main.ui). Flushed in init()
  // once the widget tree is available. Only the latest message is kept,
  // mirroring update_message's "clear-then-show" semantics.
  QString m_pending_message;

private:
  void init();

  // Called during application activation to setup the default callbacks for the prebuilt buttons
  static void register_default_buttons_callbacks(application *application);

  // Called during application activation to setup the default callbacks for the mouse and key events
  static void register_default_events_callbacks(application *application);

public:
  // The user-defined initial setup callback function
  setup_callback_fn initial_setup_callback{nullptr};

  // The user-defined callback function for handling mouse press
  mouse_callback_fn mouse_press_callback{nullptr};

  // The user-defined callback function for handling mouse move
  mouse_callback_fn mouse_move_callback{nullptr};

  // The user-defined callback function for handling keyboard press
  key_callback_fn key_press_callback{nullptr};
};

/**
 * Set the disable_event_loop flag to new_setting
 * Call with new_setting == true to make the event_loop immediately return. This is useful for 
 * unit tests, to ensure the GUI doesn't wait for user input in an automatic test.
 *
 * @param new_setting The new state of disable_event_loop flag
 */
void set_disable_event_loop(bool new_setting);
}

#endif //EZGL_APPLICATION_HPP
