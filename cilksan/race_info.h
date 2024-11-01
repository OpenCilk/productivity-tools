// -*- C++ -*-
#ifndef __RACE_INFO_H__
#define __RACE_INFO_H__

#include "debug_util.h"
#include <csi/csi.h>
#include <ostream>

#ifndef CHECK_EQUIVALENT_STACKS
#define CHECK_EQUIVALENT_STACKS false
#endif

// Forward declaration
class SBag_t;

// Type of determinacy race.  W stands for write, R stands for read
enum RaceType_t { RW_RACE = 1, WW_RACE = 2, WR_RACE = 3 };
// Type of memory access: a read/write, an allocation, a free, or a realloc
enum MAType_t : uint8_t {
  RW = 0,
  FNRW,
  ALLOC,
  FREE,
  REALLOC,
  STACK_FREE,
  UNKNOWN = 255
};

template <typename TYPE_T> struct typed_id_t {
  static_assert(sizeof(TYPE_T) < 16, "Invalid type for typed ID");
  // We assume that the top 15 bits of a CSI ID, after the sign bit, are unused.
  // Since CSI ID's identify instructions in the program, this assumption bounds
  // the number of CSI ID's based on the size of virtual memory.
  static constexpr unsigned TYPE_SHIFT = 48;
  static constexpr csi_id_t ID_MASK = ((1UL << TYPE_SHIFT) - 1);
  static constexpr csi_id_t UNKNOWN_TYPED_ID = UNKNOWN_CSI_ID & ID_MASK;

  csi_id_t typed_id;

  typed_id_t(TYPE_T type, csi_id_t id)
      : typed_id((static_cast<csi_id_t>(type) << TYPE_SHIFT) | id) {}

  __attribute__((always_inline)) csi_id_t get() const { return typed_id; }
  __attribute__((always_inline)) TYPE_T getType() const {
    return static_cast<TYPE_T>(typed_id >> TYPE_SHIFT);
  }
  __attribute__((always_inline)) csi_id_t getID() const {
    return typed_id & ID_MASK;
  }
  __attribute__((always_inline)) bool isUnknownID() const {
    return UNKNOWN_TYPED_ID == getID();
  }
  __attribute__((always_inline)) void setUnknown() {
    typed_id = UNKNOWN_TYPED_ID;
  }

  bool operator==(typed_id_t that) const {
    return typed_id == that.typed_id;
  }
  bool operator!=(typed_id_t that) const {
    return !(*this == that);
  }
  bool operator<(typed_id_t that) const {
    return typed_id < that.typed_id;
  }
  inline friend std::ostream &operator<<(std::ostream &os, typed_id_t id) {
    os << "(" << id.getType() << ", " << id.getID() << ")";
    return os;
  }
};

// Type of frame on the call stack: a function call, a spawn, or a loop.
enum CallType_t : uint8_t {
  CALL,
  SPAWN,
  LOOP
};
// Class representing the ID of a frame on the call stack.
class CallID_t {
  typed_id_t<CallType_t> typed_id;
public:
  // Default constructor
  CallID_t() : typed_id(CALL, typed_id_t<CallType_t>::UNKNOWN_TYPED_ID) {}
  CallID_t(CallType_t type, csi_id_t id) : typed_id(type, id) {}
  // Copy constructor
  CallID_t(const CallID_t &copy) : typed_id(copy.typed_id) {}

  // Copy-assignment operator
  inline CallID_t &operator=(const CallID_t &copy) {
    typed_id = copy.typed_id;
    return *this;
  }

  // Get the type of this call-stack frame
  inline CallType_t getType() const {
    return typed_id.getType();
  }

  // Get the CSI ID of this call-stack frame
  inline csi_id_t getID() const {
    return typed_id.getID();
  }

  // Returns true if this frame has an unknown ID.
  inline bool isUnknownID() const {
    return typed_id.isUnknownID();
  }

  // Operators for testing equality
  inline bool operator==(const CallID_t &that) const {
    return typed_id == that.typed_id;
  }
  inline bool operator!=(const CallID_t &that) const {
    return !(*this == that);
  }

  // Helper method for printing to an output stream
  inline friend std::ostream &operator<<(std::ostream &os, const CallID_t &id) {
    switch (id.getType()) {
    case CALL:
      os << "CALL " << id.getID();
      break;
    case SPAWN:
      os << "SPAWN " << id.getID();
      break;
    case LOOP:
      os << "LOOP " << id.getID();
      break;
    }
    return os;
  }
};

