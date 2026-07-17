#include "xdg/cuBQL/triangles.h"

#include <omp.h>

namespace xdg {

void CuBQLSurfaceMesh::release()
{
  if (d_vertices) {
    omp_target_free(d_vertices, gpu_id);
    d_vertices = nullptr;
  }
  if (d_indices) {
    omp_target_free(d_indices, gpu_id);
    d_indices = nullptr;
  }
  if (d_primitive_ids) {
    omp_target_free(d_primitive_ids, gpu_id);
    d_primitive_ids = nullptr;
  }

  num_vertices = 0;
  num_primitives = 0;
}

void CuBQLVolumeGroup::release()
{
  if (bvh.primIDs) {
    omp_target_free(bvh.primIDs, gpu_id);
    bvh.primIDs = nullptr;
  }
  if (bvh.nodes) {
    omp_target_free(bvh.nodes, gpu_id);
    bvh.nodes = nullptr;
  }
  if (d_surfaces) {
    omp_target_free(d_surfaces, gpu_id);
    d_surfaces = nullptr;
  }
  if (d_prim_refs) {
    omp_target_free(d_prim_refs, gpu_id);
    d_prim_refs = nullptr;
  }

  bvh.numNodes = 0;
  bvh.numPrims = 0;
  num_surfaces = 0;
  num_primitives = 0;
}

} // namespace xdg
