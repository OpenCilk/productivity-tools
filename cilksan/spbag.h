/* -*- Mode: C++ -*- */

#ifndef _SPBAG_H
#define _SPBAG_H

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <inttypes.h>

#include "debug_util.h"
#include "race_info.h"

#define UNINIT_STACK_PTR ((uintptr_t)0LL)

enum class BagType_t { SBag = 0, PBag = 1 };

using version_t = uint32_t;

class SPBagInterface {
protected:
  // Since the bags require 64-bits of storage at least anyway, allocate a
  // 64-bit payload in the base class.  Use the topmost bit to record whether
  // the bag is an S-bag or a P-bag, so that the bag type can be determined
  // without an indirect function call.
  static constexpr unsigned BAG_TYPE_SHIFT = 63;
  static constexpr uintptr_t BAG_TYPE_MASK = (1UL << BAG_TYPE_SHIFT);
  uintptr_t _payload;

  uintptr_t getAvailablePayload() const { return _payload & ~BAG_TYPE_MASK; }

  SPBagInterface(BagType_t type)
      : _payload(static_cast<uintptr_t>(type) << BAG_TYPE_SHIFT) {}

public:
  // Note: Base class must declare a virtual destructor; it does not have to be
  // pure and must provide a definition.
  // http://stackoverflow.com/questions/461203/when-to-use-virtual-destructors
  // <stackoverflow>/3336499/virtual-desctructor-on-pure-abstract-base-class
  virtual ~SPBagInterface() {}

  // Implement these methods directly on the base class, so that checking
  // whether an arbitrary SPBagInterface object is an S-bag or a P-bag does not
  // require an indirect function call.
  __attribute__((always_inline)) bool is_SBag() const {
    return (_payload & BAG_TYPE_MASK) == static_cast<uintptr_t>(BagType_t::SBag)
                                             << BAG_TYPE_SHIFT;
  }
  __attribute__((always_inline)) bool is_PBag() const {
    return (_payload & BAG_TYPE_MASK) == static_cast<uintptr_t>(BagType_t::PBag)
                                             << BAG_TYPE_SHIFT;
  }

  virtual uint64_t get_func_id() const = 0;
  virtual version_t get_version() const = 0;
  virtual bool inc_version() = 0;
  virtual const call_stack_t *get_call_stack() const = 0;
  virtual void update_sibling(SPBagInterface *) = 0;
};

class SBag_t final : public SPBagInterface {
private:
  // Use the base-class _payload to store the function ID and the version.  The
  // low 32 bits of the _payload are used to store the version, so that the
  // version can be updated with simple arithmetic on 32-bit registers.
  // static constexpr unsigned FUNC_ID_SHIFT = 16;
  // static constexpr uintptr_t VERSION_MASK = ((1UL << FUNC_ID_SHIFT) - 1);
  // static constexpr uintptr_t FUNC_ID_MASK = ~VERSION_MASK & ~BAG_TYPE_MASK;
  static constexpr unsigned VERSION_END_SHIFT = 32;
  static constexpr uintptr_t VERSION_MASK = ((1UL << VERSION_END_SHIFT) - 1);

  // The call stack of the function instantiation that corresponds with this
  // S-bag.  The call stack is used to report the first endpoint of a race.
  call_stack_t _call_stack;

#if CILKSAN_DEBUG
  uint64_t func_id;
#endif

  SBag_t() = delete; // disable default constructor

public:
  SBag_t(uint64_t id, const call_stack_t &call_stack)
      : SPBagInterface(BagType_t::SBag), _call_stack(call_stack)
#if CILKSAN_DEBUG
        ,
        func_id(id)
#endif
  {
    // // Use _payload to store the function ID
    // _payload |= static_cast<uintptr_t>(id);

    WHEN_CILKSAN_DEBUG(debug_count++);
  }

  // Note: The compiler will generate a default inline destructor, and it's
  // better to let the compiler to that than define your own empty destructor.
  // This is true even when the parent class has a virtual destructor.
  // http://stackoverflow.com/questions/827196/virtual-default-destructors-in-c
#if CILKSAN_DEBUG
  static long debug_count;
  ~SBag_t() override { debug_count--; }
#endif

  bool is_SBag() const { return true; }
  bool is_PBag() const { return false; }

  uint64_t get_func_id() const override {
    // return static_cast<uint64_t>(getAvailablePayload()) >> FUNC_ID_SHIFT;
#if CILKSAN_DEBUG
    return func_id;
#else
    return 0;
#endif
  }

