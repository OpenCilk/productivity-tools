#include "cilksan_internal.h"
#include "debug_util.h"
#include "driver.h"
#include "hypertable.h"
#include "vector.h"
#include <cilk/reducer>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <variant>

// Hooks for handling reducer hyperobjects.

template <typename ExtraTy>
static void reducer_register(const csi_id_t call_id, unsigned MAAP_count,
                             void *key, ExtraTy *extra) {
  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (CilkSanImpl.stealable()) {
    hyper_table *reducer_views = CilkSanImpl.get_or_create_reducer_views();
    reducer_views->insert((hyper_table::bucket){
        .key = (uintptr_t)key,
        .data = {.view = key, .extra = extra}});
    DBG_TRACE(REDUCER,
              "reducer_register: registered %p, reducer_views %p, occupancy %d\n",
              key, reducer_views, reducer_views->occupancy);
  }

  if (!is_execution_parallel())
    return;

  // For race purposes treat this as a read of the leftmost view.
  check_read_bytes(call_id, MAAP_t::Ref, key, 1);
}

CILKSAN_API void
__csan_llvm_reducer_register(const csi_id_t call_id, const csi_id_t func_id,
                             unsigned MAAP_count, const call_prop_t prop,
                             int32_t type, void *key, void *data) {
  START_HOOK(call_id);
  switch (type) {
    case 0: {
      __reducer_base *rb = static_cast<__reducer_base *>(key);
      reducer_register(call_id, MAAP_count, key, rb);
      break;
    }
    case 1: {
      __reducer_callbacks *cb =
          static_cast<__reducer_callbacks *>(data);
      reducer_register(call_id, MAAP_count, key, static_cast<__cilk_reduce_fn *>(&cb->reduce));
      break;
    }
    case 2: {
      void (*reduce)(void *, void *) =
          reinterpret_cast<void (*)(void *, void *)>(data);
      reducer_register(call_id, MAAP_count, key, reduce);
      break;
    }
    default:
      cilksan_assert(false && "Unknown reducer type in reducer_register");
  }
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
    DBG_TRACE(
        REDUCER,
        "reducer_unregister: unregistering %p, reducer_views %p, occupancy %d\n",
        key, reducer_views, reducer_views->occupancy);
    reducer_views->remove((uintptr_t)key);
  }

  if (!is_execution_parallel())
    return;

  // For race purposes treat this as a read of the leftmost view.
  check_read_bytes(call_id, MAAP_t::Ref, key, 1);
}

std::pair<void *, hyper_table *>
hyper_lookup_common(const csi_id_t call_id, const csi_id_t func_id,
                          unsigned MAAP_count, const call_prop_t prop,
                          void *view, void *key) {
  if (!CILKSAN_INITIALIZED || !should_check())
    return {view, nullptr};
  if (__builtin_expect(!call_pc[call_id], false))
    call_pc[call_id] = CALLERPC;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  if (!is_execution_parallel())
    return {view, nullptr};

  if (CilkSanImpl.stealable()) {
    // Get the table of reducer views to update.
    hyper_table *reducer_views = CilkSanImpl.get_or_create_reducer_views();
    // Check if a view has already been created, and return it if so.
    if (void *new_view =
            CilkSanImpl.reducer_lookup(reducer_views, (uintptr_t)key)) {
      DBG_TRACE(REDUCER, "hyper_lookup: found view: %p -> %p\n", key, new_view);
      return {new_view, reducer_views};
    }
    // Return nullptr for the view and a pointer to the hyper table.  The caller
    // will create a new view and install it in the hyper table.
    return {nullptr, reducer_views};
  }
  return {view, nullptr};
}

CILKSAN_API void *__csan_llvm_hyper_lookup_0(const csi_id_t call_id,
                                             const csi_id_t func_id,
                                             unsigned MAAP_count,
                                             const call_prop_t prop, void *view,
                                             __reducer_base *key) {
  auto result =
      hyper_lookup_common(call_id, func_id, MAAP_count, prop, view, key);
  if (result.first || result.second == nullptr)
    return result.first;
  // Create and return a new reducer view.
  return CilkSanImpl.create_reducer_view_0(result.second, key);
}

CILKSAN_API void *
__csan_llvm_hyper_lookup_1(const csi_id_t call_id, const csi_id_t func_id,
                           unsigned MAAP_count, const call_prop_t prop,
                           void *view, void *key,
                           const __reducer_callbacks &callbacks) {
  auto result =
      hyper_lookup_common(call_id, func_id, MAAP_count, prop, view, key);
  if (result.first || result.second == nullptr)
    return result.first;
  // Create and return a new reducer view.
  return CilkSanImpl.create_reducer_view_1(result.second, (uintptr_t)key,
                                           callbacks);
}

