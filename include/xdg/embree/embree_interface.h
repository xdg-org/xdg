
#ifdef XDG_EMBREE4

#include "xdg/embree/embree4.h"

#elif defined(XDG_EMBREE3)

#include "xdg/embree/embree3.h"

#else

#error "No embree version for XDG provided to compiler"

#endif