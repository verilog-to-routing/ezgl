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

#include "ezgl/application.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/canvas.hpp"
#include "ezgl/control.hpp"
#include "ezgl/graphics.hpp"

#include <iostream>

#include "qt/qtutils.hpp"
#define PANNING_MOUSE_BUTTON Qt::LeftButton

namespace ezgl {

/**** Callback functions for keyboard and mouse input, and for all the ezgl predefined buttons. *****/

bool press_key(QWidget* widget, QKeyEvent* event, void* data);

bool press_mouse(QWidget*, QMouseEvent* event, void* data);

bool release_mouse(QWidget*, QMouseEvent* event, void* data);

bool move_mouse(QWidget*, QMouseEvent* event, void* data);

bool scroll_mouse(QWidget* widget, QWheelEvent* event, void* data);

/**
 * React to the clicked zoom_fit button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_zoom_fit(QWidget *widget, void* data);

/**
 * React to the clicked zoom_in button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_zoom_in(QWidget *widget, void* data);

/**
 * React to the clicked zoom_out button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_zoom_out(QWidget *widget, void* data);

/**
 * React to the clicked up button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_up(QWidget *widget, void* data);

/**
 * React to the clicked up button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_down(QWidget *widget, void* data);

/**
 * React to the clicked up button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_left(QWidget *widget, void* data);

/**
 * React to the clicked up button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_right(QWidget *widget, void* data);

/**
 * React to the clicked proceed button
 *
 * @param widget The GUI widget where this event came from.
 * @param data A pointer to any user-specified data you passed in.
 *
 * @return FALSE to allow other handlers to see this event, too. TRUE otherwise.
 */
bool press_proceed(QWidget *widget, void* data);
}

#endif //EZGL_CALLBACK_HPP