CILKSAN_API void *__csan_llvm_hyper_lookup_2(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *view, void *key, size_t size,
    void (*identity_fn)(void *), void (*reduce_fn)(void *, void *)) {
  auto result =
      hyper_lookup_common(call_id, func_id, MAAP_count, prop, view, key);
  if (result.first || result.second == nullptr)
    return result.first;
  // Create and return a new reducer view.
  return CilkSanImpl.create_reducer_view_2(result.second, (uintptr_t)key, size,
                                           identity_fn, reduce_fn);
}

CILKSAN_API void *__csan_llvm_hyper_lookup_2_i64(
    const csi_id_t call_id, const csi_id_t func_id, unsigned MAAP_count,
    const call_prop_t prop, void *view, void *key, size_t size,
    void (*identity_fn)(void *), void (*reduce_fn)(void *, void *)) {
  return __csan_llvm_hyper_lookup_2(call_id, func_id, MAAP_count, prop, view,
                                    key, size, identity_fn, reduce_fn);
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
  bool holdsLeftmostViews = false;
  Vector_t<int32_t> keysToRemove;
  for (int32_t i = 0; i < capacity; ++i) {
    hyper_table::bucket b = buckets[i];
    if (!is_valid(b.key))
      continue;
    if (b.key == (uintptr_t)(b.data.view)) {
      holdsLeftmostViews = true;
      continue;
    }

    DBG_TRACE(REDUCER,
              "reduce_local_views: found view to reduce at %d: %p -> %p\n", i,
              (void *)b.key, (void *)b.data.view);

    void *left_view = (void *)b.key, *right_view = b.data.view;
    {
      // Custom version of hyper_table::bucket::reduce() to reduce view with key
      // in the same bucket.  The key points to the leftmost view.
      reducer_data rd = b.data;
      if (std::holds_alternative<__reducer_base *>(rd.extra)) {
        __reducer_base *leftmost =
            static_cast<__reducer_base *>(reinterpret_cast<void *>(b.key));
        __reducer_base *left_r = leftmost;
        __reducer_base *right_r = std::get<__reducer_base *>(rd.extra);
        leftmost->reduce(left_r, right_r);
        right_r->~__reducer_base();
      } else if (std::holds_alternative<const __cilk_reduce_fn *>(rd.extra)) {
        const __cilk_reduce_fn *reduce_fn =
            std::get<const __cilk_reduce_fn *>(rd.extra);
        (*reduce_fn)(left_view, right_view);
      } else {
        void (*reduce_fn)(void *, void *) =
            std::get<void (*)(void *, void *)>(rd.extra);
        reduce_fn(left_view, right_view);
      }
      rd.extra = (__reducer_base *)nullptr;
      rd.view = nullptr;
    }
    // Free the right view.
    free(right_view);
    mark_free(right_view);
    keysToRemove.push_back(b.key);
  }
  enable_checking();

  if (!holdsLeftmostViews) {
    // Delete the table of local reducer views
    DBG_TRACE(REDUCER, "reduce_local_views: delete reducer_views %p\n",
              reducer_views);
    delete reducer_views;
    f->reducer_views = nullptr;
  } else {
    for (int32_t i = 0; i < keysToRemove.size(); ++i)
      reducer_views->remove(buckets[keysToRemove[i]].key);
  }
}

void hyper_table::bucket::reduce(bucket *left, bucket *right) {
    assert(left->data.extra.index() == right->data.extra.index());
    void *left_view = left->data.view, *right_view = right->data.view;
    if (std::holds_alternative<__reducer_base *>(left->data.extra)) {
        __reducer_base *leftmost =
            static_cast<__reducer_base *>
            (reinterpret_cast<void *>(left->key));
        __reducer_base *left_r = std::get<__reducer_base *>(left->data.extra);
        __reducer_base *right_r =
            std::get<__reducer_base *>(right->data.extra);
        leftmost->reduce(left_r, right_r);
        right_r->~__reducer_base();
    } else if (std::holds_alternative<const __cilk_reduce_fn *>(left->data.extra)) {
        const __cilk_reduce_fn *reduce_fn =
            std::get<const __cilk_reduce_fn *>(left->data.extra);
        (*reduce_fn)(left_view, right_view);
    } else {
        void (*reduce_fn)(void *, void *) =
            std::get<void (*)(void *, void *)>(left->data.extra);
        reduce_fn(left_view, right_view);
    }
    right->data.extra = (__reducer_base *)nullptr;
    right->data.view = nullptr;
    // Free the right view.
    free(right_view);
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
      reducer_data dst_rd = dst_bucket->data;
      if (left_dst) {
        bucket::reduce(dst_bucket, &b);
        tool->mark_free(b.data.view);
      } else {
        bucket::reduce(&b, dst_bucket);
        tool->mark_free(dst_rd.view);
        dst_bucket->data = b.data;
        b.data.extra = (__reducer_base *)nullptr;
        b.data.view = nullptr;
      }
    }
  }

  // Destroy the source hyper_table, and return the destination.
  delete src;
  return dst;
}
