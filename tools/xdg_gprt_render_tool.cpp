#include <memory>
#include <set>

#include "xdg/error.h"
#include "xdg/overlap.h"

#include "xdg/xdg.h"
#include "gprt/gprt.h"
#include "xdg_gprt_render_tool_shared.h"
#include "argparse/argparse.hpp"

// The framework we'll use to create a user interface
#include "imgui.h"


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
extern GPRTProgram xdg_gprt_render_tool_deviceCode;


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
  argparse::ArgumentParser args("XDG-GPRT rendering Tool", "1.0", argparse::default_arguments::help);

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
  GPRTModule module = gprtModuleCreate(context, xdg_gprt_render_tool_deviceCode);  

  // A kernel for compositing imgui and handling temporal antialiasing
  GPRTComputeOf<CompositeGuiConstants> CompositeGui =
    gprtComputeCreate<CompositeGuiConstants>(context, module, "CompositeGui");

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
    geom_data->vertex = gprtBufferGetDevicePointer(vertex_buffers.back());
    geom_data->index = gprtBufferGetDevicePointer(connectivity_buffers.back());
    geom_data->id = surf;
    geom_data->vols = {mm->get_parent_volumes(surf).first, mm->get_parent_volumes(surf).second};
  }

  // Create geometry instance and set vertex and index buffers for each surface
  for (int i=0; i<mm->num_surfaces(); i++) { 
    auto &surf = mm->surfaces()[i];
    gprtTrianglesSetVertices(trianglesGeom[i], vertex_buffers[i], vertex_counts[i]);
    gprtTrianglesSetIndices(trianglesGeom[i], connectivity_buffers[i], index_counts[i]);
  }

  // Create build objects
  GPRTBuildParams buildParams;
  buildParams.buildMode = GPRT_BUILD_MODE_FAST_BUILD_NO_UPDATE;

  // Create a BLAS for each geometry
  std::vector<GPRTAccel> blasList;
  for (size_t i = 0; i < trianglesGeom.size(); i++) {
      GPRTAccel blas = gprtTriangleAccelCreate(context, trianglesGeom[i], buildParams.buildMode);
      gprtAccelBuild(context, blas, buildParams);
      blasList.push_back(blas);
  }

  // Create a TLAS (Top-Level Acceleration Structure) for all BLAS instances
  std::vector<gprt::Instance> instances;
  for (size_t i = 0; i < blasList.size(); i++) {
      instances.push_back(gprtAccelGetInstance(blasList[i]));
  }

  auto instanceBuffer = gprtDeviceBufferCreate<gprt::Instance>(context, instances.size(), instances.data());
  GPRTAccel world = gprtInstanceAccelCreate(context, instances.size(), instanceBuffer);
  gprtAccelBuild(context, world, buildParams);

  // Set up ray generation and miss programs
  GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context, module, "raygen");
  GPRTMissOf<void> miss = gprtMissCreate<void>(context, module, "miss");

  auto imageBuffer = gprtDeviceBufferCreate<float4>(context, fbSize.x * fbSize.y);

  GPRTTextureParams f32TexParams;
  f32TexParams.type = GPRT_IMAGE_TYPE_2D;
  f32TexParams.format = GPRT_FORMAT_R32G32B32A32_SFLOAT;
  f32TexParams.width = fbSize.x;
  f32TexParams.height = fbSize.y;
  auto imageTexture = gprtDeviceTextureCreate<float4>(context, f32TexParams, nullptr);

  GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context, fbSize.x * fbSize.y);

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
  auto worldBB = mm->global_bounding_box();
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

  // Add persistent flip states
  bool flipX = false;
  bool flipY = false;
  bool flipZ = false;

  // This is new, setup GUI frame buffer. We'll rasterize the GUI to this texture, then composite the GUI on top of the
  // rendered scene.
  GPRTTextureParams srgbTexParams = f32TexParams, d32TexParams = f32TexParams;
  srgbTexParams.format = GPRT_FORMAT_R8G8B8A8_SRGB;
  d32TexParams.format = GPRT_FORMAT_D32_SFLOAT;
  GPRTTextureOf<uint32_t> guiColorAttachment = gprtDeviceTextureCreate<uint32_t>(context, srgbTexParams, nullptr);
  GPRTTextureOf<float> guiDepthAttachment = gprtDeviceTextureCreate<float>(context, d32TexParams, nullptr);
  gprtGuiSetRasterAttachments(context, guiColorAttachment, guiDepthAttachment);

  CompositeGuiConstants guiPC;
  guiPC.fbSize = fbSize;
  guiPC.frameBuffer = gprtBufferGetDevicePointer(frameBuffer);
  guiPC.imageBuffer = gprtBufferGetDevicePointer(imageBuffer);
  guiPC.guiTexture = gprtTextureGet2DHandle<float4>(guiColorAttachment);

  // Main render loop
  do {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::NewFrame();

    float speed = .001f;
    lastxpos = xpos;
    lastypos = ypos;
    gprtGetCursorPos(context, &xpos, &ypos);
    if (firstFrame) {
        lastxpos = xpos;
        lastypos = ypos;
    }

    // Get mouse button states
    int lstate = gprtGetMouseButton(context, GPRT_MOUSE_BUTTON_LEFT);   // Left mouse button
    int rstate = gprtGetMouseButton(context, GPRT_MOUSE_BUTTON_RIGHT);  // Right mouse button
    int mstate = gprtGetMouseButton(context, GPRT_MOUSE_BUTTON_MIDDLE); // Middle mouse button

    // Get key states
    int yKeyState = gprtGetKey(context, GPRT_KEY_Y); // Check Y key state
    int xKeyState = gprtGetKey(context, GPRT_KEY_X); // Check X key state
    int zKeyState = gprtGetKey(context, GPRT_KEY_Z); // Check Z key state

    // Camera rotation (left mouse button)
    if (lstate == GPRT_PRESS || firstFrame) {
        firstFrame = false;
        float4 position = {lookFrom.x, lookFrom.y, lookFrom.z, 1.f};
        float4 pivot = {lookAt.x, lookAt.y, lookAt.z, 1.0};
#ifndef M_PI
#define M_PI 3.1415926f
#endif

        // Step 1: Calculate the amount of rotation given the mouse movement
        float deltaAngleX = (2 * M_PI / fbSize.x);
        float deltaAngleY = (M_PI / fbSize.y);
        float xAngle = float(lastxpos - xpos) * deltaAngleX;
        float yAngle = float(lastypos - ypos) * deltaAngleY;

        // Step 2: Rotate the camera around the pivot point on the first axis
        float4x4 rotationMatrixX = math::matrixFromRotation(xAngle, lookUp);
        position = (mul(rotationMatrixX, (position - pivot))) + pivot;

        // Step 3: Rotate the camera around the pivot point on the second axis
        float3 lookRight = cross(lookUp, normalize(pivot - position).xyz());
        float4x4 rotationMatrixY = math::matrixFromRotation(yAngle, lookRight);
        lookFrom = ((mul(rotationMatrixY, (position - pivot))) + pivot).xyz();

        // Update camera parameters
        pc.camera.pos = lookFrom;
        pc.camera.dir_00 = normalize(lookAt - lookFrom);
        float aspect = float(fbSize.x) / float(fbSize.y);
        pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
        pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;
    }

    // Camera panning (right mouse button)
    if (rstate == GPRT_PRESS) {
        float3 lookRight = cross(lookUp, normalize(lookAt - lookFrom));
        float dx = float(lastxpos - xpos) * speed;
        float dy = float(lastypos - ypos) * speed;

        // Calculate translation vector
        float3 translation = lookRight * dx + lookUp * -dy;

        // Apply translation to camera position and target
        lookFrom = lookFrom + translation;
        lookAt = lookAt + translation;

        // Update camera parameters
        pc.camera.pos = lookFrom;
        pc.camera.dir_00 = normalize(lookAt - lookFrom);
        float aspect = float(fbSize.x) / float(fbSize.y);
        pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
        pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;
    }

    // Camera zooming (middle mouse button)
    if (mstate == GPRT_PRESS) {
        float3 view_vec = lookFrom - lookAt;
        float dy = float(lastypos - ypos);

        // Adjust the view vector length based on mouse movement
        if (dy > 0.0) {
            view_vec *= 0.95f; // Zoom in
        } else if (dy < 0.0) {
            view_vec *= 1.05f; // Zoom out
        }

        // Update camera position
        lookFrom = lookAt + view_vec;

        // Update camera parameters
        pc.camera.pos = lookFrom;
        pc.camera.dir_00 = normalize(lookAt - lookFrom);
        float aspect = float(fbSize.x) / float(fbSize.y);
        pc.camera.dir_du = cosFovy * aspect * normalize(cross(pc.camera.dir_00, lookUp));
        pc.camera.dir_dv = cosFovy * normalize(cross(pc.camera.dir_du, pc.camera.dir_00));
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_du;
        pc.camera.dir_00 -= 0.5f * pc.camera.dir_dv;
    }

    // Toggle flip states when keys are pressed
    if (yKeyState == GPRT_PRESS) {
        flipY = !flipY; // Toggle Y-axis flip
    }
    if (xKeyState == GPRT_PRESS) {
        flipX = !flipX; // Toggle X-axis flip
    }
    if (zKeyState == GPRT_PRESS) {
        flipZ = !flipZ; // Toggle Z-axis flip
    }

    // Apply persistent flips
    if (flipY) {
        pc.camera.dir_dv = -pc.camera.dir_dv; // Flip vertical direction
    }
    if (flipX) {
        pc.camera.dir_du = -pc.camera.dir_du; // Flip horizontal direction
    }
    if (flipZ) {
        pc.camera.dir_00 = -pc.camera.dir_00; // Flip forward direction
    }

    pc.time = float(gprtGetTime(context));
    gprtRayGenLaunch2D(context, rayGen, fbSize.x, fbSize.y, pc);
    
    // Set our ImGui state
    bool show_demo_window = true;
    if (show_demo_window)
      ImGui::ShowDemoWindow(&show_demo_window);
    ImGui::EndFrame();

        // Rasterize our gui
    gprtTextureClear(guiDepthAttachment);
    gprtTextureClear(guiColorAttachment);
    gprtGuiRasterize(context);

    // Finally, composite the gui onto the screen using a compute shader.
    gprtBufferTextureCopy(context, imageBuffer, imageTexture, 0, 0, 0, 0, 0, 0, fbSize.x, fbSize.y, 1);

    gprtComputeLaunch(CompositeGui, {fbSize.x, fbSize.y, 1}, {1, 1, 1}, guiPC);

    gprtBufferPresent(context, frameBuffer);
  } while (!gprtWindowShouldClose(context));

  // Save final frame to an image
  gprtBufferSaveImage(frameBuffer, fbSize.x, fbSize.y, outFileName);

  gprtTextureDestroy(guiColorAttachment);
  gprtTextureDestroy(guiDepthAttachment);

  // Clean up resources
  gprtContextDestroy(context);
  return 0;
}