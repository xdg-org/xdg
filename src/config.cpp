
#include "xdg/config.h"
#include "xdg/error.h"
#include <cstdlib>  // for std::atexit

#ifdef XDG_HAVE_OPENMP
#include "omp.h"
#endif

using namespace xdg;

namespace xdg {
#ifdef XDG_ENABLE_LIBMESH
std::unique_ptr<libMesh::LibMeshInit> xdg_libmesh_init {nullptr};

// Custom cleanup function that will be called at program exit
// This ensures LibMeshInit is destroyed after all other global destructors
static void cleanup_libmesh_at_exit() {
  if (xdg_libmesh_init) {
    xdg_libmesh_init.reset();
  }
}
#endif
}

void XDGConfig::initialize() {
  // if threads aren't manually specified,
  // set using OpenMP if available, otherwise 1
  if (n_threads() == -1) {
  #ifdef XDG_HAVE_OPENMP
    set_n_threads(omp_get_num_threads());
  #else
    set_n_threads(1);
  #endif
  }

#ifdef XDG_ENABLE_LIBMESH
  // libmesh requires the program name, so at least one argument is needed
  int argc = 1;
  const std::string argv{"XDG"};
  const char *argv_cstr = argv.c_str();
  xdg_libmesh_init = std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, 0, n_threads());
  // register for cleanup at program exit
  // TODO: explore cleaner options that ensure this occurs after
  // destruction of all other libMesh objects more elegantly
  std::atexit(cleanup_libmesh_at_exit);
#endif

  initialized_ = true;
}

void XDGConfig::set_n_threads(int n_threads) {
  if (n_threads <= 0)
    warning("Number of threads must be positive. Using 1 thread.");

  n_threads = std::max(n_threads, 1);
  n_threads_ = n_threads;
}

#ifdef XDG_ENABLE_LIBMESH
const std::unique_ptr<libMesh::LibMeshInit>& XDGConfig::libmesh_init() {
  if (!initialized()) {
    initialize();
  }
  return xdg_libmesh_init;
}
#endif

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