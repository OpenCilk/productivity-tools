#ifndef __DEBUG_UTIL_H__
#define __DEBUG_UTIL_H__

#include <cassert>

#ifndef CILKSAN_DEBUG
#ifdef _DEBUG
#define CILKSAN_DEBUG 1
#else
#define CILKSAN_DEBUG 0
#endif
#endif

#if CILKSAN_DEBUG
#define DISJOINTSET_DEBUG 0
#else
#define DISJOINTSET_DEBUG 0
#endif

// debug_level is a bitmap
enum debug_levels {
  DEBUG_BASIC = 0x1,
  DEBUG_BACKTRACE = 0x2,
  DEBUG_BAGS = 0x4,
  DEBUG_CALLBACK = 0x8,
  DEBUG_MEMORY = 0x10,
  DEBUG_DEQUE = 0x20,
  DEBUG_REDUCER = 0x40,
  DEBUG_DISJOINTSET = 0x80,
  DEBUG_STACK = 0x100,
  DEBUG_SHADOWMEM = 0x200,
};

#if CILKSAN_DEBUG
static int debug_level = 0; // DEBUG_BASIC | DEBUG_BAGS | DEBUG_CALLBACK | DEBUG_DISJOINTSET | DEBUG_MEMORY;
#else
static int debug_level = 0;
#endif

#if CILKSAN_DEBUG
#define WHEN_CILKSAN_DEBUG(stmt) do { stmt; } while(0)
#define cilksan_assert(c)                                               \
  do { if (!(c)) { die("%s:%d assertion failure: %s\n",                 \
                       __FILE__, __LINE__, #c);} } while (0)
#define cilksan_level_assert(level, c)                                         \
  if (debug_level & level)                                                     \
    do {                                                                       \
      if (!(c)) {                                                              \
        die("%s:%d assertion failure: %s\n", __FILE__, __LINE__, #c);          \
      }                                                                        \
  } while (0)
#else
#define WHEN_CILKSAN_DEBUG(stmt)
#define cilksan_assert(c)
#define cilksan_level_assert(level, c)
#endif

#if CILKSAN_DEBUG
// debugging assert to check that the tool is catching all the runtime events
// that are supposed to match up (i.e., has event begin and event end)
enum EventType_t { ENTER_FRAME = 1, ENTER_HELPER = 2, SPAWN_PREPARE = 3,
                   DETACH = 4, CILK_SYNC = 5, LEAVE_FRAME_OR_HELPER = 6,
                   RUNTIME_LOOP = 7, NONE = 8 };
#endif

__attribute__((noreturn))
void die(const char *fmt, ...);
void debug_printf(const char *fmt, ...);

#if CILKSAN_DEBUG
#define DBG_TRACE(level,...) if (debug_level & DEBUG_##level) { debug_printf(__VA_ARGS__); }
#else
#define DBG_TRACE(level,...)
#endif

#endif
