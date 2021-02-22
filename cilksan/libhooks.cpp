#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "cilksan_internal.h"
#include "debug_util.h"
#include "driver.h"

#define START_HOOK(call_id)                                                    \
  if (!TOOL_INITIALIZED || !should_check())                                    \
    return;                                                                    \
  if (__builtin_expect(!call_pc[call_id], false))                              \
    call_pc[call_id] = CALLERPC;                                               \
  do {                                                                         \
  } while (0)

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

CILKSAN_API void __csan_default_libhook(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Pop any MAAPs associated with this hook.
  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  // Alert the user of the function that is not handled.
  const csan_source_loc_t *src_loc = __csan_get_call_source_loc(call_id);
  fprintf(err_io,
          "Cilksan Warning: Call to standard library or intrinsic function not "
          "handled in %s (%s:%d:%d)\n",
          (src_loc->name ? src_loc->name : "<no function name>"),
          (src_loc->filename ? src_loc->filename : "<no file name>"),
          src_loc->line_number, src_loc->column_number);
}

///////////////////////////////////////////////////////////////////////////
// Instrumentation for LLVM intrinsics

template <typename EL_T, int NUM_ELS> struct vec_t {
  using ELEMENT_T = EL_T;
  static constexpr unsigned NUM_ELEMENTS = NUM_ELS;
  EL_T els[NUM_ELS];
};
using v4f64 = vec_t<double, 4>;
using v4i32 = vec_t<int32_t, 4>;
using v4i64 = vec_t<int64_t, 4>;
using v4ptrs = vec_t<uintptr_t, 4>;

using v8i32 = vec_t<int32_t, 8>;
using v8ptrs = vec_t<uintptr_t, 8>;

using v32i8 = vec_t<int8_t, 32>;

template <typename VEC_T, unsigned NUM_ELS, typename MASK_T, MASK_T full_mask,
          bool is_load>
__attribute__((always_inline)) static void
generic_masked_load_store(const csi_id_t call_id, unsigned MAAP_count,
                          const call_prop_t prop, VEC_T *val, VEC_T *ptr,
                          int32_t alignment, MASK_T *mask) {
  using EL_T = typename VEC_T::ELEMENT_T;
  static_assert(NUM_ELS == VEC_T::NUM_ELEMENTS,
                "Mismatch between vector size and num-elements parameter.");
  static_assert(sizeof(VEC_T) == sizeof(EL_T) * NUM_ELS,
                "Vector type has unexpected size.");
  // printf("mask (size %d) = %x\n", NUM_ELS, *mask);

  START_HOOK(call_id);

  MAAP_t ptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    ptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (*mask == full_mask) {
    if (is_load)
      check_read_bytes(call_id, ptr_MAAPVal, ptr, sizeof(VEC_T));
    else
      check_write_bytes(call_id, ptr_MAAPVal, ptr, sizeof(VEC_T));
    return;
  }

  for (unsigned i = 0; i < NUM_ELS; ++i)
    if (*mask & (((MASK_T)(1) << i))) {
      if (is_load)
        check_read_bytes(call_id, ptr_MAAPVal, ((EL_T *)ptr) + i, sizeof(EL_T));
      else
        check_write_bytes(call_id, ptr_MAAPVal, ((EL_T *)ptr) + i,
                          sizeof(EL_T));
    }
}

