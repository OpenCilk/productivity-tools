// -*- C++ -*-
#ifndef __DRIVER_H__
#define __DRIVER_H__

#include "cilksan_internal.h"
#include "locksets.h"
#include "stack.h"
#include <csi/csi.h>
#include <cstdint>
#include <utility>

#ifndef CILKSAN_VIS
#define CILKSAN_VIS __attribute__((visibility("default")))
#endif

#ifndef CILKSAN_API
#define CILKSAN_API extern "C" CILKSAN_VIS
#endif
// The CILKSAN_WEAK macro is used to mark dynamic interposers.  On Linux, these
// symbols need to be marked weak, in order to avoid multiple-definition errors.
// On Mac, these symbols must not be marked weak, or else the dynamic linker
// will ignore the symbols.
#if __APPLE__
#define CILKSAN_WEAK
#else // __APPLE__
#define CILKSAN_WEAK __attribute__((weak))
#endif // __APPLE__
#define CALLERPC ((uintptr_t)__builtin_return_address(0))

// FILE io used to print error messages
extern FILE *err_io;

#define START_HOOK(call_id)                                                    \
  if (!CILKSAN_INITIALIZED || !should_check())                                 \
    return;                                                                    \
  if (__builtin_expect(!call_pc[call_id], false))                              \
    call_pc[call_id] = CALLERPC;                                               \
  do {                                                                         \
  } while (0)

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
extern bool CILKSAN_INITIALIZED;

// Flag to globally enable/disable instrumentation.
extern bool instrumentation;

///////////////////////////////////////////////////////////////////////////
// Methods for enabling and disabling instrumentation

static inline void enable_instrumentation() {
  DBG_TRACE(BASIC, "Enable instrumentation.\n");
  instrumentation = true;
}

static inline void disable_instrumentation() {
  DBG_TRACE(BASIC, "Disable instrumentation.\n");
  instrumentation = false;
}

// Reentrant flag for enabling/disabling instrumentation; 0 enables checking.
extern int checking_disabled;

__attribute__((always_inline)) static inline bool should_check() {
  return (instrumentation && (checking_disabled == 0));
}

extern Stack_t<uint8_t> parallel_execution;

__attribute__((always_inline)) static inline bool is_execution_parallel() {
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
constexpr LockID_t atomic_lock_id = 0;

// Range of stack used by the process
// Defined in cilksan.cpp
extern uintptr_t stack_low_addr;
extern uintptr_t stack_high_addr;

// Helper function to check if an address is in the stack.
static inline bool is_on_stack(uintptr_t addr) {
  return (addr <= stack_high_addr && addr >= stack_low_addr);
}

// Helper function for checking a function that reads len bytes starting at ptr.
static inline void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                    const void *ptr, size_t len) {
  if (checkMAAP(MAAPVal, MAAP_t::Mod)) {
    if (__builtin_expect(CilkSanImpl.locks_held(), false)) {
      CilkSanImpl.do_locked_read<MAType_t::FNRW>(call_id, (uintptr_t)ptr, len,
                                                 0);
    } else {
      CilkSanImpl.do_read<MAType_t::FNRW>(call_id, (uintptr_t)ptr, len, 0);
    }
  }
}

static inline void check_read_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                    uintptr_t ptr, size_t len) {
  if (checkMAAP(MAAPVal, MAAP_t::Mod)) {
    if (__builtin_expect(CilkSanImpl.locks_held(), false)) {
      CilkSanImpl.do_locked_read<MAType_t::FNRW>(call_id, ptr, len, 0);
    } else {
      CilkSanImpl.do_read<MAType_t::FNRW>(call_id, ptr, len, 0);
    }
  }
}

// Helper function for checking a function that writes len bytes starting at
// ptr.
static inline void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                     const void *ptr, size_t len) {
  if (checkMAAP(MAAPVal, MAAP_t::Ref)) {
    if (__builtin_expect(CilkSanImpl.locks_held(), false)) {
      CilkSanImpl.do_locked_write<MAType_t::FNRW>(call_id, (uintptr_t)ptr, len,
                                                  0);
    } else {
      CilkSanImpl.do_write<MAType_t::FNRW>(call_id, (uintptr_t)ptr, len, 0);
    }
  }
}

static inline void check_write_bytes(csi_id_t call_id, MAAP_t MAAPVal,
                                     uintptr_t ptr, size_t len) {
  if (checkMAAP(MAAPVal, MAAP_t::Ref)) {
    if (__builtin_expect(CilkSanImpl.locks_held(), false)) {
      CilkSanImpl.do_locked_write<MAType_t::FNRW>(call_id, ptr, len, 0);
    } else {
      CilkSanImpl.do_write<MAType_t::FNRW>(call_id, ptr, len, 0);
    }
  }
}

CILKSAN_API bool __cilksan_should_check(void);
CILKSAN_API void __cilksan_record_alloc(void *addr, size_t size);
CILKSAN_API void __cilksan_record_free(void *ptr);

CILKSAN_API void __cilksan_begin_atomic();
CILKSAN_API void __cilksan_end_atomic();

#endif // __DRIVER_H__
