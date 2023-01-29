// -*- C++ -*-
#ifndef __FRAME_DATA_H__
#define __FRAME_DATA_H__

#include "csan.h"
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
  bool InContin = false;
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

  void reset() {
    set_sbag(nullptr);
    clear_pbag_array();
    set_iterbag(nullptr);
  }

  FrameData_t() {}

  ~FrameData_t() {
    // update ref counts
    reset();
  }

  // Copy constructor and assignment operator ensure that reference
  // counts are properly maintained during resizing.
  FrameData_t(const FrameData_t &copy)
      : frame_data(copy.frame_data)
#if CILKSAN_DEBUG
        ,
        frame_id(copy.frame_id)
#endif
  {
    set_sbag(copy.Sbag);
    set_Sbag_used(copy.is_Sbag_used());
    copy_pbag_array(copy.num_Pbags, copy.Pbags);
    set_iterbag(copy.Iterbag);
    set_Iterbag_used(copy.is_Iterbag_used());
  }

  FrameData_t& operator=(const FrameData_t &copy) {
    frame_data = copy.frame_data;
#if CILKSAN_DEBUG
    frame_id = copy.frame_id;
#endif
    set_sbag(copy.Sbag);
    set_Sbag_used(copy.is_Sbag_used());
    copy_pbag_array(copy.num_Pbags, copy.Pbags);
    set_iterbag(copy.Iterbag);
    set_Iterbag_used(copy.is_Iterbag_used());
    return *this;
  }

  // remember to update this whenever new fields are added
  inline void init_new_function(SBag_t *_sbag) {
    cilksan_assert(Pbags == NULL);
    cilksan_assert(num_Pbags == 0);
    set_sbag(_sbag);
  }

  bool is_Sbag_used() const { return Sbag_used; }
  bool is_Iterbag_used() const { return Iterbag_used; }
  bool in_continuation() const { return InContin; }
  uint32_t get_parent_continuation() const { return ParentContin; }
  hyper_table *get_or_create_reducer_views() {
    if (!reducer_views)
      reducer_views = new hyper_table;
    return reducer_views;
  }

  void set_Sbag_used(bool v = true) const { Sbag_used = v; }
  void set_Iterbag_used(bool v = true) const { Iterbag_used = v; }
  void set_in_continuation(bool v = true) { InContin = v; }
  void enter_continuation() { InContin = true; }
  void exit_continuation() { InContin = false; }
  void set_parent_continuation(uint32_t c) { ParentContin = c; }
  void set_or_merge_reducer_views(hyper_table *right_table) {
    reducer_views =
        hyper_table::merge_two_hyper_tables(reducer_views, right_table);
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

};

#endif // __FRAME_DATA_H__
