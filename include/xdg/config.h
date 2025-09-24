
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
  XDGConfig(int n_threads = -1);

  // Configuration options
  std::unordered_map<std::string, std::string> options_;

  void initialize_libraries();

public:

  bool ray_tracer_enabled(RTLibrary rt_lib) const;
  bool mesh_manager_enabled(MeshLibrary mesh_lib) const;

private:
  #ifdef XDG_ENABLE_LIBMESH
  std::unique_ptr<libMesh::LibMeshInit> libmesh_init_ {nullptr};
  #endif
  // Data members
  int n_threads_ {-1};
  bool initialized_ {false};
};

} // namespace xdg

#endif // XDG_CONFIG_H
