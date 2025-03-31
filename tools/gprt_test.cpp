#include <memory>
#include <set>

#include "xdg/error.h"
#include "xdg/overlap.h"

#include "xdg/xdg.h"
#include "gprt/gprt.h"
#include "sharedCode.h"



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

// window size
const int2 fbSize = {1000, 1000};
const char *outFileName = "gprt-test.png";


int main(int argc, char* argv[]) {
  // A test program to check integration of GPRT into XDG

  // Parse command line arguments

  // Start up GPRT
  gprtRequestWindow(fbSize.x, fbSize.y, "GPRT-XDG Test Program");
  GPRTContext context = gprtContextCreate();

  // Compile device code into a module for GPU execution
  GPRTModule module = gprtModuleCreate(context, gprt_test_deviceCode);

  // Create a ray generation shader using the module
  GPRTRayGenOf<RayGenData> rayGen = gprtRayGenCreate<RayGenData>(context, module, "simpleRayGen");

  // Create a framebuffer to store pixel colors
  GPRTBufferOf<uint32_t> frameBuffer = gprtDeviceBufferCreate<uint32_t>(context, fbSize.x * fbSize.y);

  // Set up shader parameters
  RayGenData *data = gprtRayGenGetParameters(rayGen);
  data->color0 = float3(0.1f, 0.1f, 0.1f); // Background color
  data->color1 = float3(0.0f, 0.0f, 0.0f); // Secondary color
  data->frameBuffer = gprtBufferGetDevicePointer(frameBuffer);

  // Build the Shader Binding Table (SBT)
  gprtBuildShaderBindingTable(context);

  // Render loop: repeatedly launch the ray generation shader
  do {
    gprtRayGenLaunch2D(context, rayGen, fbSize.x, fbSize.y);
    gprtBufferPresent(context, frameBuffer); // Display to window if available
  }
  // returns true if "X" pressed or if in "headless" mode
  while (!gprtWindowShouldClose(context));

  // Save the final image to a file
  gprtBufferSaveImage(frameBuffer, fbSize.x, fbSize.y, outFileName);

  // Clean up resources
  gprtContextDestroy(context);

  return 0;
}
