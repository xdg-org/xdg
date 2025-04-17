#include <memory>
#include <set>

#include "xdg/error.h"
#include "xdg/overlap.h"

#include "xdg/xdg.h"
#include "gprt/gprt.h"
#include "sharedCode.h"
#include "argparse/argparse.hpp"



#ifdef _OPENMP
#include <omp.h>
#endif

#define LOG(message)                                            \
  std::cout << GPRT_TERMINAL_BLUE;                               \
  std::cout << "#gprt.sample(main): " << message << std::endl;   \
  std::cout << GPRT_TERMINAL_DEFAULT;
#define LOG_OK(message)                                         \
  std::cout << GPRT_TERMINAL_LIGHT_BLUE;                         \
  std::cout << "#gprt.sample(main): " << message << std::endl;   \
  std::cout << GPRT_TERMINAL_DEFAULT;

using namespace xdg;
extern GPRTProgram gprt_test_deviceCode;


// Initial image resolution
const int2 fbSize = {1400, 460};

// Output file name for the rendered image
const char *outFileName = "gprt-triangle.png";

int main(int argc, char* argv[]) {

  // Argparse
  argparse::ArgumentParser args("XDG-GPRT Integration Testing Tool", "1.0", argparse::default_arguments::help);

  args.add_argument("filename")
  .help("Path to the input file");

  try {
    args.parse_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cout << err.what() << std::endl;
    std::cout << args;
    exit(0);
  }

  auto hdf_file = args.get<std::string>("filename");

  // Initialize XDG
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(hdf_file);
  mm->init();
  xdg->prepare_raytracer();


  // Create a rendering window
  gprtRequestWindow(fbSize.x, fbSize.y, hdf_file.c_str());

  // Initialize GPRT context and modules
  GPRTContext context = gprtContextCreate();
  GPRTModule module = gprtModuleCreate(context, gprt_test_deviceCode);  

  // New: Create a "triangle" geometry type and set it's closest-hit program
  auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context, GPRT_TRIANGLES);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module, "TriangleMesh");


  std::vector<GPRTBufferOf<float3>> vertex_buffers;
  std::vector<GPRTBufferOf<uint3>> connectivity_buffers;
  std::vector<GPRTGeomOf<TrianglesGeomData>> trianglesGeom;
  std::vector<size_t> vertex_counts;
  std::vector<size_t> connectivity_counts;

  for (auto surf : mm->surfaces()) {
    auto flat_verts = mm->get_surface_vertices(surf);
    auto flat_indices = mm->get_surface_connectivity(surf);
    std::vector<float3> verts(flat_verts.size() / 3);
    std::vector<uint3> inds(flat_indices.size() / 3);
    
    for (size_t i = 0; i < flat_verts.size() / 3; i++) {
      verts[i] = float3(flat_verts[3 * i], flat_verts[3 * i + 1], flat_verts[3 * i + 2]);
    }
    vertex_counts.push_back(verts.size());
    for (size_t i = 0; i < flat_indices.size() / 3; i++) {
      inds[i] = uint3(flat_indices[3 * i], flat_indices[3 * i + 1], flat_indices[3 * i + 2]);
    }
    connectivity_counts.push_back(inds.size());

    vertex_buffers.push_back(gprtDeviceBufferCreate<float3>(context, verts.size(), verts.data()));
    connectivity_buffers.push_back(gprtDeviceBufferCreate<uint3>(context, inds.size(), inds.data()));
    trianglesGeom.push_back(gprtGeomCreate<TrianglesGeomData>(context, trianglesGeomType));
    TrianglesGeomData* geom_data = gprtGeomGetParameters(trianglesGeom.back());
    // geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffers.back());
    // geom_data->index = gprtBufferGetDevicePointer(connectivity_buffers.back());
    // geom_data->id = surf;
    // geom_data->vols = {mm->get_parent_volumes(surf).first, mm->get_parent_volumes(surf).second};

  }

  for (int i=0; i<mm->num_surfaces(); i++) { 
    auto &surf = mm->surfaces()[i];
    // New: Create geometry instance and set vertex and index buffers
    gprtTrianglesSetVertices(trianglesGeom[i], vertex_buffers[i], vertex_counts[i]);
    gprtTrianglesSetIndices(trianglesGeom[i], connectivity_buffers[i], connectivity_counts[i]);
  }

  // Create a BLAS for each geometry
  std::vector<GPRTAccel> blasList;
  for (size_t i = 0; i < trianglesGeom.size(); i++) {
      GPRTAccel blas = gprtTriangleAccelCreate(context, trianglesGeom[i], GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
      gprtAccelBuild(context, blas, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);
      blasList.push_back(blas);
  }

  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances
  std::vector<gprt::Instance> instances;
  for (size_t i = 0; i < blasList.size(); i++) {
      instances.push_back(gprtAccelGetInstance(blasList[i]));
  }

  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context, instances.size(), instances.data());
  GPRTAccel world = gprtInstanceAccelCreate(context, instances.size(), instanceBuffer);
  gprtAccelBuild(context, world, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);

  // Set up ray generation and miss programs
  GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context, module, "raygen");
  GPRTMissOf<void> miss = gprtMissCreate<void>(context, module, "miss");

  // New: Here, we place a reference to our TLAS in the ray generation
  // kernel's parameters, so that we can access that tree when
  // we go to trace our rays.
  RayGenData *rayGenData = gprtRayGenGetParameters(rayGen);
  rayGenData->world = gprtAccelGetDeviceAddress(world);

  GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context, fbSize.x * fbSize.y);
  rayGenData->frameBuffer = gprtBufferGetDevicePointer(frameBuffer);

  // Build the Shader Binding Table (SBT), updating all parameters.
  gprtBuildShaderBindingTable(context, GPRT_SBT_ALL);

  // Main render loop
  PushConstants pc;
  do {
    pc.time = float(gprtGetTime(context));
    gprtRayGenLaunch2D(context, rayGen, fbSize.x, fbSize.y, pc);
    gprtBufferPresent(context, frameBuffer);
  }
  while (!gprtWindowShouldClose(context));

  // Save final frame to an image
  gprtBufferSaveImage(frameBuffer, fbSize.x, fbSize.y, outFileName);

  // Clean up resources
  gprtContextDestroy(context);
  for (auto& blas : blasList) gprtAccelDestroy(blas);
  gprtAccelDestroy(world);
  for (auto& vert_buff : vertex_buffers) gprtBufferDestroy(vert_buff);
  for (auto& conn_buff : connectivity_buffers) gprtBufferDestroy(conn_buff);
  for (auto& geom : trianglesGeom) gprtGeomDestroy(geom);

  return 0;
}
