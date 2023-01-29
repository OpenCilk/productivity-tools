#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "cilksan_internal.h"
#include "debug_util.h"
#include "driver.h"

CILKSAN_API void *__csan_llvm_hyper_lookup(const csi_id_t call_id,
                                           const csi_id_t func_id,
                                           unsigned MAAP_count,
                                           const call_prop_t prop, void *view,
                                           void *key, size_t size,
                                           void *identity_fn, void *reduce_fn) {
  if (!CILKSAN_INITIALIZED || !should_check())
    return view;
  if (__builtin_expect(!call_pc[call_id], false))
    call_pc[call_id] = CALLERPC;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return view;

  if (CilkSanImpl.stealable()) {
    // Get the table of reducer views to update.
    hyper_table *reducer_views = CilkSanImpl.get_or_create_reducer_views();
    // Check if a view has already been created, and return it if so.
    if (void *new_view =
        CilkSanImpl.reducer_lookup(reducer_views, (uintptr_t)key))
      return new_view;
    // Create and return a new reducer view.
    return CilkSanImpl.create_reducer_view(reducer_views, (uintptr_t)key, size,
                                           identity_fn, reduce_fn);
  }
  return view;
}

CILKSAN_API void*
__csan_llvm_hyper_lookup_i64(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             void *view, void *key, size_t size,
                             void *identity_fn, void *reduce_fn) {
  return __csan_llvm_hyper_lookup(call_id, func_id, MAAP_count, prop, view, key,
                                  size, identity_fn, reduce_fn);
}

void CilkSanImpl_t::reduce_local_views() {
  FrameData_t *f = frame_stack.head();
  hyper_table *reducer_views = f->reducer_views;
  if (!reducer_views)
    // No local reducer views to reduce
    return;

  // Disable race detection to avoid spurious race reports from the execution of
  // the reduce functions.
  disable_checking();

  uint32_t parent_contin = f->get_parent_continuation();
  if (parent_contin > 0) {
    // Combine/store local reducer views with parent reducer views.
    frame_stack.ancestor(parent_contin)
        ->set_or_merge_reducer_views(reducer_views);
    enable_checking();

    f->reducer_views = nullptr;
    return;
  }

  // Reduce every reducer view in the table with its leftmost view.
  int32_t capacity = reducer_views->capacity;
  bucket *buckets = reducer_views->buckets;
  for (int32_t i = 0; i < capacity; ++i) {
    bucket b = buckets[i];
    if (!is_valid(b.key))
      continue;

    // The key is the pointer to the leftmost view.
    void *left_view = (void *)b.key;
    reducer_base rb = b.value;
    rb.reduce_fn(left_view, rb.view);
    // Delete the right view.
    free(rb.view);
  }
  enable_checking();

  // Delete the table of local reducer views
  delete reducer_views;
  f->reducer_views = nullptr;
}
