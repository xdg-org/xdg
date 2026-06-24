#ifndef _XDG_RAY_BENCHMARK_H
#define _XDG_RAY_BENCHMARK_H

#include <cmath>
#include <cstdint>

namespace xdg::tools::benchmark {

struct SourceSample {
  double position[3];
  double direction[3];
};

#ifdef _OPENMP
#pragma omp declare target
#endif

inline double rand01(std::uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return static_cast<double>(state) * (1.0 / 4294967296.0);
}

inline void random_unit_dir_lcg(std::uint32_t& state, double direction[3])
{
  double x1;
  double x2;
  double s;

  do {
    x1 = rand01(state) * 2.0 - 1.0;
    x2 = rand01(state) * 2.0 - 1.0;
    s = x1 * x1 + x2 * x2;
  } while (s <= 0.0 || s >= 1.0);

  const double t = 2.0 * std::sqrt(1.0 - s);
  direction[0] = x1 * t;
  direction[1] = x2 * t;
  direction[2] = 1.0 - 2.0 * s;
}

inline SourceSample random_spherical_source(double origin_x,
                                            double origin_y,
                                            double origin_z,
                                            std::uint32_t state,
                                            double source_radius)
{
  SourceSample sample;
  random_unit_dir_lcg(state, sample.direction);

  sample.position[0] = origin_x;
  sample.position[1] = origin_y;
  sample.position[2] = origin_z;

  if (source_radius > 0.0) {
    const double radius = source_radius * std::cbrt(rand01(state));
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
