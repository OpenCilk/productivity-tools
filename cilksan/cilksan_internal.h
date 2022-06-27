// -*- C++ -*-
#ifndef __CILKSAN_INTERNAL_H__
#define __CILKSAN_INTERNAL_H__

#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "csan.h"

#include "dictionary.h"
#include "disjointset.h"
#include "frame_data.h"
#include "locksets.h"
#include "shadow_mem_allocator.h"
#include "stack.h"

extern bool CILKSAN_INITIALIZED;

// Forward declarations
class SimpleShadowMem;

// Top-level class implementing the tool.
class CilkSanImpl_t {
public:
  CilkSanImpl_t() : color_report(ColorizeReports()) {
    CILKSAN_INITIALIZED = true;
  }
  ~CilkSanImpl_t();

  MALineAllocator &getMALineAllocator(unsigned Idx) {
    return MAAlloc[Idx];
  }

  using DSAllocator = DisjointSet_t<call_stack_t>::DSAllocator;
  DSAllocator &getDSAllocator() {
    return DSAlloc;
  }

  using DSList_t = DisjointSet_t<call_stack_t>::List_t;
  DSList_t &getDSList() {
    return DSList;
  }

  // Initialization
  void init();
  void deinit();

  // Control-flow actions
  inline void record_call(const csi_id_t id, enum CallType_t ty) {
    call_stack.push(CallID_t(ty, id));
  }

  inline void record_call_return(const csi_id_t id, enum CallType_t ty) {
    assert(call_stack.tailMatches(CallID_t(ty, id)) &&
           "Mismatched hooks around call/spawn site");
    call_stack.pop();
  }

  // TODO: Fix this architecture-specific detail.
  static const uintptr_t STACK_ALIGN = 16;
#define NEXT_STACK_ALIGN(addr) \
  ((uintptr_t) ((addr - (STACK_ALIGN-1)) & (~(STACK_ALIGN-1))))
#define PREV_STACK_ALIGN(addr) (addr + STACK_ALIGN)

  inline void push_stack_frame(uintptr_t bp, uintptr_t sp) {
    DBG_TRACE(DEBUG_STACK, "push_stack_frame %p--%p\n", bp, sp);
    // Record high location of the stack for this frame.
    uintptr_t high_stack = bp;
    // uintptr_t high_stack = PREV_STACK_ALIGN(bp);
    // fprintf(stderr, "bp = %p, NEXT_STACK_ALIGN = %p, PREV_STACK_ALIGN = %p\n",
    //         bp, NEXT_STACK_ALIGN(bp), PREV_STACK_ALIGN(bp));

    // if (sp_stack.size() > 1) {
    //   uintptr_t prev_stack = *sp_stack.head();

    //   // fprintf(stderr, "  NEXT_STACK_ALIGN(prev_stack) = %p\n",
    //   //         NEXT_STACK_ALIGN(prev_stack));
    //   if (high_stack > NEXT_STACK_ALIGN(prev_stack))
    //     high_stack = NEXT_STACK_ALIGN(prev_stack);
    //   // low_stack = (low_stack < (uintptr_t)sp) ? sp : low_stack;
    // }

    sp_stack.push();
    *sp_stack.head() = high_stack;
    // Record low location of the stack for this frame.  This value will be
    // updated by reads and writes to the stack.
    sp_stack.push();
    *sp_stack.head() = sp;
  }

  inline void advance_stack_frame(uintptr_t addr) {
    DBG_TRACE(DEBUG_STACK, "advance_stack_frame %p to include %p\n",
              *sp_stack.head(), addr);
    if (addr < *sp_stack.head())
      *sp_stack.head() = addr;
  }

  inline void pop_stack_frame() {
    // Pop stack pointers.
    uintptr_t low_stack = *sp_stack.head();
    sp_stack.pop();
    uintptr_t high_stack = *sp_stack.head();
    sp_stack.pop();
    DBG_TRACE(DEBUG_STACK, "pop_stack_frame %p--%p\n", high_stack, low_stack);
    assert(low_stack <= high_stack);
    // Clear shadow memory of stack locations.  This seems to be necessary right
    // now, in order to handle functions that dynamically allocate stack memory.
    // if (high_stack != low_stack) {
      clear_shadow_memory(low_stack, high_stack - low_stack);
      clear_alloc(low_stack, high_stack - low_stack);
    // }
  }

  // Restore the stack pointer to the previous value addr
  inline void restore_stack(csi_id_t call_id, uintptr_t addr) {
    uintptr_t current_stack = *sp_stack.head();
    if (addr > current_stack) {
      record_free(current_stack, addr - current_stack, call_id,
                  MAType_t::STACK_FREE);
      *sp_stack.head() = addr;
    }
  }

