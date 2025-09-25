#include <random>

#include <catch2/catch_test_macros.hpp>

#include "xdg/constants.h"
#include "xdg/ray_tracers.h"

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

// Factory method to create ray tracer based on which library selected
inline std::shared_ptr<xdg::RayTracer> create_raytracer(xdg::RTLibrary rt) {
  switch (rt) {
    case xdg::RTLibrary::EMBREE:
    #ifdef XDG_ENABLE_EMBREE
      return std::make_shared<xdg::EmbreeRayTracer>();
    #else
      SKIP("Embree backend not built; skipping.");
      return {};
    #endif
    case xdg::RTLibrary::GPRT:
    #ifdef XDG_ENABLE_GPRT
      return std::make_shared<xdg::GPRTRayTracer>();
    #else
      SKIP("GPRT backend not built; skipping.");
      return {};
    #endif
  }
  FAIL("Unknown RT backend enum value");
  return {};
}