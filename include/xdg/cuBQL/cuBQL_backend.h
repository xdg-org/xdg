#ifndef _XDG_CUBQL_BACKEND_H
#define _XDG_CUBQL_BACKEND_H

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <omp.h>

#include "xdg/error.h"

namespace xdg::cubql {

struct Context {
  int gpuID {0};
  int hostID {omp_get_initial_device()};
};



} // namespace xdg::cubql

#endif // include guard
