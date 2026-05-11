#ifndef DP_MATH_H
#define DP_MATH_H

/*
This header acts as a light wrapper to provide a common interface for vector math operations
in both C++ and Slang compilation contexts. It defines a `dp::vec3` type and associated
functions for dot product, cross product, and absolute value. In C++ compilation, it maps to `xdg::Vec3da`,
while in Slang compilation, it maps to `double3`.
*/

#if defined(__SLANG__) || defined(__SLANG_COMPILER__)

// Slang compilation, map dp::vec3 -> double3
namespace dp { 
  typedef double3 vec3;

  static const double DBL_EPS = 2.2204460492503131e-016;
  static const double PLUCKER_ZERO_TOL = 20.0 * DBL_EPS;
  static const double INFTY = 1.7976931348623157e+308;

  inline double dot(vec3 a, vec3 b) { return ::dot(a, b); }
  inline vec3 cross(vec3 a, vec3 b) { return ::cross(a, b); }
  inline double abs(double a) { return ::abs(a); }
}

#else
#include "xdg/vec3da.h"

// C++ compilation map dp::vec3 -> xdg::Vec3da
namespace dp {
  typedef xdg::Vec3da vec3;

  static constexpr double PLUCKER_ZERO_TOL = 20.0 * std::numeric_limits<double>::epsilon();
  constexpr double INFTY {std::numeric_limits<double>::max()};

  inline double dot(vec3 a, vec3 b) { return xdg::dot(a, b); }
  inline vec3 cross(vec3 a, vec3 b) { return xdg::cross(a, b); }
  inline double abs(double a) { return std::fabs(a); }
}

#endif // end of ifdef __slang__

#endif // DP_MATH_H
