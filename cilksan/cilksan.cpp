#include "cilksan_internal.h"
#include "debug_util.h"
#include "disjointset.h"
#include "driver.h"
#include "frame_data.h"
#include "race_detect_update.h"
#include "simple_shadow_mem.h"
#include "spbag.h"
#include "stack.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>

// FILE io used to print error messages
FILE *err_io = stderr;

#if CILKSAN_DEBUG
enum EventType_t last_event = NONE;
#endif

// Flag to track whether Cilksan is initialized.
bool CILKSAN_INITIALIZED = false;

csi_id_t total_call = 0;
csi_id_t total_spawn = 0;
csi_id_t total_loop = 0;
csi_id_t total_load = 0;
csi_id_t total_store = 0;
csi_id_t total_alloca = 0;
csi_id_t total_allocfn = 0;
csi_id_t total_free = 0;

// declared in print_addr.cpp
extern uintptr_t *call_pc;
extern uintptr_t *load_pc;
extern uintptr_t *store_pc;

// Flag to globally enable/disable instrumentation.
bool instrumentation = false;

// Flag to check if Cilksan is running under RR.
bool is_running_under_rr = false;

// Reentrant flag for enabling/disabling instrumentation; 0 enables checking.
int checking_disabled = 0;

// Stack structure for tracking whether the current execution is parallel, i.e.,
// whether there are any unsynced spawns in the program execution.
Stack_t<uint8_t> parallel_execution;
Stack_t<bool> spbags_frame_skipped;

// Storage for old values of stack_low_addr and stack_high_addr, saved when
// entering a cilkified region.
uintptr_t uncilkified_stack_low_addr = (uintptr_t)-1;
uintptr_t uncilkified_stack_high_addr = 0;

// Stack for tracking whether a stack switch has occurred.
Stack_t<uint8_t> switched_stack;

// Stack structures for keeping track of MAAPs for pointer arguments to function
// calls.
Stack_t<std::pair<csi_id_t, MAAP_t>> MAAPs;
Stack_t<unsigned> MAAP_counts;

// Raise a link-time error if the user attempts to use Cilksan with -static.
//
// This trick is copied from AddressSanitizer.  We only use this trick on Linux,
// since static linking is not supported on MacOSX anyway.
#ifdef __linux__
#include <link.h>
void *CilksanDoesNotSupportStaticLinkage() {
  // This will fail to link with -static.
  return &_DYNAMIC;  // defined in link.h
}
#endif // __linux__

// --------------------- stuff from racedetector ---------------------------

// -------------------------------------------------------------------------
//  Analysis data structures and fields
// -------------------------------------------------------------------------

template <>
DisjointSet_t<call_stack_t>::DisjointSet_t(call_stack_t data, SBag_t *bag)
    : _parent_or_bag(bag), _data(data), _rank(0), _ref_count(0)
#if DISJOINTSET_DEBUG
      ,
      _ID(DS_ID++), _destructing(false)
#endif
{
  bag->set_ds(this);

  WHEN_DISJOINTSET_DEBUG(
      DBG_TRACE(DISJOINTSET, "Creating DS %ld for SBag %p\n", _ID, bag));
  WHEN_CILKSAN_DEBUG(debug_count++);
}

template <>
DisjointSet_t<call_stack_t>::DisjointSet_t(call_stack_t data, PBag_t *bag)
    : _parent_or_bag(bag), _data(data), _rank(0), _ref_count(0)
#if DISJOINTSET_DEBUG
      ,
      _ID(DS_ID++), _destructing(false)
#endif
{
  bag->set_ds(this);

  WHEN_DISJOINTSET_DEBUG(
      DBG_TRACE(DISJOINTSET, "Creating DS %ld for PBag %p\n", _ID, bag));
  WHEN_CILKSAN_DEBUG(debug_count++);
}

static_assert(alignof(DisjointSet_t<call_stack_t>) >= 8,
              "Bad alignment for DisjointSet_t structure.");

#if CILKSAN_DEBUG
template<>
long DisjointSet_t<call_stack_t>::debug_count = 0;

long SBag_t::debug_count = 0;
long PBag_t::debug_count = 0;
#endif

// Free lists for SBags and PBags
SBag_t::FreeNode_t *SBag_t::free_list = nullptr;
PBag_t::FreeNode_t *PBag_t::free_list = nullptr;

// Code to handle references to the stack.

// Range of stack used by the process
uintptr_t stack_low_addr = (uintptr_t)-1;
uintptr_t stack_high_addr = 0;

// Free list for call-stack nodes
call_stack_node_t *call_stack_node_t::free_list = nullptr;

// Global object to manage Cilksan data structures.
CilkSanImpl_t CilkSanImpl;

// Initialize custom memory allocators for dictionaries in shadow memory.
template <>
MALineAllocator &
    SimpleDictionary<0>::MAAlloc = CilkSanImpl.getMALineAllocator(0);
template <>
MALineAllocator &
    SimpleDictionary<1>::MAAlloc = CilkSanImpl.getMALineAllocator(1);
template <>
MALineAllocator &
    SimpleDictionary<2>::MAAlloc = CilkSanImpl.getMALineAllocator(2);