  version_t get_version() const override {
    return static_cast<uint64_t>(getAvailablePayload()) & VERSION_MASK;
  }
  bool inc_version() override {
    // uint16_t new_version =
    //     static_cast<uint16_t>(getAvailablePayload() & VERSION_MASK) + 1;
    version_t new_version =
        static_cast<version_t>(getAvailablePayload() & VERSION_MASK) + 1;
    _payload = (_payload & ~VERSION_MASK) | new_version;
    return (0 != new_version);
  }

  const call_stack_t *get_call_stack() const override {
    return &_call_stack;
  }

  void update_sibling(SPBagInterface *) override {
    cilksan_assert(0 && "update_sibling called from SBag_t");
  }

  // Simple free-list allocator to conserve space and time in managing
  // SBag_t objects.

  // The structure of a node in the SBag free list.
  struct FreeNode_t {
    FreeNode_t *next = nullptr;
  };
  static FreeNode_t *free_list;

  void *operator new(size_t size) {
    if (free_list) {
      FreeNode_t *new_node = free_list;
      free_list = free_list->next;
      return new_node;
    }
    return ::operator new(size);
  }

  void operator delete(void *ptr) {
    FreeNode_t *del_node = reinterpret_cast<FreeNode_t *>(ptr);
    del_node->next = free_list;
    free_list = del_node;
  }

  static void cleanup_freelist() {
    FreeNode_t *node = free_list;
    FreeNode_t *next = nullptr;
    while (node) {
      next = node->next;
      ::operator delete(node);
      node = next;
    }
  }
};

static_assert(sizeof(SBag_t) >= sizeof(SBag_t::FreeNode_t),
              "Node structure in SBag free list must be as large as SBag.");

class PBag_t final : public SPBagInterface {
private:
  // Use the base-class _payload to store the pointer to the sibling SBag: the
  // SBag that corresponds to the function instance that holds this PBag.

  PBag_t() = delete; // disable default constructor

  // Helper method to get the pointer to the sibling SBag..
  SPBagInterface *getSib() const {
    return reinterpret_cast<SPBagInterface *>(getAvailablePayload());
  }

public:
  PBag_t(SPBagInterface *sib) : SPBagInterface(BagType_t::PBag) {
    WHEN_CILKSAN_DEBUG(debug_count++;);
    _payload |= reinterpret_cast<uintptr_t>(sib);
  }

#if CILKSAN_DEBUG
  static long debug_count;
  ~PBag_t() override {
    debug_count--;
    update_sibling(nullptr);
  }
#endif

  bool is_SBag() const { return false; }
  bool is_PBag() const { return true; }

  uint64_t get_func_id() const override {
#if CILKSAN_DEBUG
    return getSib()->get_func_id();
#else
    return 0;
#endif
  }

  // These methods should never be invoked on a P-bag.
  version_t get_version() const override {
    cilksan_assert(0 && "Called get_version on a Pbag");
    return getSib()->get_version();
  }
  bool inc_version() override {
    cilksan_assert(0 && "Called inc_version on a Pbag");
    return getSib()->inc_version();
  }
  const call_stack_t *get_call_stack() const override {
    cilksan_assert(0 && "Called get_call_stack on a Pbag");
    return getSib()->get_call_stack();
  }

  void update_sibling(SPBagInterface *new_sib) override {
    _payload =
        (_payload & BAG_TYPE_MASK) | reinterpret_cast<uintptr_t>(new_sib);
  }

  // Simple free-list allocator to conserve space and time in managing
  // PBag_t objects.
  struct FreeNode_t {
    FreeNode_t *next = nullptr;
  };
  static FreeNode_t *free_list;

  void *operator new(size_t size) {
    if (free_list) {
      FreeNode_t *new_node = free_list;
      free_list = free_list->next;
      return new_node;
    }
    return ::operator new(size);
  }

  void operator delete(void *ptr) {
    FreeNode_t *del_node = reinterpret_cast<FreeNode_t *>(ptr);
    del_node->next = free_list;
    free_list = del_node;
  }

  static void cleanup_freelist() {
    FreeNode_t *node = free_list;
    FreeNode_t *next = nullptr;
    while (node) {
      next = node->next;
      ::operator delete(node);
      node = next;
    }
  }
};

static_assert(sizeof(PBag_t) >= sizeof(PBag_t::FreeNode_t),
              "Node structure in PBag free list must be as large as PBag.");

#endif // #ifndef _SPBAG_H
