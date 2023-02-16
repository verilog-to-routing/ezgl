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

#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP
#define ECE297

#include "ezgl/canvas.hpp"
#include "ezgl/control.hpp"
#include "ezgl/callback.hpp"
#include "ezgl/graphics.hpp"
#include "ezgl/color.hpp"

#include <map>
#include <memory>
#include <string>
#include <ctime>
#include <vector>

#include <gtk/gtk.h>

/**
 * A library for creating a graphical user interface.
 */
namespace ezgl {

// A flag to specify whether the GUI is built from an XML file or an XML resource
#ifndef ECE297
const bool build_ui_from_file = false;
#else
const bool build_ui_from_file = true;
#endif

class application;

/**
 * The signature of a function that connects GObject to functions via signals.
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
using button_callback_fn = void (*)(GtkWidget *widget, application *app);

/**
 * The signature of a user-defined callback function for mouse events
 */
using mouse_callback_fn = void (*)(application *app, GdkEventButton *event, double x, double y);

/**
 * The signature of a user-defined callback function for keyboard events
 */
using key_callback_fn = void (*)(application *app, GdkEventKey *event, char *key_name);

/**
 * The signature of a user-defined callback function for the combo-box "changed" signal
 */
using combo_box_callback_fn = void (*)(GtkComboBoxText* self, application* app);

/**
 * The signature of a user-defined callback function for a dialog window
 */
using dialog_callback_fn = void (*)(GtkDialog* self, gint response_id, application* app);

/**
 * The core application.
 *
 * The GUI of an application is created from an XML file. Widgets created in the XML file can be retrieved from an
 * application object, but only after the object has been initialized by GTK via application::run.
 * application is a singleton class: only create one.
 */
class application {
public:
  /**
   * Configuration settings for the application.
   *
   * The GUI will be built from the XML description given by main_ui_resource.
   * The XML file must contain a GtkWindow with the name in window_identifier.
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
     * A user-defined name of the GTK application
     *
     * Application identifiers should follow the following format:
     * https://developer.gnome.org/gio/stable/GApplication.html#g-application-id-is-valid
     * Use g_application_id_is_valid () to check its validity
     */
    std::string application_identifier;

    /**
     * Specify the function that will connect GUI objects to user-defined callbacks.
     *
     * GUI objects (i.e., a GObject) can be retrieved from this application object. These objects can then be connected
     * to specific events using g_signal_connect. A list of signals that can be used to make these connections can be
     * found <a href = "https://docs.gtk.org/gtk3/class.Widget.html#signals">here</a>.
     *
     * If not provided, application::register_default_buttons_callbacks function will be used, which assumes that the
     * UI has GtkButton widgets named "ZoomFitButton", "ZoomInButton", "ZoomOutButton", "UpButton", "DownButton",
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
      // This allows multiple instances of the program to run independelty.
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
      // This allows multiple instance of the program to run independelty.
      application_identifier += ".t" + std::to_string(std::time(nullptr));
    }
  };

public:
  /**
   * Create an application.
   *
   * @param s The preconfigured settings.
   */
  explicit application(application::settings s);

  /**
   * Add a canvas to the application.
   *
   * If the canvas has already been added, it will not be overwritten and a warning will be displayed.
   *
   * @param canvas_id The id of the GtkDrawingArea in the ui XML file.
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
   * @note The following functions create UI Elements and add them to the Gtk Grid "InnerGrid".
   * The example main.ui file already includes a grid called "InnerGrid", as well as the Zoom and pan buttons.
   * As long a GtkGrid called "InnerGrid" exists, the functions will work and add the UI elements to that grid. 
   */

  /**
   * Add a button
   *
   * @param button_text the new button text
   * @param left the column number to attach the left side of the new button to
   * @param top the row number to attach the top side of the new button to
   * @param width the number of columns that the button will span
   * @param height the number of rows that the button will span
   * @param button_func callback function for the button
   *
   * The function assumes that the UI has a GtkGrid named "InnerGrid"
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
   *          fn prototype: void fn_name(GtkButton* self, ezgl::application* app);
   * The function assumes that the UI has a GtkGrid named "InnerGrid"
   */
  void create_button(const char *button_text, int insert_row, button_callback_fn button_func);

