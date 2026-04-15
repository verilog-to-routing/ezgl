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

#include "ezgl/graphics.hpp"

#include <cassert>

#include <QFile>
#include "ezgl/qt/ezgl_qtcompat.hpp"
#include "ezgl/qt/painter.hpp"

#include <numbers>

namespace ezgl {

/**
 * Test if a line segment can pass through a clipping edge (Liang-Barsky algorithm)
 * 
 * @param direction_component: The directional component (dx or dy)
 * @param distance_to_edge: Distance from line start to the clipping edge
 * @param t_enter: Parameter for line entry point (modified by reference)
 * @param t_exit: Parameter for line exit point (modified by reference)
 * @return true if line segment can potentially be visible, false if completely clipped
 */
static bool test_clipping_edge(double direction_component, 
                              double distance_to_edge, 
                              double& t_enter, 
                              double& t_exit)
{
    if (direction_component < 0.0) {
        // Line is entering the clipping region through this edge
        double t_intersection = distance_to_edge / direction_component;
        
        if (t_intersection > t_exit) {
            // Line enters after it should exit - completely outside
            return false;
        }
        
        if (t_intersection > t_enter) {
            // Update entry point
            t_enter = t_intersection;
        }
    }
    else if (direction_component > 0.0) {
        // Line is exiting the clipping region through this edge
        double t_intersection = distance_to_edge / direction_component;
        
        if (t_intersection < t_enter) {
            // Line exits before it should enter - completely outside
            return false;
        }
        
        if (t_intersection < t_exit) {
            // Update exit point
            t_exit = t_intersection;
        }
    }
    else {
        // Line is parallel to this edge (direction_component == 0)
        if (distance_to_edge < 0.0) {
            // Line is completely outside this edge
            return false;
        }
        // If distance_to_edge >= 0, line is inside or on the edge
    }
    
    return true;
}

/**
 * Clip a line segment against a rectangular window using Liang-Barsky algorithm
 * https://www.geeksforgeeks.org/computer-graphics/liang-barsky-algorithm/
 * 
 * @param clip_window: The rectangular clipping window
 * @param start_point: Starting point of the line (modified if clipped)
 * @param end_point: Ending point of the line (modified if clipped)
 * @return true if any part of the line is visible, false if completely clipped
 */
static bool clip_line(const rectangle& clip_window, 
                                  point2d& start_point, 
                                  point2d& end_point)
{
    // Parametric line equation: P(t) = P1 + t * (P2 - P1), where 0 <= t <= 1
    double dx = end_point.x - start_point.x;
    double dy = end_point.y - start_point.y;
    
    // Parameter values for entry and exit points
    double t_enter = 0.0;  // Start of visible line segment
    double t_exit = 1.0;   // End of visible line segment
    
    // Test against left edge: x = clip_window.left()
    if (!test_clipping_edge(-dx, start_point.x - clip_window.left(), t_enter, t_exit)) {
        return false;
    }
    
    // Test against right edge: x = clip_window.right()
    if (!test_clipping_edge(dx, clip_window.right() - start_point.x, t_enter, t_exit)) {
        return false;
    }
    
    // Test against bottom edge: y = clip_window.bottom()
    if (!test_clipping_edge(-dy, start_point.y - clip_window.bottom(), t_enter, t_exit)) {
        return false;
    }
    
    // Test against top edge: y = clip_window.top()
    if (!test_clipping_edge(dy, clip_window.top() - start_point.y, t_enter, t_exit)) {
        return false;
    }
    
    // If we reach here, some part of the line is visible
    
    // Clip the end point if necessary
    if (t_exit < 1.0) {
        end_point.x = start_point.x + t_exit * dx;
        end_point.y = start_point.y + t_exit * dy;
    }
    
    // Clip the start point if necessary
    if (t_enter > 0.0) {
        start_point.x = start_point.x + t_enter * dx;
        start_point.y = start_point.y + t_enter * dy;
    }
    
    return true;
}

renderer::renderer(Painter *painter,
    transform_fn transform,
    camera *p_camera,
    QImage *m_surface)
    : m_painter(painter), m_transform(std::move(transform)), m_camera(p_camera), rotation_angle(0)
{
  if (m_painter != nullptr) {
    current_font = m_painter->font();
  }
}

renderer::~renderer()
{
}

bool renderer::clip_line_world(const rectangle &clip_window, point2d &start, point2d &end)
{
  return clip_line(clip_window, start, end);
}

void renderer::update_renderer(Painter *painter, QImage *m_surface)
{
  // Update Cairo Context
  m_painter = painter;

  if (m_painter != nullptr) {
    m_painter->setFont(current_font);
  }

  // Restore graphics attributes
  set_color(current_color);
  set_line_width(current_line_width);
  set_line_cap(current_line_cap);
  set_line_dash(current_line_dash);
}

void renderer::set_coordinate_system(t_coordinate_system new_coordinate_system)
{
  current_coordinate_system = new_coordinate_system;
}

void renderer::set_visible_world(rectangle new_world)
{
  // Change the aspect ratio of the new_world to align with the aspect ratio of the initial world
  // Get the width and height of the new_world
  point2d n_center = new_world.center();
  double n_width = new_world.width();
  double n_height = new_world.height();

  // Get the aspect ratio of the initial world
  double i_width = m_camera->get_initial_world().width();
  double i_height = m_camera->get_initial_world().height();
  double i_aspect_ratio = i_width / i_height;

  // Make sure the required area is entirely visible
  if (n_width/i_aspect_ratio >= n_height) {
    // Change the height
    double new_height = n_width/i_aspect_ratio;
    new_world ={{n_center.x-n_width/2, n_center.y-new_height/2}, n_width, new_height};
  }
  else {
    // Change the width
    double new_width = n_height*i_aspect_ratio;
    new_world ={{n_center.x-new_width/2, n_center.y-n_height/2}, new_width, n_height};
  }

  // set the visible bounds of the world
  m_camera->set_world(new_world);
}

rectangle renderer::get_visible_world()
{
  // m_camera->get_world() is not good representative of the visible world since it doesn't
  // account for the drawable margins.
  // TODO: precalculate the visible world in camera class to speedup the clipping

  // Get the world and screen dimensions
  rectangle world = m_camera->get_world();
  rectangle screen = m_camera->get_screen();

  // Calculate the margins by converting the screen origin to world coordinates
  point2d margin = screen.bottom_left() * m_camera->get_world_scale_factor();

  // The actual visible world
  return {(world.bottom_left() - margin), (world.top_right() + margin)};
}

rectangle renderer::get_visible_screen() const
{
  // Get the widget dimensions
  return m_camera->get_widget();
}

rectangle renderer::world_to_screen(const rectangle& box)
{
  point2d origin = m_transform(box.bottom_left());
  point2d top_right = m_transform(box.top_right());

  return rectangle(origin, top_right);
}

bool renderer::rectangle_off_screen(rectangle rect)
{
  if(current_coordinate_system == SCREEN)
    return false;

  rectangle visible = get_visible_world();

  if(rect.right() < visible.left())
    return true;

  if(rect.left() > visible.right())
    return true;

  if(rect.top() < visible.bottom())
    return true;

  if(rect.bottom() > visible.top())
    return true;

  return false;
}

void renderer::set_color(color c)
{
  set_color(c.red, c.green, c.blue, c.alpha);
}

void renderer::set_color(color c, uint_fast8_t alpha)
{
  set_color(c.red, c.green, c.blue, alpha);
}

void renderer::set_color(uint_fast8_t red,
    uint_fast8_t green,
    uint_fast8_t blue,
    uint_fast8_t alpha)
{
  // set color for cairo
  m_painter->set_source_rgba(red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0);

  // set current_color
  current_color = {red, green, blue, alpha};
}

void renderer::set_line_cap(line_cap cap)
{
  auto cairo_cap = static_cast<Qt::PenCapStyle>(cap);
  m_painter->set_line_cap(cairo_cap);

  current_line_cap = cap;
}

void renderer::set_line_dash(line_dash dash)
{
  if(dash == line_dash::none) {
    int num_dashes = 0; // disables dashing

    m_painter->set_dash(nullptr, num_dashes, 0);
  } else if(dash == line_dash::asymmetric_5_3) {
    static double dashes[] = {5.0, 3.0};
    int num_dashes = 2; // asymmetric dashing

    m_painter->set_dash(dashes, num_dashes, 0);
  }

  current_line_dash = dash;
}

void renderer::set_line_width(int width)
{
  m_painter->set_line_width(width == 0 ? 1 : width);

  current_line_width = width;
}

void renderer::set_font_size(double new_size)
{
  current_font.setPixelSize(std::max(1, int(std::lround(new_size))));
  m_painter->set_font_size(new_size);
}

void renderer::format_font(std::string const &family, font_slant slant, font_weight weight)
{
  current_font.setFamily(QString::fromStdString(family));
  current_font.setStyle(static_cast<QFont::Style>(slant));
  current_font.setWeight(static_cast<QFont::Weight>(weight));
  m_painter->select_font_face(family.c_str(), static_cast<QFont::Style>(slant),
      static_cast<QFont::Weight>(weight));
}

void renderer::format_font(std::string const &family,
    font_slant slant,
    font_weight weight,
    double new_size)
{
  set_font_size(new_size);
  format_font(family, slant, weight);
}

void renderer::set_text_rotation(double degrees)
{
  // Bad rotation values (inf, NaN) can cause permanent problems in the 
  // graphics, as the cairo_restore to undo the rotation doesn't work.
  // Check for them before changing the angle.
  if (degrees >= -360. && degrees <= 360.) {
    // convert the given angle to rad
    rotation_angle = -degrees * std::numbers::pi / 180;
  }
  else {
    q_warning("set_text_rotation: bad rotation angle of %f. Ignored!", degrees);
  }
}

void renderer::set_horiz_justification(justification horiz_just)
{
  // Ignore illegal values for horizontal justification
  if (horiz_just != justification::top && horiz_just != justification::bottom)
    horiz_justification = horiz_just;
}

void renderer::set_vert_justification(justification vert_just)
{
  // Ignore illegal values for vertical justification
  if (vert_just != justification::right && vert_just != justification::left)
    vert_justification = vert_just;
}

void renderer::draw_line(point2d start, point2d end)
{
  if(rectangle_off_screen({start, end}))
    return;

  if(current_coordinate_system == WORLD) {

    rectangle world_clip_bounds = get_visible_world();

    // Clip the line in world coordinates using Liang-Barsky algorithm
    if (!clip_line(world_clip_bounds, start, end)) {
      // Line is completely outside the drawable area
      return;
    }

    // Transform to screen coordinates
    start = m_transform(start);
    end = m_transform(end);
  }

  m_painter->move_to(start.x, start.y);
  m_painter->line_to(end.x, end.y);

  m_painter->stroke();
}

void renderer::draw_rectangle(point2d start, point2d end)
{
  if(rectangle_off_screen({start, end}))
    return;

  draw_rectangle_path(start, end, false);
}

void renderer::draw_rectangle(point2d start, double width, double height)
{
  if(rectangle_off_screen({start, {start.x + width, start.y + height}}))
    return;

  draw_rectangle_path(start, {start.x + width, start.y + height}, false);
}

void renderer::draw_rectangle(rectangle r)
{
  if(rectangle_off_screen({{r.left(), r.bottom()}, {r.right(), r.top()}}))
    return;

  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()}, false);
}