template <>
DisjointSet_t<call_stack_t>::DSAllocator &
    DisjointSet_t<call_stack_t>::Alloc = CilkSanImpl.getDSAllocator();

template <>
DisjointSet_t<call_stack_t>::List_t &
    DisjointSet_t<call_stack_t>::disjoint_set_list = CilkSanImpl.getDSList();

////////////////////////////////////////////////////////////////////////
// Events functions
////////////////////////////////////////////////////////////////////////

/// Helper function for merging returning child's bag into parent's
inline void
CilkSanImpl_t::merge_bag_from_returning_child(bool returning_from_detach,
                                              unsigned parent_sync_reg) {
  FrameData_t *parent = frame_stack.ancestor(1);
  FrameData_t *child = frame_stack.head();
  cilksan_assert(parent->Sbag);
  cilksan_assert(child->Sbag);

  if (returning_from_detach) {
    // We are returning from a detach.  Merge the child S- and P-bags
    // into the parent P-bag.

    // Get the parent P-bag.
    cilksan_assert(parent->Pbags && "No parent Pbags array");
    PBag_t *parent_pbag = parent->Pbags[parent_sync_reg];
    if (!parent_pbag) { // lazily create PBag when needed
      DBG_TRACE(BAGS, "frame %ld creates a PBag ", parent->Sbag->get_func_id());
      parent_pbag = createNewPBag();
      parent->set_pbag(parent_sync_reg, parent_pbag);
      DBG_TRACE(BAGS, "%p\n", parent_pbag);
    }

    // Combine child S-bag into parent P-bag.
    if (child->is_Sbag_used()) {
      DBG_TRACE(
          BAGS,
          "Merge S-bag from detached child %ld to P-bag %d from parent %ld.\n",
          child->Sbag->get_func_id(), parent_sync_reg,
          parent->Sbag->get_func_id());
      parent_pbag->combine(child->Sbag);
      // parent->set_Pbag_used();
      // The child entry of frame_stack keeps child->Sbag alive until its bags
      // are reset at the end of this function.
    }

    // Combine child P-bag into parent P-bag.
    if (child->Pbags) {
      for (unsigned i = 0; i < child->num_Pbags; ++i) {
        if (child->Pbags[i]) {
          // if (child->is_Pbag_used()) {
          DBG_TRACE(BAGS,
                    "Merge P-bag %d from spawned child %ld to P-bag %d from "
                    "parent %ld.\n",
                    i, child->Sbag->get_func_id(), parent_sync_reg,
                    parent->Sbag->get_func_id());
          parent_pbag->combine(child->Pbags[i]);
          // parent->set_Pbag_used();
          // }
        }
      }
    }

  } else {
    // We are returning from a call.  Merge the child S-bag into the
    // parent S-bag, and merge the child P-bags into the parent P-bag.
    // cilksan_assert(parent->Sbag->get_set_node()->is_SBag());
    if (child->is_Sbag_used()) {
      DBG_TRACE(BAGS,
                "Merge S-bag from called child %ld to S-bag from parent %ld.\n",
                child->Sbag->get_func_id(), parent->Sbag->get_func_id());
      parent->Sbag->combine(child->Sbag);
      parent->set_Sbag_used();
    }

    // Test if we need to merge child Pbags into the parent.
    bool merge_child_pbags = false;
    if (child->Pbags) {
      for (unsigned i = 0; i < child->num_Pbags; ++i) {
        if (child->Pbags[i]) {
          merge_child_pbags = true;
          break;
        }
      }
    }

    if (__builtin_expect(merge_child_pbags, false)) {
      // Get the parent P-bag.
      cilksan_assert(parent->Pbags && "No parent Pbags array");
      PBag_t *parent_pbag = parent->Pbags[parent_sync_reg];
      if (!parent_pbag) { // lazily create PBag when needed
        DBG_TRACE(BAGS, "frame %ld creates a PBag ",
                  parent->Sbag->get_func_id());
        parent_pbag = createNewPBag();
        parent->set_pbag(parent_sync_reg, parent_pbag);
        DBG_TRACE(BAGS, "%p\n", parent_pbag);
      }
      cilksan_assert(parent_pbag);
      for (unsigned i = 0; i < child->num_Pbags; ++i) {
        // Combine child P-bags into parent P-bag.
        // if (child->is_Pbag_used()) {
        DBG_TRACE(BAGS,
                  "Merge P-bag %d from called child %ld to P-bag %d from "
                  "parent %ld.\n",
                  i, child->frame_id, parent_sync_reg,
                  parent->Sbag->get_func_id());
        parent_pbag->combine(child->Pbags[i]);
        // parent->set_Pbag_used();
        // }
      }
    }
  }
  DBG_TRACE(BAGS, "After merge, parent set node func id: %ld.\n",
            parent->Sbag->get_func_id());

  // Reset child's bags.
  child->set_sbag(NULL);
  // child->set_pbag(NULL);
  child->clear_pbag_array();
}