CILKSAN_API void __csan_llvm_masked_load_v4i32_p0v4i32(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *result, v4i32 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i32, 4, uint8_t, 0x0f, true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_store_v4i64_p0v4i64(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i64 *val, v4i64 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i64, 4, uint8_t, 0x0f, false>(
      call_id, MAAP_count, prop, val, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_load_v32i8_p0v32i8(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v32i8 *result, v32i8 *ptr, int32_t alignment,
    uint32_t *mask) {
  generic_masked_load_store<v32i8, 32, uint32_t, (uint32_t)(-1), true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

template <typename VEC_T, unsigned NUM_ELS, typename MASK_T, bool is_load>
__attribute__((always_inline)) static void
generic_masked_gather_scatter(const csi_id_t call_id, unsigned MAAP_count,
                              const call_prop_t prop, VEC_T *val,
                              vec_t<uintptr_t, NUM_ELS> *addrs,
                              int32_t alignment, MASK_T *mask) {
  using EL_T = typename VEC_T::ELEMENT_T;
  static_assert(NUM_ELS == VEC_T::NUM_ELEMENTS,
                "Mismatch between vector size and num-elements parameter.");
  static_assert(sizeof(VEC_T) == sizeof(EL_T) * NUM_ELS,
                "Vector type has unexpected size.");

  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  for (unsigned i = 0; i < NUM_ELS; ++i)
    if (*mask & (((MASK_T)(1) << i))) {
      if (is_load)
        check_read_bytes(call_id, MAAP_t::ModRef, addrs->els[i], sizeof(EL_T));
      else
        check_write_bytes(call_id, MAAP_t::ModRef, addrs->els[i], sizeof(EL_T));
    }
}

CILKSAN_API void __csan_llvm_masked_gather_v4f64_v4p0f64(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f64 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4f64, 4, uint8_t, true>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4i32_v4p0i32(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4i32, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4i64_v4p0i64(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i64 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4i64, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_gather_v8i32_v8p0i32(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *result, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask, v8i32 *passthru) {
  generic_masked_gather_scatter<v8i32, 8, uint8_t, true>(
      call_id, MAAP_count, prop, result, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v8i32_v8p0i32(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *val, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v8i32, 8, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_stacksave(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, void *sp) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  CilkSanImpl.advance_stack_frame((uintptr_t)sp);
}

CILKSAN_API void __csan_llvm_prefetch_p0i8(const csi_id_t call_id,
                                           const csi_id_t func_id,
                                           unsigned MAAP_count,
                                           const call_prop_t prop, void *addr,
                                           int32_t rw, int32_t locality,
                                           int32_t cache_ty) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_stackrestore(const csi_id_t call_id,
                                          const csi_id_t func_id,
                                          unsigned MAAP_count,
                                          const call_prop_t prop, void *sp) {
  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  CilkSanImpl.restore_stack(call_id, (uintptr_t)sp);
}

CILKSAN_API void __csan_llvm_trap(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_va_start(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, va_list ap) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_va_end(const csi_id_t call_id,
                                    const csi_id_t func_id, unsigned MAAP_count,
                                    const call_prop_t prop, va_list ap) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_va_copy(const csi_id_t call_id,
                                     const csi_id_t func_id,
                                     unsigned MAAP_count,
                                     const call_prop_t prop, va_list dst,
                                     va_list src) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  MAAP_t dst_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  // TODO: Determine how to handle the implementation-dependent va_list type.
}

///////////////////////////////////////////////////////////////////////////
// Instrumentation for standard library functions

#include "hook_format.inc"

__attribute__((always_inline)) static void
vprintf_common(const csi_id_t call_id, unsigned MAAP_count, const char *format,
               va_list ap) {
  unsigned local_MAAP_count = MAAP_count;
  va_list aq;
  va_copy(aq, ap);
  printf_common(call_id, local_MAAP_count, format, aq);
  va_end(aq);

  // Pop any remaining MAAPs
  while (local_MAAP_count > 0) {
    MAAPs.pop();
    --local_MAAP_count;
  }
}

__attribute__((always_inline)) static void
vscanf_common(const csi_id_t call_id, unsigned MAAP_count, int result,
              const char *format, va_list ap) {
  unsigned local_MAAP_count = MAAP_count;
  va_list aq;
  va_copy(aq, ap);
  if (result > 0)
    scanf_common(call_id, local_MAAP_count, result, /*allowGnuMalloc*/ true,
                 format, aq);
  va_end(aq);

  // Pop any remaining MAAPs
  while (local_MAAP_count > 0) {
    MAAPs.pop();
    --local_MAAP_count;
  }
}

CILKSAN_API void
__csan___cxa_atexit(const csi_id_t call_id, const csi_id_t func_id,
                    unsigned MAAP_count, const call_prop_t prop, int result,
                    void (*func)(void *), void *arg, void *dso_handle) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan___isoc99_scanf(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, int result,
                                       const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  va_list ap;
  va_start(ap, format);
  vscanf_common(call_id, MAAP_count, result, format, ap);
  va_end(ap);
}

CILKSAN_API void
__csan___isoc99_sscanf(const csi_id_t call_id, const csi_id_t func_id,
                       unsigned MAAP_count, const call_prop_t prop, int result,
                       const char *s, const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t s_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    s_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  check_read_bytes(call_id, s_MAAPVal, s, strlen(s) + 1);

  va_list ap;
  va_start(ap, format);
  vscanf_common(call_id, MAAP_count, result, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_abs(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            int result, int n) {
  return;
}

CILKSAN_API void __csan_labs(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long result, long n) {
  return;
}

CILKSAN_API void __csan_llabs(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long long result, long long n) {
  return;
}

CILKSAN_API void __csan_acosf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_acos(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_acosl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_acoshf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_acosh(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_acoshl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_asinf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_asin(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_asinl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_asinhf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_asinh(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_asinhl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_atanf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_atan(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_atanl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_atan2f(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_atan2(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_atan2l(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_atanhf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_atanh(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_atanhl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_atof(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, const char *str) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // Use strtol with base 10 to determine number of bytes read.
  char *private_endptr;
  strtod(str, &private_endptr);
  if (str != private_endptr)
    check_read_bytes(call_id, str_MAAPVal, str, private_endptr - str + 1);
}

template <typename RESULT_T>
__attribute__((always_inline)) void
generic_atol(const csi_id_t call_id, unsigned MAAP_count,
             const call_prop_t prop, RESULT_T result, const char *str) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // Use strtol with base 10 to determine number of bytes read.
  char *private_endptr;
  strtol(str, &private_endptr, 10);
  if (str != private_endptr)
    check_read_bytes(call_id, str_MAAPVal, str, private_endptr - str + 1);
}

CILKSAN_API void __csan_atoi(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const char *str) {
  generic_atol(call_id, MAAP_count, prop, result, str);
}

CILKSAN_API void __csan_atol(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long result, const char *str) {
  generic_atol(call_id, MAAP_count, prop, result, str);
}

CILKSAN_API void __csan_atoll(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long long result, const char *str) {
  generic_atol(call_id, MAAP_count, prop, result, str);
}

CILKSAN_API void __csan_bcmp(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const void *s1, const void *s2,
                             size_t n) {
  START_HOOK(call_id);

  MAAP_t s1_MAAPVal = MAAP_t::ModRef, s2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    // Pop MAAP values off in reverse order
    s1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    s2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, s1_MAAPVal, s1, n);
  check_read_bytes(call_id, s2_MAAPVal, s2, n);
}

CILKSAN_API void __csan_cbrtf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_cbrt(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_cbrtl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_clearerr(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_cosf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float arg) {
  return;
}

CILKSAN_API void __csan_cos(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double arg) {
  return;
}

CILKSAN_API void __csan_cosl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_coshf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_cosh(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_coshl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_div(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            div_t result, int x, int y) {
  return;
}

CILKSAN_API void __csan_ldiv(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             ldiv_t result, long x, long y) {
  return;
}

CILKSAN_API void __csan_lldiv(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              lldiv_t result, long long x, long long y) {
  return;
}

CILKSAN_API void __csan_execl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *filename, const char *arg,
                              ...) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_execlp(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename,
                               const char *arg, ...) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_execv(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *filename,
                              char *const argv[]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_execvP(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename,
                               const char *search_path, char *const argv[]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef,
         search_path_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    search_path_MAAPVal = MAAPs.back().second;
    for (unsigned i = 1; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  check_read_bytes(call_id, search_path_MAAPVal, search_path,
                   strlen(search_path) + 1);
}

CILKSAN_API void __csan_execve(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename,
                               char *const argv[], char *const envp[]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_execvp(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename,
                               char *const argv[]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_execvpe(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, const char *filename,
                                char *const argv[], char *const envp[]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_expf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float arg) {
  return;
}

CILKSAN_API void __csan_exp(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double arg) {
  return;
}

CILKSAN_API void __csan_expl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_exp2f(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_exp2(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_exp2l(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_expm1f(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_expm1(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_expm1l(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_fabsf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_fabs(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_fabsl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_fclose(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fdimf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float x, float y) {
  return;
}

CILKSAN_API void __csan_fdim(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_fdiml(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double x,
                              long double y) {
  return;
}

CILKSAN_API void __csan_fdopen(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               FILE *result, int fd, const char *mode) {
  START_HOOK(call_id);

  MAAP_t mode_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    mode_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, mode_MAAPVal, mode, strlen(mode) + 1);
}

CILKSAN_API void __csan_feof(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_ferror(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fflush(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fflush_unlocked(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count,
                                        const call_prop_t prop, int result,
                                        FILE *stream) {
  START_HOOK(call_id);

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, stream_MAAPVal, stream, 1);
}

CILKSAN_API void __csan_fgetc(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fgetc_unlocked(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, int result,
                                       FILE *stream) {
  START_HOOK(call_id);

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, stream_MAAPVal, stream, 1);
}

CILKSAN_API void __csan_fileno(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fmaf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float x, float y, float z) {
  return;
}

CILKSAN_API void __csan_fma(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double x, double y, double z) {
  return;
}

CILKSAN_API void __csan_fmal(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double x, long double y,
                             long double z) {
  return;
}

CILKSAN_API void __csan_fmaxf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float x, float y) {
  return;
}

CILKSAN_API void __csan_fmax(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_fmaxl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double x,
                              long double y) {
  return;
}

CILKSAN_API void __csan_fminf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float x, float y) {
  return;
}

CILKSAN_API void __csan_fmin(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_fminl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double x,
                              long double y) {
  return;
}

CILKSAN_API void __csan_fmodf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float x, float y) {
  return;
}

CILKSAN_API void __csan_fmod(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_fmodl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double x,
                              long double y) {
  return;
}

CILKSAN_API void __csan_fopen(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              FILE *result, const char *filename,
                              const char *mode) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef, mode_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    mode_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  check_read_bytes(call_id, mode_MAAPVal, mode, strlen(mode) + 1);
}

CILKSAN_API void __csan_fork(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             pid_t result) {
  return;
}

CILKSAN_API void __csan_fprintf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, FILE *stream, const char *format,
                                ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_fputc(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, int ch, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fputc_unlocked(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, int result,
                                       int ch, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, stream_MAAPVal, stream, 1);
}

CILKSAN_API void __csan_fputs(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *str, FILE *stream) {
  START_HOOK(call_id);

  // Most operations on streams are locked by default

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, str_MAAPVal, str, strlen(str) + 1);
}

CILKSAN_API void __csan_fread(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              size_t result, const void *buffer, size_t size,
                              size_t count, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == size || 0 == result)
    // Nothing to do if size or result is 0
    return;

  check_write_bytes(call_id, buffer_MAAPVal, buffer, size * result);
}

CILKSAN_API void __csan_fread_unlocked(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, size_t result,
                                       const void *buffer, size_t size,
                                       size_t count, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef, stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == size || 0 == result)
    // Nothing to do if size or result is 0
    return;

  check_write_bytes(call_id, buffer_MAAPVal, buffer, size * result);
  check_write_bytes(call_id, stream_MAAPVal, stream, 1);
}

CILKSAN_API void __csan_freopen(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                FILE *result, const char *filename,
                                const char *mode, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef, mode_MAAPVal = MAAP_t::ModRef,
         stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    mode_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  check_read_bytes(call_id, mode_MAAPVal, mode, strlen(mode) + 1);
}

CILKSAN_API void __csan_fscanf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream, const char *format,
                               ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  va_list ap;
  va_start(ap, format);
  vscanf_common(call_id, MAAP_count, result, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_fseek(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, FILE *stream, long offset,
                              int origin) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fstat(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, int fd, struct stat *buf) {
  START_HOOK(call_id);

  MAAP_t buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, buf_MAAPVal, buf, sizeof(struct stat));
}

CILKSAN_API void __csan_ftell(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long result, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fwrite(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               size_t result, const void *buffer, size_t size,
                               size_t count, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef, stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == size || 0 == result)
    // Nothing to do if size or result is 0
    return;

  check_read_bytes(call_id, buffer_MAAPVal, buffer, size * result);
}

CILKSAN_API void __csan_fwrite_unlocked(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count,
                                        const call_prop_t prop, size_t result,
                                        const void *buffer, size_t size,
                                        size_t count, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef, stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == size || 0 == result)
    // Nothing to do if size or result is 0
    return;

  check_read_bytes(call_id, buffer_MAAPVal, buffer, size * result);
  check_write_bytes(call_id, stream_MAAPVal, stream, 1);
}

CILKSAN_API void __csan_getenv(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, const char *name) {
  START_HOOK(call_id);

  MAAP_t name_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    name_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, name_MAAPVal, name, strlen(name) + 1);
}

CILKSAN_API void __csan_gettimeofday(const csi_id_t call_id,
                                     const csi_id_t func_id,
                                     unsigned MAAP_count,
                                     const call_prop_t prop, int result,
                                     struct timeval *tv, struct timezone *tz) {
  START_HOOK(call_id);

  MAAP_t tv_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    tv_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // Record the memory write to tv
  if (checkMAAP(tv_MAAPVal, MAAP_t::Ref)) {
    if (__builtin_expect(CilkSanImpl.locks_held(), false)) {
      CilkSanImpl.do_locked_write<MAType_t::FNRW>(
          call_id, (uintptr_t)(&tv->tv_sec), sizeof(tv->tv_sec), 0);
      CilkSanImpl.do_locked_write<MAType_t::FNRW>(
          call_id, (uintptr_t)(&tv->tv_usec), sizeof(tv->tv_usec), 0);
    } else {
      CilkSanImpl.do_write<MAType_t::FNRW>(call_id, (uintptr_t)(&tv->tv_sec),
                                           sizeof(tv->tv_sec), 0);
      CilkSanImpl.do_write<MAType_t::FNRW>(call_id, (uintptr_t)(&tv->tv_usec),
                                           sizeof(tv->tv_usec), 0);
    }
  }

  // TODO: Record the memory write when tz != nullptr.  tz is deprecated,
  // however, and in practice it should be null.
  return;
}

CILKSAN_API void __csan_hypotf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float x, float y) {
  return;
}

CILKSAN_API void __csan_hypot(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_hypotl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double x,
                               long double y) {
  return;
}

CILKSAN_API void __csan_isascii(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, int ch) {
  return;
}

CILKSAN_API void __csan_isdigit(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, int ch) {
  return;
}

CILKSAN_API void __csan_ldexpf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg, int exp) {
  return;
}

CILKSAN_API void __csan_ldexp(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg, int exp) {
  return;
}

CILKSAN_API void __csan_ldexpl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg, int exp) {
  return;
}

CILKSAN_API void __csan_logf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float arg) {
  return;
}

CILKSAN_API void __csan_log(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double arg) {
  return;
}

CILKSAN_API void __csan_logl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_log10f(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_log10(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_log10l(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_log2f(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_log2(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_log2l(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_log1pf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_log1p(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_log1pl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_lstat(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *path, struct stat *buf) {
  START_HOOK(call_id);

  MAAP_t path_MAAPVal = MAAP_t::ModRef, buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, path_MAAPVal, path, strlen(path) + 1);
  check_write_bytes(call_id, buf_MAAPVal, buf, sizeof(struct stat));
}

CILKSAN_API void __csan_memalign(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 void *result, size_t boundary, size_t size) {
  return;
}

CILKSAN_API void __csan_memcmp(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const void *lhs, const void *rhs,
                             size_t count) {
  START_HOOK(call_id);

  MAAP_t lhs_MAAPVal = MAAP_t::ModRef, rhs_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    // Pop MAAP values off in reverse order
    lhs_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    rhs_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, lhs_MAAPVal, lhs, count);
  check_read_bytes(call_id, rhs_MAAPVal, rhs, count);
}

CILKSAN_API void __csan_mkdir(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *filename, mode_t mode) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_open(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const char *pathname, int flags, ...) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_powf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float base, float exponent) {
  return;
}

CILKSAN_API void __csan_pow(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double base, double exponent) {
  return;
}

CILKSAN_API void __csan_powl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double base,
                             long double exponent) {
  return;
}

CILKSAN_API void __csan_printf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_putc(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, int ch, FILE *stream) {
  if (!TOOL_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_putchar(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, int ch) {
  return;
}

CILKSAN_API void __csan_puts(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const char *str) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, str_MAAPVal, str, strlen(str) + 1);
}

CILKSAN_API void __csan_qsort(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              void *ptr, size_t count, size_t size,
                              int (*comp)(const void *, const void *)) {
  START_HOOK(call_id);

  MAAP_t ptr_MAAPVal = MAAP_t::ModRef, comp_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    ptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    comp_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // FIXME: Model the memory references performed by qsort and comp more
  // precisely.
  check_read_bytes(call_id, comp_MAAPVal, (const void *)comp, sizeof(comp));
  check_write_bytes(call_id, ptr_MAAPVal, ptr, count * size);
}

CILKSAN_API void __csan_read(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             ssize_t result, int fd, void *buf, size_t count) {
  START_HOOK(call_id);

  MAAP_t buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (result < 0)
    // Do nothing on error
    return;

  check_write_bytes(call_id, buf_MAAPVal, buf, result);
}

CILKSAN_API void __csan_remainderf(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, float result,
                                   float x, float y) {
  return;
}

CILKSAN_API void __csan_remainder(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop, double result,
                                  double x, double y) {
  return;
}

CILKSAN_API void __csan_remainderl(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, long double result,
                                   long double x, long double y) {
  return;
}

CILKSAN_API void __csan_remove(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel() || result < 0)
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
}

CILKSAN_API void __csan_remquof(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                float result, float x, float y, int *quo) {
  START_HOOK(call_id);

  MAAP_t quo_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    quo_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, quo_MAAPVal, quo, sizeof(int));
}

CILKSAN_API void __csan_remquo(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               double result, double x, double y, int *quo) {
  START_HOOK(call_id);

  MAAP_t quo_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    quo_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, quo_MAAPVal, quo, sizeof(int));
}

CILKSAN_API void __csan_remquol(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                long double result, long double x,
                                long double y, int *quo) {
  START_HOOK(call_id);

  MAAP_t quo_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    quo_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, quo_MAAPVal, quo, sizeof(int));
}

CILKSAN_API void __csan_scanf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  va_list ap;
  va_start(ap, format);
  vscanf_common(call_id, MAAP_count, result, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_sinf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float arg) {
  return;
}

CILKSAN_API void __csan_sin(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double arg) {
  return;
}

CILKSAN_API void __csan_sinl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_sinhf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_sinh(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_sinhl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_snprintf(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, char *str, size_t n,
                                 const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);

  check_write_bytes(call_id, str_MAAPVal, str,
                    ((size_t)result + 1 < n - 1) ? result + 1 : n - 1);
}

CILKSAN_API void __csan_sprintf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, char *str, const char *format,
                                ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);

  check_write_bytes(call_id, str_MAAPVal, str, result + 1);
}

CILKSAN_API void __csan_sqrtf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_sqrt(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_sqrtl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_sscanf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *s, const char *format,
                               ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t s_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    s_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  check_read_bytes(call_id, s_MAAPVal, s, strlen(s)+1);

  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_stat(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const char *path, struct stat *buf) {
  START_HOOK(call_id);

  MAAP_t path_MAAPVal = MAAP_t::ModRef, buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, path_MAAPVal, path, strlen(path) + 1);
  check_write_bytes(call_id, buf_MAAPVal, buf, sizeof(struct stat));
}

CILKSAN_API void __csan_strcasecmp(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, int result,
                                   const char *str1, const char *str2) {
  START_HOOK(call_id);

  MAAP_t str1_MAAPVal = MAAP_t::ModRef, str2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    str2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result) {
    // The strings are identical, so both are read in full.
    size_t read_len = strlen(str1);
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len + 1);
  } else {
    // Find the first character in str1 and str2 that differs
    size_t i = 0;
    const char *c1 = str1, *c2 = str2;
    while (*c1 && *c2 && (tolower(*c1++) == tolower(*c2++))) {
      ++i;
    }
    check_read_bytes(call_id, str1_MAAPVal, str1, i + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, i + 1);
  }
}

CILKSAN_API void __csan_strcat(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, char *dest, const char *src) {
  START_HOOK(call_id);

  MAAP_t dest_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dest_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t src_len = strlen(src);
  check_read_bytes(call_id, src_MAAPVal, src, src_len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest + strlen(dest), src_len + 1);
}

CILKSAN_API void __csan_strchr(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, const char *str, int ch) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t len = strlen(str) + 1;
  if (result)
    len = result - str;

  check_read_bytes(call_id, str_MAAPVal, str, len);
}

CILKSAN_API void __csan_strcmp(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *str1, const char *str2) {
  START_HOOK(call_id);

  MAAP_t str1_MAAPVal = MAAP_t::ModRef, str2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    str2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result) {
    // The strings are identical, so both are read in full.
    size_t read_len = strlen(str1);
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len + 1);
  } else {
    // Find the first character in str1 and str2 that differs
    size_t i = 0;
    const char *c1 = str1, *c2 = str2;
    while (*c1 && *c2 && (*c1++ == *c2++)) {
      ++i;
    }
    check_read_bytes(call_id, str1_MAAPVal, str1, i + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, i + 1);
  }
}

CILKSAN_API void __csan_strcpy(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, char *dest, const char *src) {
  START_HOOK(call_id);

  MAAP_t dest_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dest_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t src_len = strlen(src);
  check_read_bytes(call_id, src_MAAPVal, src, src_len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest, src_len + 1);
}

CILKSAN_API void __csan_strlen(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               size_t result, const char *str) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, str_MAAPVal, str, result + 1);
}

CILKSAN_API void __csan_strncasecmp(const csi_id_t call_id,
                                    const csi_id_t func_id, unsigned MAAP_count,
                                    const call_prop_t prop, int result,
                                    const char *str1, const char *str2,
                                    size_t count) {
  START_HOOK(call_id);

  MAAP_t str1_MAAPVal = MAAP_t::ModRef, str2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    str2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result) {
    // The strings are identical, so both are read in full.
    size_t read_len = strlen(str1) + 1;
    if (read_len > count)
      read_len = count;
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len);
  } else {
    // Find the first character in str1 and str2 that differs
    size_t i = 0;
    const char *c1 = str1, *c2 = str2;
    while (i < count && *c1 && *c2 && (tolower(*c1++) == tolower(*c2++))) {
      ++i;
    }
    size_t read_len = (i == count) ? count : i + 1;
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len);
  }
}

CILKSAN_API void __csan_strncat(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                char *result, char *dest, const char *src,
                                size_t count) {
  START_HOOK(call_id);

  MAAP_t dest_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dest_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t len = strlen(src);
  check_read_bytes(call_id, src_MAAPVal, src, len >= count ? count : len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest + strlen(dest),
                    len >= count ? count + 1 : len + 1);
}

CILKSAN_API void __csan_strncmp(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, const char *str1, const char *str2,
                                size_t count) {
  START_HOOK(call_id);

  MAAP_t str1_MAAPVal = MAAP_t::ModRef, str2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    str2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result) {
    // The strings are identical, so both are read in full.
    size_t read_len = strlen(str1) + 1;
    if (read_len > count)
      read_len = count;
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len);
  } else {
    // Find the first character in str1 and str2 that differs
    size_t i = 0;
    const char *c1 = str1, *c2 = str2;
    while (i < count && *c1 && *c2 && (*c1++ == *c2++)) {
      ++i;
    }
    size_t read_len = (i == count) ? count : i + 1;
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len);
  }
}

CILKSAN_API void __csan_strncpy(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                char *result, char *dest, const char *src,
                                size_t count) {
  START_HOOK(call_id);

  MAAP_t dest_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dest_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t src_len = strlen(src) + 1;
  if (src_len > count)
    src_len = count;
  check_read_bytes(call_id, src_MAAPVal, src, src_len);
  check_write_bytes(call_id, dest_MAAPVal, dest, count);
}

CILKSAN_API void __csan_strrchr(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                char *result, const char *str, int ch) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t len = strlen(str) + 1;
  check_read_bytes(call_id, str_MAAPVal, str, len);
}

CILKSAN_API void __csan_strstr(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, const char *str1,
                               const char *str2) {
  START_HOOK(call_id);

  MAAP_t str1_MAAPVal = MAAP_t::ModRef, str2_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str1_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    str2_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result) {
    // The strings are identical, so both are read in full.
    size_t read_len = strlen(str1);
    check_read_bytes(call_id, str1_MAAPVal, str1, read_len + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, read_len + 1);
  } else {
    // Find the first character in str1 and str2 that differs
    size_t i = 0;
    const char *c1 = str1, *c2 = str2;
    while (*c1 && *c2 && (*c1++ == *c2++)) {
      ++i;
    }
    check_read_bytes(call_id, str1_MAAPVal, str1, i + 1);
    check_read_bytes(call_id, str2_MAAPVal, str2, i + 1);
  }
}

