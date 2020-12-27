// -*- C++ -*-
#ifndef __DICTIONARY__
#define __DICTIONARY__

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <inttypes.h>

#include "debug_util.h"
#include "disjointset.h"
#include "race_info.h"
#include "spbag.h"

class MemoryAccess_t {
  static constexpr unsigned VERSION_SHIFT = 48;
  static constexpr unsigned TYPE_SHIFT = 44;
  static constexpr csi_id_t ID_MASK = ((1UL << TYPE_SHIFT) - 1);
  static constexpr csi_id_t TYPE_MASK = ((1UL << VERSION_SHIFT) - 1) & ~ID_MASK;
  static constexpr csi_id_t UNKNOWN_CSI_ACC_ID = UNKNOWN_CSI_ID & ID_MASK;

public:
  DisjointSet_t<SPBagInterface *> *func = nullptr;
  csi_id_t ver_acc_id = UNKNOWN_CSI_ACC_ID;

  // Default constructor
  MemoryAccess_t() {}
  // Constructor
  MemoryAccess_t(DisjointSet_t<SPBagInterface *> *func, csi_id_t acc_id,
                 MAType_t type)
      : func(func), ver_acc_id((acc_id & ID_MASK) |
                               (static_cast<csi_id_t>(type) << TYPE_SHIFT)) {
    if (func) {
      func->inc_ref_count();
      ver_acc_id |= static_cast<csi_id_t>(func->get_node()->get_version())
                    << VERSION_SHIFT;
    }
  }
  // Copy constructor
  MemoryAccess_t(const MemoryAccess_t &copy)
      : func(copy.func), ver_acc_id(copy.ver_acc_id) {
    if (func)
      func->inc_ref_count();
  }
  // Move constructor
  MemoryAccess_t(const MemoryAccess_t &&move)
      : func(move.func), ver_acc_id(move.ver_acc_id) {}
  // Destructor
  ~MemoryAccess_t() {
    if (func) {
      func->dec_ref_count();
      func = nullptr;
    }
  }

  // Returns true if this MemoryAccess_t is valid, meaning it refers to an
  // actual memory access in the program-under-test.
  bool isValid() const {
    return (nullptr != func);
  }

  // Render this MemoryAccess_t invalid.
  void invalidate() {
    if (func)
      func->dec_ref_count();
    func = nullptr;
    ver_acc_id = UNKNOWN_CSI_ACC_ID;
  }

  // Get the disjoint-set node for the function containing this memory access.
  DisjointSet_t<SPBagInterface *> *getFunc() const {
    return func;
  }

  // Get the CSI ID for this memory access.
  csi_id_t getAccID() const {
    if ((ver_acc_id & ID_MASK) == UNKNOWN_CSI_ACC_ID)
      return UNKNOWN_CSI_ID;
    return (ver_acc_id & ID_MASK);
  }
  MAType_t getAccType() const {
    if ((ver_acc_id & ID_MASK) == UNKNOWN_CSI_ACC_ID)
      return MAType_t::UNKNOWN;
    return static_cast<MAType_t>((ver_acc_id & TYPE_MASK) >> TYPE_SHIFT);
  }
  uint16_t getVersion() const {
    return static_cast<uint16_t>(ver_acc_id >> VERSION_SHIFT);
  }
  AccessLoc_t getLoc() const {
    if (!isValid())
      return AccessLoc_t();
    cilksan_level_assert(DEBUG_BASIC, func->get_node()->is_SBag());
    return AccessLoc_t(
        getAccID(), getAccType(),
        *static_cast<SBag_t *>(func->get_node())->get_call_stack());
  }

  // Set the fields of this MemoryAccess_t directly.  This method is used to
  // avoid unnecessary updates to reference counts that may be incurred by using
  // the copy contructor.
  void set(DisjointSet_t<SPBagInterface *> *func, csi_id_t acc_id,
           MAType_t type) {
    if (this->func != func) {
      if (func)
        func->inc_ref_count();
      if (this->func)
        this->func->dec_ref_count();
      this->func = func;
    }
    ver_acc_id = (acc_id & ID_MASK) |
                     (static_cast<csi_id_t>(type) << TYPE_SHIFT);
    if (func) {
      cilksan_level_assert(DEBUG_BASIC, func->get_node()->is_SBag());
      ver_acc_id |= static_cast<csi_id_t>(
                        static_cast<SBag_t *>(func->get_node())->get_version())
                    << VERSION_SHIFT;
    }
  }

  // Copy assignment
  MemoryAccess_t &operator=(const MemoryAccess_t &copy) {
    if (func != copy.func) {
      if (copy.func)
        copy.func->inc_ref_count();
      if (func)
        func->dec_ref_count();
      func = copy.func;
    }
    ver_acc_id = copy.ver_acc_id;

    return *this;
  }

  // Move assignment
  MemoryAccess_t &operator=(MemoryAccess_t &&move) {
    if (func)
      func->dec_ref_count();
    func = move.func;
    ver_acc_id = move.ver_acc_id;
    return *this;
  }

  void inc_ref_counts(int64_t count) {
    assert(func);
    func->inc_ref_count(count);
  }

  void dec_ref_counts(int64_t count) {
    if (!func->dec_ref_count(count))
      func = nullptr;
  }

  void inherit(const MemoryAccess_t &copy) {
    if (func)
      func->dec_ref_count();
    func = copy.func;
    ver_acc_id = copy.ver_acc_id;
  }

  // TODO: Get rid of PC from these comparisons
  bool operator==(const MemoryAccess_t &that) const {
    return (func == that.func);
  }

  bool operator!=(const MemoryAccess_t &that) const {
    return !(*this == that);
  }

  inline friend
  std::ostream& operator<<(std::ostream &os, const MemoryAccess_t &acc) {
    os << "function " << acc.func->get_node()->get_func_id()
       << ", acc id " << acc.getAccID() << ", type " << acc.getAccType()
       << ", version " << acc.getVersion();
    return os;
  }

  // // Simple free-list allocator to conserve space and time in managing
  // // arrays of PAGE_SIZE MemoryAccess_t objects.
  // struct FreeNode_t {
  //   // static size_t FreeNode_ObjSize;
  //   FreeNode_t *next;
  // };
  // // TODO: Generalize this.
  // static const unsigned numFreeLists = 6;
  // static FreeNode_t *free_list[numFreeLists];

  // void *operator new[](size_t size) {
  //   unsigned lgSize = __builtin_ctzl(size);
  //   // if (!FreeNode_t::FreeNode_ObjSize)
  //   //   FreeNode_t::FreeNode_ObjSize = size;
  //   if (free_list[lgSize]) {
  //     // assert(size == FreeNode_t::FreeNode_ObjSize);
  //     FreeNode_t *new_node = free_list[lgSize];
  //     free_list[lgSize] = free_list[lgSize]->next;
  //     return new_node;
  //   }
  //   // std::cerr << "MemoryAccess_t::new[] called, size " << size << "\n";
  //   return ::operator new[](size);
  // }

  // void operator delete[](void *ptr) {
  //   FreeNode_t *del_node = reinterpret_cast<FreeNode_t *>(ptr);
  //   del_node->next = free_list;
  //   free_list = del_node;
  // }

  // static void cleanup_freelist() {
  //   for (unsigned i = 0; i < numFreeLists; ++i) {
  //     FreeNode_t *node = free_list[i];
  //     FreeNode_t *next = nullptr;
  //     while (node) {
  //       next = node->next;
  //       ::operator delete[](node);
  //       node = next;
  //     }
  //   }
  // }
};

#endif  // __DICTIONARY__
