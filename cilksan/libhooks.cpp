#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <utime.h>

#include "cilksan_internal.h"
#include "debug_util.h"
#include "driver.h"

CILKSAN_API void __csan_default_libhook(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count) {
  if (!CILKSAN_INITIALIZED)
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
  const csan_source_loc_t *func_loc = __csan_get_func_source_loc(func_id);
  fprintf(err_io,
          "Cilksan Warning: Call to function '%s' not "
          "handled in %s (%s:%d:%d)\n",
          (func_loc->name ? func_loc->name : "<no function name>"),
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

using v2f32 = vec_t<float, 2>;
using v2f64 = vec_t<double, 2>;
using v2i32 = vec_t<int32_t, 2>;

using v4f32 = vec_t<float, 4>;
using v4f64 = vec_t<double, 4>;
using v4i32 = vec_t<int32_t, 4>;
using v4i64 = vec_t<int64_t, 4>;
using v4ptrs = vec_t<uintptr_t, 4>;

using v8f32 = vec_t<float, 8>;
using v8f64 = vec_t<double, 8>;
using v8i8 = vec_t<int8_t, 8>;
using v8i16 = vec_t<int16_t, 8>;
using v8i32 = vec_t<int32_t, 8>;
using v8ptrs = vec_t<uintptr_t, 8>;

using v16i8 = vec_t<int8_t, 16>;
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

CILKSAN_API void __csan_llvm_masked_load_v4i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *result, v4i32 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i32, 4, uint8_t, 0x0f, true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_store_v4i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *val, v4i32 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i32, 4, uint8_t, 0x0f, false>(
      call_id, MAAP_count, prop, val, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_load_v4i64_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i64 *result, v4i64 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i64, 4, uint8_t, 0x0f, true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_store_v4i64_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i64 *val, v4i64 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v4i64, 4, uint8_t, 0x0f, false>(
      call_id, MAAP_count, prop, val, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_load_v8i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *result, v8i32 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v8i32, 8, uint8_t, 0xff, true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_store_v8i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *val, v8i32 *ptr, int32_t alignment,
    uint8_t *mask) {
  generic_masked_load_store<v8i32, 8, uint8_t, 0xff, false>(
      call_id, MAAP_count, prop, val, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_load_v16i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v16i8 *result, v16i8 *ptr, int32_t alignment,
    uint16_t *mask) {
  generic_masked_load_store<v16i8, 16, uint16_t, (uint16_t)(-1), true>(
      call_id, MAAP_count, prop, result, ptr, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_load_v32i8_p0(
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

CILKSAN_API void __csan_llvm_masked_gather_v4f64_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f64 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4f64, 4, uint8_t, true>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4f64_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f64 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4f64, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4i32_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4i32, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4i64_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i64 *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4i64, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_gather_v4p0_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4ptrs *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4ptrs, 4, uint8_t, true>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v4p0_v4p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4ptrs *val, v4ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v4ptrs, 4, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_gather_v8f64_v8p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8f64 *val, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v8f64, 8, uint8_t, true>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v8f64_v8p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8f64 *val, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v8f64, 8, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_gather_v8i32_v8p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *result, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask, v8i32 *passthru) {
  generic_masked_gather_scatter<v8i32, 8, uint8_t, true>(
      call_id, MAAP_count, prop, result, addrs, alignment, mask);
}

CILKSAN_API void __csan_llvm_masked_scatter_v8i32_v8p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *val, v8ptrs *addrs, int32_t alignment,
    uint8_t *mask) {
  generic_masked_gather_scatter<v8i32, 8, uint8_t, false>(
      call_id, MAAP_count, prop, val, addrs, alignment, mask);
}

template <typename VEC_T, unsigned NUM_ELS, typename IDX_T, bool is_load>
__attribute__((always_inline)) static void
generic_x86_gather_scatter(const csi_id_t call_id, unsigned MAAP_count,
                           const call_prop_t prop, VEC_T *val, VEC_T *vbase,
                           void *base, IDX_T *index, VEC_T *mask,
                           int8_t scale) {
  using EL_T = typename VEC_T::ELEMENT_T;
  static_assert(NUM_ELS == VEC_T::NUM_ELEMENTS,
                "Mismatch between vector size and num-elements parameter.");
  static_assert(sizeof(VEC_T) == sizeof(EL_T) * NUM_ELS,
                "Vector type has unexpected size.");
  static_assert(
      NUM_ELS <= IDX_T::NUM_ELEMENTS,
      "Mismatch between index-vector size and num-elements parameter.");

  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  // Compute the addresses accessed.
  vec_t<uintptr_t, NUM_ELS> addrs;
  for (unsigned i = 0; i < NUM_ELS; ++i)
    addrs.els[i] = (uintptr_t)base + vbase->els[i] + (index->els[i] * scale);

  for (unsigned i = 0; i < NUM_ELS; ++i)
    // Conditionality is specified by the most significant bit of each data
    // element of the mask register.
    if (static_cast<uint64_t>(mask->els[i]) &
        ((uint64_t)(1) << (sizeof(EL_T) * 8 - 1))) {
      if (is_load)
        check_read_bytes(call_id, MAAP_t::ModRef, addrs.els[i], sizeof(EL_T));
      else
        check_write_bytes(call_id, MAAP_t::ModRef, addrs.els[i], sizeof(EL_T));
    }
}

CILKSAN_API void
__csan_llvm_x86_avx2_gather_d_d(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                v4i32 *result, v4i32 *vbase, void *base,
                                v4i32 *index, v4i32 *mask, int8_t scale) {
  generic_x86_gather_scatter<v4i32, 4, v4i32, true>(
      call_id, MAAP_count, prop, result, vbase, base, index, mask, scale);
}

CILKSAN_API void __csan_llvm_x86_avx2_gather_d_d_256(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i32 *result, v8i32 *vbase, void *base,
    v8i32 *index, v8i32 *mask, int8_t scale) {
  generic_x86_gather_scatter<v8i32, 8, v8i32, true>(
      call_id, MAAP_count, prop, result, vbase, base, index, mask, scale);
}

CILKSAN_API void
__csan_llvm_x86_avx2_gather_d_pd(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 v2f64 *result, v2f64 *vbase, void *base,
                                 v4i32 *index, v2f64 *mask, int8_t scale) {
  generic_x86_gather_scatter<v2f64, 2, v4i32, true>(
      call_id, MAAP_count, prop, result, vbase, base, index, mask, scale);
}

CILKSAN_API void __csan_llvm_x86_avx2_gather_d_pd_256(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f64 *result, v4f64 *vbase, void *base,
    v4i32 *index, v4f64 *mask, int8_t scale) {
  generic_x86_gather_scatter<v4f64, 4, v4i32, true>(
      call_id, MAAP_count, prop, result, vbase, base, index, mask, scale);
}

CILKSAN_API void __csan_llvm_x86_sse2_pause(const csi_id_t call_id,
                                            const csi_id_t func_id,
                                            unsigned MAAP_count,
                                            const call_prop_t prop) {
  // Nothing to do to check a pause instruction.
  return;
}

CILKSAN_API void __csan_llvm_aarch64_clrex(const csi_id_t call_id,
					   const csi_id_t func_id,
					   unsigned MAAP_count,
					   const call_prop_t prop) {
  // Nothing to do
  return;
}

CILKSAN_API void __csan_llvm_aarch64_hint(const csi_id_t call_id,
                                          const csi_id_t func_id,
                                          unsigned MAAP_count,
                                          const call_prop_t prop, int32_t arg) {
  switch (arg) {
  case 1: { // yield instruction
    // Nothing to do to check a yield instruction.
    return;
  }
  default: {
    // Unknown hint.  Call the default libhook.
    __csan_default_libhook(call_id, func_id, MAAP_count);
    return;
  }
  }
}

// Hooks for Arm64 ldxr and stxr intrinsics.

template <typename Ty>
__attribute__((always_inline)) static void
generic_aarch64_ldxr(const csi_id_t call_id, unsigned MAAP_count,
                     const call_prop_t prop, int64_t res, Ty *ptr) {
  START_HOOK(call_id);

  MAAP_t ptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    ptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  __cilksan_begin_atomic();
  check_read_bytes(call_id, ptr_MAAPVal, ptr, sizeof(Ty));
  __cilksan_end_atomic();
}

CILKSAN_API void __csan_llvm_aarch64_ldxr_p0(const csi_id_t call_id,
                                             const csi_id_t func_id,
                                             unsigned MAAP_count,
                                             const call_prop_t prop,
                                             int64_t result, int8_t *addr) {
  generic_aarch64_ldxr<int8_t>(call_id, MAAP_count, prop, result, addr);
}

template <typename Ty>
__attribute__((always_inline)) static void
generic_aarch64_stxr(const csi_id_t call_id, unsigned MAAP_count,
                     const call_prop_t prop, int32_t res, int64_t val, Ty *ptr) {
  START_HOOK(call_id);

  MAAP_t ptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    ptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  __cilksan_begin_atomic();
  check_write_bytes(call_id, ptr_MAAPVal, ptr, sizeof(Ty));
  __cilksan_end_atomic();
}

CILKSAN_API void
__csan_llvm_aarch64_stxr_p0(const csi_id_t call_id, const csi_id_t func_id,
                            unsigned MAAP_count, const call_prop_t prop,
                            int32_t result, int64_t val, int8_t *addr) {
  generic_aarch64_stxr<int8_t>(call_id, MAAP_count, prop, result, val, addr);
}


// Hooks for Arm64 Neon vector load and store intrinsics.  For details
// on these operations, see
// https://developer.arm.com/documentation/102159/0400/Load-and-store---data-structures.
//
// TODO: Add support for ld*r, ld*lane, st*r, and st*lane intrinsics,
// which access less memory and either replicate the result of
// populate only an individual vector lane.

template <typename VEC_T, unsigned NUM>
__attribute__((always_inline)) static void
generic_aarch64_neon_ld(const csi_id_t call_id, unsigned MAAP_count,
                        const call_prop_t prop, void *val, void *ptr) {
  using EL_T = typename VEC_T::ELEMENT_T;

  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, MAAP_t::ModRef, ptr,
                   sizeof(EL_T) * VEC_T::NUM_ELEMENTS * NUM);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x2_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, float *ptr) {
  generic_aarch64_neon_ld<v4f32, 2>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x2_v8i16_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, int8_t *ptr) {
  generic_aarch64_neon_ld<v8i16, 2>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x2_v16i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, int8_t *ptr) {
  generic_aarch64_neon_ld<v16i8, 2>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x3_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, float *ptr) {
  generic_aarch64_neon_ld<v4f32, 3>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x4_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, float *ptr) {
  generic_aarch64_neon_ld<v4f32, 4>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld1x4_v16i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *val, int8_t *ptr) {
  generic_aarch64_neon_ld<v16i8, 4>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld2_v2f32_p0(const csi_id_t call_id,
                                                       const csi_id_t func_id,
                                                       unsigned MAAP_count,
                                                       const call_prop_t prop,
                                                       void *val, v2f32 *ptr) {
  generic_aarch64_neon_ld<v2f32, 2>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld2_v4f32_p0(const csi_id_t call_id,
                                                       const csi_id_t func_id,
                                                       unsigned MAAP_count,
                                                       const call_prop_t prop,
                                                       void *val, v4f32 *ptr) {
  generic_aarch64_neon_ld<v4f32, 2>(call_id, MAAP_count, prop, val, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_ld4_v4f32_p0(const csi_id_t call_id,
                                                       const csi_id_t func_id,
                                                       unsigned MAAP_count,
                                                       const call_prop_t prop,
                                                       void *val, v4f32 *ptr) {
  generic_aarch64_neon_ld<v4f32, 4>(call_id, MAAP_count, prop, val, ptr);
}

template <typename VEC_T, unsigned NUM>
__attribute__((always_inline)) static void
generic_aarch64_neon_st(const csi_id_t call_id, unsigned MAAP_count,
                        const call_prop_t prop, void *ptr) {
  using EL_T = typename VEC_T::ELEMENT_T;

  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, MAAP_t::ModRef, ptr,
                    sizeof(EL_T) * VEC_T::NUM_ELEMENTS * NUM);
}

// TODO: Check the difference in memory accesses for st1x* and st*
// intrinsics.

CILKSAN_API void __csan_llvm_aarch64_neon_st1x2_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f32 *arg1, v4f32 *arg2, float *ptr) {
  generic_aarch64_neon_st<v4f32, 2>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st1x3_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f32 *arg1, v4f32 *arg2, v4f32 *arg3, float *ptr) {
  generic_aarch64_neon_st<v4f32, 3>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st1x4_v4f32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4f32 *arg1, v4f32 *arg2, v4f32 *arg3, v4f32 *arg4,
    float *ptr) {
  generic_aarch64_neon_st<v4f32, 4>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st2_v2i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v2i32 *arg1, v2i32 *arg2, int8_t *ptr) {
  generic_aarch64_neon_st<v2i32, 2>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st2_v4i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *arg1, v4i32 *arg2, int8_t *ptr) {
  generic_aarch64_neon_st<v4i32, 2>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st2_v8i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i8 *arg1, v8i8 *arg2, int8_t *ptr) {
  generic_aarch64_neon_st<v8i8, 2>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st3_v2i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v2i32 *arg1, v2i32 *arg2, v2i32 *arg3,
    int8_t *ptr) {
  generic_aarch64_neon_st<v2i32, 3>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st3_v4i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *arg1, v4i32 *arg2, v4i32 *arg3,
    int8_t *ptr) {
  generic_aarch64_neon_st<v4i32, 3>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st3_v8i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i8 *arg1, v8i8 *arg2, v8i8 *arg3, int8_t *ptr) {
  generic_aarch64_neon_st<v8i8, 3>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st4_v2i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v2i32 *arg1, v2i32 *arg2, v2i32 *arg3, v2i32 *arg4,
    int8_t *ptr) {
  generic_aarch64_neon_st<v2i32, 4>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st4_v4i32_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v4i32 *arg1, v4i32 *arg2, v4i32 *arg3, v4i32 *arg4,
    int8_t *ptr) {
  generic_aarch64_neon_st<v4i32, 4>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_aarch64_neon_st4_v8i8_p0(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, v8i8 *arg1, v8i8 *arg2, v8i8 *arg3, v8i8 *arg4,
    int8_t *ptr) {
  generic_aarch64_neon_st<v8i8, 4>(call_id, MAAP_count, prop, ptr);
}

CILKSAN_API void __csan_llvm_clear_cache(const csi_id_t call_id,
                                         const csi_id_t func_id,
                                         unsigned MAAP_count,
                                         const call_prop_t prop) {
  // Nothing to do
  return;
}

CILKSAN_API void __csan_llvm_stacksave(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, void *sp) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;

  CilkSanImpl.advance_stack_frame((uintptr_t)sp);
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

CILKSAN_API void __csan_llvm_get_dynamic_area_offset_i64(const csi_id_t call_id,
                                                         const csi_id_t func_id,
                                                         unsigned MAAP_count,
                                                         const call_prop_t prop,
                                                         int64_t result) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;
}

CILKSAN_API void __csan_llvm_get_dynamic_area_offset_i32(const csi_id_t call_id,
                                                         const csi_id_t func_id,
                                                         unsigned MAAP_count,
                                                         const call_prop_t prop,
                                                         int32_t result) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return;
}

CILKSAN_API void
__csan_llvm_prefetch_p0(const csi_id_t call_id, const csi_id_t func_id,
                        unsigned MAAP_count, const call_prop_t prop, void *addr,
                        int32_t rw, int32_t locality, int32_t cache_ty) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_threadlocal_address_p0(const csi_id_t call_id,
                                                    const csi_id_t func_id,
                                                    unsigned MAAP_count,
                                                    const call_prop_t prop,
                                                    void *result, void *addr) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_trap(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop) {
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_llvm_va_end(const csi_id_t call_id,
                                    const csi_id_t func_id, unsigned MAAP_count,
                                    const call_prop_t prop, va_list ap) {
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
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
  (void)dst_MAAPVal;
  (void)src_MAAPVal;
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
  if (!CILKSAN_INITIALIZED)
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

CILKSAN_API void __csan_access(const csi_id_t call_id, const csi_id_t func_id,
			       unsigned MAAP_count, const call_prop_t prop,
			       int result, const char *path, int amode) {
  START_HOOK(call_id);

  MAAP_t path_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, path_MAAPVal, path, strlen(path) + 1);
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

CILKSAN_API void __csan_aligned_alloc(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, void *result,
                                      size_t alignment, size_t size) {
  START_HOOK(call_id);

  if (MAAP_count > 0) {
    MAAPs.pop();
  }

  __cilksan_record_alloc(result, size);
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

CILKSAN_API void __csan_calloc(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               void *result, size_t num, size_t size) {
  START_HOOK(call_id);

  if (MAAP_count > 0) {
    MAAPs.pop();
  }

  __cilksan_record_alloc(result, num * size);
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

CILKSAN_API void __csan_ceil(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg) {
  return;
}

CILKSAN_API void __csan_ceilf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg) {
  return;
}

CILKSAN_API void __csan_ceill(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_clearerr(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_copysign(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 double result, double x, double y) {
  return;
}

CILKSAN_API void __csan_copysignf(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop, float result, float x,
                                  float y) {
  return;
}

CILKSAN_API void __csan_copysignl(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop, long double result,
                                  long double x, long double y) {
  return;
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
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
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
  if (!CILKSAN_INITIALIZED)
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

CILKSAN_API void __csan_fgetpos(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, FILE *stream, fpos_t *pos) {
  START_HOOK(call_id);

  MAAP_t stream_MAAPVal = MAAP_t::ModRef, pos_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    pos_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (result < 0)
    return;

  (void)stream_MAAPVal;
  check_write_bytes(call_id, pos_MAAPVal, pos, sizeof(fpos_t));
}

CILKSAN_API void __csan_fgets(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              char *result, char *str, int num, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef, stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  (void)stream_MAAPVal;
  size_t len = strlen(str);
  check_write_bytes(call_id, str_MAAPVal, str, len + 1);
}

CILKSAN_API void __csan_fgets_unlocked(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, char *result,
                                       char *str, int num, FILE *stream) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef, stream_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  size_t len = strlen(str);
  check_read_bytes(call_id, stream_MAAPVal, stream, 1);
  check_write_bytes(call_id, str_MAAPVal, str, len + 1);
}

CILKSAN_API void __csan_fileno(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_floor(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_floorf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_floorl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
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

CILKSAN_API void __csan_fopen64(const csi_id_t call_id, const csi_id_t func_id,
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

  (void)stream_MAAPVal;
  va_list ap;
  va_start(ap, format);
  vprintf_common(call_id, MAAP_count, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_fputc(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, int ch, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
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

CILKSAN_API void __csan_free(const csi_id_t call_id, const csi_id_t func_id,
			     unsigned MAAP_count, const call_prop_t prop,
			     void *ptr) {
  START_HOOK(call_id);

  if (MAAP_count > 0) {
    MAAPs.pop();
  }

  __cilksan_record_free(ptr);
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

  (void)stream_MAAPVal;
  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  check_read_bytes(call_id, mode_MAAPVal, mode, strlen(mode) + 1);
}

CILKSAN_API void __csan_frexp(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg, int *exp) {
  START_HOOK(call_id);

  MAAP_t exp_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    exp_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, exp_MAAPVal, exp, sizeof(int));
}

CILKSAN_API void __csan_frexpf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg, int *exp) {
  START_HOOK(call_id);

  MAAP_t exp_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    exp_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, exp_MAAPVal, exp, sizeof(int));
}

CILKSAN_API void __csan_frexpl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg, int *exp) {
  START_HOOK(call_id);

  MAAP_t exp_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    exp_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, exp_MAAPVal, exp, sizeof(int));
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

  (void)stream_MAAPVal;
  va_list ap;
  va_start(ap, format);
  vscanf_common(call_id, MAAP_count, result, format, ap);
  va_end(ap);
}

CILKSAN_API void __csan_fseek(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, FILE *stream, long offset,
                              int origin) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_fseeko(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream, off_t offset,
                               int origin) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

#if defined(_LARGEFILE64_SOURCE)
CILKSAN_API void __csan_fseeko64(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, FILE *stream, off64_t offset,
                                 int origin) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}
#endif // defined(_LARGEFILE64_SOURCE)

CILKSAN_API void __csan_fsetpos(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, FILE *stream, const fpos_t *pos) {
  START_HOOK(call_id);

  MAAP_t stream_MAAPVal = MAAP_t::ModRef, pos_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    pos_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (result < 0)
    return;

  (void)stream_MAAPVal;
  check_read_bytes(call_id, pos_MAAPVal, pos, sizeof(fpos_t));
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
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_ftello(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               off_t result, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

#if defined(_LARGEFILE64_SOURCE)
CILKSAN_API void __csan_ftello64(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 off64_t result, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}
#endif // defined(_LARGEFILE64_SOURCE)

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

  (void)stream_MAAPVal;
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

CILKSAN_API void __csan_getc(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_getc_unlocked(const csi_id_t call_id,
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

CILKSAN_API void __csan_getchar(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result) {
  return;
}

CILKSAN_API void __csan_getchar_unlocked(const csi_id_t call_id,
                                         const csi_id_t func_id,
                                         unsigned MAAP_count,
                                         const call_prop_t prop, int result) {
  START_HOOK(call_id);

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, MAAP_t::ModRef, stdin, 1);
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

  // TODO: Model access to environment list.
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

#if defined(__linux__)
CILKSAN_API void __csan__IO_getc(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, _IO_FILE *__fp) {
  START_HOOK(call_id);

  MAAP_t fp_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    fp_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, fp_MAAPVal, __fp, 1);
}
#endif

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

CILKSAN_API void __csan_malloc(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               void *result, size_t size) {
  START_HOOK(call_id);

  if (MAAP_count > 0) {
    MAAPs.pop();
  }

  __cilksan_record_alloc(result, size);
}

CILKSAN_API void __csan_memalign(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 void *result, size_t boundary, size_t size) {
  // TODO: Properly handle memalign calls.
  return;
}

CILKSAN_API void __csan_memchr(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               char *result, const char *ptr, int val,
			       size_t size) {
  START_HOOK(call_id);

  MAAP_t ptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    ptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (nullptr == result)
    check_read_bytes(call_id, ptr_MAAPVal, ptr, size);
  else
    check_read_bytes(call_id, ptr_MAAPVal, ptr, result - ptr + 1);
}

CILKSAN_API void __csan_memcmp(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const void *lhs, const void *rhs,
                               size_t count) {
  START_HOOK(call_id);

  MAAP_t lhs_MAAPVal = MAAP_t::ModRef, rhs_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
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

CILKSAN_API void __csan_memcpy(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               void *result, void *dst, const void *src,
                               size_t count) {
  START_HOOK(call_id);

  MAAP_t dst_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (nullptr == dst || nullptr == src)
    return;

  check_read_bytes(call_id, src_MAAPVal, src, count);
  check_write_bytes(call_id, dst_MAAPVal, dst, count);
}

CILKSAN_API void __csan___memcpy_chk(const csi_id_t call_id, const csi_id_t func_id,
                                     unsigned MAAP_count, const call_prop_t prop,
                                     void *result, void *dst, const void *src, size_t len,
                                     size_t destlen) {
  START_HOOK(call_id);

  MAAP_t dest_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dest_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel() || len > destlen)
    return;

  if (nullptr == dst || nullptr == src)
    return;

  check_read_bytes(call_id, src_MAAPVal, src, len);
  check_write_bytes(call_id, dest_MAAPVal, dst, len);
}

CILKSAN_API void __csan_memmove(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                void *result, void *dst, const void *src,
                                size_t count) {
  START_HOOK(call_id);

  MAAP_t dst_MAAPVal = MAAP_t::ModRef, src_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    src_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (nullptr == dst || nullptr == src)
    return;

  check_read_bytes(call_id, src_MAAPVal, src, count);
  check_write_bytes(call_id, dst_MAAPVal, dst, count);
}

CILKSAN_API void __csan_memset(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               void *result, void *dst, int ch, size_t count) {
  START_HOOK(call_id);

  MAAP_t dst_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, dst_MAAPVal, dst, count);
}

CILKSAN_API void
__csan___memset_chk(const csi_id_t call_id, const csi_id_t func_id,
                    unsigned MAAP_count, const call_prop_t prop, void *result,
                    void *dst, int ch, size_t len, size_t count) {
  START_HOOK(call_id);

  MAAP_t dst_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel() || len > count)
    return;

  check_write_bytes(call_id, dst_MAAPVal, dst, count);
}

template<int PATTERNLEN>
__attribute__((always_inline)) static void
generic_memset_pattern(const csi_id_t call_id, const csi_id_t func_id,
                       unsigned MAAP_count, const call_prop_t prop, void *dst,
                       const void *pattern, size_t count) {
  START_HOOK(call_id);

  MAAP_t dst_MAAPVal = MAAP_t::ModRef, pattern_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    dst_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    pattern_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pattern_MAAPVal, pattern, PATTERNLEN);
  check_write_bytes(call_id, dst_MAAPVal, dst, count);
}

CILKSAN_API void __csan_memset_pattern4(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count,
                                        const call_prop_t prop, void *dst,
                                        const void *pattern, size_t count) {
  generic_memset_pattern<4>(call_id, func_id, MAAP_count, prop, dst, pattern,
                            count);
}

CILKSAN_API void __csan_memset_pattern8(const csi_id_t call_id,
                                        const csi_id_t func_id,
                                        unsigned MAAP_count,
                                        const call_prop_t prop, void *dst,
                                        const void *pattern, size_t count) {
  generic_memset_pattern<8>(call_id, func_id, MAAP_count, prop, dst, pattern,
                            count);
}

CILKSAN_API void __csan_memset_pattern16(const csi_id_t call_id,
                                         const csi_id_t func_id,
                                         unsigned MAAP_count,
                                         const call_prop_t prop, void *dst,
                                         const void *pattern, size_t count) {
  generic_memset_pattern<16>(call_id, func_id, MAAP_count, prop, dst, pattern,
                             count);
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

CILKSAN_API void __csan_mktime(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               time_t result, struct tm *timeptr) {
  START_HOOK(call_id);

  MAAP_t timeptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    timeptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, timeptr_MAAPVal, timeptr, sizeof(struct tm));
}

CILKSAN_API void __csan_modf(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double arg, double *iptr) {
  START_HOOK(call_id);

  MAAP_t iptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    iptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, iptr_MAAPVal, iptr, sizeof(double));
}

CILKSAN_API void __csan_modff(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float arg, float *iptr) {
  START_HOOK(call_id);

  MAAP_t iptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    iptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, iptr_MAAPVal, iptr, sizeof(float));
}

CILKSAN_API void __csan_modfl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double arg,
                              long double *iptr) {
  START_HOOK(call_id);

  MAAP_t iptr_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    iptr_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, iptr_MAAPVal, iptr, sizeof(long double));
}

CILKSAN_API void __csan_nearbyint(const csi_id_t call_id,
                                  const csi_id_t func_id, unsigned MAAP_count,
                                  const call_prop_t prop, double result,
                                  double arg) {
  return;
}

CILKSAN_API void __csan_nearbyintl(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, float result,
                                   float arg) {
  return;
}

CILKSAN_API void __csan_nearbyintf(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, long double result,
                                   long double arg) {
  return;
}

CILKSAN_API void __csan_ntohl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              uint32_t result, uint32_t netlong) {
  return;
}

CILKSAN_API void __csan_ntohs(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              uint16_t result, uint16_t netlong) {
  return;
}

CILKSAN_API void __csan_open(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int result, const char *pathname, int flags, ...) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);
}

CILKSAN_API void __csan_open64(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *pathname, int flags,
                               ...) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);
}