void renderer::fill_rectangle(point2d start, point2d end)
{
  if(rectangle_off_screen({start, end}))
    return;

  draw_rectangle_path(start, end, true);
}

void renderer::fill_rectangle(point2d start, double width, double height)
{
  if(rectangle_off_screen({start, {start.x + width, start.y + height}}))
    return;

  draw_rectangle_path(start, {start.x + width, start.y + height}, true);
}

void renderer::fill_rectangle(rectangle r)
{
  if(rectangle_off_screen({{r.left(), r.bottom()}, {r.right(), r.top()}}))
    return;

  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()}, true);
}

// For speed, use a fixed size polygon point buffer when possible
// Dynamically allocate an arbitrary size buffer only when necessary.
#define X11_MAX_FIXED_POLY_PTS 100

void renderer::fill_poly(std::vector<point2d> const &points)
{
  assert(points.size() > 1);

  if (defer_fill_poly(points))
    return;

  // Conservative but fast clip test -- check containing rectangle of polygon
  double x_min = points[0].x;
  double x_max = points[0].x;
  double y_min = points[0].y;
  double y_max = points[0].y;

  for(std::size_t i = 1; i < points.size(); ++i) {
    x_min = std::min(x_min, points[i].x);
    x_max = std::max(x_max, points[i].x);
    y_min = std::min(y_min, points[i].y);
    y_max = std::max(y_max, points[i].y);
  }

  if(rectangle_off_screen({{x_min, y_min}, {x_max, y_max}}))
    return;

  point2d next_point = points[0];

  if(current_coordinate_system == WORLD)
    next_point = m_transform(points[0]);

  m_painter->move_to(next_point.x, next_point.y);

  for(std::size_t i = 1; i < points.size(); ++i) {
    if(current_coordinate_system == WORLD)
      next_point = m_transform(points[i]);
    else
      next_point = points[i];
    m_painter->line_to(next_point.x, next_point.y);
  }

  m_painter->close_path();
  m_painter->fill();
}

