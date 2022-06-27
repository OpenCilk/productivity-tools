// -*- C++ -*-
#ifndef __FRAME_DATA_H__
#define __FRAME_DATA_H__

#include "csan.h"
#include "disjointset.h"
#include "spbag.h"

enum EntryType_t : uint8_t { SPAWNER = 1, HELPER = 2, DETACHER = 3 };
enum FrameType_t : uint8_t { SHADOW_FRAME = 1, FULL_FRAME = 2, LOOP_FRAME = 3 };

typedef struct Entry_t {
  enum EntryType_t entry_type;
  enum FrameType_t frame_type;
} Entry_t;

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
typedef struct FrameData_t {
  mutable bool Sbag_used = false;
  mutable bool Iterbag_used = false;
  Entry_t frame_data;
  unsigned num_Pbags = 0;
  SBag_t *Sbag = nullptr;
  PBag_t **Pbags = nullptr;
  SBag_t *Iterbag = nullptr;

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

  void set_Sbag_used(bool v = true) const { Sbag_used = v; }
  void set_Iterbag_used(bool v = true) const { Iterbag_used = v; }

  bool is_loop_frame() const {
    return (LOOP_FRAME == frame_data.frame_type);
  }

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
  bool check_parallel_iter(const SBag_t *LCA, uint16_t version) const {
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

} FrameData_t;

#endif // __FRAME_DATA_H__