CILKSAN_API void __csan_pclose(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_perror(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               const char *str) {
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

CILKSAN_API void __csan_popen(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              FILE *result, const char *command,
                              const char *type) {
  START_HOOK(call_id);

  MAAP_t command_MAAPVal = MAAP_t::ModRef, type_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    command_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    type_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, command_MAAPVal, command, strlen(command) + 1);
  check_read_bytes(call_id, type_MAAPVal, type, strlen(type) + 1);
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

CILKSAN_API void __csan_pread(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              ssize_t result, int fd, const void *buffer,
                              size_t count, off_t offset) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 >= result)
    // Nothing to do if result is <= 0
    return;

  check_write_bytes(call_id, buffer_MAAPVal, buffer, result);
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
  if (!CILKSAN_INITIALIZED)
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

CILKSAN_API void __csan_putchar_unlocked(const csi_id_t call_id,
                                         const csi_id_t func_id,
                                         unsigned MAAP_count,
                                         const call_prop_t prop, int result,
                                         int ch) {
  START_HOOK(call_id);

  if (!is_execution_parallel())
    return;

  check_write_bytes(call_id, MAAP_t::ModRef, stdout, 1);
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

CILKSAN_API void __csan_pwrite(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               ssize_t result, int fd, const void *buffer,
                               size_t count, off_t offset) {
  START_HOOK(call_id);

  MAAP_t buffer_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    buffer_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 >= result)
    // Nothing to do if result is <= 0
    return;

  check_read_bytes(call_id, buffer_MAAPVal, buffer, result);
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

CILKSAN_API void __csan_readlink(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 ssize_t result, const char *__restrict__ pathname,
                                 char *__restrict__ buf, size_t bufsiz) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef, buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);
  check_write_bytes(call_id, buf_MAAPVal, buf, result);
}