/// Helper function for handling the start of a new function.  This
/// function can be a spawned or called Cilk function or a spawned C
/// function.  A called C function is treated as inlined.
inline void CilkSanImpl_t::start_new_function(unsigned num_sync_reg) {
  frame_id++;
  frame_stack.push();

  DBG_TRACE(CALLBACK, "Enter frame %ld, ", frame_id);

  // Get the parent pointer after we push, because once pused, the
  // pointer may no longer be valid due to resize.
  FrameData_t *parent = frame_stack.ancestor(1);
  WHEN_CILKSAN_DEBUG(
      { DBG_TRACE(CALLBACK, "parent frame %ld.\n", parent->frame_id); });
  SBag_t *child_sbag;

  FrameData_t *child = frame_stack.head();
  cilksan_assert(child->Sbag == NULL);
  cilksan_assert(child->Pbags == NULL);
  cilksan_assert(child->num_Pbags == 0);

  DBG_TRACE(BAGS, "Creating SBag for frame %ld\n", frame_id);
  child_sbag = createNewSBag(frame_id, call_stack);

  child->init_new_function(child_sbag);

  if (parent->in_continuation())
    child->set_parent_continuation(1);
  else {
    uint32_t parent_contin = parent->get_parent_continuation();
    if (parent_contin > 0)
      child->set_parent_continuation(parent_contin + 1);
    else
      child->set_parent_continuation(0);
  }
  cilksan_assert(!child->in_continuation() &&
                 "New function marked as in-continuation");
  cilksan_assert(child->reducer_views == nullptr &&
                 "New function has non-null table of reducer views");

  if (num_sync_reg > 0) {
    DBG_TRACE(BAGS, "Creating PBag array of size %d for frame %ld\n",
              num_sync_reg, frame_id);
    child->make_pbag_array(num_sync_reg);
  } else {
    DBG_TRACE(BAGS, "Skipping PBag-array creation for frame %ld\n", frame_id);
  }

  // We do the assertion after the init so that ref_count is 1.

  WHEN_CILKSAN_DEBUG(frame_stack.head()->frame_id = frame_id);

  DBG_TRACE(CALLBACK, "Enter function id %ld\n", frame_id);
}

/// Helper function for exiting a function; counterpart of start_new_function.
inline void CilkSanImpl_t::exit_function() {
  // Popping doesn't actually destruct the object so we need to
  // manually dec the ref counts here.
  frame_stack.head()->reset();
  frame_stack.pop();
}

/// Action performed on entering a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::enter_cilk_function(unsigned num_sync_reg) {
  DBG_TRACE(CALLBACK, "entering a Cilk function, push frame_stack\n");
  start_new_function(num_sync_reg);
}

/// Action performed on leaving a Cilk function (excluding spawn helper).
inline void CilkSanImpl_t::leave_cilk_function(unsigned sync_reg) {
  DBG_TRACE(CALLBACK,
            "leaving a Cilk function (spawner or helper), pop frame_stack\n");

  // param: not returning from a spawn, sync region 0.
  merge_bag_from_returning_child(0, sync_reg);
  exit_function();
}

/// Action performed when returning from a spawned child.
/// (That is, returning from a spawn helper.)
inline void CilkSanImpl_t::return_from_detach(unsigned sync_reg) {
  DBG_TRACE(CALLBACK, "return from detach, pop frame_stack\n");
  cilksan_assert(isDetacher(frame_stack.head()->frame_data));

  // param: we are returning from a spawn
  merge_bag_from_returning_child(1, sync_reg);
  exit_function();
}

/// Action performed immediately after passing a sync.
inline void CilkSanImpl_t::complete_sync(unsigned sync_reg) {
  FrameData_t *f = frame_stack.head();
  DBG_TRACE(CALLBACK, "frame %d done sync\n", f->Sbag->get_func_id());

  // Pbag could be NULL if we encounter a sync without any spawn (i.e., any Cilk
  // function that executes the base case)
  cilksan_assert(sync_reg < f->num_Pbags && "Invalid sync_reg");
  cilksan_assert(f->Pbags && "Cannot sync NULL pbags array");
  if (f->Pbags[sync_reg]) {
    DBG_TRACE(BAGS, "Merge P-bag %d (%p) in frame %ld into S-bag.\n",
              sync_reg, f->Pbags[sync_reg], f->Sbag->get_func_id());
    // if (f->is_Pbag_used()) {
    f->Sbag->combine(f->Pbags[sync_reg]);
    f->set_Sbag_used();
    // }
    f->set_pbag(sync_reg, NULL);
  }
}

//---------------------------------------------------------------
// Callback functions
//---------------------------------------------------------------
void CilkSanImpl_t::do_enter(unsigned num_sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == NONE));
  WHEN_CILKSAN_DEBUG(last_event = ENTER_FRAME);
  DBG_TRACE(CALLBACK, "frame %ld cilk_enter_frame_begin, stack depth %d\n",
            frame_id + 1, frame_stack.size());
  enter_cilk_function(num_sync_reg);
  frame_stack.head()->frame_data = EntryFrameType::SPAWNER_SHADOW_FRAME;

  WHEN_CILKSAN_DEBUG(
      cilksan_assert(last_event == ENTER_FRAME || last_event == ENTER_HELPER));
  WHEN_CILKSAN_DEBUG(last_event = NONE);
  DBG_TRACE(CALLBACK, "cilk_enter_end\n");
}

