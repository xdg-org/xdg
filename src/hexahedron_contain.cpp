#include "xdg/hexahedron_contain.h"

#include "xdg/constants.h"
#include "xdg/geometry/face_common.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/ray.h"
#include "xdg/vec3da.h"

namespace xdg
{

bool select_diagonal(const std::array<Vertex, 8>& verts,
                     const std::array<int, 4>& face)
{
  std::array<Vertex, 4> face_verts = {verts[face[0]], verts[face[1]], verts[face[2]], verts[face[3]]};
  return canonical_diagonal(face_verts);
}

bool hex_containment_test(const Position& point,
                          const std::array<Vertex, 8>& verts)
{
  static constexpr std::array<std::array<int, 4>, 6> k_hex_faces = {
    std::array<int, 4>{0, 3, 2, 1},
    std::array<int, 4>{4, 5, 6, 7},
    std::array<int, 4>{0, 1, 5, 4},
    std::array<int, 4>{1, 2, 6, 5},
    std::array<int, 4>{2, 3, 7, 6},
    std::array<int, 4>{3, 0, 4, 7}
  };

  Position centroid {0.0, 0.0, 0.0};
  for (const auto& v : verts) {
    centroid += v;
  }
  centroid = centroid / 8.0;

  auto outside_triangle = [&](int i0, int i1, int i2) {
    const auto& v0 = verts[i0];
    const auto& v1 = verts[i1];
    const auto& v2 = verts[i2];
    Direction normal = (v1 - v0).cross(v2 - v0);
    if (normal.dot(centroid - v0) > 0.0) {
      normal = -normal;
    }

    const double dist = normal.dot(point - v0);
    return dist > PLUCKER_ZERO_TOL;
  };

  for (const auto& face : k_hex_faces) {
    // diagonal is selected based on a lexicographic comparison
    // of vertex coordinates so diagonals are chosen consistently
    // regardless of face connectivity ordering
    if (select_diagonal(verts, face)) {
      if (outside_triangle(face[0], face[1], face[2]) ||
          outside_triangle(face[0], face[2], face[3])) {
        return false;
      }
    } else {
      if (outside_triangle(face[1], face[2], face[3]) ||
          outside_triangle(face[1], face[3], face[0])) {
        return false;
      }
    }
  }

  return true;
}

void HexahedronIntersectionFunc(RTCIntersectFunctionNArguments* args)
{
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);
  if (vertices.size() != 8) {
    fatal_error("HexahedronIntersectionFunc expected 8 vertices, got {}", vertices.size());
  }

  std::array<Vertex, 8> verts;
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i] = vertices[i];
  }

  RTCDualRayHit* rayhit = (RTCDualRayHit*)args->rayhit;
  RTCSurfaceDualRay& ray = rayhit->ray;

  Position ray_origin = {ray.dorg[0], ray.dorg[1], ray.dorg[2]};

  bool inside = hex_containment_test(ray_origin, verts);
  if (!inside) return;

  rayhit->hit.u = 0.0;
  rayhit->hit.v = 0.0;
  rayhit->hit.Ng_x = 0.0;
  rayhit->hit.Ng_y = 0.0;
  rayhit->hit.Ng_z = 0.0;
  rayhit->hit.geomID = args->geomID;
  rayhit->hit.primID = args->primID;
}

void HexahedronOcclusionFunc(RTCOccludedFunctionNArguments* args)
{
  const VolumeElementsUserData* user_data = (const VolumeElementsUserData*)args->geometryUserPtr;
  const MeshManager* mesh_manager = user_data->mesh_manager;

  const PrimitiveRef primitive_ref = user_data->prim_ref_buffer[args->primID];
  auto vertices = mesh_manager->element_vertices(primitive_ref.primitive_id);
  if (vertices.size() != 8) {
    fatal_error("HexahedronOcclusionFunc expected 8 vertices, got {}", vertices.size());
  }

  std::array<Vertex, 8> verts;
  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i] = vertices[i];
  }

  RTCElementDualRay* ray = (RTCElementDualRay*)args->ray;
  Position ray_origin = {ray->dorg[0], ray->dorg[1], ray->dorg[2]};

  bool inside = hex_containment_test(ray_origin, verts);
  if (!inside) return;

  // indicate that the query is complete due to a successful containment result
  ray->element = primitive_ref.primitive_id;
  ray->set_tfar(-INFTY);
}

} // namespace xdg
