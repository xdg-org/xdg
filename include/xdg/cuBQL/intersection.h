#ifndef _XDG_CUBQL_INTERSECTION_H
#define _XDG_CUBQL_INTERSECTION_H

// Guards to prevent CUDA headers from being included in host code, which causes
// failed compilation with LLVM-clang.
#if defined(__CUDA_ARCH__) && !defined(__CUDACC__)
#undef __CUDA_ARCH__
#endif

#include <cstddef>
#include <vector>

#include "xdg/constants.h"
#include "xdg/cuBQL/triangles.h"
#include "cuBQL/math/vec.h"

namespace xdg {

struct CuBQLRay {
  cuBQL::vec3d origin;
  cuBQL::vec3d direction;
  double tMin {0.0};
  double tMax {INFTY};
  MeshID volume {ID_NONE}; // volume we are tracing ray against
};

/* POD SurfaceRay struct for external population*/
// struct CuBQLSurfaceRay {
//   double origin[3];
//   double direction[3];
//   uint32_t volume_slot;
//   uint32_t enabled;
//   const MeshID* exclude_primitives;
//   int32_t exclude_count;
// };

// TODO - Consider whether this is useful/necessary as its own struct
// struct CuBQLExcludeList {
//   const MeshID* primitives {nullptr};
//   int count {0};
// };

struct CuBQLSurfaceHit {
  double distance {INFTY};
  MeshID surface {ID_NONE};
  MeshID primitive {ID_NONE};
  PointInVolume piv {OUTSIDE};

  bool hit_found() const { return primitive != ID_NONE; }
};

inline bool orientation_cull(double normal_dot_direction,
                             HitOrientation orientation)
{
  if (orientation == HitOrientation::ANY) return false;

  if (orientation == HitOrientation::EXITING && normal_dot_direction < 0.0) {
    return true;
  } else if (orientation == HitOrientation::ENTERING && normal_dot_direction >= 0.0) {
    return true;
  }

  return false;
}

/*
Wrapper for launching a single ray intersection query against the surface tree, with Host<->Device staging of ray and hit data
Performs host side staging and transfer hit data back to host after device side traversal
*/
void
intersect_surface_tree_scalar(const cubql::Context& context,
                              const CuBQLVolumeTLAS& volume_tlas,
                              const CuBQLRay& ray,
                              CuBQLSurfaceHit& hit,
                              HitOrientation hit_orientation,
                              const std::vector<MeshID>* exclude_primitives);


void
intersect_surface_tree_batch(const cubql::Context& context,
                             const CuBQLVolumeTLAS::DD* d_volume_to_tlas,
                             const CuBQLRay* d_rays,
                             CuBQLSurfaceHit* d_hits,
                             std::size_t num_rays,
                             HitOrientation hit_orientation);

} // namespace xdg

#endif // include guard
