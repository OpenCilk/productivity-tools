// -*- C++ -*-
#ifndef __FRAME_DATA_H__
#define __FRAME_DATA_H__

#include "disjointset.h"
#include "hypertable.h"
#include "spbag.h"

enum class EntryFrameType : uint8_t {
  NONE = 0,
  // Low two bits denote the entry type.
  SPAWNER = 1,
  HELPER = 2,
  DETACHER = 3,
  ENTRY_MASK = 3,
  // Next two bits denote the frame type
  SHADOW_FRAME = 1 << 2,
  FULL_FRAME = 2 << 2,
  LOOP_FRAME = 3 << 2,
  FRAME_MASK = 3 << 2,
  // Convenient combined values for entry and frame types.
  SPAWNER_SHADOW_FRAME = SPAWNER | SHADOW_FRAME,
  DETACHER_SHADOW_FRAME = DETACHER | SHADOW_FRAME,
};

static inline bool isSpawner(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::SPAWNER)) ==
         static_cast<int>(EntryFrameType::SPAWNER);
}
static inline bool isHelper(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::HELPER)) ==
         static_cast<int>(EntryFrameType::HELPER);
}
static inline bool isDetacher(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) & static_cast<int>(EntryFrameType::DETACHER)) ==
         static_cast<int>(EntryFrameType::DETACHER);
}
static inline bool isShadowFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::SHADOW_FRAME)) ==
         static_cast<int>(EntryFrameType::SHADOW_FRAME);
}
static inline bool isFullFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::FULL_FRAME)) ==
         static_cast<int>(EntryFrameType::FULL_FRAME);
}
static inline bool isLoopFrame(const EntryFrameType EFT) {
  return (static_cast<int>(EFT) &
          static_cast<int>(EntryFrameType::LOOP_FRAME)) ==
         static_cast<int>(EntryFrameType::LOOP_FRAME);
}
static inline EntryFrameType setSpawner(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::SPAWNER));
}
static inline EntryFrameType setHelper(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::HELPER));
}
static inline EntryFrameType setDetacher(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::ENTRY_MASK)) |
      static_cast<int>(EntryFrameType::DETACHER));
}
static inline EntryFrameType setShadowFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::SHADOW_FRAME));
}
static inline EntryFrameType setFullFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::FULL_FRAME));
}
static inline EntryFrameType setLoopFrame(const EntryFrameType EFT) {
  return EntryFrameType(
      (static_cast<int>(EFT) & ~static_cast<int>(EntryFrameType::FRAME_MASK)) |
      static_cast<int>(EntryFrameType::LOOP_FRAME));
}

// Helper method to create a new SBag.
static inline SBag_t *createNewSBag(uint64_t func_id, call_stack_t call_stack) {
  SBag_t *newSBag = new SBag_t(func_id);
  DisjointSet_t<call_stack_t> *newDS =
      new DisjointSet_t<call_stack_t>(call_stack, newSBag);
  (void)newDS;
  return newSBag;
}

// Helper method to create a new PBag.
static inline PBag_t *createNewPBag() {
  PBag_t *newPBag = new PBag_t();
  return newPBag;
}

// Struct for keeping track of shadow frame
struct FrameData_t {
  mutable bool Sbag_used = false;
  mutable bool Iterbag_used = false;
  EntryFrameType frame_data;
  // Whether the current instruction is in a continuation in this frame.
  uint8_t InContin = 0;
  // If this frame was called from the continuation of an ancestor, identifies
  // that ancestor.  Otherwise equals 0.
  uint32_t ParentContin = 0;
  // Pointers to bags
  unsigned num_Pbags = 0;
  SBag_t *Sbag = nullptr;
  PBag_t **Pbags = nullptr;
  SBag_t *Iterbag = nullptr;
  hyper_table *reducer_views = nullptr;

  // fields that are for debugging purpose only
#if CILKSAN_DEBUG
  uint64_t frame_id;
#endif

  void set_sbag(SBag_t *that) {
    if (Sbag)
      delete Sbag;
    Sbag = that;
    set_Sbag_used(false);
  }

  void set_pbag(unsigned idx, PBag_t *that) {
    cilksan_assert(idx < num_Pbags && "Invalid index");
    if (Pbags[idx])
      delete Pbags[idx];
    Pbags[idx] = that;
  }

  void set_iterbag(SBag_t *that) {
    if (Iterbag)
      delete Iterbag;
    Iterbag = that;
    set_Iterbag_used(false);
  }

  void clear_pbag_array() {
    if (!Pbags)
      return;

    for (unsigned i = 0; i < num_Pbags; ++i)
      set_pbag(i, nullptr);

    delete[] Pbags;
    Pbags = nullptr;
    num_Pbags = 0;
  }

