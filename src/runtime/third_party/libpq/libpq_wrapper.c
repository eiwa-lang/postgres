// libpq_wrapper.c — compiles the libpq_wrapper.h helpers into exported symbols
// so the LLVM backend (which ignores @Header includes) can resolve the FFI
// functions eiwa_pq_* via dlsym / the native link.
#include "libpq_wrapper.h"
