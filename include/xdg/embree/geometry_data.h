#ifndef _XDG_GEOMETRY_DATA_H
#define _XDG_GEOMETRY_DATA_H

#include "xdg/constants.h"

namespace xdg
{

struct MeshManager; // Forward declaration
struct PrimitiveRef; // Forward declaration

struct SurfaceUserData {
  MeshID surface_id {ID_NONE}; //! ID of the surface this geometry data is associated with
  MeshManager* mesh_manager {nullptr}; //! Pointer to the mesh manager for this geometry
  PrimitiveRef* prim_ref_buffer {nullptr}; //! Pointer to the mesh primitives in the geometry
  double box_bump; //! Bump distance for the bounding boxes in this geometry
  MeshID forward_vol {ID_NONE}; // ID of the forward sense volume
  MeshID reverse_vol {ID_NONE}; // ID of the reverse sense volume
};

struct VolumeElementsUserData {
  MeshID volume_id {ID_NONE}; //! ID of the volume this geometry data is associated with
  MeshManager* mesh_manager {nullptr}; //! Pointer to the mesh manager for this geometry
  PrimitiveRef* prim_ref_buffer {nullptr}; //! Pointer to the mesh primitives in the geometry
};

} // namespace xdg
#endif // include guard