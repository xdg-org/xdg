
#include "xdg/config.h"

#ifdef XDG_HAVE_OPENMP
#include "omp.h"
#endif

using namespace xdg;

XDGConfig::XDGConfig(int n_threads) {
  // Set the number of threads
  if (n_threads != -1) {
    n_threads_ = n_threads;
  }

  // if threads aren't manually specified,
  // set using OpenMP
  if (n_threads_ == -1) {
  #ifdef XDG_HAVE_OPENMP
    n_threads_ = omp_get_num_threads();
  #else
    n_threads_ = 1;
  #endif
  }

  // Initialize libMesh if enabled
  initialize_libraries();
}

void XDGConfig::initialize_libraries() {
#ifdef XDG_ENABLE_LIBMESH
  // libmesh requires the program name, so at least one argument is needed
  int argc = 1;
  const std::string argv{"XDG"};
  const char *argv_cstr = argv.c_str();
  libmesh_init_ = std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, 0, n_threads_);
#endif

  initialized_ = true;
}

bool XDGConfig::ray_tracer_enabled(RTLibrary rt_lib) const {
  #ifdef XDG_ENABLE_EMBREE
  if (rt_lib == RTLibrary::EMBREE) return true;
  #endif
  #ifdef XDG_ENABLE_GPRT
  if (rt_lib == RTLibrary::GPRT) return true;
  #endif
  return false;
}

bool XDGConfig::mesh_manager_enabled(MeshLibrary mesh_lib) const {
  #ifdef XDG_ENABLE_MOAB
  if (mesh_lib == MeshLibrary::MOAB) return true;
  #endif
  #ifdef XDG_ENABLE_LIBMESH
  if (mesh_lib == MeshLibrary::LIBMESH) return true;
  #endif
  return false;
}