#ifndef _XDG_CUBQL_TRIANGLES_H
#define _XDG_CUBQL_TRIANGLES_H

#include <cstdint>

// Guards to prevent CUDA headers from being included in host code, which causes
// failed compilation with LLVM-clang.
#if defined(__CUDA_ARCH__) && !defined(__CUDACC__)
#undef __CUDA_ARCH__
#endif

#include "cuBQL/bvh.h"
#include "cuBQL/math/vec.h"
#include "xdg/constants.h"
#include "xdg/cuBQL/cuBQL_backend.h"

namespace xdg {

/**
  Owns the primitive buffers for one topological surface. The nested DD type is
  the compact, non-owning device-data view used in OpenMP target regions.
*/
struct CuBQLSurfaceMesh {
  struct DD {
    // Topological metadata
    MeshID surface_id {ID_NONE};
    double max_parent_volume_bump {0.0};

    // Geometric data
    const cuBQL::vec3d* vertices {nullptr};
    const cuBQL::vec3i* indices {nullptr};
    const MeshID* primitive_ids {nullptr};
  };

  // Topological metadata
  MeshID surface_id {ID_NONE};
  double max_parent_volume_bump {0.0};

  // Device buffers for primitive data
  cuBQL::vec3d* d_vertices {nullptr};
  cuBQL::vec3i* d_indices {nullptr};
  MeshID* d_primitive_ids {nullptr};

  std::uint32_t num_vertices {0};
  std::uint32_t num_primitives {0};
  int gpu_id {0};

  // Return the non-owning view used by device traversal and intersection code.
  DD get_device_data() const
  {
    return {
      surface_id,
      max_parent_volume_bump,
      d_vertices,
      d_indices,
      d_primitive_ids
    };
  }

  void release();
};

/**
  Owns the flattened cuBQL BVH for one topological volume. The BVH is built over
  all primitives belonging to the volume's surfaces, while PrimRef maps each
  BVH primitive back to its surface and surface-local primitive. The nested DD
  type is the compact, non-owning device-data view used during traversal.
*/
struct CuBQLVolumeGroup {
  struct SurfaceDD {
    CuBQLSurfaceMesh::DD mesh;

    bool reverse_sense {false};
    MeshID next_volume {ID_NONE};
    SurfaceBoundaryCondition boundary_condition {UNSET};
  };

  // Identifies a primitive within the volume group's local surface array.
  // TODO - Should this exist outside of volume group as an indpendent struct?
  // TODO - can we think of a better name to distinguish between CuBQLSurfaceMesh::primitive_ids and this struct?
  struct PrimRef {
    std::uint32_t surface_index {0};
    std::uint32_t primitive_index {0};
  };

  struct DD {
    const SurfaceDD* surfaces {nullptr};
    const PrimRef* prim_refs {nullptr};
    cuBQL::bvh3f bvh;
  };

  cuBQL::bvh3f bvh;
  SurfaceDD* d_surfaces {nullptr};
  PrimRef* d_prim_refs {nullptr};

  std::uint32_t num_surfaces {0};
  std::uint32_t num_primitives {0};
  int gpu_id {0};

  // Return the non-owning view used by device traversal and intersection code.
  DD get_device_data() const
  {
    return {d_surfaces, d_prim_refs, bvh};
  }

  void release();
};

// Future acceleration structure over instances of volume groups.
struct CuBQLInstanceGroup;

} // namespace xdg

#endif // include guard
