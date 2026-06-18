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
  if (d_primitive_refs) {
    omp_target_free(d_primitive_refs, gpu_id);
    d_primitive_refs = nullptr;
  }
}

void CuBQLSurfaceBLAS::release()
{
  if (bvh.primIDs) {
    omp_target_free(bvh.primIDs, gpu_id);
    bvh.primIDs = nullptr;
  }
  if (bvh.nodes) {
    omp_target_free(bvh.nodes, gpu_id);
    bvh.nodes = nullptr;
  }
  mesh.release();
}

void CuBQLVolumeTLAS::release()
{
  if (bvh.primIDs) {
    omp_target_free(bvh.primIDs, gpu_id);
    bvh.primIDs = nullptr;
  }
  if (bvh.nodes) {
    omp_target_free(bvh.nodes, gpu_id);
    bvh.nodes = nullptr;
  }
  if (d_surface_instances) {
    omp_target_free(d_surface_instances, gpu_id);
    d_surface_instances = nullptr;
  }
}

} // namespace xdg
