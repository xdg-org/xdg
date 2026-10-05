#ifndef XDG_UTIL_RNG_H
#define XDG_UTIL_RNG_H

#include <cmath>
#include <cstdint>
#include <random>

namespace xdg {

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<double> dis(0.0, 1.0);

inline void seed_rand_double(unsigned int seed)
{
  gen.seed(seed);
  dis.reset();
}

inline double rand_double(double min=0.0, double max=1.0)
{
  return min + (max - min) * dis(gen);
}

#ifdef _OPENMP
#pragma omp declare target
#endif

inline double lcg_rand01(std::uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return static_cast<double>(state) * (1.0 / 4294967296.0);
}

inline void random_unit_direction_lcg(std::uint32_t& state,
                                      double direction[3])
{
  double x1;
  double x2;
  double s;

  do {
    x1 = lcg_rand01(state) * 2.0 - 1.0;
    x2 = lcg_rand01(state) * 2.0 - 1.0;
    s = x1 * x1 + x2 * x2;
  } while (s <= 0.0 || s >= 1.0);

  const double t = 2.0 * std::sqrt(1.0 - s);
  direction[0] = x1 * t;
  direction[1] = x2 * t;
  direction[2] = 1.0 - 2.0 * s;
}

#ifdef _OPENMP
#pragma omp end declare target
#endif

} // namespace xdg

#endif // XDG_UTIL_RNG_H
