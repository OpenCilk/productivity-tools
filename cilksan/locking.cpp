#include <dlfcn.h>
#include <pthread.h>

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
    fprintf(stderr,
            "Cilksan Warning: Cannot model lock-acquire of unknown lock at "
            "location %p\n",
            mutex);
  else
    fprintf(stderr,
            "Cilksan Warning: Cannot model lock-release of unknown lock at "
            "location %p\n",
            mutex);
}

///////////////////////////////////////////////////////////////////////////
// API for Cilk fake locks

CILKSAN_API void __cilksan_acquire_lock(const void *mutex) {
  if (TOOL_INITIALIZED && is_execution_parallel()) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
    else
      emit_acquire_release_warning(true, mutex);
  }
}

CILKSAN_API void __cilksan_release_lock(const void *mutex) {
  if (TOOL_INITIALIZED && is_execution_parallel()) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_release_lock(*lock_id);
    else
      emit_acquire_release_warning(false, mutex);
  }
}

CILKSAN_API void __cilksan_begin_atomic() {
  if (TOOL_INITIALIZED && is_execution_parallel()) {
    CilkSanImpl.do_acquire_lock(atomic_lock_id);
  }
}

CILKSAN_API void __cilksan_end_atomic() {
  if (TOOL_INITIALIZED && is_execution_parallel()) {
    CilkSanImpl.do_release_lock(atomic_lock_id);
  }
}

CILKSAN_API void __cilksan_register_lock_explicit(const void *mutex) {
  if (TOOL_INITIALIZED && !lock_ids.contains((uintptr_t)mutex)) {
    lock_ids.insert((uintptr_t)mutex, next_lock_id++);
  }
}

CILKSAN_API void __cilksan_unregister_lock_explicit(const void *mutex) {
  if (TOOL_INITIALIZED && lock_ids.contains((uintptr_t)mutex)) {
    lock_ids.remove((uintptr_t)mutex);
  }
}

///////////////////////////////////////////////////////////////////////////
// Interposers for Pthread locking routines

struct __cilkrts_worker;
extern "C" __cilkrts_worker *__cilkrts_get_tls_worker();

typedef int (*pthread_mutex_init_t)(pthread_mutex_t *,
                                    const pthread_mutexattr_t *);
static pthread_mutex_init_t dl_pthread_mutex_init = NULL;

CILKSAN_API int pthread_mutex_init(pthread_mutex_t *mutex,
                                   const pthread_mutexattr_t *attr) {
  START_DL_INTERPOSER(pthread_mutex_init, pthread_mutex_init_t);

  int result = dl_pthread_mutex_init(mutex, attr);
  if (TOOL_INITIALIZED) {
    lock_ids.insert((uintptr_t)mutex, next_lock_id++);
  }
  return result;
}

typedef int (*pthread_mutex_destroy_t)(pthread_mutex_t *);
static pthread_mutex_destroy_t dl_pthread_mutex_destroy = NULL;

CILKSAN_API int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  START_DL_INTERPOSER(pthread_mutex_destroy, pthread_mutex_destroy_t);

  int result = dl_pthread_mutex_destroy(mutex);
  if (lock_ids.contains((uintptr_t)mutex)) {
    lock_ids.remove((uintptr_t)mutex);
  }
  return result;
}

typedef int(*pthread_mutex_lockfn_t)(pthread_mutex_t *);
static pthread_mutex_lockfn_t dl_pthread_mutex_lock = NULL;
static pthread_mutex_lockfn_t dl_pthread_mutex_trylock = NULL;
static pthread_mutex_lockfn_t dl_pthread_mutex_unlock = NULL;

CILKSAN_API int pthread_mutex_lock(pthread_mutex_t *mutex) {
  START_DL_INTERPOSER(pthread_mutex_lock, pthread_mutex_lockfn_t);

  int result = dl_pthread_mutex_lock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (TOOL_INITIALIZED && __cilkrts_get_tls_worker() &&
      is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  START_DL_INTERPOSER(pthread_mutex_trylock, pthread_mutex_lockfn_t);

  int result = dl_pthread_mutex_trylock(mutex);
  // Only record the lock acquire if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (TOOL_INITIALIZED && __cilkrts_get_tls_worker() &&
      is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_acquire_lock(*lock_id);
  }
  return result;
}

CILKSAN_API int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  START_DL_INTERPOSER(pthread_mutex_unlock, pthread_mutex_lockfn_t);

  int result = dl_pthread_mutex_unlock(mutex);
  // Only record the lock release if the tool is initialized and this routine is
  // run on a Cilk worker.
  if (TOOL_INITIALIZED && __cilkrts_get_tls_worker() &&
      is_execution_parallel() && !result) {
    if (const LockID_t *lock_id = lock_ids.get((uintptr_t)mutex))
      CilkSanImpl.do_release_lock(*lock_id);
  }
  return result;
}

typedef int (*pthread_once_fn_t)(pthread_once_t *, void (*init_routine)(void));
static pthread_once_fn_t dl_pthread_once = NULL;

CILKSAN_API int pthread_once(pthread_once_t *once_control,
                             void (*init_routine)(void)) {
  START_DL_INTERPOSER(pthread_once, pthread_once_fn_t);

  // pthread_once ensures that the given function is run just once by any thread
  // in the process.  We simply disable instrumentation around the invocation.
  disable_checking();
  int result = dl_pthread_once(once_control, init_routine);
  enable_checking();
  return result;
}
