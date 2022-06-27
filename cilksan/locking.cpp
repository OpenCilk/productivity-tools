#include <dlfcn.h>
#include <pthread.h>
#ifndef __STDC_NO_THREADS__
#include <threads.h>
#endif // __STDC_NO_THREADS__
#include <cilk/cilk_api.h>

#include "driver.h"

// Running counter to track the next available lock ID.  We reserve lock_id == 0
// for atomic operations.
extern const LockID_t atomic_lock_id;
static LockID_t next_lock_id = atomic_lock_id + 1;

// Map from memory addresses to locks allocated at those locations.
static AddrMap_t<LockID_t> lock_ids;

static inline void emit_acquire_release_warning(bool is_aquire,
                                                const void *mutex) {
  if (is_aquire)
    fprintf(err_io,
            "Cilksan Warning: Cannot model lock-acquire of unknown lock at "
            "location %p\n",
            mutex);
  else
    fprintf(err_io,
            "Cilksan Warning: Cannot model lock-release of unknown lock at "
            "location %p\n",
            mutex);
}

///////////////////////////////////////////////////////////////////////////
// API for Cilk fake locks

CILKSAN_API void __cilksan_acquire_lock(const void *mutex) {
  if (CILKSAN_INITIALIZED && is_execution_parallel()) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
    else
      emit_acquire_release_warning(true, mutex);
  }
}

CILKSAN_API void __cilksan_release_lock(const void *mutex) {
  if (CILKSAN_INITIALIZED && is_execution_parallel()) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_release_lock(*lock_id);
    else
      emit_acquire_release_warning(false, mutex);
  }
}

CILKSAN_API void __cilksan_begin_atomic() {
  if (CILKSAN_INITIALIZED && is_execution_parallel()) {
    CilkSanImpl.do_acquire_lock(atomic_lock_id);
  }
}

CILKSAN_API void __cilksan_end_atomic() {
  if (CILKSAN_INITIALIZED && is_execution_parallel()) {
    CilkSanImpl.do_release_lock(atomic_lock_id);
  }
}

CILKSAN_API void __cilksan_register_lock_explicit(const void *mutex) {
  if (CILKSAN_INITIALIZED && !lock_ids.contains((uintptr_t)mutex)) {
    lock_ids.insert((uintptr_t)mutex, next_lock_id++);
  }
}

CILKSAN_API void __cilksan_unregister_lock_explicit(const void *mutex) {
  if (CILKSAN_INITIALIZED && lock_ids.contains((uintptr_t)mutex)) {
    lock_ids.remove((uintptr_t)mutex);
  }
}

///////////////////////////////////////////////////////////////////////////
// Interposers for Pthread locking routines

CILKSAN_API int __csan_pthread_mutex_init(pthread_mutex_t *mutex,
                                          const pthread_mutexattr_t *attr) {
  int result = pthread_mutex_init(mutex, attr);
  if (CILKSAN_INITIALIZED)
    lock_ids.insert((uintptr_t)mutex, next_lock_id++);
  return result;
}

CILKSAN_API int __csan_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  int result = pthread_mutex_destroy(mutex);
  if (CILKSAN_INITIALIZED && lock_ids.contains((uintptr_t)mutex))
    lock_ids.remove((uintptr_t)mutex);
  return result;
}

#ifndef __STDC_NO_THREADS__
CILKSAN_API int __csan_mtx_init(mtx_t *mutex, int type) {
  int result = mtx_init(mutex, type);
  if (CILKSAN_INITIALIZED)
    lock_ids.insert((uintptr_t)mutex, next_lock_id++);
  return result;
}

CILKSAN_API void __csan_mtx_destroy(mtx_t *mutex) {
  mtx_destroy(mutex);
  if (CILKSAN_INITIALIZED && lock_ids.contains((uintptr_t)mutex))
    lock_ids.remove((uintptr_t)mutex);
}
#endif // __STDC_NO_THREADS__

