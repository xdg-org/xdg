#ifndef _XDG_RAY_BENCHMARK_H
#define _XDG_RAY_BENCHMARK_H

#include <cmath>
#include <cstdint>

#include "xdg/util/rng.h"

namespace xdg::tools::benchmark {

struct SourceSample {
  double position[3];
  double direction[3];
};

#ifdef _OPENMP
#pragma omp declare target
#endif

inline SourceSample random_spherical_source(double origin_x,
                                            double origin_y,
                                            double origin_z,
                                            std::uint32_t state,
                                            double source_radius)
{
  SourceSample sample;
  random_unit_direction_lcg(state, sample.direction);

  sample.position[0] = origin_x;
  sample.position[1] = origin_y;
  sample.position[2] = origin_z;

  if (source_radius > 0.0) {
    const double radius = source_radius * std::cbrt(lcg_rand01(state));
    sample.position[0] += sample.direction[0] * radius;
    sample.position[1] += sample.direction[1] * radius;
    sample.position[2] += sample.direction[2] * radius;
  }

  return sample;
}

#ifdef _OPENMP
#pragma omp end declare target
#endif

} // namespace xdg::tools::benchmark

#endif // _XDG_RAY_BENCHMARK_H
