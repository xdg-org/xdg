#ifndef XDG_UTIL_RNG_H
#define XDG_UTIL_RNG_H

#include <random>

namespace xdg {

static std::random_device rd;
static std::mt19937 gen(rd());

inline double rand_double(double min, double max)
{
  std::uniform_real_distribution<double> dis(min, max);
  return dis(gen);
}

} // namespace xdg

#endif // XDG_UTIL_RNG_H
