#ifndef EZGL_RECTANGLE_HPP
#define EZGL_RECTANGLE_HPP

#include <ezgl/point.hpp>

#include <algorithm>

namespace ezgl {

/**
 * Represents a rectangle as two diagonally opposite points in a Cartesian plane.
 */
class rectangle {
public:
  /**
   * Create a rectangle from two diagonally opposite points.
   */
  rectangle(point2d origin, point2d top_right) : m_first(origin), m_second(top_right)
  {
  }

  /**
   * Create a rectangle with a given width and height.
   */
  rectangle(point2d origin, double width, double height) : m_first(origin), m_second(origin)
  {
    m_second.offset(width, height);
  }

  /**
   * @return The minimum x-coordinate.
   */
  double left() const
  {
    return std::min(m_first.x(), m_second.x());
  }

  /**
   * @return The maximum x-coordinate.
   */
  double right() const
  {
    return std::max(m_first.x(), m_second.x());
  }

  /**
   * @return The minimum y-coordinate.
   */
  double bottom() const
  {
    return std::min(m_first.y(), m_second.y());
  }

  /**
   * @return The maximum y-coordinate.
   */
  double top() const
  {
    return std::max(m_first.y(), m_second.y());
  }

  /**
   * @return true if x and y are inside the rectangle, false otherwise.
   */
  bool contains(double x, double y) const
  {
    if(x < left() || right() < x || y < bottom() || top() < y) {
      return false;
    }

    return true;
  }

  /**
   * @return true if the point is inside the rectangle, false otherwise.
   */
  bool contains(point2d point) const
  {
    return contains(point.x(), point.y());
  }

  /**
   * Move the rectangle along the x and y plane.
   */
  void offset(double x_offset, double y_offset)
  {
    m_first.offset(x_offset, y_offset);
    m_second.offset(x_offset, y_offset);
  }

  /**
   * @return The width of the rectangle.
   */
  double width() const
  {
    return right() - left();
  }

  /**
   * @return The height of the rectangle.
   */
  double height() const
  {
    return top() - bottom();
  }

  /**
   *
   * @return The area of the rectangle.
   */
  double area() const
  {
    return width() * height();
  }

  /**
   * @return The centre of the rectangle in the x plane.
   */
  double centre_x() const
  {
    return (right() + left()) * 0.5;
  }

  /**
   * @return The centre of the rectangle in the y plane.
   */
  double centre_y() const
  {
    return (top() + bottom()) * 0.5;
  }

  /**
   * @return The centre of the recangle.
   */
  point2d centre() const
  {
    return {centre_x(), centre_y()};
  }

  /**
   * Test for equality.
   */
  bool operator==(const rectangle &rhs) const
  {
    return m_first == rhs.m_first && m_second == rhs.m_second;
  }

  /**
   * Test for inequality.
   */
  bool operator!=(const rectangle &rhs) const
  {
    return !(rhs == *this);
  }

private:
  point2d m_first;
  point2d m_second;
};
}

#endif //EZGL_RECTANGLE_HPP