// Specialized stack data structure for representing the call stack.  Cilksan
// models the call stack using a singly-linked list with reference-counted
// nodes.  When a race is recorded, the call stack for that race is preserved by
// saving a pointer to the tail of the call stack.

// Class for reference-counted nodes on the call stack.
class call_stack_node_t {
  friend class call_stack_t;
  friend class AccessLoc_t;

  // A node on the call stack contains a frame, a pointer to a previous (parent)
  // node, and a reference count.
  CallID_t id;
  call_stack_node_t *prev;
  int64_t ref_count;

public:
  // Constructor
  call_stack_node_t(CallID_t id, call_stack_node_t *prev = nullptr)
      // Set ref_count to 1, under the assumption that only call_stack_t
      // constructs nodes and will immediately set the tail pointer to point to
      // this node.
      : id(id), prev(prev), ref_count(1) {
    if (prev)
      prev->ref_count++;
  }
  // Destructor
  ~call_stack_node_t() {
    cilksan_assert(!ref_count);
    if (prev) {
      call_stack_node_t *old_prev = prev;
      prev = nullptr;
      cilksan_assert(old_prev->ref_count > 0);
      old_prev->ref_count--;
      if (!old_prev->ref_count)
        delete old_prev;
    }
  }

  // Get the ID of this call-stack frame
  inline const CallID_t &getCallID() const {
    return id;
  }

  // Get the previous (parent) call-stack frame
  inline const call_stack_node_t *getPrev() const {
    return prev;
  }

  // Simple free-list allocator to conserve space and time in managing
  // call_stack_node_t objects.
  static call_stack_node_t *free_list;

  // Overloaded new operator for free-list allocator
  void *operator new(size_t size) {
    if (free_list) {
      call_stack_node_t *new_node = free_list;
      free_list = free_list->prev;
      return new_node;
    }
    return ::operator new(size);
  }

  // Overloaded delete operator for free-list allocator
  void operator delete(void *ptr) {
    call_stack_node_t *del_node = reinterpret_cast<call_stack_node_t *>(ptr);
    del_node->prev = free_list;
    free_list = del_node;
  }

  // Static method for cleaning up the free-list at the end of the program
  static void cleanup_freelist() {
    call_stack_node_t *node = free_list;
    call_stack_node_t *next = nullptr;
    while (node) {
      next = node->prev;
      ::operator delete(node);
      node = next;
    }
  }
};

// Top-level class for the call stack.
class call_stack_t {
  friend class AccessLoc_t;
  friend class SBag_t;

  call_stack_node_t *tail = nullptr;

public:
  // Default constructor
  call_stack_t() {}
  // Copy constructor
  call_stack_t(const call_stack_t &copy) : tail(copy.tail) {
    if (tail)
      tail->ref_count++;
  }
  // Destructor
  ~call_stack_t() {
    if (tail) {
      call_stack_node_t *old_tail = tail;
      tail = nullptr;
      cilksan_assert(old_tail->ref_count > 0);
      old_tail->ref_count--;
      if (!old_tail->ref_count)
        delete old_tail;
    }
  }

  // Get the end of this call stack
  inline const call_stack_node_t *getTail() const {
    return tail;
  }

  // Test if the end of this call stack matches the given ID
  inline bool tailMatches(const CallID_t &id) const {
    return tail->id == id;
  }

  // Copy-assignment operator
  inline call_stack_t &operator=(const call_stack_t &copy) {
    call_stack_node_t *old_tail = tail;
    tail = copy.tail;
    if (tail)
      tail->ref_count++;
    if (old_tail) {
      old_tail->ref_count--;
      if (!old_tail->ref_count)
        delete old_tail;
    }
    return *this;
  }

  // Move-assignment operator
  inline call_stack_t &operator=(const call_stack_t &&move) {
    if (tail) {
      tail->ref_count--;
      if (!tail->ref_count)
        delete tail;
    }
    tail = move.tail;
    return *this;
  }

  inline void overwrite(const call_stack_t &copy) {
    tail = copy.tail;
  }

