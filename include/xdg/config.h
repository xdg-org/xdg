
#ifndef XDG_CONFIG_H
#define XDG_CONFIG_H

#include <memory>
#include <string>
#include <unordered_map>

#include "xdg/constants.h"
#include "xdg/error.h"

#ifdef XDG_ENABLE_LIBMESH
#include "libmesh/libmesh.h"
#endif

namespace xdg {

namespace config {

#ifdef XDG_ENABLE_LIBMESH
extern std::unique_ptr<libMesh::LibMeshInit> xdg_libmesh_init;
extern const libMesh::LibMeshInit* external_libmesh_init;
extern const libMesh::Parallel::Communicator* external_libmesh_comm;
#endif

} // namespace config
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

  void reset() {
    initialized_ = false;
    n_threads_ = -1;
    reset_libmesh_init();
  }

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

  void set_libmesh_external_init(
    const std::unique_ptr<libMesh::LibMeshInit>& libmesh_init
  )
  {
    set_libmesh_external_init(libmesh_init.get());
  }

  void set_libmesh_external_init(
    libMesh::LibMeshInit* libmesh_init
  )
  {
    if (config::external_libmesh_init != nullptr) {
      fatal_error("LibMesh external initialization has already been set. Overwriting.");
    }
    config::external_libmesh_init = libmesh_init;
    config::external_libmesh_comm = &(libmesh_init->comm());
  }
  #endif

  void reset_libmesh_init()
  {
  // function is a null-op if libmesh not enabled
#ifdef XDG_ENABLE_LIBMESH
    if (config::xdg_libmesh_init) {
      config::xdg_libmesh_init.reset();
    }
    // reset external pointers if set
    config::external_libmesh_init = nullptr;
    config::external_libmesh_comm = nullptr;
#endif
  }

private:
  // Data members
  int n_threads_ {-1};
  bool initialized_ {false};
};

} // namespace xdg

#endif // XDG_CONFIG_H
