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

// TODO - Is this the right way to handle this for cubql openmp target offload compilation?
// In theory, we can compile the C++ pathway that Embree uses but will the vec3da types be 
// omptarget friendly? 
// For now I have defined this separate compilation pathway which is enabled with a new 
// precompile definition only set when when compiling the CuBQLRayTracer that maps to cuBQL's math types
#elif defined(XDG_DP_MATH_CUBQL) 
#include "cuBQL/math/common.h"

// C++ compilation with cuBQL math types, map dp::vec3 -> cuBQL::vec3d
namespace dp {
  typedef cuBQL::vec3d vec3;

  inline double dot(vec3 a, vec3 b) { return cuBQL::dot(a, b); }
  inline vec3 cross(vec3 a, vec3 b) { return cuBQL::cross(a, b); }
  inline double abs(double a) { return cuBQL::abst(a); }

  static constexpr double DBL_ZERO_TOL = 20.0 * 2.2204460492503131e-16; // same as 20 * std::numeric_limits<double>::epsilon()
  constexpr double INFTY {std::numeric_limits<double>::max()};
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