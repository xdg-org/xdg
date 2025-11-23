// stl includes
#include <memory>

// testing includes
#include <catch2/catch_test_macros.hpp>

// xdg includes
#include "xdg/config.h"
#include "xdg/error.h"
#include "xdg/mesh_managers.h"

#ifdef XDG_ENABLE_LIBMESH
#include "libmesh/libmesh.h"
#endif

#ifdef XDG_ENABLE_LIBMESH
TEST_CASE("External LibMesh Initialization")
{
  // Initialize libMesh as though an external application is doing it
  // (e.g. a MOOSE app)
  int argc = 1;
  char* argv[] = { (char*)"xdg_libmesh_init" };
  std::unique_ptr<libMesh::LibMeshInit> xdg_libmesh_init =
      std::make_unique<libMesh::LibMeshInit>(argc, argv);

  // apply this LibMeshInit object to XDG's config
  xdg::XDGConfig::config().set_libmesh_external_init(xdg_libmesh_init.get());

  REQUIRE(xdg::XDGConfig::config().libmesh_init() == xdg_libmesh_init.get());
  REQUIRE(xdg::XDGConfig::config().libmesh_comm() == &xdg_libmesh_init->comm());
}

TEST_CASE("Internal LibMesh Initialization")
{
  xdg::XDGConfig::config().reset();
  REQUIRE(xdg::config::xdg_libmesh_init == nullptr);

  // the first call for these objects through the XDGConfig singleton
  // should cause XDG to initialize its own LibMeshInit object
  REQUIRE(xdg::XDGConfig::config().libmesh_init() != nullptr);
  REQUIRE(xdg::XDGConfig::config().libmesh_comm() != nullptr);

  REQUIRE(xdg::config::xdg_libmesh_init.get() == xdg::XDGConfig::config().libmesh_init());
  REQUIRE(&xdg::config::xdg_libmesh_init->comm() == xdg::XDGConfig::config().libmesh_comm());
}
#endif

TEST_CASE("Config set threads")
{
  xdg::XDGConfig::config().reset();
  xdg::XDGConfig::config().set_n_threads(4);
  REQUIRE(xdg::XDGConfig::config().n_threads() == 4);
}