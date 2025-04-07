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


// Vertices defining the triangle
const int NUM_VERTICES = 3;
float3 vertices[NUM_VERTICES] = {
    {-1.f, -.5f, 0.f},
    {+1.f, -.5f, 0.f},
    {0.f, +.5f, 0.f},
};

// Indices defining the connections between vertices
const int NUM_INDICES = 1;
uint3 indices[NUM_INDICES] = {{0, 1, 2}};

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

  // Initialize XDG
  std::shared_ptr<XDG> xdg = XDG::create(MeshLibrary::MOAB);
  const auto& mm = xdg->mesh_manager();
  mm->load_file(args.get<std::string>("filename"));
  mm->init();
  xdg->prepare_raytracer();
  

  // Create a rendering window
  gprtRequestWindow(fbSize.x, fbSize.y, "GPRT Triangle XDG-Test");

  // Initialize GPRT context and modules
  GPRTContext context = gprtContextCreate();
  GPRTModule module = gprtModuleCreate(context, gprt_test_deviceCode);

  // New: Create a "triangle" geometry type and set it's closest-hit program
  auto trianglesGeomType = gprtGeomTypeCreate<TrianglesGeomData>(context, GPRT_TRIANGLES);
  gprtGeomTypeSetClosestHitProg(trianglesGeomType, 0, module, "TriangleMesh");

  // Upload vertex and index data to GPU buffers
  auto vertexBuffer = gprtDeviceBufferCreate<float3>(context, NUM_VERTICES, vertices);
  auto indexBuffer = gprtDeviceBufferCreate<uint3>(context, NUM_INDICES, indices);

  // New: Create geometry instance and set vertex and index buffers
  auto trianglesGeom = gprtGeomCreate<TrianglesGeomData>(context, trianglesGeomType);
  gprtTrianglesSetVertices(trianglesGeom, vertexBuffer, NUM_VERTICES);
  gprtTrianglesSetIndices(trianglesGeom, indexBuffer, NUM_INDICES);

  // Place the geometry into a bottom-level acceleration structure (BLAS).
  // A BLAS organizes triangles into a data structure that allows rays to quickly
  // determine potential intersections, significantly speeding up ray tracing by narrowing down
  // the search to relevant geometry instead of testing every triangle.
  GPRTAccel trianglesAccel = gprtTriangleAccelCreate(context, trianglesGeom);
  gprtAccelBuild(context, trianglesAccel, GPRT_BUILD_MODE_FAST_TRACE_NO_UPDATE);

  // Create a single instance of the BLAS in a top-level acceleration structure (TLAS), required for ray tracing.
  // (We'll cover TLAS in more depth later)
  gprt::Instance instance = gprtAccelGetInstance(trianglesAccel);
  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context, 1, &instance);
  GPRTAccel world = gprtInstanceAccelCreate(context, 1, instanceBuffer);
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

  return 0;
}