  /**
   * Deletes a button by its label (displayed text)
   * 
   * @param the text of the button to delete
   * @return whether the button was found and deleted
   *
   * The function assumes that the UI has a GtkGrid named "InnerGrid"
   */
  bool destroy_button(const char *button_text_to_destroy);


  //SEB NEW STARTS HERE
  //===================================================
  /**
   * @brief Creates a label object in Inner Grid
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
   * @brief Creates a GTK combo box object in Inner Grid
   * 
   * GTK Combo Box convenience function. Creates a combo box at the row id given by
   * insert_row. Assumes default height of 1 and width of 3
   * 
   * @param id_string A id string used to track combo box. Can be any UNIQUE string, not a label/not visible
   *              used to identify widget to destroy/modify it.
   * @param insert_row  the row in the right bar to insert the button.
   *         If there is already a button there, it and the following buttons shift down 1 row.
   * @param combo_box_fn Callback function for "changed" signal, emmitted when a new option is selected.
   *              fn prototype: void fn_name(GtkComboBoxText* self, ezgl::application* app);
   * @param options A string vector containing the options to be contained in the combo box. String at index 0 is set as default
   */
  void create_combo_box_text(
    const char* id_string,
    int insert_row, 
    combo_box_callback_fn combo_box_fn, 
    std::vector<std::string> options);

  /**
   * @brief Create a combo box text object
   * 
   * 
   * Creates a GtkComboBox at the given location. A combo box is a dropdown menu with different options. EZGL provides functions to modify 
   * the options in your combo box, and you can connect a callback function to the signal sent when the selected option is changed
   * 
   * @param id_string A id string used to track combo box. Can be any UNIQUE string, not a label/not visible
   *              used to identify widget to destroy/modify it.
   * @param left the column number to attach the left side of the new button to
   * @param top the row number to attach the top side of the new button to
   * @param width the number of columns that the button will span
   * @param height the number of rows that the button will span
   * @param combo_box_fn Callback function for "changed" signal, emmitted when a new option is selected.
   *              fn prototype: void fn_name(GtkComboBoxText* self, ezgl::application* app);
   * @param options A string vector containing the options to be contained in the combo box. String at index 0 is set as default
   */
  void create_combo_box_text(
    const char* id_string,
    int left,
    int top,
    int width,
    int height,
    combo_box_callback_fn combo_box_fn, 
    std::vector<std::string> options);

  /**
   * @brief changes list of options to new given vector. Erases all old options. 
   * 
   * This will call your callback function. Make sure you have some check that returns/ends the function if
   * your combo box has no active id (this occurs while erasing the old options)

   * @param id_string identifying string of GtkComboBoxText, given in creation
   * @param new_options new string vector of options
   */
  void change_combo_box_text_options(const char* name, std::vector<std::string> new_options);

  /**
   * @brief Creates a simple dialog window with "OK" and "CANCEL" buttons. 
   *
   * This function creates a dialog window with three buttons that send the following response_ids:
   * OK - GTK_RESPONSE_ACCEPT
   * CANCEL - GTK_RESPONSE_REJECT
   * X - GTK_RESPONSE_DELETE_EVENT
   * It is dynamically created and shown through this function. Hitting any option in the dialog will
   * run the attached cbk fn. Follow the given fn prototype and use the response_id to act accordingly.
   * you must call gtk_widget_destroy(ptr to dialog window) in your cbk function.
   *
   * @param cbk_fn Dialog callback function. Function prototype:
   *              void dialog_cbk(GtkDialog* self, gint response_id, application* app);
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
   * Make sure to call gtk_widget_destroy(ptr to popup) to close the popup in the cbk fn
   * 
   * @param cbk_fn Popup Callback Function
   * @param title Popup Message Title
   * @param message Popup Message Body
   */
  void create_popup_message_with_callback(dialog_callback_fn cbk_fn, const char* title, const char *message);

