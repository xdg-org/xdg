#ifndef _XDG_CUBQL_BACKEND_H
#define _XDG_CUBQL_BACKEND_H

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <omp.h>

// Guards to prevent CUDA headers from being included in host code, which causes
// failed compilation with LLVM-clang.
#if defined(__CUDA_ARCH__) && !defined(__CUDACC__)
#undef __CUDA_ARCH__
#endif

#include "xdg/error.h"

namespace xdg::cubql {

struct Context {
  int gpuID {0};
  int hostID {omp_get_initial_device()};
};



} // namespace xdg::cubql

#endif // include guard
