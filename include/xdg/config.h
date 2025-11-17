
#ifndef XDG_CONFIG_H
#define XDG_CONFIG_H

#include <memory>
#include <string>
#include <unordered_map>

#include "xdg/constants.h"

#ifdef XDG_ENABLE_LIBMESH
#include "libmesh/libmesh.h"
#endif

namespace xdg {

#ifdef XDG_ENABLE_LIBMESH
extern std::unique_ptr<libMesh::LibMeshInit> xdg_libmesh_init;
#endif
class XDGConfig {
public:
  // Get the singleton instance
  static XDGConfig& config() {
    static XDGConfig instance;
    return instance;
  }

  // Delete copy constructor and assignment operator
  XDGConfig(const XDGConfig&) = delete;
  XDGConfig& operator=(const XDGConfig&) = delete;

private:
  // Private constructor
  XDGConfig() {};

  // Configuration options
  std::unordered_map<std::string, std::string> options_;


public:

  void initialize();

  int n_threads() const { return n_threads_; }

  void set_n_threads(int n_threads);

  bool ray_tracer_enabled(RTLibrary rt_lib) const;

  bool mesh_manager_enabled(MeshLibrary mesh_lib) const;

  bool initialized() const { return initialized_; }

  #ifdef XDG_ENABLE_LIBMESH
  const libMesh::LibMeshInit* libmesh_init();
  const libMesh::Parallel::Communicator* libmesh_comm();
  #endif

private:
  // Data members
  int n_threads_ {-1};
  bool initialized_ {false};
};

} // namespace xdg

#endif // XDG_CONFIG_H
