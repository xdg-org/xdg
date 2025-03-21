#ifndef _XDG_GEOMETRY_DATA_H
#define _XDG_GEOMETRY_DATA_H

#include "xdg/constants.h"

namespace xdg
{

struct MeshManager; // Forward declaration
struct PrimitiveRef; // Forward declaration

struct GeometryUserData {
  MeshID surface_id {ID_NONE}; //! ID of the surface this geometry data is associated with
  MeshManager* mesh_manager {nullptr}; //! Pointer to the mesh manager for this geometry
  PrimitiveRef* prim_ref_buffer {nullptr}; //! Pointer to the mesh primitives in the geometry
  double box_bump; //! Bump distance for the bounding boxes in this geometry
};

struct VolumeElementsUserData {
  MeshID volume_id {ID_NONE};
  MeshManager* mesh_manager {nullptr};
}

} // namespace xdg
#endif // include guard