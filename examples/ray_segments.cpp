#include <iostream>
#include <iomanip>
#include <numeric>
#include <random>

#include "xdg/xdg.h"
#include "xdg/constants.h"
#include "xdg/mesh_managers.h"

int main(int argc, char* argv[])
{

#if defined(XDG_ENABLE_MOAB)
    auto mesh_manager = std::make_shared<xdg::MOABMeshManager>();
    mesh_manager->load_file("cube.h5m");
#elif defined(XDG_ENABLE_LIBMESH)
    auto mesh_manager = std::make_shared<xdg::LibMeshManager>();
    mesh_manager->load_file("brick.exo");
#endif

    mesh_manager->init();

}