void renderer::draw_elliptic_arc(point2d center,
    double radius_x,
    double radius_y,
    double start_angle,
    double extent_angle)
{
  if (defer_arc(center, radius_x, radius_y, start_angle, extent_angle, false))
    return;

  if(rectangle_off_screen(
         {{center.x - radius_x, center.y - radius_y}, {center.x + radius_x, center.y + radius_y}}))
    return;

  // define the stretch factor (i.e. An ellipse is a stretched circle)
  double stretch_factor = radius_y / radius_x;

  draw_arc_path(center, radius_x, start_angle, extent_angle, stretch_factor, false);
}

void renderer::draw_arc(point2d center, double radius, double start_angle, double extent_angle)
{
  if (defer_arc(center, radius, radius, start_angle, extent_angle, false))
    return;

  if(rectangle_off_screen(
         {{center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}}))
    return;

  draw_arc_path(center, radius, start_angle, extent_angle, 1, false);
}

void renderer::fill_elliptic_arc(point2d center,
    double radius_x,
    double radius_y,
    double start_angle,
    double extent_angle)
{
  if (defer_arc(center, radius_x, radius_y, start_angle, extent_angle, true))
    return;

  if(rectangle_off_screen(
         {{center.x - radius_x, center.y - radius_y}, {center.x + radius_x, center.y + radius_y}}))
    return;

  // define the stretch factor (i.e. An ellipse is a stretched circle)
  double stretch_factor = radius_y / radius_x;

  draw_arc_path(center, radius_x, start_angle, extent_angle, stretch_factor, true);
}