CILKSAN_API void __csan_readlinkat(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, ssize_t result,
                                   int dirfd, const char *__restrict__ pathname,
                                   char *__restrict__ buf, size_t bufsiz) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef, buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);
  check_write_bytes(call_id, buf_MAAPVal, buf, result);
}

CILKSAN_API void __csan_realloc(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                void *result, void *ptr, size_t new_size) {
  START_HOOK(call_id);

  if (MAAP_count > 0) {
    MAAPs.pop();
  }

  __cilksan_record_free(ptr);
  __cilksan_record_alloc(result, new_size);
}

CILKSAN_API void __csan_realpath(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 char *result, const char *__restrict__ path,
                                 char *__restrict__ resolved_path) {
  START_HOOK(call_id);

  MAAP_t path_MAAPVal = MAAP_t::ModRef, resolved_path_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    resolved_path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }
  (void)resolved_path_MAAPVal;

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, path_MAAPVal, path, strlen(path) + 1);
  if (result != NULL)
    check_write_bytes(call_id, MAAP_t::ModRef, result, strlen(result) + 1);
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

CILKSAN_API void __csan_rename(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *oldname,
                               const char *newname) {
  START_HOOK(call_id);

  MAAP_t oldname_MAAPVal = MAAP_t::ModRef, newname_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    oldname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    newname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, oldname_MAAPVal, oldname, strlen(oldname) + 1);
  check_read_bytes(call_id, newname_MAAPVal, newname, strlen(newname) + 1);
}

