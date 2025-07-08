#include "xdg/element_face_accessor.h"
#include "xdg/error.h"

#ifdef XDG_ENABLE_MOAB
#include "xdg/moab/mesh_manager.h"
#endif

#ifdef XDG_ENABLE_LIBMESH
#include "xdg/libmesh/mesh_manager.h"
#endif

#include "xdg/testing/mesh_mock.h"

namespace xdg {

std::shared_ptr<ElementFaceAccessor> ElementFaceAccessor::create(const MeshManager* mesh_manager, MeshID element) {
  #ifdef XDG_ENABLE_MOAB
  if (mesh_manager->mesh_library() == MeshLibrary::MOAB) {
    const MOABMeshManager* moab_mesh_manager = dynamic_cast<const MOABMeshManager*>(mesh_manager);
    return std::make_shared<MOABElementFaceAccessor>(moab_mesh_manager, element);
  }
  #endif
  #ifdef XDG_ENABLE_LIBMESH
  if (mesh_manager->mesh_library() == MeshLibrary::LIBMESH) {
    const LibMeshManager* libmesh_mesh_manager = dynamic_cast<const LibMeshManager*>(mesh_manager);
    return std::make_shared<LibMeshElementFaceAccessor>(libmesh_mesh_manager, element);
  }
  #endif
  // for testing
  if (mesh_manager->mesh_library() == MeshLibrary::MOCK) {
    const MeshMock* mock_mesh_manager = dynamic_cast<const MeshMock*>(mesh_manager);
    return std::make_shared<MockElementFaceAccessor>(mock_mesh_manager, element);
  }
  fatal_error("ElementFaceAccessor::create: Unsupported mesh library");
  return nullptr;
}

} // namespace xdg