  /**
   * @brief Destroys widget.
   * 
   * @param widget_name The ID given in Glade/Name set in creation function
   * @return true if widget found and destroyed, false if not found
   */
  bool destroy_widget(const char* widget_name);

  /**
   * @brief Searches inner grid for widget with given name
   * 
   * This function will search the inner grid (sidebar) for the widget with the given name/id. 
   * It will return a Widget ptr to it. This function is powerful; it will search through, in this order:
   * String IDs created in Glade for widgets
   * Names set using ezgl::application method functions that make widgets (i.e create_combo_box)
   * Button labels set using application::create_button 
   * 
   * @param widget_name string to be searched for
   * @return GtkWidget* GtkWdiget to pointer. can be cast to appropriate type
   */
  GtkWidget* find_widget(const char* widget_name);

  //SEB NEW ENDS HERE

  /**
   * Change the label of the button (displayed text)
   *
   * @param button_text the old text of the button
   * @param new_button_text the new button text
   *
   * The function assumes that the UI has a GtkGrid named "InnerGrid"
   */
  void change_button_text(const char *button_text, const char *new_button_text);

  /**
   * Update the message in the status bar
   *
   * @param message The message that will be displayed on the status bar
   *
   * The function assumes that the UI has a GtkStatusbar named "StatusBar"
   */
  void update_message(std::string const &message);

  /**
   * Change the coordinate system of a previously created canvas
   * 
   * This changes the current visible world (as set_visible_world would) and also changes 
   * the saved initial coordinate_system so that Zoom Fit shows the proper area.
   *
   * @param canvas_id The id of the GtkDrawingArea in the XML file, e.g. "MainCanvas"
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
   * After initialization, control of the program will be given to GTK. You will only regain control for the signals
   * that you have registered callbacks for.
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

  /**
   * Retrieve a GLib Object (i.e., a GObject).
   *
   * This is useful for retrieving GUI elements specified in your ui XML file(s). You should only call this function after
   * the application has been run, otherwise the GUI elements will have not been created yet.
   *
   * @param name The ID of the object.
   * @return The object with the ID, or NULL if it could not be found.
   *
   * @see application::run
   */
  GObject *get_object(gchar const *name) const;

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

private:
  // The package path to the XML file that describes the UI.
  std::string m_main_ui;

  // The ID of the main window to add to our GTK application.
  std::string m_window_id;

  // The ID of the main canvas. This canvas is where ezgl renderer calls (e.g. draw_line) display
  std::string m_canvas_id;

  // The ID of the GTK application
  std::string m_application_id;

  // The GTK application.
  GtkApplication *m_application;

  // The GUI builder that parses an XML user interface.
  GtkBuilder *m_builder;

  // The function to call when the application is starting up.
  connect_g_objects_fn m_register_callbacks;

  // The collection of canvases added to the application.
  std::map<std::string, std::unique_ptr<canvas>> m_canvases;

  // A flag that indicates if the run() was called before or not to allow multiple reruns
  bool first_run;

  // A flag that indicates if we are resuming an older run to allow proper quitting
  bool resume_run;

private:
  // Called when our GtkApplication is initialized for the first time.
  static void startup(GtkApplication *gtk_app, gpointer user_data);

  // Called when GTK activates our application for the first time.
  static void activate(GtkApplication *gtk_app, gpointer user_data);

  // Called during application activation to setup the default callbacks for the prebuilt buttons
  static void register_default_buttons_callbacks(application *application);

  // Called during application activation to setup the default callbacks for the mouse and key events
  static void register_default_events_callbacks(application *application);

public:
  // The user-defined initial setup callback function
  setup_callback_fn initial_setup_callback;

  // The user-defined callback function for handling mouse press
  mouse_callback_fn mouse_press_callback;

  // The user-defined callback function for handling mouse move
  mouse_callback_fn mouse_move_callback;

  // The user-defined callback function for handling keyboard press
  key_callback_fn key_press_callback;
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