  // Push a new call-stack frame onto this call stack
  inline void push(CallID_t id) {
    call_stack_node_t *new_node = new call_stack_node_t(id, tail);
    // new_node has ref_count 1 and, if tail was not null, has incremented
    // tail's ref count.
    tail = new_node;
    // Decrement the tail's ref_count, since we no longer point to that tail
    if (tail->prev) {
      cilksan_assert(tail->prev->ref_count > 1);
      tail->prev->ref_count--;
    }
    // Now the ref_count's should reflect the pointer structure.
  }

  // Pop the call-stack frame off the end of this call stack
  inline void pop() {
    cilksan_assert(tail);
    call_stack_node_t *old_node = tail;
    tail = tail->prev;
    // Increment the new tail's ref_count, since we now point to it
    if (tail)
      tail->ref_count++;
    // Decrement the old tail's ref_count, and delete it if necessary
    cilksan_assert(old_node->ref_count > 0);
    old_node->ref_count--;
    if (!old_node->ref_count)
      // Deleting the old node will decrement tail's ref_count.
      delete old_node;
  }

  // Get the size of this call stack
  inline int size() const {
    call_stack_node_t *node = tail;
    int size = 0;
    while (node) {
      ++size;
      node = node->prev;
    }
    return size;
  }
};

// Class representing a memory access.
class AccessLoc_t {
  // CSI ID of the access.
  csi_id_t acc_loc;

  // TODO: Combine type with acc_loc, if this is a performance issue.
  MAType_t type;

  // Call stack for the access.
  call_stack_t call_stack;

public:
  // Default constructor
  AccessLoc_t()
      : acc_loc(UNKNOWN_CSI_ID), type(MAType_t::UNKNOWN), call_stack() {}

  // Constructor
  AccessLoc_t(csi_id_t _acc_loc, MAType_t _type,
              const call_stack_t &_call_stack)
      : acc_loc(_acc_loc), type(_type), call_stack(_call_stack) {}

  // Copy constructor
  AccessLoc_t(const AccessLoc_t &copy)
      : acc_loc(copy.acc_loc), type(copy.type), call_stack(copy.call_stack) {}

  // Move constructor
  AccessLoc_t(AccessLoc_t &&move) : acc_loc(move.acc_loc), type(move.type) {
    call_stack.overwrite(move.call_stack);
  }

  // Destructor
  ~AccessLoc_t() = default;

  // Accessors
  inline csi_id_t getID() const { return acc_loc; }
  inline MAType_t getType() const { return type; }
  inline const call_stack_node_t *getCallStack() const {
    return call_stack.tail;
  }
  inline int getCallStackSize() const { return call_stack.size(); }

  inline bool isValid() const { return acc_loc != UNKNOWN_CSI_ID; }

  inline void invalidate() {
    dec_ref_count();
    call_stack.tail = nullptr;
    acc_loc = UNKNOWN_CSI_ID;
  }

  // Copy assignment operator
  inline AccessLoc_t& operator=(const AccessLoc_t &copy) {
    acc_loc = copy.acc_loc;
    type = copy.type;
    call_stack = copy.call_stack;
    return *this;
  }
  // Move assignment operator
  inline AccessLoc_t& operator=(AccessLoc_t &&move) {
    acc_loc = move.acc_loc;
    type = move.type;
    call_stack = std::move(move.call_stack);
    return *this;
  }

  // Methods for managing the reference count
  inline int64_t inc_ref_count(int64_t count = 1) {
    if (!call_stack.tail)
      return 0;
    call_stack.tail->ref_count += count;
    return call_stack.tail->ref_count;
  }

  inline int64_t dec_ref_count(int64_t count = 1) {
    if (!call_stack.tail)
      return 0;
    cilksan_assert(call_stack.tail->ref_count >= count);
    call_stack.tail->ref_count -= count;
    if (!call_stack.tail->ref_count) {
      delete call_stack.tail;
      call_stack.tail = nullptr;
      return 0;
    }
    return call_stack.tail->ref_count;
  }

  // Equality comparison operator
  inline bool operator==(const AccessLoc_t &that) const {
    if (acc_loc != that.acc_loc || type != that.type)
      return false;
#if CHECK_EQUIVALENT_STACKS
    call_stack_node_t *this_node = call_stack.tail;
    call_stack_node_t *that_node = that.call_stack.tail;
    while (this_node && that_node) {
      if (this_node->id != that_node->id)
        return false;
      this_node = this_node->prev;
      that_node = that_node->prev;
    }
    if (this_node || that_node)
      return false;
#endif // CHECK_EQUIVALENT_STACKS
    return true;
  }
  // Inequality comparison operator
  inline bool operator!=(const AccessLoc_t &that) const {
    return !(*this == that);
  }

