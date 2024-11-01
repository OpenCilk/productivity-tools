/* -*- Mode: C++ -*- */

#ifndef _SPBAG_H
#define _SPBAG_H

#include "debug_util.h"
#include "disjointset.h"
#include "race_info.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define UNINIT_STACK_PTR ((uintptr_t)0LL)

enum class BagType_t { SBag = 0, PBag = 1 };

// NOTE: MemoryAccess_t reserves only 16 bits to store a version number, so this
// code only uses 16-bit version numbers to match.
using version_t = uint16_t;
static_assert(8 * sizeof(version_t) < 64,
              "Version type too large to fit in spbag payload.");

class SPBagInterface {
protected:
  using DS_t = DisjointSet_t<call_stack_t>;

  // Since the bags require 64-bits of storage at least anyway, allocate a
  // 64-bit payload in the base class.  Use the topmost bit to record whether
  // the bag is an S-bag or a P-bag, so that the bag type can be determined
  // without an indirect function call.
  static constexpr unsigned BAG_TYPE_SHIFT = 63;
  static constexpr uintptr_t BAG_TYPE_MASK = (1UL << BAG_TYPE_SHIFT);
  DS_t *_ds = nullptr;
  uintptr_t _payload;

  uintptr_t getAvailablePayload() const { return _payload & ~BAG_TYPE_MASK; }

  SPBagInterface(BagType_t type)
      : _payload(static_cast<uintptr_t>(type) << BAG_TYPE_SHIFT) {}

public:
  // Note: Base class must declare a virtual destructor; it does not have to be
  // pure and must provide a definition.
  // http://stackoverflow.com/questions/461203/when-to-use-virtual-destructors
  // <stackoverflow>/3336499/virtual-desctructor-on-pure-abstract-base-class
  virtual ~SPBagInterface() {
    if (_ds)
      _ds->dec_ref_count();
  }

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

  void set_ds(DS_t *ds) {
    if (ds == _ds)
      return;

    ds->inc_ref_count();
    if (_ds)
      _ds->dec_ref_count();
    _ds = ds;
  }
  DS_t *get_ds() const { return _ds; }

  virtual uint64_t get_func_id() const = 0;
  virtual version_t get_version() const = 0;
  virtual bool inc_version() = 0;

  void combine(SPBagInterface *that) {
    if (!that->get_ds())
      return;

    // cilksan_assert(that->_ds && "No disjointset node with that bag.");
    if (!_ds) {
      that->get_ds()->inc_ref_count();
      _ds = that->get_ds();
      return;
    }

    cilksan_assert(_ds && "No disjointset node with this bag.");
    set_ds(_ds->combine(that->get_ds()));
  }
};

static_assert(alignof(SPBagInterface) >= 8, "Bad alignment for SPBagInterface.");

class SBag_t final : public SPBagInterface {
private:
  static constexpr unsigned VERSION_END_SHIFT = 8 * sizeof(version_t);
  static constexpr uintptr_t VERSION_MASK = ((1UL << VERSION_END_SHIFT) - 1);

#if CILKSAN_DEBUG
  uint64_t func_id;
#endif

  SBag_t() = delete; // disable default constructor

public:
  SBag_t(uint64_t id)
      : SPBagInterface(BagType_t::SBag)
#if CILKSAN_DEBUG
        ,
        func_id(id)
#endif
  {
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
    version_t new_version =
        static_cast<version_t>(getAvailablePayload() & VERSION_MASK) + 1;
    _payload = (_payload & ~VERSION_MASK) | new_version;
    return (0 != new_version);
  }

  void combine(SPBagInterface *that) {
    DS_t *old_ds = _ds;
    SPBagInterface::combine(that);
    if (_ds != old_ds)
      _ds->set_sbag(this);
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
public:
  PBag_t() : SPBagInterface(BagType_t::PBag) {
    WHEN_CILKSAN_DEBUG(debug_count++;);
  }

#if CILKSAN_DEBUG
  static long debug_count;
  ~PBag_t() override {
    debug_count--;
  }
#endif

  bool is_SBag() const { return false; }
  bool is_PBag() const { return true; }

  uint64_t get_func_id() const override { return 0; }

  // These methods should never be invoked on a P-bag.
  version_t get_version() const override {
    cilksan_assert(false && "Called get_version on a Pbag");
    return 0;
  }
  bool inc_version() override {
    cilksan_assert(false && "Called inc_version on a Pbag");
    return false;
  }

  void combine(SPBagInterface *that) {
    DS_t *old_ds = _ds;
    SPBagInterface::combine(that);
    if (_ds != old_ds)
      _ds->set_pbag(this);
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