  void make_pbag_array(unsigned num_pbags) {
    clear_pbag_array();
    Pbags = new PBag_t*[num_pbags];

    for (unsigned i = 0; i < num_pbags; ++i)
      Pbags[i] = nullptr;
    num_Pbags = num_pbags;
  }

  void copy_pbag_array(unsigned copy_num_Pbags, PBag_t **copy_Pbags) {
    clear_pbag_array();
    Pbags = copy_Pbags;
    num_Pbags = copy_num_Pbags;
  }

  // This function, not the FrameData_t destructor, is the primary way in which
  // frames are deinitialized.  Remember to update this function whenever new
  // fields are added.
  void reset() {
    set_sbag(nullptr);
    clear_pbag_array();
    set_iterbag(nullptr);
    InContin = 0;
    set_parent_continuation(0);
    // reducer_views = nullptr;
  }

  FrameData_t() = default;
  FrameData_t(const FrameData_t &copy) = delete;

  ~FrameData_t() {
    // update ref counts
    reset();
  }

  // This function, not the FrameData_t constructor, is the primary way in which
  // frames are initialized.  Remember to update this function whenever new
  // fields are added.
  inline void init_new_function(SBag_t *_sbag) {
    cilksan_assert(Pbags == NULL);
    cilksan_assert(num_Pbags == 0);
    set_sbag(_sbag);
  }

  bool is_Sbag_used() const { return Sbag_used; }
  bool is_Iterbag_used() const { return Iterbag_used; }
  bool in_continuation() const { return InContin != 0; }
  uint32_t get_parent_continuation() const { return ParentContin; }
  hyper_table *get_or_create_reducer_views() {
    if (!reducer_views)
      reducer_views = new hyper_table;
    return reducer_views;
  }

  void set_Sbag_used(bool v = true) const { Sbag_used = v; }
  void set_Iterbag_used(bool v = true) const { Iterbag_used = v; }
  // Bits of InContin identify different types of continuations:
  //   Bit 0 - the computation is in the continuation of a parallel loop.
  //   Bit x > 0 - the computation is in an ordinary continuation for a
  //     particular sync region.
  void enter_loop_continuation() { InContin |= 0x1; }
  void exit_loop_continuation() { InContin &= ~0x1; }
  void enter_continuation(const unsigned sync_reg) {
    cilksan_assert(sync_reg < 7 &&
                   "Error marking continuation.  Please report this issue.");
    InContin |= (0x2 << sync_reg);
  }
  void exit_continuation(const unsigned sync_reg) {
    cilksan_assert(sync_reg < 7 &&
                   "Error marking continuation.  Please report this issue.");
    InContin &= ~(0x2 << sync_reg);
  }
  void set_parent_continuation(uint32_t c) { ParentContin = c; }
  void set_or_merge_reducer_views(CilkSanImpl_t *__restrict__ tool,
                                  hyper_table *__restrict__ right_table) {
    reducer_views =
        hyper_table::merge_two_hyper_tables(tool, reducer_views, right_table);
  }

  bool is_loop_frame() const { return isLoopFrame(frame_data); }

  void create_iterbag() {
    cilksan_assert(is_loop_frame());
    const DisjointSet_t<call_stack_t> *SbagDS = Sbag->get_ds();
    SBag_t *newIterbag = createNewSBag(Sbag->get_func_id(), SbagDS->get_data());
    set_iterbag(newIterbag);
  }
  bool inc_version() {
    cilksan_assert(nullptr != Iterbag);
    return Iterbag->inc_version();
  }
  bool check_parallel_iter(const SBag_t *LCA, version_t version) const {
    if (!is_loop_frame())
      return false;
    return (LCA == Iterbag) && (version < LCA->get_version());
  }

  SBag_t *getSbagForAccess() const {
    if (!is_loop_frame()) {
      set_Sbag_used();
      return Sbag;
    }
    set_Iterbag_used();
    return Iterbag;
  }

  FrameData_t &operator=(FrameData_t &&that) {
    Sbag_used = that.Sbag_used;
    Iterbag_used = that.Iterbag_used;
    frame_data = that.frame_data;
    InContin = that.InContin;
    ParentContin = that.ParentContin;
    num_Pbags = that.num_Pbags;
    Sbag = that.Sbag;
    Pbags = that.Pbags;
    Iterbag = that.Iterbag;
    reducer_views = that.reducer_views;

    that.Sbag_used = false;
    that.Iterbag_used = false;
    that.InContin = 0;
    that.ParentContin = 0;
    that.num_Pbags = 0;
    that.Sbag = nullptr;
    that.Pbags = nullptr;
    that.Iterbag = nullptr;
    that.reducer_views = nullptr;

    return *this;
  }
};

#endif // __FRAME_DATA_H__