void CilkSanImpl_t::do_enter_helper(unsigned num_sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "frame %ld cilk_enter_helper_begin\n", frame_id + 1);
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == NONE));
  WHEN_CILKSAN_DEBUG(last_event = ENTER_HELPER;);

  enter_cilk_function(num_sync_reg);
  frame_stack.head()->frame_data = EntryFrameType::DETACHER_SHADOW_FRAME;

  WHEN_CILKSAN_DEBUG(
      cilksan_assert(last_event == ENTER_FRAME || last_event == ENTER_HELPER));
  WHEN_CILKSAN_DEBUG(last_event = NONE);
  DBG_TRACE(CALLBACK, "cilk_enter_end\n");
}

void CilkSanImpl_t::do_detach() {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == NONE));
  WHEN_CILKSAN_DEBUG(last_event = DETACH);

  update_strand_stats();
  shadow_memory->clearOccupied();

  DBG_TRACE(CALLBACK, "cilk_detach\n");

  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == DETACH));
  WHEN_CILKSAN_DEBUG(last_event = NONE);

  // At this point, the frame_stack.head is still the parent (spawning) frame
  WHEN_CILKSAN_DEBUG({
    FrameData_t *parent = frame_stack.head();
    DBG_TRACE(CALLBACK, "frame %ld about to spawn.\n",
              parent->Sbag->get_func_id());
  });
}

void CilkSanImpl_t::do_detach_continue(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "cilk_detach_continue\n");

  reduce_local_views();
  update_strand_stats();
  shadow_memory->clearOccupied();
  frame_stack.head()->enter_continuation(sync_reg);
}

void CilkSanImpl_t::do_loop_iteration_begin(unsigned num_sync_reg) {
  DBG_TRACE(CALLBACK, "do_loop_iteration_begin()\n");
  if (start_new_loop) {
    // The first time we enter the loop, create a LOOP_FRAME at the head of
    // frame_stack.
    DBG_TRACE(CALLBACK, "starting new loop\n");
    // Start a new frame.
    do_enter_helper(num_sync_reg > 0 ? num_sync_reg : 1);
    // Set this frame's type as LOOP_FRAME.
    FrameData_t *func = frame_stack.head();
    func->frame_data = setLoopFrame(func->frame_data);
    // Create a new iteration bag for this frame.
    DBG_TRACE(BAGS, "frame %ld creates an Iter-bag ",
              func->Sbag->get_func_id());
    func->create_iterbag();
    DBG_TRACE(BAGS, "%p\n", func->Iterbag);
    // Finish initializing the frame.
    do_detach();
    start_new_loop = false;
  } else {
    cilksan_assert(in_loop());
    update_strand_stats();
    shadow_memory->clearOccupied();
    frame_stack.head()->enter_loop_continuation();
  }
}

void CilkSanImpl_t::do_loop_iteration_end() {
  reduce_local_views();
  update_strand_stats();
  shadow_memory->clearOccupied();
  frame_stack.head()->exit_loop_continuation();

  // At the end of each iteration, update the LOOP_FRAME for reuse.
  DBG_TRACE(CALLBACK, "do_loop_iteration_end()\n");
  FrameData_t *func = frame_stack.head();
  cilksan_assert(in_loop());
  // Get this frame's P-bag, creating it if necessary.
  PBag_t *my_pbag = func->Pbags[0];
  if (!my_pbag) {
    DBG_TRACE(BAGS, "frame %ld creates a P-bag ", func->Sbag->get_func_id());
    my_pbag = createNewPBag();
    func->set_pbag(0, my_pbag);
    DBG_TRACE(BAGS, "%p\n", my_pbag);
  }
  cilksan_assert(my_pbag);

  // Merge the S-bag into the P-bag.
  SBag_t *my_sbag = func->Sbag;
  uint64_t func_id = my_sbag->get_func_id();
  if (func->is_Sbag_used()) {
    DBG_TRACE(BAGS, "Merge S-bag in loop frame %ld into P-bag.\n", func_id);
    my_pbag->combine(my_sbag);
    // func->set_Pbag_used();

    // Create a new S-bag for the frame.
    DBG_TRACE(BAGS, "frame %ld creates an S-bag ", func_id);
    func->set_sbag(createNewSBag(func_id, call_stack));
    DBG_TRACE(BAGS, "%p\n", func->Sbag);
  }

  // Increment the Iter-frame.
  if (!func->inc_version()) {
    // Combine the Iter-bag into this P-bag.
    if (func->is_Iterbag_used()) {
      DBG_TRACE(BAGS, "Merge Iter-bag in loop frame %ld into P-bag.\n",
                func_id);
      my_pbag->combine(func->Iterbag);
      // func->set_Pbag_used();

      // Create a new Iter-bag.
      DBG_TRACE(BAGS, "frame %ld creates an Iter-bag ", func_id);
      func->create_iterbag();
      DBG_TRACE(BAGS, "%p\n", func->Iterbag);
    }
  }
}