// FIXME: Ideally we would disallow locking any mutex we haven't seen before,
// but some lock implementations, such as C++11 std::mutex, use
// pthread_mutex_lock on storage not initialized by pthread_mutex_init.
//
// In the future, we might replace the constructor for std::mutex by providing a
// custom implementation in a distinct header file that is only used when
// compiling with Cilksan.  But for now we simply allow locking routines to
// initialize locks.
//
// We should also modify Cilksan to properly remove locks when the underlying
// storage is deallocated, even if the lock is not explicitly destroyed.

CILKSAN_API int __csan_pthread_mutex_lock(pthread_mutex_t *mutex) {
  int result = pthread_mutex_lock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (!lock_ids.contains((uintptr_t)mutex))
      lock_ids.insert((uintptr_t)mutex, next_lock_id++);
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int __csan_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int result = pthread_mutex_trylock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (!lock_ids.contains((uintptr_t)mutex))
      lock_ids.insert((uintptr_t)mutex, next_lock_id++);
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int __csan_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int result = pthread_mutex_unlock(mutex);
  // Only record the lock release if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_release_lock(*lock_id);
  }
  return result;
}

#ifndef __STDC_NO_THREADS__
CILKSAN_API int __csan_mtx_lock(mtx_t *mutex) {
  int result = mtx_lock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (!lock_ids.contains((uintptr_t)mutex))
      lock_ids.insert((uintptr_t)mutex, next_lock_id++);
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int __csan_mtx_trylock(mtx_t *mutex) {
  int result = mtx_trylock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (!lock_ids.contains((uintptr_t)mutex))
      lock_ids.insert((uintptr_t)mutex, next_lock_id++);
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int
__csan_mtx_timedlock(mtx_t *__restrict__ mutex,
                     const struct timespec *__restrict__ time_point) {
  int result = mtx_timedlock(mutex, time_point);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if ((thrd_success == result) && CILKSAN_INITIALIZED &&
      __cilkrts_running_on_workers() && is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int __csan_mtx_unlock(mtx_t *mutex) {
  int result = mtx_unlock(mutex);
  // Only record the lock release if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (CILKSAN_INITIALIZED && __cilkrts_running_on_workers() &&
      is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_release_lock(*lock_id);
  }
  return result;
}
#endif // __STDC_NO_THREADS__

CILKSAN_API int __csan_pthread_once(pthread_once_t *once_control,
                                    void (*init_routine)(void)) {
  // pthread_once ensures that the given function is run just once by any thread
  // in the process.  We simply disable instrumentation around the invocation.
  disable_checking();
  int result = pthread_once(once_control, init_routine);
  enable_checking();
  return result;
}

#ifndef __STDC_NO_THREADS__
CILKSAN_API void __csan_call_once(once_flag *once_control, void (*fn)(void)) {
  // call_once ensures that the given function is run just once by any thread
  // in the process.  We simply disable instrumentation around the invocation.
  disable_checking();
  call_once(once_control, fn);
  enable_checking();
}
#endif // __STDC_NO_THREADS__

typedef struct guard_t guard_t;

CILKSAN_API void __csan___cxa_guard_abort(const csi_id_t call_id,
                                          const csi_id_t func_id,
                                          unsigned MAAP_count,
                                          const call_prop_t prop,
                                          guard_t *guard) {
  if (!CILKSAN_INITIALIZED)
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  // Interpositioning of the pthread routines should handle the semantics of
  // __cxa_guard_abort.
}

CILKSAN_API void __csan___cxa_guard_acquire(const csi_id_t call_id,
                                            const csi_id_t func_id,
                                            unsigned MAAP_count,
                                            const call_prop_t prop, int result,
                                            guard_t *guard) {
  if (!CILKSAN_INITIALIZED)
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  // Interpositioning of the pthread routines should handle the semantics of
  // __cxa_guard_acquire.
}

CILKSAN_API void __csan___cxa_guard_release(const csi_id_t call_id,
                                            const csi_id_t func_id,
                                            unsigned MAAP_count,
                                            const call_prop_t prop,
                                            guard_t *guard) {
  if (!CILKSAN_INITIALIZED)
    return;

  for (unsigned i = 0; i < MAAP_count; ++i)
    MAAPs.pop();

  // Interpositioning of the pthread routines should handle the semantics of
  // __cxa_guard_release.
}