  inline bool is_local_synced() const {
    FrameData_t *f = frame_stack.head();
    // If this is a loop frame, assume we're not locally synced.
    if (LOOP_FRAME == f->frame_data.frame_type)
      return false;
    // Otherwise check if this frame has nonempty P-bags.
    if (f->Pbags)
      for (unsigned i = 0; i < f->num_Pbags; ++i)
        if (f->Pbags[i])
          return false;
    return true;
  }

  // Control-flow actions
  void do_enter(unsigned num_sync_reg);
  void do_enter_helper(unsigned num_sync_reg);
  void do_detach();
  void do_detach_continue();
  void do_loop_begin() {
    start_new_loop = true;
  }
  void do_loop_iteration_begin(unsigned num_sync_reg);
  void do_loop_iteration_end();
  void do_loop_end(unsigned sync_reg);
  bool in_loop() const {
    return LOOP_FRAME == frame_stack.head()->frame_data.frame_type;
  }
  bool handle_loop() const {
    return in_loop() || (true == start_new_loop);
  }
  void do_sync(unsigned sync_reg);
  void do_return();
  void do_leave(unsigned sync_reg);

  // Memory actions
  template <MAType_t type>
  void do_read(const csi_id_t id, uintptr_t addr, size_t len,
               unsigned alignment);
  template <MAType_t type>
  void do_write(const csi_id_t id, uintptr_t addr, size_t len,
                unsigned alignment);

  void clear_shadow_memory(size_t start, size_t end);
  void record_alloc(size_t start, size_t size, csi_id_t alloca_id);
  void record_free(size_t start, size_t size, csi_id_t acc_id, MAType_t type);
  void clear_alloc(size_t start, size_t size);

  // Methods for locked accesses
  inline void do_acquire_lock(LockID_t lock_id) {
    lockset.insert(lock_id);
    lockset_empty = false;
  }
  inline void do_release_lock(LockID_t lock_id) {
    lockset.remove(lock_id);
    lockset_empty = lockset.isEmpty();
  }
  inline bool locks_held() const { return !lockset_empty; }
  template <MAType_t type>
  void do_locked_read(const csi_id_t load_id, uintptr_t addr, size_t len,
                      unsigned alignment);
  template <MAType_t type>
  void do_locked_write(const csi_id_t store_id, uintptr_t addr, size_t len,
                       unsigned alignment);
  void do_atomic_read(const csi_id_t load_id, uintptr_t addr, size_t len,
                      unsigned alignment, LockID_t atomic_lock_id) {
    if (check_atomics) {
      lockset.insert(atomic_lock_id);
      do_locked_read<MAType_t::RW>(load_id, addr, len, alignment);
      lockset.remove(atomic_lock_id);
    } else {
      do_read<MAType_t::RW>(load_id, addr, len, alignment);
    }
  }
  void do_atomic_write(const csi_id_t store_id, uintptr_t addr, size_t len,
                       unsigned alignment, LockID_t atomic_lock_id) {
    if (check_atomics) {
      lockset.insert(atomic_lock_id);
      do_locked_write<MAType_t::RW>(store_id, addr, len, alignment);
      lockset.remove(atomic_lock_id);
    } else {
      do_write<MAType_t::RW>(store_id, addr, len, alignment);
    }
  }

  // Interface to RR
  static bool RunningUnderRR();

  // Methods for recording and reporting races
  const call_stack_t &get_current_call_stack() const {
    return call_stack;
  }
  void report_race(
      const AccessLoc_t &first_inst, const AccessLoc_t &second_inst,
      uintptr_t addr, enum RaceType_t race_type);
  void report_race(
      const AccessLoc_t &first_inst, const AccessLoc_t &second_inst,
      const AccessLoc_t &alloc_inst, uintptr_t addr,
      enum RaceType_t race_type);
  void print_race_report();
  int get_num_races_found();

private:
  inline void merge_bag_from_returning_child(bool returning_from_detach,
                                             unsigned sync_reg);
  inline void start_new_function(unsigned num_sync_reg);
  inline void exit_function();
  inline void enter_cilk_function(unsigned num_sync_reg);
  inline void leave_cilk_function(unsigned sync_reg);
  inline void enter_detach_child(unsigned num_sync_reg);
  inline void return_from_detach(unsigned sync_reg);
  inline void complete_sync(unsigned sync_reg);
  template <bool is_read, MAType_t type>
  inline void record_mem_helper(const csi_id_t acc_id, uintptr_t addr,
                                size_t mem_size, unsigned alignment);
  template <bool is_read, MAType_t type>
  inline void record_locked_mem_helper(const csi_id_t acc_id, uintptr_t addr,
                                       size_t mem_size, unsigned alignment);
  inline void print_stats();
  static bool ColorizeReports();
  static bool PauseOnRace();