void CilkSanImpl_t::do_loop_end(unsigned sync_reg) {
  DBG_TRACE(CALLBACK, "do_loop_end()\n");
  FrameData_t *func = frame_stack.head();
  cilksan_assert(in_loop());
  // Get this frame's P-bag, creating it if necessary.
  PBag_t *my_pbag = func->Pbags[0];
  if (!my_pbag) {
    DBG_TRACE(BAGS, "frame %ld creates a P-bag ", func->Sbag->get_func_id());
    my_pbag = createNewPBag();
    func->set_pbag(0, my_pbag);
    DBG_TRACE(BAGS, "%p\n", my_pbag);
  }
  cilksan_assert(my_pbag);

  // Combine the Iter-bag into this P-bag.
  if (func->is_Iterbag_used()) {
    DBG_TRACE(BAGS, "Merge Iter-bag in loop frame %ld into P-bag.\n",
              my_pbag->get_func_id());
    my_pbag->combine(func->Iterbag);
    // func->set_Pbag_used();
  }
  // The loop frame is done, so clear the Iter-bag.
  func->set_iterbag(nullptr);

  // Return from the frame.
  do_leave(sync_reg);
}

void CilkSanImpl_t::do_sync(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(CALLBACK, "frame %ld cilk_sync_begin\n",
            frame_stack.head()->Sbag->get_func_id());
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == NONE));
  WHEN_CILKSAN_DEBUG(last_event = CILK_SYNC);

  update_strand_stats();
  shadow_memory->clearOccupied();

  DBG_TRACE(CALLBACK, "cilk_sync_end\n");
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == CILK_SYNC));
  WHEN_CILKSAN_DEBUG(last_event = NONE);

  reduce_local_views();
  complete_sync(sync_reg);
  frame_stack.head()->exit_continuation(sync_reg);
}

void CilkSanImpl_t::do_leave(unsigned sync_reg) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == NONE));
  WHEN_CILKSAN_DEBUG(last_event = LEAVE_FRAME_OR_HELPER);
  DBG_TRACE(CALLBACK, "frame %ld cilk_leave_begin\n",
            frame_stack.head()->frame_id);
  cilksan_assert(frame_stack.size() > 1);

  EntryFrameType EFT = frame_stack.head()->frame_data;
  if (isSpawner(EFT)) {
    DBG_TRACE(CALLBACK, "cilk_leave_frame_begin\n");
  } else if (isHelper(EFT)) {
    DBG_TRACE(CALLBACK, "cilk_leave_helper_begin\n");
  } else if (isDetacher(EFT)) {
    DBG_TRACE(CALLBACK, "cilk_leave_begin from detach\n");
  }

  if (isDetacher(EFT))
    return_from_detach(sync_reg);
  else
    leave_cilk_function(sync_reg);

  DBG_TRACE(CALLBACK, "cilk_leave_end\n");
  WHEN_CILKSAN_DEBUG(cilksan_assert(last_event == LEAVE_FRAME_OR_HELPER));
  WHEN_CILKSAN_DEBUG(last_event = NONE);
}

// called by do_read and do_write.
template <bool is_read, MAType_t type>
__attribute__((always_inline)) void
CilkSanImpl_t::record_mem_helper(const csi_id_t acc_id, uintptr_t addr,
                                 size_t mem_size, unsigned alignment) {
  // Do nothing for 0-byte accesses
  if (!mem_size)
    return;

  // Use fast path for small, statically aligned accesses.
  if (alignment && mem_size <= alignment &&
      alignment <= (1 << SimpleShadowMem::getLgSmallAccessSize())) {
    // We're committed to using the fast-path check.  Update the occupied bits,
    // and if that process discovers unoccupied entries, perform the check.
    if (shadow_memory->setOccupiedFast(is_read, addr, mem_size)) {
      FrameData_t *f = frame_stack.head();
      check_races_and_update_fast<is_read>(acc_id, type, addr, mem_size, f,
                                           *shadow_memory);
    }
    // Return early.
    return;
  }

  FrameData_t *f = frame_stack.head();
  check_races_and_update<is_read>(acc_id, type, addr, mem_size, f,
                                  *shadow_memory);
}

// called by do_locked_read and do_locked_write.
template <bool is_read, MAType_t type>
void CilkSanImpl_t::record_locked_mem_helper(const csi_id_t acc_id,
                                             uintptr_t addr, size_t mem_size,
                                             unsigned alignment) {
  // Do nothing for 0-byte accesses
  if (!mem_size)
    return;

  // TODO: Add a fast path for handling locked accesses.

  // // Use fast path for small, statically aligned accesses.
  // if (alignment && mem_size <= alignment &&
  //     alignment <= (1 << SimpleShadowMem::getLgSmallAccessSize())) {
  //   // We're committed to using the fast-path check.  Update the occupied bits,
  //   // and if that process discovers unoccupied entries, perform the check.
  //   if (shadow_memory->setOccupiedFast(is_read, addr, mem_size)) {
  //     FrameData_t *f = frame_stack.head();
  //     check_races_and_update_fast<is_read>(acc_id, type, addr, mem_size, f,
  //                                          *shadow_memory);
  //   }
  //   // Return early.
  //   return;
  // }

  FrameData_t *f = frame_stack.head();
  check_data_races_and_update<is_read>(acc_id, type, addr, mem_size, f, lockset,
                                       *shadow_memory);
}

