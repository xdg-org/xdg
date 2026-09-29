#ifndef XDG_PRIMITIVE_REF_H
#define XDG_PRIMITIVE_REF_H

#include "xdg/constants.h"

namespace xdg {

struct PrimitiveRef {
  MeshID primitive_id {ID_NONE};
};

} // namespace xdg

#endif // include guard