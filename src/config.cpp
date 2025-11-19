
#include "xdg/config.h"
#include "xdg/error.h"
#include <cstdlib>  // for std::atexit

#ifdef XDG_HAVE_OPENMP
#include "omp.h"
#endif

using namespace xdg;

namespace xdg {
#ifdef XDG_ENABLE_LIBMESH

namespace config {
std::unique_ptr<libMesh::LibMeshInit> xdg_libmesh_init {nullptr};
// External pointers that can be set by external applications
const libMesh::LibMeshInit* external_libmesh_init {nullptr};
const libMesh::Parallel::Communicator* external_libmesh_comm {nullptr};
} // namespace config

// libMesh expects to be able to clean up some static objects upon destruction
// of LibMeshInit. If they are already cleaned up by the compiler at program exit,
// libMesh can attempt to double-free them. To avoid this, we register a cleanup
// function to be called at exit before those objects are deleted by libMesh.
static void cleanup_libmesh_at_exit() {
  if (config::xdg_libmesh_init) {
    config::xdg_libmesh_init.reset();
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
  if (config::external_libmesh_init == nullptr) {
    int argc = 1;
    const std::string argv{"XDG"};
    const char *argv_cstr = argv.c_str();
    // in one version of the LibMeshInit constructor, MPI_Comm is an int and in
    // another, it is MPI_Comm (which is not compatible with int for some MPI
    // implementations), so we need to handle both cases here.
    #ifdef LIBMESH_HAVE_MPI
    config::xdg_libmesh_init =
      std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, MPI_COMM_WORLD, n_threads());
    #else
    config::xdg_libmesh_init =
      std::make_unique<libMesh::LibMeshInit>(argc, &argv_cstr, 0, n_threads());
    #endif
  }
  // register for cleanup at program exit

  // libMesh expects to be able to clean some static objects up at exit if they
  // aren't present, so we register this cleanup function to be called at exit
  // before those objects are deleted by libMesh. Otherwise, a double-free can
  // occur.
  std::atexit(cleanup_libmesh_at_exit);
#endif

initialized_ = true;
}

void XDGConfig::set_n_threads(int n_threads) {
  if (n_threads <= 0)
    warning("Number of threads must be positive. Using 1 thread.");

  #ifdef XDG_ENABLE_LIBMESH
  if (config::xdg_libmesh_init != nullptr || config::external_libmesh_init != nullptr) {
    warning("Changing number of threads after LibMesh initialization has no effect.\n"
      "       Please set number of threads before accessing any LibMesh functionality on this class.");
  }
  #endif

  n_threads_ = std::max(n_threads, 1);
}

#ifdef XDG_ENABLE_LIBMESH
const libMesh::LibMeshInit*
XDGConfig::libmesh_init() {
  if (!initialized()) {
    initialize();
  }
  if (config::external_libmesh_init != nullptr) {
    return config::external_libmesh_init;
  }
  return config::xdg_libmesh_init.get();
}

const libMesh::Parallel::Communicator*
XDGConfig::libmesh_comm() {
  if (config::external_libmesh_comm != nullptr) {
    return config::external_libmesh_comm;
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