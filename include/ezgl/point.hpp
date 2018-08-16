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
  point2d(double x_coord, double y_coord) : x(x_coord), y(y_coord)
  {
  }

  /**
   * Location of the x-coordinate.
   */
  double x = 0.0;

  /**
   * Location of the y-coordinate.
   */
  double y = 0.0;

  /**
   * Test for equality.
   */
  friend bool operator==(point2d const &lhs, point2d const &rhs)
  {
    return (lhs.x == rhs.x) && (lhs.y == rhs.y);
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
    return {lhs.x + rhs.x, lhs.y + rhs.y};
  }

  /**
   * Add the right-hand side to the left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator+=(point2d &lhs, point2d const &rhs)
  {
    lhs.x += rhs.x;
    lhs.y += rhs.y;

    return lhs;
  }

  /**
   * Subtract two points and return the result.
   */
  friend point2d operator-(point2d const &lhs, point2d const &rhs)
  {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
  }

  /**
   * Subtract the right-hand side to the left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator-=(point2d &lhs, point2d const &rhs)
  {
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;

    return lhs;
  }

  /**
   * Multiply two points and return the result.
   */
  friend point2d operator*(point2d const &lhs, point2d const &rhs)
  {
    return {lhs.x * rhs.x, lhs.y * rhs.y};
  }

  /**
   * Multiply the right- and left-hand side and store the result in the left-hand side.
   */
  friend point2d &operator*=(point2d &lhs, point2d const &rhs)
  {
    lhs.x *= rhs.x;
    lhs.y *= rhs.y;

    return lhs;
  }
};
}

#endif //EZGL_POINT_HPP