void renderer::fill_arc(point2d center, double radius, double start_angle, double extent_angle)
{
  if (defer_arc(center, radius, radius, start_angle, extent_angle, true))
    return;

  if(rectangle_off_screen(
         {{center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}}))
    return;

  draw_arc_path(center, radius, start_angle, extent_angle, 1, true);
}

void renderer::draw_text(point2d point, std::string const &text)
{
  // call the draw_text function with no bounds
  draw_text(point, text, DBL_MAX, DBL_MAX);
}

void renderer::draw_text(point2d point, std::string const &text, double bound_x, double bound_y)
{
  if (defer_text(point, text, bound_x, bound_y))
    return;

  // get the width and height of the drawn text
  text_extents_t text_extents{0,0,0,0,0,0};
  m_painter->text_extents(text.c_str(), &text_extents);

  // get more information about the font used
  font_extents_t font_extents{0,0,0,0,0};
  m_painter->font_extents(&font_extents);

  // get text width and height in the current coordinate system to check against the bounds
  // Note: text width and height are constant in widget coordinates
  point2d world_scale = m_camera->get_world_scale_factor();
  double scaled_width, scaled_height;
  if (current_coordinate_system == WORLD) {
    scaled_width = text_extents.width * world_scale.x;
    scaled_height = text_extents.height * world_scale.y;
  } else {  /* SCREEN coordinates */
    scaled_width = text_extents.width;
    scaled_height = text_extents.height;
  }

  const bool bounded_x = std::isfinite(bound_x) && bound_x < DBL_MAX;
  const bool bounded_y = std::isfinite(bound_y) && bound_y < DBL_MAX;
  const double clip_width = bounded_x ? bound_x : scaled_width;
  const double clip_height = bounded_y ? bound_y : scaled_height;

  // For bounded text, require the full measured text to fit before drawing.
  // This avoids rendering clipped/unreadable label fragments.
  if ((bounded_x && scaled_width > bound_x)
      || (bounded_y && scaled_height > bound_y)) {
    return;
  }

  // the center point of the text
  point2d center = point;

  // roughly calculate the center point for pre-clipping
  if (horiz_justification == justification::left)
    center.x += clip_width/2;
  else if (horiz_justification == justification::right)
    center.x -= clip_width/2;
  if (vert_justification == justification::top)
    center.y -= clip_height/2;
  else if (vert_justification == justification::bottom)
    center.y += clip_height/2;

  if(rectangle_off_screen({{center.x - clip_width / 2, center.y - clip_height / 2}, clip_width, clip_height}))
    return;

  {
    auto local_clip_x = [](justification just, double extent) {
      if (just == justification::left)
        return 0.0;
      if (just == justification::right)
        return -extent;
      return -extent / 2.0;
    };

    auto local_clip_y = [](justification just, double extent) {
      if (just == justification::bottom)
        return 0.0;
      if (just == justification::top)
        return -extent;
      return -extent / 2.0;
    };

    // transform the given point
    if(current_coordinate_system == WORLD)
      center = m_transform(point);
    else
      center = point;

    QString qtext = QString::fromStdString(text);
    QFontMetricsF fm(m_painter->font());
    QRectF br = fm.boundingRect(qtext);

    // Save QPainter state so that translate/rotate are undone after drawing.
    m_painter->save();

    // We will rotate around the desired visual text center = `center`
    m_painter->translate(center.x, center.y);
    m_painter->rotate(rotation_angle * 180.0 / std::numbers::pi);

    // Position text from the full bounding rect, not just width/height.
    // This keeps the Qt placement aligned with the measured extents,
    // including font bearings.
    QPointF offset(-(br.x() + br.width() / 2.0),
                   -(br.y() + br.height() / 2.0));

    // Apply horizontal justification
    if (horiz_justification == justification::left) {
      offset.rx() += br.width() / 2.0;       // anchor is left edge, so shift center -> left
    } else if (horiz_justification == justification::right) {
      offset.rx() -= br.width() / 2.0;       // anchor is right edge, so shift center -> right
    }

    // Apply vertical justification
    if (vert_justification == justification::top) {
      offset.ry() += br.height() / 2.0;      // anchor at top -> move center to top edge
    } else if (vert_justification == justification::bottom) {
      offset.ry() -= br.height() / 2.0;      // anchor at bottom -> move center to bottom edge
    }

    if (bounded_x || bounded_y) {
      const QRectF text_rect = br.translated(offset);
      const double clip_width_screen = bounded_x ? (current_coordinate_system == WORLD
                                                        ? bound_x / std::max(world_scale.x, DBL_EPSILON)
                                                        : bound_x)
                                                 : text_rect.width() + 2.0;
      const double clip_height_screen = bounded_y ? (current_coordinate_system == WORLD
                                                         ? bound_y / std::max(world_scale.y, DBL_EPSILON)
                                                         : bound_y)
                                                  : text_rect.height() + 2.0;
      const QRectF clip_rect(bounded_x ? local_clip_x(horiz_justification, clip_width_screen)
                                       : text_rect.left() - 1.0,
                             bounded_y ? local_clip_y(vert_justification, clip_height_screen)
                                       : text_rect.top() - 1.0,
                             clip_width_screen,
                             clip_height_screen);
      m_painter->setClipRect(clip_rect, Qt::IntersectClip);
    }

    // Set pen color for text — QPainter::drawText() uses the pen color.
    // m_pen is only applied to QPainter by stroke()/fill(), so set it explicitly here.
    m_painter->setPen(QColor(current_color.red, current_color.green, current_color.blue, current_color.alpha));
    m_painter->drawText(offset, qtext);
  }

  // restore the old state to undo the performed rotation
  m_painter->restore();
}

