#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>

#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"

int main(int argc, char* argv[])
{
    // Not sure if this is the best way but based on CI matrix in the event
    // where MOAB and LibMesh both defined we are gonna end up using MOAB mesh manager
#if defined(XDG_ENABLE_MOAB)
    auto mesh_manager = std::make_shared<xdg::MOABMeshManager>();
    mesh_manager->load_file("cube.h5m");
#elif defined(XDG_ENABLE_LIBMESH)
    auto mesh_manager = std::make_shared<xdg::LibMeshManager>();
    mesh_manager->load_file("brick.exo");
#endif

    mesh_manager->init();

}