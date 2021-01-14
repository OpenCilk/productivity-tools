// -*- C++ -*-
#ifndef __CHECKING_H__
#define __CHECKING_H__

#include "debug_util.h"

extern int checking_disabled;

static inline void enable_checking() {
  checking_disabled--;
  DBG_TRACE(DEBUG_BASIC, "%d: Enable checking.\n", checking_disabled);
  cilksan_assert(checking_disabled >= 0);
}

static inline void disable_checking() {
  cilksan_assert(checking_disabled >= 0);
  checking_disabled++;
  DBG_TRACE(DEBUG_BASIC, "%d: Disable checking.\n", checking_disabled);
}

// RAII object to disable checking in tool routines.
struct CheckingRAII {
  CheckingRAII() {
#if CILKSAN_DYNAMIC
    disable_checking();
#endif
  }
  ~CheckingRAII() {
#if CILKSAN_DYNAMIC
    enable_checking();
#endif
  }
};

#endif // __CHECKING_H__
