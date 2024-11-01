#include "cilksan_internal.h"
#include "debug_util.h"
#include "driver.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// Hooks for handling reducer hyperobjects.

static void reducer_register(const csi_id_t call_id, unsigned MAAP_count,
                             void *key, void *identity_ptr, void *reduce_ptr) {
  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (CilkSanImpl.stealable()) {
    hyper_table *reducer_views = CilkSanImpl.get_or_create_reducer_views();
    reducer_views->insert((hyper_table::bucket){
        .key = (uintptr_t)key,
        .value = {.view = key, .reduce_fn = (__cilk_reduce_fn)reduce_ptr}});
  }

  if (!is_execution_parallel())
    return;

  // For race purposes treat this as a read of the leftmost view.
  check_read_bytes(call_id, MAAP_t::Ref, key, 1);
}

CILKSAN_API void
__csan_llvm_reducer_register_i32(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 void *key, size_t size, void *identity_ptr,
                                 void *reduce_ptr) {
  START_HOOK(call_id);

  reducer_register(call_id, MAAP_count, key, identity_ptr, reduce_ptr);
}

CILKSAN_API void
__csan_llvm_reducer_register_i64(const csi_id_t call_id, const csi_id_t func_id,
                                 unsigned MAAP_count, const call_prop_t prop,
                                 void *key, size_t size, void *identity_ptr,
                                 void *reduce_ptr) {
  START_HOOK(call_id);

  reducer_register(call_id, MAAP_count, key, identity_ptr, reduce_ptr);
}

CILKSAN_API void __csan_llvm_reducer_unregister(const csi_id_t call_id,
                                                const csi_id_t func_id,
                                                unsigned MAAP_count,
                                                const call_prop_t prop,
                                                void *key) {
  START_HOOK(call_id);

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  // Remove this reducer from the table.
  if (hyper_table *reducer_views = CilkSanImpl.get_reducer_views()) {
    reducer_views->remove((uintptr_t)key);
  }

  if (!is_execution_parallel())
    return;

  // For race purposes treat this as a read of the leftmost view.
  check_read_bytes(call_id, MAAP_t::Ref, key, 1);
}

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
            CilkSanImpl.reducer_lookup(reducer_views, (uintptr_t)key)) {
      DBG_TRACE(REDUCER, "hyper_lookup: found view: %p -> %p\n", key, new_view);
      return new_view;
    }
    // Create and return a new reducer view.
    return CilkSanImpl.create_reducer_view(reducer_views, (uintptr_t)key, size,
                                           identity_fn, reduce_fn);
  }
  return view;
}

CILKSAN_API void *
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

  DBG_TRACE(REDUCER,
            "reduce_local_views: processing reducer_views %p, occupancy %d\n",
            reducer_views, reducer_views->occupancy);

  // Disable race detection to avoid spurious race reports from the execution of
  // the reduce functions.
  disable_checking();

  uint32_t parent_contin = f->get_parent_continuation();
  if (parent_contin > 0) {
    // Combine/store local reducer views with parent reducer views.
    frame_stack.ancestor(parent_contin)
        ->set_or_merge_reducer_views(this, reducer_views);
    enable_checking();

    f->reducer_views = nullptr;
    return;
  }

  // Reduce every reducer view in the table with its leftmost view.
  int32_t capacity = reducer_views->capacity;
  hyper_table::bucket *buckets = reducer_views->buckets;
  for (int32_t i = 0; i < capacity; ++i) {
    hyper_table::bucket b = buckets[i];
    if (!is_valid(b.key))
      continue;

    DBG_TRACE(REDUCER,
              "reduce_local_views: found view to reduce at %d: %p -> %p\n", i,
              (void *)b.key, (void *)b.value.view);
    // The key is the pointer to the leftmost view.
    void *left_view = (void *)b.key;
    reducer_base rb = b.value;
    rb.reduce_fn(left_view, rb.view);
    // Delete the right view.
    free(rb.view);
    mark_free(rb.view);
  }
  enable_checking();

  // Delete the table of local reducer views
  DBG_TRACE(REDUCER, "reduce_local_views: delete reducer_views %p\n",
            reducer_views);
  delete reducer_views;
  f->reducer_views = nullptr;
}

hyper_table *
hyper_table::merge_two_hyper_tables(CilkSanImpl_t *__restrict__ tool,
                                    hyper_table *__restrict__ left,
                                    hyper_table *__restrict__ right) {
  DBG_TRACE(REDUCER, "merge_two_hyper_tables: %p, %p\n", left, right);
  // In the trivial case of an empty hyper_table, return the other hyper_table.
  if (!left)
    return right;
  if (!right)
    return left;
  if (left->occupancy == 0) {
    delete left;
    return right;
  }
  if (right->occupancy == 0) {
    delete right;
    return left;
  }

  // Pick the smaller hyper_table to be the source to iterate over.
  bool left_dst;
  hyper_table *src, *dst;
  if (left->occupancy >= right->occupancy) {
    src = right;
    dst = left;
    left_dst = true;
  } else {
    src = left;
    dst = right;
    left_dst = false;
  }

  int32_t src_capacity =
      (src->capacity < MIN_HT_CAPACITY) ? src->occupancy : src->capacity;
  hyper_table::bucket *src_buckets = src->buckets;
  // Iterate over the contents of the source hyper_table.
  for (int32_t i = 0; i < src_capacity; ++i) {
    hyper_table::bucket b = src_buckets[i];
    if (!is_valid(b.key))
      continue;

    // For each valid key in the source table, lookup that key in the
    // destination table.
    hyper_table::bucket *dst_bucket = dst->find(b.key);

    if (nullptr == dst_bucket) {
      // The destination table does not contain this key.  Insert the
      // key-value pair from the source table into the destination.
      dst->insert(b);
    } else {
      // Merge the two views in the source and destination buckets, being sure
      // to preserve left-to-right ordering.  Free the right view when done.
      reducer_base dst_rb = dst_bucket->value;
      if (left_dst) {
        dst_rb.reduce_fn(dst_rb.view, b.value.view);
        free(b.value.view);
        tool->mark_free(b.value.view);
      } else {
        dst_rb.reduce_fn(b.value.view, dst_rb.view);
        free(dst_rb.view);
        tool->mark_free(dst_rb.view);
        dst_bucket->value.view = b.value.view;
      }
    }
  }

  // Destroy the source hyper_table, and return the destination.
  delete src;
  return dst;
}
