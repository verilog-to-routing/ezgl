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

#ifndef EZGL_CALLBACK_HPP
#define EZGL_CALLBACK_HPP

#include "ezgl/camera.hpp"
#include "ezgl/canvas.hpp"
#include "ezgl/control.hpp"
#include "ezgl/graphics.hpp"

#include <iostream>

#include "qt/qtutils.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#define PANNING_MOUSE_BUTTON Qt::LeftButton

namespace ezgl {

class application;

/**** Callback functions for keyboard and mouse input, and for all the ezgl predefined buttons. *****/

/**
 * Handle a key press on the canvas.
 *
 * Forwards the event to the user-defined key press callback, if one is set,
 * passing the human-readable key name.
 *
 * @param event The Qt key event.
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false so the event keeps propagating to other Qt widgets (the main
 *         window grabs keyboard events for the whole application).
 */
bool press_key(QKeyEvent* event, application* app, QWidget* sender = nullptr);

/**
 * Handle a mouse button press on the canvas.
 *
 * Pressing the panning button (PANNING_MOUSE_BUTTON) begins a click-and-drag
 * pan. Any other button is forwarded to the user-defined mouse press callback,
 * with the click position converted to world coordinates.
 *
 * @param event The Qt mouse event.
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return true; the event is always consumed.
 */
bool press_mouse(QMouseEvent* event, application* app, QWidget* sender = nullptr);

/**
 * Handle a mouse button release on the canvas.
 *
 * Ends a panning drag. If the panning button is released without any panning
 * having occurred, the release is treated as a plain click and forwarded to the
 * user-defined mouse press callback (in world coordinates). This lets one button
 * serve both for drag-panning and simple clicking.
 *
 * @param event The Qt mouse event.
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return true; the event is always consumed.
 */
bool release_mouse(QMouseEvent* event, application* app, QWidget* sender = nullptr);

/**
 * Handle mouse movement over the canvas.
 *
 * While the panning button is held, drags the view to follow the cursor.
 * Otherwise the motion is forwarded to the user-defined mouse move callback,
 * with the cursor position converted to world coordinates.
 *
 * @param event The Qt mouse event.
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return true; the event is always consumed.
 */
bool move_mouse(QMouseEvent* event, application* app, QWidget* sender = nullptr);

/**
 * Handle a scroll (wheel or trackpad) over the canvas.
 *
 * Scrolling up zooms in and scrolling down zooms out, centered on the cursor
 * position. Both standard wheel (angle delta) and smooth/trackpad (pixel delta)
 * input are supported; horizontal scrolling is ignored.
 *
 * @param event The Qt wheel event.
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return true; the event is always consumed.
 */
bool scroll_mouse(QWheelEvent* event, application* app, QWidget* sender = nullptr);

/**
 * React to the clicked zoom_fit button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_zoom_fit(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked zoom_in button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_zoom_in(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked zoom_out button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_zoom_out(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked up button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_up(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked down button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_down(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked left button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_left(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked right button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_right(application* app, QWidget* sender = nullptr);

/**
 * React to the clicked proceed button
 *
 * @param app A pointer to the application.
 * @param sender The GUI widget where this event came from.
 *
 * @return false to allow other handlers to see this event, too. true otherwise.
 */
bool press_proceed(application* app, QWidget* sender = nullptr);
}

#endif //EZGL_CALLBACK_HPP
