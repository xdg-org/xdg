#ifndef DP_MATH_H
#define DP_MATH_H

/*
This header acts as a light wrapper to provide a common interface for vector math operations
in both C++ and Slang compilation contexts. It defines a `dp::vec3` type and associated
functions for dot product, cross product, and absolute value. In C++ compilation, it maps to `xdg::Vec3da`,
while in Slang compilation, it maps to `double3`.
*/

#ifdef __SLANG__

// Slang compilation, map dp::vec3 -> double3
namespace dp { 
  typedef double3 vec3;

  inline double dot(vec3 a, vec3 b) { return ::dot(a, b); }
  inline vec3 cross(vec3 a, vec3 b) { return ::cross(a, b); }
  inline double abs(double a) { return ::abs(a); }

  static const double DBL_ZERO_TOL = 20 * DBL_EPSILON;
  static const double INFTY = 1.7976931348623157e+308; // std::numeric_limits<double>::max() is not available in slang
}

#else
#include "xdg/vec3da.h"

// C++ compilation map dp::vec3 -> xdg::Vec3da
namespace dp {
  typedef xdg::Vec3da vec3;

  inline double dot(vec3 a, vec3 b) { return xdg::dot(a, b); }
  inline vec3 cross(vec3 a, vec3 b) { return xdg::cross(a, b); }
  inline double abs(double a) { return std::fabs(a); }

  static constexpr double DBL_ZERO_TOL = 20.0 * std::numeric_limits<double>::epsilon();
  constexpr double INFTY {std::numeric_limits<double>::max()};

}

#endif // end of ifdef __slang__

#endif // DP_MATH_H