void CilkSanImpl_t::record_free(uintptr_t addr, size_t mem_size,
                                csi_id_t acc_id, MAType_t type) {
  // Do nothing for 0-byte frees
  if (!mem_size)
    return;

  FrameData_t *f = frame_stack.head();
  if (locks_held()) {
    check_data_races_and_update<false>(acc_id, type, addr, mem_size, f,
                                       lockset, *shadow_memory);
  } else {
    check_races_and_update<false>(acc_id, type, addr, mem_size, f,
                                  *shadow_memory);
  }
}

// Check races on memory [addr, addr+mem_size) with this read access.  Once done
// checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory) {
  shadow_memory.update_with_read(acc_id, type, addr, mem_size, f);
  shadow_memory.check_race_with_prev_write<true>(acc_id, type, addr, mem_size,
                                                 f);
}

// Check races on memory [addr, addr+mem_size) with this write access.  Once
// done checking, update shadow_memory with this new read access.  Very similar
// to check_races_and_update_with_read function.
__attribute__((always_inline)) void check_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory) {
  shadow_memory.check_and_update_write(acc_id, type, addr, mem_size, f);
  shadow_memory.check_race_with_prev_read(acc_id, type, addr, mem_size, f);
}

// Check races on memory [addr, addr+mem_size) with this memory access.  Once
// done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_races_and_update(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size, FrameData_t *f,
                            SimpleShadowMem &shadow_memory) {
  // Set the occupancy bits in the shadow memory, to deduplicate memory accesses
  // in the same strand at runtime.  If we find that all occupancy bits for
  // [addr, addr+mem_size) are already set, then this access is redundant with a
  // previous access in the same strand, and we can quit early.
  if (!shadow_memory.setOccupied(is_read, addr, mem_size))
    return;

  if (is_read)
    check_races_and_update_with_read(acc_id, type, addr, mem_size, f,
                                     shadow_memory);
  else
    check_races_and_update_with_write(acc_id, type, addr, mem_size, f,
                                      shadow_memory);
}

// Fast-path check for races on memory [addr, addr+mem_size) with this memory
// access.  Once done checking, update shadow_memory with the new access.
// Assumes that mem_size is small and addr is aligned based on mem_size.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
__attribute__((always_inline)) void
check_races_and_update_fast(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size, FrameData_t *f,
                            SimpleShadowMem &shadow_memory) {
  if (is_read)
    shadow_memory.check_read_fast(acc_id, type, addr, mem_size, f);
  else
    shadow_memory.check_write_fast(acc_id, type, addr, mem_size, f);
}

// Check data races on memory [addr, addr+mem_size) with this read access.  Once
// done checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_data_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory) {
  shadow_memory.update_with_read(acc_id, type, addr, mem_size, f);
  shadow_memory.update_lockers_with_read(acc_id, type, addr, mem_size, f,
                                         lockset);
  shadow_memory.check_data_race_with_prev_write<true>(acc_id, type, addr,
                                                      mem_size, f, lockset);
}

// Check data races on memory [addr, addr+mem_size) with this write access.
// Once done checking, update shadow_memory with this new read access.  Very
// similar to check_data_races_and_update_with_read function.
__attribute__((always_inline)) void check_data_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory) {
  shadow_memory.check_data_race_and_update_write(acc_id, type, addr, mem_size,
                                                 f, lockset);
  shadow_memory.check_data_race_with_prev_read(acc_id, type, addr, mem_size, f,
                                               lockset);
}

// Check for data races on memory [addr, addr+mem_size) with this memory access.
// Once done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// lockset: set of currently held locks
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_data_races_and_update(const csi_id_t acc_id, MAType_t type,
                                 uintptr_t addr, size_t mem_size, FrameData_t *f,
                                 const LockSet_t &lockset,
                                 SimpleShadowMem &shadow_memory) {
  // Set the occupancy bits in the shadow memory, to deduplicate memory accesses
  // in the same strand at runtime.  If we find that all occupancy bits for
  // [addr, addr+mem_size) are already set, then this access is redundant with a
  // previous access in the same strand, and we can quit early.
  if (!shadow_memory.setOccupied(is_read, addr, mem_size))
    return;

  if (is_read)
    check_data_races_and_update_with_read(acc_id, type, addr, mem_size, f,
                                          lockset, shadow_memory);
  else
    check_data_races_and_update_with_write(acc_id, type, addr, mem_size, f,
                                           lockset, shadow_memory);
}

template <MAType_t type>
void CilkSanImpl_t::do_read(const csi_id_t load_id, uintptr_t addr,
                            size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(MEMORY, "record read %lu: %lu bytes at addr %p and rip %p.\n",
            load_id, mem_size, addr,
            (load_id != UNKNOWN_CSI_ID) ? load_pc[load_id] : 0);
  if (collect_stats)
    collect_read_stat(mem_size);

  bool on_stack = is_on_stack(addr);
  if (on_stack)
    advance_stack_frame(addr);

  record_mem_helper<true, type>(load_id, addr, mem_size, alignment);
}

