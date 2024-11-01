// -*- C++ -*-
#ifndef __DICTIONARY__
#define __DICTIONARY__

#include "debug_util.h"
#include "disjointset.h"
#include "frame_data.h"
#include "spbag.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using DS_t = DisjointSet_t<call_stack_t>;

class MemoryAccess_t {
  static constexpr unsigned VERSION_SHIFT = 8 * (sizeof(uintptr_t) - sizeof(version_t));
  static constexpr unsigned TYPE_SHIFT = VERSION_SHIFT - 4;
  static constexpr csi_id_t ID_MASK = ((1UL << TYPE_SHIFT) - 1);
  static constexpr csi_id_t TYPE_MASK = ((1UL << VERSION_SHIFT) - 1) & ~ID_MASK;
  static constexpr csi_id_t UNKNOWN_CSI_ACC_ID = UNKNOWN_CSI_ID & ID_MASK;

  static csi_id_t makeTypedID(csi_id_t acc_id, MAType_t type) {
    return (acc_id & ID_MASK) | (static_cast<csi_id_t>(type) << TYPE_SHIFT);
  }

  static constexpr uintptr_t PTR_MASK = (1UL << VERSION_SHIFT) - 1;
  DS_t *getFuncFromVerFunc() const {
    return reinterpret_cast<DS_t *>(ver_func & PTR_MASK);
  }
  version_t getVersionFromVerFunc() const {
    return static_cast<version_t>(ver_func >> VERSION_SHIFT);
  }

  static uintptr_t makeVerFunc(DS_t *func, version_t version) {
    return reinterpret_cast<uintptr_t>(func) |
           (static_cast<uintptr_t>(version) << VERSION_SHIFT);
  }
  void clearVerFunc() {
    ver_func = reinterpret_cast<uintptr_t>(nullptr);
  }
  bool haveVerFunc() const {
    return reinterpret_cast<uintptr_t>(nullptr) != ver_func;
  }

public:
  uintptr_t ver_func = reinterpret_cast<uintptr_t>(nullptr);
  csi_id_t ver_acc_id = UNKNOWN_CSI_ACC_ID;

  // Default constructor
  MemoryAccess_t() {}
  MemoryAccess_t(DS_t *func, version_t version, csi_id_t acc_id, MAType_t type)
      : ver_func(makeVerFunc(func, version)),
        ver_acc_id(makeTypedID(acc_id, type)) {
    if (func) {
      func->inc_ref_count();
    }
  }
  MemoryAccess_t(DS_t *func, version_t version, csi_id_t typed_id)
      : ver_func(makeVerFunc(func, version)), ver_acc_id(typed_id) {
    if (func) {
      func->inc_ref_count();
    }
  }

  // Copy constructor
  MemoryAccess_t(const MemoryAccess_t &copy)
      : ver_func(copy.ver_func), ver_acc_id(copy.ver_acc_id) {
    if (haveVerFunc())
      getFuncFromVerFunc()->inc_ref_count();
  }

  // Move constructor
  MemoryAccess_t(const MemoryAccess_t &&move)
      : ver_func(move.ver_func), ver_acc_id(move.ver_acc_id) {}

  // Destructor
  ~MemoryAccess_t() {
    if (haveVerFunc()) {
      getFuncFromVerFunc()->dec_ref_count();
      clearVerFunc();
    }
  }

  // Returns true if this MemoryAccess_t is valid, meaning it refers to an
  // actual memory access in the program-under-test.
  bool isValid() const {
    return haveVerFunc();
  }

  // Render this MemoryAccess_t invalid.
  void invalidate() {
    if (haveVerFunc())
      getFuncFromVerFunc()->dec_ref_count();
    clearVerFunc();
    ver_acc_id = UNKNOWN_CSI_ACC_ID;
  }

  // Get the disjoint-set node for the function containing this memory access.
  DS_t *getFunc() const { return getFuncFromVerFunc(); }

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
  version_t getVersion() const { return getVersionFromVerFunc(); }
  AccessLoc_t getLoc() const {
    if (!isValid())
      return AccessLoc_t();
    DS_t *func = getFunc();
    return AccessLoc_t(getAccID(), getAccType(), func->get_data());
  }

  // Set the fields of this MemoryAccess_t directly.  This method is used to
  // avoid unnecessary updates to reference counts that may be incurred by using
  // the copy contructor.
  void set(DS_t *func, version_t version, csi_id_t acc_id, MAType_t type) {
    DS_t *this_func = getFuncFromVerFunc();
    if (this_func != func) {
      if (func)
        func->inc_ref_count();
      if (this_func)
        this_func->dec_ref_count();
      ver_func = makeVerFunc(func, version);
    }
    ver_acc_id = makeTypedID(acc_id, type);
    if (func) {
      cilksan_level_assert(DEBUG_BASIC, func->is_sbag());
    }
  }
  void set(DS_t *func, version_t version, csi_id_t typed_id) {
    DS_t *this_func = getFuncFromVerFunc();
    if (this_func != func) {
      if (func)
        func->inc_ref_count();
      if (this_func)
        this_func->dec_ref_count();
      ver_func = makeVerFunc(func, version);
    }
    ver_acc_id = typed_id;
    if (func) {
      cilksan_level_assert(DEBUG_BASIC, func->is_sbag());
    }
  }

  // Copy assignment
  MemoryAccess_t &operator=(const MemoryAccess_t &copy) {
    if (ver_func != copy.ver_func) {
      if (copy.haveVerFunc())
        copy.getFuncFromVerFunc()->inc_ref_count();
      if (haveVerFunc())
        getFuncFromVerFunc()->dec_ref_count();
      ver_func = copy.ver_func;
    }
    ver_acc_id = copy.ver_acc_id;

    return *this;
  }

  // Move assignment
  MemoryAccess_t &operator=(MemoryAccess_t &&move) {
    if (haveVerFunc())
      getFuncFromVerFunc()->dec_ref_count();
    ver_func = move.ver_func;
    ver_acc_id = move.ver_acc_id;
    return *this;
  }

  bool operator==(const MemoryAccess_t &that) const {
    return (ver_func == that.ver_func);
  }

  bool operator!=(const MemoryAccess_t &that) const {
    return !(*this == that);
  }

#if CILKSAN_DEBUG
  inline friend
  std::ostream& operator<<(std::ostream &os, const MemoryAccess_t &acc) {
    os << "function " << acc.getFunc()
       << ", acc id " << acc.getAccID() << ", type " << acc.getAccType()
       << ", version " << acc.getVersion();
    return os;
  }
#endif

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

  // Logic to check if the given previous MemoryAccess_t is logically in
  // parallel with the current strand.
  __attribute__((always_inline)) static bool
  previousAccessInParallel(MemoryAccess_t *PrevAccess, const FrameData_t *f) {
    // Get the function for this previous access
    uintptr_t ver_func = PrevAccess->ver_func;
    DS_t *Func = reinterpret_cast<DS_t *>(ver_func & PTR_MASK);
    version_t version = static_cast<version_t>(ver_func >> VERSION_SHIFT);

    // Get the Sbag for the previous access or null if the previous access is in
    // a Pbag.
    SBag_t *LCASbagOrNull = Func->get_sbag_or_null();
    return (nullptr == LCASbagOrNull) ||
           f->check_parallel_iter(LCASbagOrNull, version);
  }
  __attribute__((always_inline)) static bool
  previousAccessInParallel(const MemoryAccess_t *PrevAccess,
                           const FrameData_t *f) {
    return previousAccessInParallel(const_cast<MemoryAccess_t *>(PrevAccess),
                                    f);
  }
};

#endif  // __DICTIONARY__