void renderer::draw_rectangle_path(point2d start, point2d end, bool fill_flag)
{
  if(current_coordinate_system == WORLD) {
    start = m_transform(start);
    end = m_transform(end);
  }

  m_painter->move_to(start.x, start.y);
  m_painter->line_to(start.x, end.y);
  m_painter->line_to(end.x, end.y);
  m_painter->line_to(end.x, start.y);

  m_painter->close_path();

  // actual drawing
  if(fill_flag)
    m_painter->fill();
  else
    m_painter->stroke();
}

void renderer::draw_arc_path(point2d center,
    double radius,
    double start_angle,
    double extent_angle,
    double stretch_factor,
    bool fill_flag)
{
  // point_x is a point on the arc outline
  point2d point_x = {center.x + radius, center.y};

  // transform the center point of the arc, and the other point
  if(current_coordinate_system == WORLD) {
    center = m_transform(center);
    point_x = m_transform(point_x);
  }

  // calculate the new radius after transforming to the new coordinates
  radius = point_x.x - center.x;

  // save the current state to undo the scaling needed for drawing ellipse
  m_painter->save();

  // scale the drawing by the stretch factor to draw elliptic circles
  m_painter->scale(1 / stretch_factor, 1);
  center.x = center.x * stretch_factor;
  radius = radius * stretch_factor;

  // start a new path (forget the current point). Alternative for cairo_move_to() for drawing non-filled arc
  m_painter->new_path();

  // if the arc will be filled in, start drawing from the center of the arc
  if(fill_flag)
    m_painter->move_to(center.x, center.y);
  else {
    // this step is not needed for cairo but is needed for QPainter
    double start_angle_radians = -start_angle * std::numbers::pi / 180;
    double startx = center.x + radius * std::cos(start_angle_radians);
    double starty = center.y + radius * std::sin(start_angle_radians);
    m_painter->move_to(startx, starty);
  }

  // calculating the ending angle
  double end_angle = start_angle + extent_angle;

  // draw the arc in counter clock-wise direction if the extent angle is positive
  if(extent_angle >= 0) {
    m_painter->arc_negative(
        center.x, center.y, radius, -start_angle * std::numbers::pi / 180, -end_angle * std::numbers::pi / 180);
  }
  // draw the arc in clock-wise direction if the extent angle is negative
  else {
    m_painter->arc(
        center.x, center.y, radius, -start_angle * std::numbers::pi / 180, -end_angle * std::numbers::pi / 180);
  }

  // if the arc will be filled in, return back to the center of the arc
  if(fill_flag)
    m_painter->close_path();

  if(fill_flag)
    m_painter->fill();
  else
    m_painter->stroke();

  m_painter->restore();
}