  // ANGE: Each function that causes a Disjoint set to be created has a
  // unique ID (i.e., Cilk function and spawned C function).
  // If a spawned function is also a Cilk function, a Disjoint Set is created
  // for code between the point where detach finishes and the point the Cilk
  // function calls enter_frame, which may be unnecessary in some case.
  // (But potentially necessary in the case where the base case is executed.)
  uint64_t frame_id = 0;

  // Data associated with the stack of Cilk frames or spawned C frames.
  // head contains the SP bags for the function we are currently processing
  Stack_t<FrameData_t> frame_stack;
  // Call stack for the current instruction
  call_stack_t call_stack;
  // Stack maintaining the stack pointer SP, and specifically, the range of
  // stack memory used by each function instantiation.
  Stack_t<uintptr_t> sp_stack;

  // Flag for whether the next loop iteration is the first iteration of a loop
  bool start_new_loop = false;

  // Flag for whether to check whether a memory address that is accessed by an
  // atomic operation is always accessed by atomic operations
  bool check_atomics = true;

  // Set of locks held at the current instruction
  bool lockset_empty = true;
  LockSet_t lockset;

  // Shadow memory, which maps a memory address to its last reader and writer
  // and allocation.
  SimpleShadowMem *shadow_memory = nullptr;

  // Use separate allocators for each dictionary in the shadow memory.
  MALineAllocator MAAlloc[3];

  // Allocator for disjoint sets
  DSAllocator DSAlloc;

  // Helper list for disjoint sets
  DSList_t DSList;

  // A map keeping track of races found, keyed by the larger instruction address
  // involved in the race.  Races that have same instructions that made the same
  // types of accesses are considered as the the same race (even for races where
  // one is read followed by write and the other is write followed by read, they
  // are still considered as the same race).  Races that have the same
  // instruction addresses but different address for memory location is
  // considered as a duplicate.  The value of the map stores the number
  // duplicates for the given race.
  using RaceMap_t = std::unordered_multimap<uint64_t, RaceInfo_t>;
  RaceMap_t races_found;
  // The number of duplicated races found
  uint32_t duplicated_races = 0;
  const bool color_report;

  // Basic statistics
  bool collect_stats = false;
  uint64_t strand_count = 0;
  uint64_t total_reads_checked = 0;
  uint64_t total_writes_checked = 0;
  std::unordered_map<size_t, uint64_t> num_reads_checked;
  std::unordered_map<size_t, uint64_t> num_writes_checked;

  std::unordered_map<size_t, uint64_t> max_num_reads_checked;
  std::unordered_map<size_t, uint64_t> max_num_writes_checked;

  std::unordered_map<size_t, uint64_t> strand_num_reads_checked;
  std::unordered_map<size_t, uint64_t> strand_num_writes_checked;

  void collect_read_stat(size_t mem_size) {
    ++total_reads_checked;
    if (!num_reads_checked.count(mem_size))
      num_reads_checked.insert(std::make_pair(mem_size, 0));
    ++num_reads_checked[mem_size];

    if (!strand_num_reads_checked.count(mem_size))
      strand_num_reads_checked.insert(std::make_pair(mem_size, 0));
    ++strand_num_reads_checked[mem_size];
  }
  void collect_write_stat(size_t mem_size) {
    ++total_writes_checked;
    if (!num_writes_checked.count(mem_size))
      num_writes_checked.insert(std::make_pair(mem_size, 0));
    ++num_writes_checked[mem_size];

    if (!strand_num_writes_checked.count(mem_size))
      strand_num_writes_checked.insert(std::make_pair(mem_size, 0));
    ++strand_num_writes_checked[mem_size];
  }

  void update_strand_stats() {
    if (!collect_stats)
      return;

    ++strand_count;

    for (auto &entry : strand_num_reads_checked) {
      if (!max_num_reads_checked.count(entry.first))
        max_num_reads_checked.insert(entry);
      else if (max_num_reads_checked[entry.first] < entry.second)
        max_num_reads_checked[entry.first] = entry.second;
    }
    strand_num_reads_checked.clear();
    for (auto &entry : strand_num_writes_checked) {
      if (!max_num_writes_checked.count(entry.first))
        max_num_writes_checked.insert(entry);
      else if (max_num_writes_checked[entry.first] < entry.second)
        max_num_writes_checked[entry.first] = entry.second;
    }
    strand_num_writes_checked.clear();
  }
};

#endif // __CILKSAN_INTERNAL_H__
