#include <random>

#include "xdg/constants.h"
#include <catch2/catch_test_macros.hpp>

static std::random_device rd;
static std::mt19937 gen(rd());

inline double rand_double(double min, double max)
{
  std::uniform_real_distribution<double> dis(min, max);
  return dis(gen);
}

inline void check_ray_tracer_supported(xdg::RTLibrary rt) {
  #ifndef XDG_ENABLE_EMBREE
  if (rt == xdg::RTLibrary::EMBREE) {
    SKIP("Embree backend not built; skipping.");
  }
  #endif

  #ifndef XDG_ENABLE_GPRT
  if (rt == xdg::RTLibrary::GPRT) {
    SKIP("GPRT backend not built; skipping.");
  }
  #endif
}