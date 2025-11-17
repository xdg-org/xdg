
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

// External pointers that can be set by external applications
const libMesh::LibMeshInit* external_libmesh_init {nullptr};
const libMesh::Parallel::Communicator* external_libmesh_comm {nullptr};

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
  // this is here entirely to make sure that
  // libMesh respects the OpenMP settings of the host application
  if (n_threads() == -1) {
  #ifdef XDG_HAVE_OPENMP
    set_n_threads(omp_get_num_threads());
  #else
    set_n_threads(1);
  #endif
  }

#ifdef XDG_ENABLE_LIBMESH
  // libmesh requires the program name, so at least one argument is needed
  if (external_libmesh_init == nullptr) {
    int argc = 1;
    const std::string argv{"XDG"};
    const char *argv_cstr = argv.c_str();
    // in one version of the LibMeshInit constructor, MPI_Comm is an int and in
    // another, it is MPI_Comm (which is not compatible with int for some MPI
    // implementations), so we need to handle both cases here.
    #ifdef LIBMESH_HAVE_MPI
    xdg_libmesh_init = std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, MPI_COMM_WORLD, n_threads());
    #else
    xdg_libmesh_init = std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, 0, n_threads());
    #endif
  }
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
const libMesh::LibMeshInit*
XDGConfig::libmesh_init() {
  if (!initialized()) {
    initialize();
  }
  if (external_libmesh_init != nullptr) {
    return external_libmesh_init;
  }
  return xdg_libmesh_init.get();
}

const libMesh::Parallel::Communicator*
XDGConfig::libmesh_comm() {
  if (external_libmesh_comm != nullptr) {
    return external_libmesh_comm;
  }
  return &(libmesh_init()->comm());
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