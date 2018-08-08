#ifndef EZGL_POINT_HPP
#define EZGL_POINT_HPP

namespace ezgl {

/**
 * Represents a two-dimensional point in Cartesian coordinates.
 */
class point2d {
public:
  /**
   * Create a point at the origin (0.0, 0.0).
   */
  point2d() = default;

  /**
   * Create a point at the given x and y position.
   */
  point2d(double x, double y) : m_x(x), m_y(y)
  {
  }

  /**
   * Location of the x-coordinate.
   */
  double x() const
  {
    return m_x;
  }

  /**
   * Location of the y-coordinate.
   */
  double y() const
  {
    return m_y;
  }

  /**
   * Move the point object in the x and y directions.
   */
  void offset(double x, double y)
  {
    m_x += x;
    m_y += y;
  }

  /**
   * Test for equality.
   */
  friend bool operator==(point2d const &lhs, point2d const &rhs)
  {
    return (lhs.m_x == rhs.m_x) && (lhs.m_y == rhs.m_y);
  }

  /**
   * Test for inequality.
   */
  friend bool operator!=(point2d const &lhs, point2d const &rhs)
  {
    return !(lhs == rhs);
  }

  /**
   * Add two points together and return the result.
   */
  friend point2d operator+(point2d const &lhs, point2d const &rhs)
  {
    return {lhs.m_x + rhs.m_x, lhs.m_y + rhs.m_y};
  }

  /**
   * Add the right-hand side to the left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator+=(point2d &lhs, point2d const &rhs)
  {
    lhs.m_x += rhs.m_x;
    lhs.m_y += rhs.m_y;

    return lhs;
  }

  /**
   * Subtract two points and return the result.
   */
  friend point2d operator-(point2d const &lhs, point2d const &rhs)
  {
    return {lhs.m_x - rhs.m_x, lhs.m_y - rhs.m_y};
  }

  /**
   * Subtract the right-hand side to the left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator-=(point2d &lhs, point2d const &rhs)
  {
    lhs.m_x -= rhs.m_x;
    lhs.m_y -= rhs.m_y;

    return lhs;
  }

  /**
   * Multiply two points and return the result.
   */
  friend point2d operator*(point2d const &lhs, point2d const &rhs)
  {
    return {lhs.m_x * rhs.m_x, lhs.m_y * rhs.m_y};
  }

  /**
   * Multiply the right- and left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator*=(point2d &lhs, point2d const &rhs)
  {
    lhs.m_x *= rhs.m_x;
    lhs.m_y *= rhs.m_y;

    return lhs;
  }

private:
  double m_x = 0.0;
  double m_y = 0.0;
};
}

#endif //EZGL_POINT_HPP
