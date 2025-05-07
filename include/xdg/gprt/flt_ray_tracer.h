#ifndef _XDG_GPRT_FLOAT_RAY_TRACING_INTERFACE_H
#define _XDG_GPRT_FLOAT_RAY_TRACING_INTERFACE_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "xdg/constants.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/primitive_ref.h"
#include "xdg/geometry_data.h"
#include "xdg/ray_tracing_interface.h"
#include "xdg/ray.h"
#include "xdg/error.h"
#include "gprt/gprt.h"


// This file contains template specilizations for the GPRTRayTracer class included within XDG 

extern GPRTProgram flt_deviceCode;

namespace xdg {

  template<>
  class GPRTRayTracer<float> : public RayTracer {
public:

  GPRTRayTracer() {
    context_ = gprtContextCreate();
    module_ = gprtModuleCreate(context_, flt_deviceCode);
  };

  ~GPRTRayTracer() {
    gprtContextDestroy(context_);
  };



  private:
    GPRTContext context_;
    GPRTProgram deviceCode_; // device code for float precision shaders
    GPRTModule module_; // device code module for single precision shaders
  };


} // namespace xdg
 
#endif // include guard