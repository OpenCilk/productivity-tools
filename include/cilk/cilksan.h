// -*- C++ -*-
#ifndef INCLUDED_CILK_CILKSAN_H
#define INCLUDED_CILK_CILKSAN_H

#ifdef __cplusplus

#define CILKSAN_EXTERN_C extern "C"
#define CILKSAN_NOTHROW noexcept

#else // #ifdef __cplusplus

#include <stdbool.h>

#define CILKSAN_EXTERN_C
#define CILKSAN_NOTHROW __attribute__((nothrow))

#endif // #ifdef __cplusplus

#ifdef __cilksan__

CILKSAN_EXTERN_C void __cilksan_enable_checking(void) CILKSAN_NOTHROW;
CILKSAN_EXTERN_C void __cilksan_disable_checking(void) CILKSAN_NOTHROW;
CILKSAN_EXTERN_C bool __cilksan_is_checking_enabled(void) CILKSAN_NOTHROW;

CILKSAN_EXTERN_C void __cilksan_acquire_lock(const void *mutex) CILKSAN_NOTHROW;
CILKSAN_EXTERN_C void __cilksan_release_lock(const void *mutex) CILKSAN_NOTHROW;
CILKSAN_EXTERN_C void __cilksan_begin_atomic() CILKSAN_NOTHROW;
CILKSAN_EXTERN_C void __cilksan_end_atomic() CILKSAN_NOTHROW;

CILKSAN_EXTERN_C void
__cilksan_register_lock_explicit(const void *mutex) CILKSAN_NOTHROW;
CILKSAN_EXTERN_C void
__cilksan_unregister_lock_explicit(const void *mutex) CILKSAN_NOTHROW;

#else // #ifdef __cilksan__

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
static inline void __cilksan_enable_checking(void) CILKSAN_NOTHROW {}
static inline void __cilksan_disable_checking(void) CILKSAN_NOTHROW {}
static inline bool __cilksan_is_checking_enabled(void) { return false; }

static inline void __cilksan_acquire_lock(const void *mutex) CILKSAN_NOTHROW {}
static inline void __cilksan_release_lock(const void *mutex) CILKSAN_NOTHROW {}
static inline void __cilksan_begin_atomic() CILKSAN_NOTHROW {}
static inline void __cilksan_end_atomic() CILKSAN_NOTHROW {}

static inline void
__cilksan_register_lock_explicit(const void *mutex) CILKSAN_NOTHROW {}
static inline void
__cilksan_unregister_lock_explicit(const void *mutex) CILKSAN_NOTHROW {}
#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // #ifdef __cilksan__

typedef struct Cilksan_fake_mutex {
  int fake_mutex;

#ifdef __cplusplus
  Cilksan_fake_mutex() {
    __cilksan_register_lock_explicit(this);
  }
  ~Cilksan_fake_mutex() {
    __cilksan_unregister_lock_explicit(this);
  }
#endif
} Cilksan_fake_mutex;

#ifdef __cplusplus

class Cilksan_fake_lock_guard {
  const void *_mutex;
  Cilksan_fake_lock_guard() = delete;

public:
  Cilksan_fake_lock_guard(const void *mutex) : _mutex(mutex) {
    __cilksan_acquire_lock(_mutex);
  }
  ~Cilksan_fake_lock_guard() {
    __cilksan_release_lock(_mutex);
  }
};

#endif

#endif // INCLUDED_CILK_CILKSAN_H