  // Less-than comparison operator.  Used for tie-breaking in data structures
  inline bool operator<(const AccessLoc_t &that) const {
    return (acc_loc < that.acc_loc) || ((acc_loc == that.acc_loc) &&
                                        (type < that.type));
  }

  // Output-stream operator
  inline friend std::ostream &operator<<(std::ostream &os,
                                         const AccessLoc_t &loc) {
    os << loc.acc_loc;
    // call_stack_node_t *node = loc.call_stack.tail;
    const call_stack_node_t *node = loc.getCallStack();
    while (node) {
      switch (node->getCallID().getType()) {
      case CALL:
        os << " CALL";
        break;
      case SPAWN:
        os << " SPAWN";
        break;
      case LOOP:
        os << " LOOP";
        break;
      }
      os << " " << std::dec << node->getCallID().getID();
      node = node->getPrev();
    }
    return os;
  }
};

// TODO: Replace this class copied from
// sanitizer_common/sanitizer_report_decorator.h with the sanitizer header file
// itself.
class Decorator {
  // FIXME: This is not portable. It assumes the special strings are printed to
  // stdout, which is not the case on Windows (see SetConsoleTextAttribute()).
 public:
  Decorator(bool color_report) : ansi_(color_report) {}
  const char *Bold() const { return ansi_ ? "\033[1m" : ""; }
  const char *Default() const { return ansi_ ? "\033[0m"  : ""; }
  const char *Warning() const { return Red(); }
  const char *Error() const { return Red(); }
  const char *MemoryByte() const { return Magenta(); }

  const char *RaceLoc() const { return Magenta(); }
  const char *InstAddress() const { return Yellow(); }
  const char *Function() const { return Blue(); }
  const char *Variable() const { return Cyan(); }
  const char *Filename() const { return Green(); }

protected:
  const char *Black()   const { return ansi_ ? "\033[30m" : ""; }
  const char *Red()     const { return ansi_ ? "\033[31m" : ""; }
  const char *Green()   const { return ansi_ ? "\033[32m" : ""; }
  const char *Yellow()  const { return ansi_ ? "\033[33m" : ""; }
  const char *Blue()    const { return ansi_ ? "\033[34m" : ""; }
  const char *Magenta() const { return ansi_ ? "\033[35m" : ""; }
  const char *Cyan()    const { return ansi_ ? "\033[36m" : ""; }
  const char *White()   const { return ansi_ ? "\033[37m" : ""; }
 private:
  const bool ansi_;
};

static enum RaceType_t flipRaceType(enum RaceType_t type) {
  switch(type) {
  case RW_RACE: return WR_RACE;
  case WW_RACE: return WW_RACE;
  case WR_RACE: return RW_RACE;
  }
}

// Class representing a single race.
class RaceInfo_t {
  // const AccessLoc_t first_inst;  // instruction addr of the first access
  // const AccessLoc_t second_inst; // instruction addr of the second access
  // const AccessLoc_t alloc_inst;  // instruction addr of memory allocation
  typed_id_t<MAType_t> first_acc;
  typed_id_t<MAType_t> second_acc;
  csi_id_t alloc_id;
  uintptr_t addr;       // addr of memory location that got raced on
  enum RaceType_t type; // type of race

public:
  RaceInfo_t(const AccessLoc_t &_first, const AccessLoc_t &_second,
             const AccessLoc_t &_alloc, uintptr_t _addr, enum RaceType_t _type)
      : first_acc(_first.getType(), _first.getID()),
        second_acc(_second.getType(), _second.getID()),
        alloc_id(_alloc.getID()), addr(_addr), type(_type) {}

  ~RaceInfo_t() = default;

  bool is_equivalent_race(const RaceInfo_t& other) const {
    if (((first_acc == other.first_acc && second_acc == other.second_acc &&
          type == other.type) ||
         (first_acc == other.second_acc && second_acc == other.first_acc &&
          type == flipRaceType(other.type))) &&
        alloc_id == other.alloc_id) {
      return true;
    }
    return false;
  }

  inline void print(const AccessLoc_t &first, const AccessLoc_t &second,
                    const AccessLoc_t &alloc, const Decorator &d) const;
};

#endif  // __RACE_INFO_H__