CILKSAN_API void __csan_rewind(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_rint(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             double result, double x) {
  return;
}

CILKSAN_API void __csan_rintf(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              float result, float x) {
  return;
}

CILKSAN_API void __csan_rintl(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              long double result, long double x) {
  return;
}

CILKSAN_API void __csan_rmdir(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *path) {
  START_HOOK(call_id);

  MAAP_t path_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    path_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, path_MAAPVal, path, strlen(path) + 1);
}

CILKSAN_API void __csan_round(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double x) {
  return;
}

CILKSAN_API void __csan_roundf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float x) {
  return;
}

CILKSAN_API void __csan_roundl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double x) {
  return;
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

CILKSAN_API void __csan_setvbuf(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int res, FILE *__restrict__ stream,
                                char *__restrict__ buf, int mode, size_t size) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || res != 0) {
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
    return;
  }

  MAAP_t stream_MAAPVal = MAAP_t::ModRef, buf_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    stream_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    buf_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  (void)stream_MAAPVal;
  if (buf != nullptr)
    check_write_bytes(call_id, buf_MAAPVal, buf, size);
}

CILKSAN_API void __csan_setbuf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               FILE *__restrict__ stream,
                               char *__restrict__ buf) {
  __csan_setvbuf(call_id, func_id, MAAP_count, prop, 0, stream, buf,
                 buf ? _IOFBF : _IONBF, BUFSIZ);
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

CILKSAN_API void __csan___snprintf_chk(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, int result,
                                       char *str, size_t n, int flag,
                                       size_t strlen, const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0 || strlen < n) {
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

CILKSAN_API void __csan___sprintf_chk(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, int result,
                                      char *str, int flag, size_t slen,
                                      const char *format, ...) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0 || slen < (size_t)result) {
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

CILKSAN_API void __csan_stpcpy(const csi_id_t call_id, const csi_id_t func_id,
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

CILKSAN_API void __csan___stpcpy_chk(const csi_id_t call_id, const csi_id_t func_id,
                                     unsigned MAAP_count, const call_prop_t prop,
                                     char *result, char *dest, const char *src,
                                     size_t destlen) {
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
  if (src_len > destlen)
    return;

  check_read_bytes(call_id, src_MAAPVal, src, src_len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest, src_len + 1);
}

CILKSAN_API void __csan_stpncpy(const csi_id_t call_id, const csi_id_t func_id,
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

CILKSAN_API void
__csan___stpncpy_chk(const csi_id_t call_id, const csi_id_t func_id,
                     unsigned MAAP_count, const call_prop_t prop, char *result,
                     char *dest, const char *src, size_t count) {
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
    return;
  check_read_bytes(call_id, src_MAAPVal, src, src_len);
  check_write_bytes(call_id, dest_MAAPVal, dest, count);
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

CILKSAN_API void __csan___strcat_chk(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, char *result, char *dest, const char *src,
    size_t destlen) {
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
  if (src_len + strlen(dest) + 1 > destlen)
    return;

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

CILKSAN_API void __csan_strcspn(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                size_t result, const char *str1,
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

  check_read_bytes(call_id, str2_MAAPVal, str2, strlen(str2) + 1);
  check_read_bytes(call_id, str1_MAAPVal, str1, result + 1);
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

CILKSAN_API void __csan_strcoll(const csi_id_t call_id, const csi_id_t func_id,
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

CILKSAN_API void __csan___strcpy_chk(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, char *result, char *dest, const char *src,
    size_t destlen) {
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
  if (src_len + 1 > destlen)
    return;
  check_read_bytes(call_id, src_MAAPVal, src, src_len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest, src_len + 1);
}

CILKSAN_API void __csan_strlcat(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                size_t result, char *dest, const char *src,
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

CILKSAN_API void __csan___strlcat_chk(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, size_t result,
                                      char *dest, const char *src, size_t count,
                                      size_t destlen) {
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
  if (destlen < ((len >= count) ? count + 1 : len + 1))
    return;
  check_read_bytes(call_id, src_MAAPVal, src, len >= count ? count : len + 1);
  check_write_bytes(call_id, dest_MAAPVal, dest + strlen(dest),
                    len >= count ? count + 1 : len + 1);
}

CILKSAN_API void __csan_strlcpy(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                size_t result, char *dest, const char *src,
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

CILKSAN_API void __csan___strlcpy_chk(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, size_t result,
                                      char *dest, const char *src, size_t count,
                                      size_t destlen) {
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
  if (count > destlen)
    return;
  check_read_bytes(call_id, src_MAAPVal, src, src_len);
  check_write_bytes(call_id, dest_MAAPVal, dest, count);
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

CILKSAN_API void __csan___strncat_chk(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, char *result,
                                      char *dest, const char *src, size_t count,
                                      size_t destlen) {
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
  if (destlen < ((len >= count) ? count + 1 : len + 1))
    return;
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
  check_write_bytes(call_id, dest_MAAPVal, dest, src_len + 1);
}

CILKSAN_API void __csan___strncpy_chk(const csi_id_t call_id,
                                      const csi_id_t func_id,
                                      unsigned MAAP_count,
                                      const call_prop_t prop, char *result,
                                      char *dest, const char *src, size_t count,
                                      size_t destlen) {
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
  if (src_len + 1 > destlen)
    return;
  check_read_bytes(call_id, src_MAAPVal, src, src_len);
  check_write_bytes(call_id, dest_MAAPVal, dest, src_len + 1);
}

CILKSAN_API void __csan_strnlen(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                size_t result, const char *str, size_t maxlen) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    for (unsigned i = 0; i < MAAP_count; ++i)
      MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (0 == result)
    return;
  check_read_bytes(call_id, str_MAAPVal, str,
                   result + 1 >= maxlen ? maxlen : result + 1);
}

CILKSAN_API void __csan_strpbrk(const csi_id_t call_id, const csi_id_t func_id,
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

  check_read_bytes(call_id, str2_MAAPVal, str2, strlen(str2) + 1);
  if (nullptr == result)
    check_read_bytes(call_id, str1_MAAPVal, str1, strlen(str1) + 1);
  else
    check_read_bytes(call_id, str1_MAAPVal, str1, result - str1 + 1);
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

CILKSAN_API void __csan_strspn(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               size_t result, const char *str1,
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

  check_read_bytes(call_id, str2_MAAPVal, str2, strlen(str2) + 1);
  check_read_bytes(call_id, str1_MAAPVal, str1, result + 1);
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

CILKSAN_API void __csan_strtok_r(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 char *result, char *str,
                                 const char *delim, char **saveptr) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef, delim_MAAPVal = MAAP_t::ModRef,
         saveptr_p_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    delim_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    saveptr_p_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  // Determine the string that will be scanned.
  char *my_str = str;
  if (nullptr == my_str) {
    my_str = *saveptr;
    if (is_execution_parallel())
      check_read_bytes(call_id, saveptr_p_MAAPVal, saveptr, sizeof(char *));
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
    check_write_bytes(call_id, saveptr_p_MAAPVal, saveptr, sizeof(char *));
  }
}

CILKSAN_API void __csan___strtok_r(const csi_id_t call_id,
                                   const csi_id_t func_id, unsigned MAAP_count,
                                   const call_prop_t prop, char *result,
                                   char *str, const char *delim,
                                   char **saveptr) {
  START_HOOK(call_id);

  MAAP_t str_MAAPVal = MAAP_t::ModRef, delim_MAAPVal = MAAP_t::ModRef,
         saveptr_p_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    str_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    delim_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
    saveptr_p_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  // Determine the string that will be scanned.
  char *my_str = str;
  if (nullptr == my_str) {
    my_str = *saveptr;
    if (is_execution_parallel())
      check_read_bytes(call_id, saveptr_p_MAAPVal, saveptr, sizeof(char *));
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
    check_write_bytes(call_id, saveptr_p_MAAPVal, saveptr, sizeof(char *));
  }
}

CILKSAN_API void __csan_strxfrm(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                size_t result, char *str1, const char *str2,
                                size_t n) {
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

  (void)str1_MAAPVal;
  check_read_bytes(call_id, str2_MAAPVal, str2, strlen(str2) + 1);
  size_t xfrm_len = 1 + strxfrm(nullptr, str2, 0);
  if (nullptr != str1)
    check_write_bytes(call_id, str2_MAAPVal, str2, xfrm_len);
}

CILKSAN_API void __csan_system(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *command) {
  START_HOOK(call_id);

  MAAP_t command_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    command_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, command_MAAPVal, command, strlen(command) + 1);
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

CILKSAN_API void __csan_toascii(const csi_id_t call_id, const csi_id_t func_id,
                                unsigned MAAP_count, const call_prop_t prop,
                                int result, int c) {
  return;
}

CILKSAN_API void __csan_trunc(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              double result, double arg) {
  return;
}

CILKSAN_API void __csan_truncf(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               float result, float arg) {
  return;
}

CILKSAN_API void __csan_truncl(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               long double result, long double arg) {
  return;
}

CILKSAN_API void __csan_ungetc(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, int ch, FILE *stream) {
  if (!CILKSAN_INITIALIZED)
    return;

  if (!should_check())
    return;

  // Most operations on streams are locked by default

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();
}

CILKSAN_API void __csan_unlink(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *pathname) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);

  // TODO: Simulate system-level modifications to unlink pathname from the
  // filesystem.
}

CILKSAN_API void __csan_unlinkat(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, int dirfd, const char *pathname,
                                 int flags) {
  START_HOOK(call_id);

  MAAP_t pathname_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    pathname_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  check_read_bytes(call_id, pathname_MAAPVal, pathname, strlen(pathname) + 1);

  // TODO: Simulate system-level modifications to unlink pathname from the
  // filesystem.
}

CILKSAN_API void __csan_unsetenv(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 int result, const char *name) {
  START_HOOK(call_id);

  MAAP_t name_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    name_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;

  if (nullptr == name)
    return;

  // TODO: Model access to environment list.
  check_read_bytes(call_id, name_MAAPVal, name, strlen(name) + 1);
}

CILKSAN_API void __csan_utime(const csi_id_t call_id, const csi_id_t func_id,
                              unsigned MAAP_count, const call_prop_t prop,
                              int result, const char *filename,
                              const struct utimbuf *times) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef, times_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    times_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;
  if (result < 0)
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  if (nullptr != times)
    check_read_bytes(call_id, times_MAAPVal, times, sizeof(struct utimbuf));
}

CILKSAN_API void __csan_utimes(const csi_id_t call_id, const csi_id_t func_id,
                               unsigned MAAP_count, const call_prop_t prop,
                               int result, const char *filename,
                               const struct timeval times[2]) {
  START_HOOK(call_id);

  MAAP_t filename_MAAPVal = MAAP_t::ModRef, times_MAAPVal = MAAP_t::ModRef;
  if (MAAP_count > 0) {
    filename_MAAPVal = MAAPs.back().second;
    MAAPs.pop();

    times_MAAPVal = MAAPs.back().second;
    MAAPs.pop();
  }

  if (!is_execution_parallel())
    return;
  if (result < 0)
    return;

  check_read_bytes(call_id, filename_MAAPVal, filename, strlen(filename) + 1);
  if (nullptr != times)
    check_read_bytes(call_id, times_MAAPVal, times, sizeof(struct timeval[2]));
}

CILKSAN_API void __csan_vfprintf(const csi_id_t call_id, const csi_id_t func_id,
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

  (void)stream_MAAPVal;
  vprintf_common(call_id, MAAP_count, format, ap);
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

  (void)stream_MAAPVal;
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

CILKSAN_API void
__csan___vsnprintf_chk(const csi_id_t call_id, const csi_id_t func_id,
                       unsigned MAAP_count, const call_prop_t prop, int result,
                       char *buf, size_t count, int flags, size_t slen,
                       const char *format, va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0 || slen < count ||
      slen < (size_t)result) {
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

CILKSAN_API void __csan___vsprintf_chk(const csi_id_t call_id,
                                       const csi_id_t func_id,
                                       unsigned MAAP_count,
                                       const call_prop_t prop, int result,
                                       char *buf, int flags, size_t slen,
                                       const char *format, va_list ap) {
  START_HOOK(call_id);

  if (!is_execution_parallel() || result <= 0 || slen < (size_t)result) {
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
