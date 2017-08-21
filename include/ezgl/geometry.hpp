#ifndef EZGL_GEOMETRY_HPP
#define EZGL_GEOMETRY_HPP

namespace ezgl {

/**
 * Represents a point in Cartesian coordinates.
 */
struct point {
  /**
   * Location of the x-coordinate.
   */
  double x;

  /**
   * Location of the y-coordinate.
   */
  double y;

  /**
   * Test for equality.
   *
   * @param lhs The left hand side of the expression.
   * @param rhs The right hand side of the expression.
   *
   * @return true if the points have the same (x, y) tuple, false otherwise.
   */
  friend bool operator==(point const &lhs, point const &rhs)
  {
    return (lhs.x == rhs.x) && (lhs.y == rhs.y);
  }

  /**
   * Test for inequality.
   *
   * @param lhs The left hand side of the expression.
   * @param rhs The right hand side of the expression.
   *
   * @return true if the points do not have the same (x, y) tuple, false otherwise.
   */
  friend bool operator!=(point const &lhs, point const &rhs)
  {
    return !(lhs == rhs);
  }

  /**
   * Add two points together and return the result.
   *
   * @param lhs The left hand side of the expression.
   * @param rhs The right hand side of the expression.
   *
   * @return A new point representing the cartesian sum of lhs and rhs.
   */
  friend point operator+(point const &lhs, point const &rhs)
  {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
  }

  /**
   * Add the right-hand side to the left-hand side and store the result in the left-hand side.
   *
   * @param lhs The left-hand side of the expression
   * @param rhs The right-hand side of the expression
   *
   * @return A reference to the summation, which is the left-hand side
   */
  friend point &operator+=(point &lhs, point const &rhs)
  {
    lhs.x += rhs.x;
    lhs.y += rhs.y;

    return lhs;
  }

  /**
   * Subtract two points and return the result.
   *
   * @param lhs The left-hand side of the expression
   * @param rhs The right-hand side of the expression
   * @return The difference of the left- and right-hand side
   */
  friend point operator-(point const &lhs, point const &rhs)
  {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
  }

  /**
   * Subtract the right-hand side to the left-hand side and store the result in the left-hand side.
   *
   * @param lhs The left-hand side of the expression
   * @param rhs The right-hand side of the expression
   * @return A reference to the difference, which is the left-hand side
   */
  friend point &operator-=(point &lhs, point const &rhs)
  {
    lhs.x -= rhs.x;
    lhs.y -= rhs.y;

    return lhs;
  }

  /**
   * Multiply two points and return the result.
   *
   * @param lhs The left-hand side of the expression
   * @param rhs The right-hand side of the expression
   * @return The product of the left- and right-hand side
   */
  friend point operator*(point const &lhs, point const &rhs)
  {
    return {lhs.x * rhs.x, lhs.y * rhs.y};
  }

  /**
   * Multiply the right- and left-hand side and store the result in the left-hand side.
   *
   * @param lhs The left-hand side of the expression
   * @param rhs The right-hand side of the expression
   * @return A reference to the product, which is the left-hand side
   */
  friend point &operator*=(point &lhs, point const &rhs)
  {
    lhs.x *= rhs.x;
    lhs.y *= rhs.y;

    return lhs;
  }
};
}

#endif //EZGL_GEOMETRY_HPP
