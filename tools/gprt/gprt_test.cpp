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
const int2 fbSize = {1400, 1000};

// Initial camera parameters
float3 lookFrom = {1.5f, 6.f, -10.f};
float3 lookAt = {1.5f, 1.5f, -1.5f};
float3 lookUp = {0.f, -1.f, 0.f};
float cosFovy = 0.66f;

// Output file name for the rendered image
const char *outFileName = "gprt-test.png";

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
  std::vector<size_t> index_counts;

  for (const auto &surf : mm->surfaces()) {
    // Get surface mesh vertcies and associated connectivities
    auto meshParams = mm->get_surface_mesh(surf);
    auto vertices = meshParams.first;
    auto indices = meshParams.second;

    // Convert vertices to float3 
    std::vector<float3> fl3Vertices;
    fl3Vertices.reserve(vertices.size());    
    for (const auto &vertex : vertices) {
      fl3Vertices.emplace_back(vertex.x, vertex.y, vertex.z);
    }
    vertex_counts.push_back(fl3Vertices.size());

    // Convert connectivities/indices to uint3
    std::vector<uint3> ui3Indices;
    ui3Indices.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
      ui3Indices.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
    }
    index_counts.push_back(ui3Indices.size());

    // Create GPRT buffers and geometry data
    vertex_buffers.push_back(gprtDeviceBufferCreate<float3>(context, fl3Vertices.size(), fl3Vertices.data()));
    connectivity_buffers.push_back(gprtDeviceBufferCreate<uint3>(context, ui3Indices.size(), ui3Indices.data()));
    trianglesGeom.push_back(gprtGeomCreate<TrianglesGeomData>(context, trianglesGeomType));
    TrianglesGeomData* geom_data = gprtGeomGetParameters(trianglesGeom.back());
    // geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffers.back());
    // geom_data->index = gprtBufferGetDevicePointer(connectivity_buffers.back());
    // geom_data->id = surf;
    // geom_data->vols = {mm->get_parent_volumes(surf).first, mm->get_parent_volumes(surf).second};
  }

  // Create geometry instance and set vertex and index buffers for each surface
  for (int i=0; i<mm->num_surfaces(); i++) { 
    auto &surf = mm->surfaces()[i];
    gprtTrianglesSetVertices(trianglesGeom[i], vertex_buffers[i], vertex_counts[i]);
    gprtTrianglesSetIndices(trianglesGeom[i], connectivity_buffers[i], index_counts[i]);
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

  PushConstants pc;
  bool firstFrame = true;
  double xpos = 0.f, ypos = 0.f;
  double lastxpos, lastypos;

  // Retrieve the bounding box of the TLAS
  auto worldBB = mm->world_bounding_box();
  auto tlasMin = float3(worldBB.min_x, worldBB.min_y, worldBB.min_z);
  auto tlasMax = float3(worldBB.max_x, worldBB.max_y, worldBB.max_z);

  // Calculate the center of the bounding box
  float3 center = (tlasMin + tlasMax) * 0.5f;

  // Update camera parameters
  lookAt = center;

  // Calculate a suitable `lookFrom` position based on the bounding box size
  float3 boxSize = tlasMax - tlasMin;
  float distance = length(boxSize) * 1.0f; // Adjust the multiplier for desired zoom level
  lookFrom = center + float3(0.0f, 0.0f, -distance); // Place the camera behind the center

  // Set the up vector
  lookUp = float3(0.0f, 1.0f, 0.0f); // Y-axis up

  // Update push constants
  pc.camera.pos = lookFrom;
  pc.camera.dir_00 = normalize(lookAt - lookFrom);
  float aspect = float(fbSize.x) / float(fbSize.y);
  pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
  pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
  pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
  pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;

  // Main render loop
  do {
    float speed = .001f;
    lastxpos = xpos;
    lastypos = ypos;
    gprtGetCursorPos(context, &xpos, &ypos);
    if (firstFrame) {
      lastxpos = xpos;
      lastypos = ypos;
    }
    int state = gprtGetMouseButton(context, GPRT_MOUSE_BUTTON_LEFT);

    // If we click the mouse, we should rotate the camera
    // Here, we implement some simple camera controls
    if (state == GPRT_PRESS || firstFrame) {
      firstFrame = false;
      float4 position = {lookFrom.x, lookFrom.y, lookFrom.z, 1.f};
      float4 pivot = {lookAt.x, lookAt.y, lookAt.z, 1.0};
#ifndef M_PI
#define M_PI 3.1415926f
#endif

      // step 1 : Calculate the amount of rotation given the mouse movement.
      float deltaAngleX = (2 * M_PI / fbSize.x);
      float deltaAngleY = (M_PI / fbSize.y);
      float xAngle = float(lastxpos - xpos) * deltaAngleX;
      float yAngle = float(lastypos - ypos) * deltaAngleY;

      // step 2: Rotate the camera around the pivot point on the first axis.
      float4x4 rotationMatrixX = math::matrixFromRotation(xAngle, lookUp);
      position = (mul(rotationMatrixX, (position - pivot))) + pivot;

      // step 3: Rotate the camera around the pivot point on the second axis.
      float3 lookRight = cross(lookUp, normalize(pivot - position).xyz());
      float4x4 rotationMatrixY = math::matrixFromRotation(yAngle, lookRight);
      lookFrom = ((mul(rotationMatrixY, (position - pivot))) + pivot).xyz();

      // ----------- compute variable values  ------------------
      pc.camera.pos = lookFrom;
      pc.camera.dir_00 = normalize(lookAt - lookFrom);
      float aspect = float(fbSize.x) / float(fbSize.y);
      pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
      pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
      pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
      pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;
    }
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
