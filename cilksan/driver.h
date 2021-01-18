// -*- C++ -*-
#ifndef __DRIVER_H__
#define __DRIVER_H__

#include <csi/csi.h>
#include <cstdint>
#include <utility>

#include "addrmap.h"
#include "cilksan_internal.h"
#include "locksets.h"
#include "stack.h"

#define CILKSAN_API extern "C" __attribute__((visibility("default")))
#define CALLERPC ((uintptr_t)__builtin_return_address(0))

// FILE io used to print error messages
extern FILE *err_io;

#define START_DL_INTERPOSER(func, type)                                        \
  if (__builtin_expect(dl_##func == NULL, false)) {                            \
    dl_##func = (type)dlsym(RTLD_NEXT, #func);                                 \
    if (__builtin_expect(dl_##func == NULL, false)) {                          \
      char *error = dlerror();                                                 \
      if (error != NULL) {                                                     \
        fputs(error, err_io);                                                  \
        fflush(err_io);                                                        \
      }                                                                        \
      abort();                                                                 \
    }                                                                          \
  }

extern CilkSanImpl_t CilkSanImpl;

// Defined in print_addr.cpp
extern uintptr_t *call_pc;
extern uintptr_t *spawn_pc;
extern uintptr_t *loop_pc;
extern uintptr_t *load_pc;
extern uintptr_t *store_pc;
extern uintptr_t *alloca_pc;
extern uintptr_t *allocfn_pc;
extern allocfn_prop_t *allocfn_prop;
extern uintptr_t *free_pc;

// Flag to track whether Cilksan is initialized.
extern bool TOOL_INITIALIZED;

// Flag to globally enable/disable instrumentation.
extern bool instrumentation;

// Reentrant flag for enabling/disabling instrumentation; 0 enables checking.
extern int checking_disabled;

__attribute__((always_inline))
static inline bool should_check() {
  return (instrumentation && (checking_disabled == 0));
}

extern Stack_t<uint8_t> parallel_execution;

__attribute__((always_inline))
static inline bool is_execution_parallel() {
  return parallel_execution.back();
}

// Stack structures for keeping track of MAAP (May Access Alias in Parallel)
// information inserted by the compiler before a call.
enum class MAAP_t : uint8_t {
  NoAccess = 0,
  Mod = 1,
  Ref = 2,
  ModRef = Mod | Ref,
  NoAlias = 4,
};
extern Stack_t<std::pair<csi_id_t, MAAP_t>> MAAPs;
static inline bool checkMAAP(MAAP_t val, MAAP_t flag) {
  return static_cast<uint8_t>(val) & static_cast<uint8_t>(flag);
}

// Designated lock ID for atomic operations
extern const LockID_t atomic_lock_id;

#endif // __DRIVER_H__