template <typename RESULT_T, RESULT_T (*STRTOD_FN)(const char *, char **)>
__attribute__((always_inline)) static void
generic_strtod(const csi_id_t call_id, unsigned MAAP_count,
               const call_prop_t prop, RESULT_T result, const char *nptr,
               char **endptr) {
  START_HOOK(call_id);

  MAAP_t nptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    nptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    // The second MAAP doesn't matter, since we actually care about *endptr, not
    // endptr.
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // TODO: Handle calls to strtol that fail due to improperly formed inputs

  if (nullptr != endptr) {
    if (nptr != *endptr)
      // Record the memory read from nptr
      check_read_bytes(call_id, nptr_MAAPVal, nptr, *endptr - nptr + 1);

    // Record the memory write when endptr
    check_write_bytes(call_id, MAAP_t::ModRef, *endptr, 1);

    return;
  }

  if (!is_execution_parallel())
    return;

  // Execute strtol directly with our own endptr to determine the number of
  // bytes read.
  char *private_endptr;
  STRTOD_FN(nptr, &private_endptr);

  if (nptr != private_endptr)
    // Record the memory read from nptr
    check_read_bytes(call_id, nptr_MAAPVal, nptr, private_endptr - nptr + 1);
}

CILKSAN_API void __csan_strtof(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, const char *nptr, char **endptr) {
  generic_strtod<float, strtof>(call_id, MAAP_count, prop, result, nptr,
                                endptr);
}

