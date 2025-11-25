#ifndef XDG_UTIL_RNG_H
#define XDG_UTIL_RNG_H

#include <random>

namespace xdg {

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<double> dis(0.0, 1.0);

inline double rand_double(double min=0.0, double max=1.0)
{
  return min + (max - min) * dis(gen);
}

} // namespace xdg

#endif // XDG_UTIL_RNG_H
