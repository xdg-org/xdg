#include "xdg/gprt/ray_tracer.h"


namespace xdg {

GPRTRayTracer::GPRTRayTracer()
{
  context_ = gprtContextCreate();
}

GPRTRayTracer::~GPRTRayTracer()
{
  gprtContextDestroy(context_);
}

// void GPRTRayTracer::add_module(GPRTProgram device_code, std::string name)
// {
//   GPRTModule module = gprtModuleCreate(context_, device_code);
//   device_codes_[name] = module;
// }


} // namespace xdg