CILKSAN_API void __csan_strtod(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               double result, const char *nptr, char **endptr) {
  generic_strtod<double, strtod>(call_id, MAAP_count, prop, result, nptr,
                                 endptr);
}

CILKSAN_API void __csan_strtold(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                long double result, const char *nptr,
                                char **endptr) {
  generic_strtod<long double, strtold>(call_id, MAAP_count, prop, result, nptr,
                                       endptr);
}

template <typename RESULT_T, RESULT_T (*STRTOL_FN)(const char *, char **, int)>
__attribute__((always_inline)) static void
generic_strtol(const csi_id_t call_id, unsigned MAAP_count,
               const call_prop_t prop, RESULT_T result, const char *nptr,
               char **endptr, int base) {
  START_HOOK(call_id);

  MAAP_t nptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    nptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    // The second MAAP doesn't matter, since we actually care about *endptr, not
    // endptr.
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  // Check for an invalid base
  if (base == 0 || (base >= 2 && base <= 36))
    return;

  // TODO: Handle calls to strtol that fail due to improperly formed inputs

  if (nullptr != endptr) {
    if (nptr != *endptr)
      // Record the memory read from nptr
      check_read_bytes(call_id, nptr_MAAPVal, nptr, *endptr - nptr + 1);

    // Record the memory write when endptr
    check_write_bytes(call_id, MAAP_t::ModRef, *endptr, 1);

    return;
  }

  // Execute strtol directly with our own endptr to determine the number of
  // bytes read.
  char *private_endptr;
  STRTOL_FN(nptr, &private_endptr, base);

  if (nptr != private_endptr)
    // Record the memory read from nptr
    check_read_bytes(call_id, nptr_MAAPVal, nptr, private_endptr - nptr + 1);
}