template <MAType_t type>
void CilkSanImpl_t::do_write(const csi_id_t store_id, uintptr_t addr,
                             size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(MEMORY, "record write %ld: %lu bytes at addr %p and rip %p.\n",
            store_id, mem_size, addr, store_pc[store_id]);
  if (collect_stats)
    collect_write_stat(mem_size);

  bool on_stack = is_on_stack(addr);
  if (on_stack)
    advance_stack_frame(addr);

  record_mem_helper<false, type>(store_id, addr, mem_size, alignment);
}

template void CilkSanImpl_t::do_read<MAType_t::RW>(const csi_id_t id,
                                                   uintptr_t addr, size_t len,
                                                   unsigned alignment);
template void CilkSanImpl_t::do_read<MAType_t::FNRW>(const csi_id_t id,
                                                     uintptr_t addr, size_t len,
                                                     unsigned alignment);
template void CilkSanImpl_t::do_read<MAType_t::ALLOC>(const csi_id_t id,
                                                      uintptr_t addr,
                                                      size_t len,
                                                      unsigned alignment);

template void CilkSanImpl_t::do_write<MAType_t::RW>(const csi_id_t id,
                                                    uintptr_t addr, size_t len,
                                                    unsigned alignment);
template void CilkSanImpl_t::do_write<MAType_t::FNRW>(const csi_id_t id,
                                                      uintptr_t addr,
                                                      size_t len,
                                                      unsigned alignment);
template void CilkSanImpl_t::do_write<MAType_t::ALLOC>(const csi_id_t id,
                                                       uintptr_t addr,
                                                       size_t len,
                                                       unsigned alignment);

template <MAType_t type>
void CilkSanImpl_t::do_locked_read(const csi_id_t load_id, uintptr_t addr,
                                   size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(MEMORY,
            "record read %lu: %lu bytes at addr %p and rip %p, locked.\n",
            load_id, mem_size, addr,
            (load_id != UNKNOWN_CSI_ID) ? load_pc[load_id] : 0);
  if (collect_stats)
    collect_read_stat(mem_size);

  bool on_stack = is_on_stack(addr);
  if (on_stack)
    advance_stack_frame(addr);

  record_locked_mem_helper<true, type>(load_id, addr, mem_size, alignment);
}

template <MAType_t type>
void CilkSanImpl_t::do_locked_write(const csi_id_t store_id, uintptr_t addr,
                                    size_t mem_size, unsigned alignment) {
  WHEN_CILKSAN_DEBUG(cilksan_assert(CILKSAN_INITIALIZED));
  DBG_TRACE(MEMORY,
            "record write %ld: %lu bytes at addr %p and rip %p, locked.\n",
            store_id, mem_size, addr, store_pc[store_id]);
  if (collect_stats)
    collect_write_stat(mem_size);

  bool on_stack = is_on_stack(addr);
  if (on_stack)
    advance_stack_frame(addr);

  record_locked_mem_helper<false, type>(store_id, addr, mem_size, alignment);
}

template void CilkSanImpl_t::do_locked_read<MAType_t::RW>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_read<MAType_t::FNRW>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_read<MAType_t::ALLOC>(
    const csi_id_t load_id, uintptr_t addr, size_t len, unsigned alignment);

template void CilkSanImpl_t::do_locked_write<MAType_t::RW>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_write<MAType_t::FNRW>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);
template void CilkSanImpl_t::do_locked_write<MAType_t::ALLOC>(
    const csi_id_t store_id, uintptr_t addr, size_t len, unsigned alignment);

// clear the memory block at [start,start+size) (end is exclusive).
void CilkSanImpl_t::clear_shadow_memory(size_t start, size_t size) {
  if (!size)
    return;
  DBG_TRACE(MEMORY, "cilksan_clear_shadow_memory(%p, %ld)\n", start, size);
  shadow_memory->clear(start, size);
}

void CilkSanImpl_t::record_alloc(size_t start, size_t size,
                                 csi_id_t alloca_id) {
  if (!size)
    return;
  DBG_TRACE(MEMORY, "cilksan_record_alloc(%p, %ld)\n", start, size);
  FrameData_t *f = frame_stack.head();
  shadow_memory->record_alloc(start, size, f, alloca_id);
}

void CilkSanImpl_t::clear_alloc(size_t start, size_t size) {
  if (!size)
    return;
  DBG_TRACE(MEMORY, "cilksan_clear_alloc(%p, %ld)\n", start, size);
  shadow_memory->clear_alloc(start, size);
}

inline void CilkSanImpl_t::print_stats() {
  std::cout << ",size (bytes),count\n";

  for (std::pair<size_t, uint64_t> reads : num_reads_checked)
    std::cout << "reads," << reads.first << "," << reads.second << "\n";
  std::cout << "total reads,," << total_reads_checked << "\n";

  for (std::pair<size_t, uint64_t> writes : num_writes_checked)
    std::cout << "writes," << writes.first << "," << writes.second << "\n";
  std::cout << "total writes,," << total_writes_checked << "\n";

  std::cout << "total strands,," << strand_count << "\n";

  for (std::pair<size_t, uint64_t> reads : max_num_reads_checked)
    std::cout << "max reads," << reads.first << "," << reads.second << "\n";

  for (std::pair<size_t, uint64_t> writes : max_num_writes_checked)
    std::cout << "max writes," << writes.first << "," << writes.second << "\n";
}