void renderer::draw_surface(surface *p_surface, point2d point, double scale_factor)
{
  if (defer_surface(p_surface, point, scale_factor))
    return;

  if (p_surface->isNull()) {
    q_warning("renderer::draw_surface: Error drawing surface at address %p; surface is not valid.", (void*) p_surface);
    return;
  }

  // calculate surface width and height in screen coordinates
  double s_width = (double)p_surface->width() * scale_factor;
  double s_height = (double)p_surface->height() * scale_factor;

  // calculate surface width and height in world coordinates
  if (current_coordinate_system == WORLD) {
    s_width *= m_camera->get_world_scale_factor().x;
    s_height *= m_camera->get_world_scale_factor().y;
  }

  // Calculate the top left point
  point2d top_left = point;
  if (horiz_justification == justification::center)
    top_left.x -= s_width/2;
  else if (horiz_justification == justification::right)
    top_left.x -= s_width;
  // Vertical justifaction is calculated differently based on the current coordinate system
  // Since the origin point of screen coordinates is at the top left,
  // while the origin point of world coordinates is at the bottom left
  if (vert_justification == justification::center)
    top_left.y += (current_coordinate_system == WORLD) ? s_height/2 : -s_height/2;
  else if (vert_justification == justification::bottom)
    top_left.y += (current_coordinate_system == WORLD) ? s_height : -s_height;

  if (rectangle_off_screen({{top_left.x, top_left.y - s_height}, s_width, s_height}))
    return;

  // transform the given point
  if(current_coordinate_system == WORLD)
    top_left = m_transform(top_left);

  if (scale_factor != 1) {
    // save the current state to undo the scaling
    m_painter->save();

    // scale the cairo context with the given scale factor
    m_painter->scale(scale_factor, scale_factor);

    // adjust the corner point based on the context scaling
    top_left.x /= scale_factor;
    top_left.y /= scale_factor;
  }

  // Create a source for painting from the surface
  m_painter->set_source_surface(p_surface, top_left.x, top_left.y);

  // Actual drawing
  m_painter->paint();

  if (scale_factor != 1) {
    // restore the old state to undo the performed scaling
    m_painter->restore();
  }
}

surface *renderer::load_png(const char *file_path)
{
  QImage* image = new QImage;

  if (!QFile::exists(QString::fromLatin1(file_path))) {
    q_warning("renderer::load_png: File %s not found.", file_path);
  }

  if (!image->load(QString::fromLatin1(file_path))) {
    q_warning("renderer::load_png: Error loading file %s.", file_path);
  }

  return image;
}

void renderer::free_surface(surface *p_surface)
{
  delete p_surface;
}
}