CILKSAN_API void __csan_strtol(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long result, const char *nptr, char **endptr,
                               int base) {
  generic_strtol<long, strtol>(call_id, MAAP_count, prop, result, nptr, endptr,
                               base);
}

CILKSAN_API void __csan_strtoll(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                long long result, const char *nptr,
                                char **endptr, int base) {
  generic_strtol<long long, strtoll>(call_id, MAAP_count, prop, result, nptr,
                                     endptr, base);
}

CILKSAN_API void __csan_strtoul(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                unsigned long result, const char *nptr,
                                char **endptr, int base) {
  generic_strtol<unsigned long, strtoul>(call_id, MAAP_count, prop, result,
                                         nptr, endptr, base);
}

CILKSAN_API void __csan_strtoull(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 unsigned long long result, const char *nptr,
                                 char **endptr, int base) {
  generic_strtol<unsigned long long, strtoull>(call_id, MAAP_count, prop,
                                               result, nptr, endptr, base);
}

CILKSAN_API void __csan_strtok(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, char *str,
                               const char *delim) {
  static char *__csan_strtok_str;
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef, delim_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    delim_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  // Determine the string that will be scanned.
  char *my_str = str;
  if (nullptr == my_str) {
    my_str = __csan_strtok_str;
    if (is_execution_parallel())
      check_read_bytes(call_id, MAAP_t::ModRef, &__csan_strtok_str,
                       sizeof(char *));
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, delim_MAAPVal, delim, strlen(delim)+1);

  // We don't have visibility into the static variable keeping track of the last
  // non-null value of str passed to strtok, so we use our own copy as a proxy
  // for detecting races.

  if (nullptr == result) {
    // No match found to a character in delim.
    check_read_bytes(call_id, str_MAAPVal, my_str, strlen(my_str) + 1);
  } else {
    // Record the reads and writes performed.
    size_t result_len = strlen(result);
    check_read_bytes(call_id, str_MAAPVal, my_str,
                     result - my_str + result_len + 1);
    check_write_bytes(call_id, str_MAAPVal, result + result_len, 1);
    // Save a pointer to the next location in str that strtok will scan if given
    // str == nullptr.
    __csan_strtok_str = result + result_len + 1;
    check_write_bytes(call_id, MAAP_t::ModRef, &__csan_strtok_str,
                      sizeof(char *));
  }
}