///////////////////////////////////////////////////////////////////////////
// Tool initialization and deinitialization

void CilkSanImpl_t::deinit() {
  static bool deinit = false;
  if (!deinit)
    deinit = true;
  else
    return; // deinit-ed already

  print_race_report();
  // Optionally print statistics.
  if (collect_stats)
    print_stats();

  // Remove references to the disjoint set nodes so they can be freed.
  // We expect just 1 frame on the stack at this point, unless the
  // program terminated from within a function, e.g., by calling
  // exit().
  bool foundNonemptyPBags = false;
  // Return from all functions still on the stack.
  while (frame_stack.size() > 1) {
    FrameData_t *f = frame_stack.head();
    // Check if there are any nonempty PBags in this frame.
    if (f->Pbags) {
      // We found nonempty PBags at program termination.  This
      // typically should not happen unless the program exits from an
      // unusual place, e.g., by calling exit() in a continuation.
      if (!foundNonemptyPBags) {
        std::cerr << "WARNING: Unsynchronized spawns detected!  Did the "
                     "program terminate early?\n";
        foundNonemptyPBags = true;
      }

      // Synchronize all Pbags.
      for (unsigned sync_reg = 0; sync_reg < f->num_Pbags; ++sync_reg)
        do_sync(sync_reg);
    }

    // Return from the function.
    do_leave(0);
  }

  // Free the shadow memory
  if (shadow_memory) {
    delete shadow_memory;
    shadow_memory = nullptr;
  }

  // Cleanup final frame.
  frame_stack.head()->reset();
  frame_stack.pop();
  cilksan_assert(frame_stack.size() == 0);

  WHEN_CILKSAN_DEBUG({
      if (DisjointSet_t<call_stack_t>::debug_count != 0)
        std::cerr << "DisjointSet_t<call_stack_t>::debug_count = "
                  << DisjointSet_t<call_stack_t>::debug_count << "\n";
      if (SBag_t::debug_count != 0)
        std::cerr << "SBag_t::debug_count = "
                  << SBag_t::debug_count << "\n";
      if (PBag_t::debug_count != 0)
        std::cerr << "PBag_t::debug_count = "
                  << PBag_t::debug_count << "\n";
    });

  // Free the call-stack nodes in the free list.
  call_stack_node_t::cleanup_freelist();

  // Free the free lists for SBags and PBags.
  SBag_t::cleanup_freelist();
  PBag_t::cleanup_freelist();

  DisjointSet_t<call_stack_t>::cleanup();
}

// called upon process exit
static void csan_destroy(void) {
  disable_instrumentation();
  disable_checking();
  CilkSanImpl.deinit();
  fflush(stdout);
  if (call_pc) {
    free(call_pc);
    call_pc = nullptr;
  }
  if (spawn_pc) {
    free(spawn_pc);
    spawn_pc = nullptr;
  }
  if (loop_pc) {
    free(loop_pc);
    loop_pc = nullptr;
  }
  if (load_pc) {
    free(load_pc);
    load_pc = nullptr;
  }
  if (store_pc) {
    free(store_pc);
    store_pc = nullptr;
  }
  if (alloca_pc) {
    free(alloca_pc);
    alloca_pc = nullptr;
  }
  if (allocfn_pc) {
    free(allocfn_pc);
    allocfn_pc = nullptr;
  }
  if (allocfn_prop) {
    free(allocfn_prop);
    allocfn_prop = nullptr;
  }
  if (free_pc) {
    free(free_pc);
    free_pc = nullptr;
  }
}

CilkSanImpl_t::~CilkSanImpl_t() {
  csan_destroy();
  CILKSAN_INITIALIZED = false;
}

void CilkSanImpl_t::init() {
  DBG_TRACE(CALLBACK, "cilksan_init()\n");

  // Enable stats collection if requested
  {
    char *e = getenv("CILKSAN_STATS");
    if (e && 0 != strcmp(e, "0"))
      collect_stats = true;
  }
  // Enable checking of atomics if requested
  {
    char *e = getenv("CILKSAN_CHECK_ATOMICS");
    if (e) {
      if (0 == strcmp(e, "0"))
        check_atomics = false;
      else
        check_atomics = true;
    }
  }

  std::cerr << "Running Cilksan race detector.\n";

  // these are true upon creation of the stack
  cilksan_assert(frame_stack.size() == 1);

  shadow_memory = new SimpleShadowMem(*this);

  // for the main function before we enter the first Cilk context
  SBag_t *sbag;
  DBG_TRACE(BAGS, "Creating SBag for frame %ld\n", frame_id);
  sbag = createNewSBag(frame_id, call_stack);
  frame_stack.head()->set_sbag(sbag);
  WHEN_CILKSAN_DEBUG(frame_stack.head()->frame_data =
                         setLoopFrame(frame_stack.head()->frame_data));
  WHEN_CILKSAN_DEBUG(CILKSAN_INITIALIZED = true);
}