CILKSAN_API void __csan_tanf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             float result, float arg) {
  return;
}

CILKSAN_API void __csan_tan(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            double result, double arg) {
  return;
}

CILKSAN_API void __csan_tanl(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_tanhf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_tanh(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_tanhl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_vfscanf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, FILE *stream, const char *format,
                                va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  vscanf_common(call_id, MAAP_count, result, format, ap);
}

CILKSAN_API void __csan_vprintf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, const char *format, va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  vprintf_common(call_id, MAAP_count, format, ap);
}

CILKSAN_API void __csan_vsnprintf(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop, int result, char *buf,
                                  size_t count, const char *format,
                                  va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  vprintf_common(call_id, MAAP_count, format, ap);

  size_t len = result;
  if (len > count)
    len = count;
  if (len > 0)
    check_write_bytes(call_id, buf_MAAPVal, buf, len);
}

CILKSAN_API void __csan_vsprintf(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, char *buf, const char *format,
                                 va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  vprintf_common(call_id, MAAP_count, format, ap);

  check_write_bytes(call_id, buf_MAAPVal, buf, result);
}

CILKSAN_API void __csan_vscanf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *format, va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  vscanf_common(call_id, MAAP_count, result, format, ap);
}

CILKSAN_API void __csan_vsscanf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, const char *s, const char *format,
                                va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t s_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    s_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    --MAAP_count;
  }

  check_read_bytes(call_id, s_MAAPVal, s, strlen(s)+1);

  vscanf_common(call_id, MAAP_count, result, format, ap);
}

CILKSAN_API void __csan_wcslen(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               size_t result, const wchar_t *str) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, str_MAAPVal, str, sizeof(wchar_t) * (result + 1));
}

CILKSAN_API void __csan_write(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              ssize_t result, int fd, const void *buf,
                              size_t count) {
  START_HOOK(call_id);

  MAAP_t buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (result <= 0)
    // Do nothing on error
    return;

  check_read_bytes(call_id, buf_MAAPVal, buf, result);
}
