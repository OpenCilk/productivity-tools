/* -*- Mode: C++ -*- */

#ifndef _DISJOINTSET_H
#define _DISJOINTSET_H

#include "aligned_alloc.h"
#include "debug_util.h"
#include "race_info.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if DISJOINTSET_DEBUG
#define WHEN_DISJOINTSET_DEBUG(stmt) do { stmt; } while(0)
#else
#define WHEN_DISJOINTSET_DEBUG(stmt)
#endif

// Declarations of Bag-related classes.
class SPBagInterface;
class SBag_t;
class PBag_t;

#if CILKSAN_DEBUG
static int64_t DS_ID = 0;
#endif

// Helper macro to get the size of a struct field.
#define member_size(type, member) sizeof(((type *)0)->member)

template <typename DISJOINTSET_DATA_T>
class DisjointSet_t {
  struct ParentOrBag_t {
  private:
    // The low 2 bits of _ptr encode whether this pointer points to a bag (bit
    // 0), and if so, what type (bit 1).
    static constexpr uintptr_t BAG_DATA_MASK = 3UL;
    static constexpr uintptr_t IS_BAG_MASK = 1UL;
    static constexpr uintptr_t SBAG_DATA = 1UL;
    static constexpr uintptr_t PBAG_DATA = 3UL;

    uintptr_t _ptr = reinterpret_cast<uintptr_t>(nullptr);

    void set(uintptr_t bag_data, uintptr_t ptr) {
      _ptr = bag_data | ptr;
    }

    ParentOrBag_t() = delete;

  public:
    explicit ParentOrBag_t(DisjointSet_t *Parent) { setParent(Parent); }
    explicit ParentOrBag_t(SBag_t *Bag) { setSBag(Bag); }
    explicit ParentOrBag_t(PBag_t *Bag) { setPBag(Bag); }
    ParentOrBag_t(const ParentOrBag_t &copy) = default;
    ParentOrBag_t(ParentOrBag_t &&move) = default;

    bool isBag() const { return _ptr & IS_BAG_MASK; }
    bool isSBag() const { return (_ptr & BAG_DATA_MASK) == SBAG_DATA; }
    bool isPBag() const { return (_ptr & BAG_DATA_MASK) == PBAG_DATA; }
    bool isParent() const { return !(_ptr & IS_BAG_MASK); }

    DisjointSet_t *getParent() const {
      cilksan_assert((_ptr & BAG_DATA_MASK) == 0);
      return reinterpret_cast<DisjointSet_t *>(_ptr);
    }
    SPBagInterface *getBag() const {
      return reinterpret_cast<SPBagInterface *>(_ptr & ~BAG_DATA_MASK);
    }
    SBag_t *getSBag() const {
      return reinterpret_cast<SBag_t *>(_ptr & ~BAG_DATA_MASK);
    }
    PBag_t *getPBag() const {
      return reinterpret_cast<PBag_t *>(_ptr & ~BAG_DATA_MASK);
    }

    void setParent(DisjointSet_t *Parent) {
      set(0, reinterpret_cast<uintptr_t>(Parent));
    }
    void setSBag(SBag_t *Bag) {
      set(SBAG_DATA, reinterpret_cast<uintptr_t>(Bag));
    }
    void setPBag(PBag_t *Bag) {
      set(PBAG_DATA, reinterpret_cast<uintptr_t>(Bag));
    }
  };

public:
  class List_t {
  private:
    const int _DEFAULT_CAPACITY = 128;

    int _length = 0;
    DisjointSet_t **_list = nullptr;
#if DISJOINTSET_DEBUG
    bool _locked = false;
#endif
    int _capacity = 0;

  public:
    List_t()
        : _length(0),
#if DISJOINTSET_DEBUG
          _locked(false),
#endif
          _capacity(_DEFAULT_CAPACITY) {

      _list = (DisjointSet_t **)malloc(_capacity * sizeof(DisjointSet_t *));
    }

    // Returns a pointer to the first element in the list. Elements are stored
    // contiguously.
    //
    // This must be called again after a push() in case the list has changed.
    //
    // The ordering of the elements will not be changed, even if the result
    // changes.
    __attribute__((always_inline)) DisjointSet_t **list() {
      return _list;
    }

    // The length of the list. Automically reset to 0 on unlock().
    __attribute__((always_inline)) int length() { return _length; }

    // Crashes the program if lock() is called a second time before unlock().
    __attribute__((always_inline)) void lock() {
      WHEN_DISJOINTSET_DEBUG(cilksan_assert(!_locked));

#if DISJOINTSET_DEBUG
      _locked = true;
#endif
    }

    __attribute__((always_inline)) void unlock() {
      WHEN_DISJOINTSET_DEBUG(cilksan_assert(_locked));

#if DISJOINTSET_DEBUG
      _locked = false;
#endif
      _length = 0;
    }

    // Reclaims any memory used by the list. Should be called at the end of the
    // program.
    __attribute__((always_inline)) void free_list() {
      WHEN_DISJOINTSET_DEBUG(cilksan_assert(!_locked));

      if (_list != NULL)
        free(_list);
    }

    // Adds an element to the end of the list.
    __attribute__((always_inline)) void push(DisjointSet_t *obj) {
      WHEN_DISJOINTSET_DEBUG(cilksan_assert(_locked));

      if (__builtin_expect(_length == _capacity, false)) {
        _capacity *= 2;
        _list = (DisjointSet_t **)realloc(_list,
                                          _capacity * sizeof(DisjointSet_t *));
      }

      _list[_length++] = obj;
    }
  };

  static List_t &disjoint_set_list;

private:
  // Pointer to either the parent disjoint-set node or a bag.
  mutable ParentOrBag_t _parent_or_bag;
  const DISJOINTSET_DATA_T _data;
  uint64_t _rank; // roughly as the height of this node

  mutable int64_t _ref_count;

#if DISJOINTSET_DEBUG
public:
  int64_t _ID;
private:
  bool _destructing;
#endif

  __attribute__((always_inline)) void assert_not_freed() const {
    WHEN_DISJOINTSET_DEBUG(cilksan_level_assert(
        DEBUG_DISJOINTSET, _destructing || _ref_count >= 0));
  }

  __attribute__((always_inline)) bool isRoot() const {
    return _parent_or_bag.isBag();
  }

  // Frees the old parent if it has no more references.
  __attribute__((always_inline)) void internal_set_parent(DisjointSet_t *that) {
    assert_not_freed();

    ParentOrBag_t old_parent(this->_parent_or_bag);
    this->_parent_or_bag.setParent(that);
    that->inc_ref_count();

    cilksan_level_assert(DEBUG_DISJOINTSET, old_parent.isParent());

    DisjointSet_t *old_djs = old_parent.getParent();
    // dec_ref_count checks whether a node is its only reference (through
    // parent). If we called dec_ref_count (removing the parent relationship)
    // before setting this's parent and we had another reference besides the
    // parent relationship, dec_ref_count would incorrectly believe that this's
    // only reference is in having itself as a parent.
    cilksan_level_assert(DEBUG_DISJOINTSET, old_djs != NULL);

    old_djs->dec_ref_count();
    WHEN_DISJOINTSET_DEBUG(
        DBG_TRACE(DEBUG_DISJOINTSET,
                  "DS %ld (refcnt %ld) points to DS %ld (refcnt %ld)\n", _ID,
                  _ref_count, that->_ID, that->_ref_count));
  }

  // Frees the old parent if it has no more references.
  __attribute__((always_inline)) void
  root_set_parent(DisjointSet_t *that) const {
    assert_not_freed();

    ParentOrBag_t old_parent(this->_parent_or_bag);
    this->_parent_or_bag.setParent(that);
    that->inc_ref_count();

    (void)old_parent;
    cilksan_level_assert(DEBUG_DISJOINTSET, !old_parent.isParent());

    WHEN_DISJOINTSET_DEBUG(
        DBG_TRACE(DEBUG_DISJOINTSET,
                  "DS %ld (refcnt %ld) points to DS %ld (refcnt %ld)\n", _ID,
                  _ref_count, that->_ID, that->_ref_count));
  }

  /*
   * Links this disjoint set to that disjoint set.
   * Don't need to be public.
   *
   * @param that that disjoint set.
   */
  __attribute__((always_inline)) DisjointSet_t *link(DisjointSet_t *that) {
    assert_not_freed();
    cilksan_assert(that != NULL);

    // link the node with smaller height into the node with larger height
    if (this->_rank > that->_rank) {
      that->root_set_parent(this);
      return this;
    } else {
      this->root_set_parent(that);
      if (this->_rank == that->_rank)
	++that->_rank;
      return that;
    }
  }

  /*
   * Finds the set containing this disjoint set element.
   *
   * Note: Performs path compression along the way.
   *       The _set_parent field will be updated after the call.
   */
  __attribute__((always_inline)) DisjointSet_t *find_set() const {
    assert_not_freed();
    WHEN_DISJOINTSET_DEBUG(
        cilksan_level_assert(DEBUG_DISJOINTSET, !_destructing));

    // TODO: Revisit this const_cast.
    DisjointSet_t *node = const_cast<DisjointSet_t *>(this);
    node->assert_not_freed();
    // Fast test to see if node the root.
    if (node->isRoot())
      return node;
    // Fast test to see if the parent is the root.
    DisjointSet_t *parent = node->_parent_or_bag.getParent();
    if (__builtin_expect(parent->isRoot(), true))
      return parent;

    // Fast test failed.  Traverse the list to get to the set, and do path
    // compression along the way.

    disjoint_set_list.lock();

#if DISJOINTSET_DEBUG
    int64_t tmp_ref_count = _ref_count;
#endif

    while (node->_parent_or_bag.isParent()) {
      cilksan_level_assert(DEBUG_DISJOINTSET,
                           node->_parent_or_bag.getParent() != nullptr);

      DisjointSet_t *prev = node;
      node = node->_parent_or_bag.getParent();
      if (node->_parent_or_bag.isParent())
        disjoint_set_list.push(prev);
    }

    WHEN_DISJOINTSET_DEBUG(cilksan_assert(tmp_ref_count == _ref_count));

    // node is now the root. Perform path compression by updating the parents
    // of each of the nodes we saw.
    // We process backwards so that in case a node ought to be freed (i.e. its
    // child was the last referencing it), we don't process it after freeing.
    for (int i = disjoint_set_list.length() - 1; i >= 0; i--) {
      DisjointSet_t *p = disjoint_set_list.list()[i];
      // We don't need to check that p != p->_set_parent because the root of
      // the set wasn't pushed to the list (see the while loop above).
      p->internal_set_parent(node);
    }

    disjoint_set_list.unlock();
    return node;
  }

  DisjointSet_t() = delete;
  DisjointSet_t(const DisjointSet_t &) = delete;
  DisjointSet_t(DisjointSet_t &&) = delete;

public:
  explicit DisjointSet_t(DISJOINTSET_DATA_T data, SBag_t *bag);
  explicit DisjointSet_t(DISJOINTSET_DATA_T data, PBag_t *bag);

#if CILKSAN_DEBUG
  static long debug_count;
  static uint64_t nodes_created;
#endif

  ~DisjointSet_t() {
    WHEN_DISJOINTSET_DEBUG(_destructing = true);
    if (!isRoot()) {
      _parent_or_bag.getParent()->dec_ref_count();
    }

    WHEN_CILKSAN_DEBUG({
      WHEN_DISJOINTSET_DEBUG({
        DBG_TRACE(DEBUG_DISJOINTSET, "Deleting DS %ld\n", _ID);
        _destructing = false;
      });
      _ref_count = -1;

      debug_count--;
    });
  }

  // Decrements the ref count.  Returns true if the node was deleted
  // as a result.
  __attribute__((always_inline)) int64_t dec_ref_count(int64_t count = 1) {
    assert_not_freed();
    cilksan_level_assert(DEBUG_DISJOINTSET, _ref_count >= count);
    _ref_count -= count;
    WHEN_DISJOINTSET_DEBUG(DBG_TRACE(
        DEBUG_DISJOINTSET, "DS %ld dec_ref_count to %ld\n", _ID, _ref_count));
    if (_ref_count == 0) {
      delete this;
      return 0;
    }
    return _ref_count;
  }

  __attribute__((always_inline)) void inc_ref_count(int64_t count = 1) const {
    assert_not_freed();

    _ref_count += count;
    WHEN_DISJOINTSET_DEBUG(DBG_TRACE(
        DEBUG_DISJOINTSET, "DS %ld inc_ref_count to %ld\n", _ID, _ref_count));
  }

  __attribute__((always_inline)) DISJOINTSET_DATA_T get_data() const {
    assert_not_freed();
    return _data;
  }

  __attribute__((always_inline)) SPBagInterface *get_bag() const {
    assert_not_freed();
    return find_set()->_parent_or_bag.getBag();
  }

  __attribute__((always_inline)) bool is_sbag() const {
    assert_not_freed();
    return find_set()->_parent_or_bag.isSBag();
  }
  __attribute__((always_inline)) bool is_pbag() const {
    assert_not_freed();
    return find_set()->_parent_or_bag.isPBag();
  }

  __attribute__((always_inline)) SBag_t *get_sbag() const {
    assert_not_freed();
    return find_set()->_parent_or_bag.getSBag();
  }
  __attribute__((always_inline)) PBag_t *get_pbag() const {
    assert_not_freed();
    return find_set()->_parent_or_bag.getPBag();
  }

  __attribute__((always_inline)) SBag_t *get_sbag_or_null() const {
    assert_not_freed();
    ParentOrBag_t parent_or_bag = find_set()->_parent_or_bag;
    if (parent_or_bag.isSBag())
      return parent_or_bag.getSBag();
    return nullptr;
  }

  __attribute__((always_inline)) void set_sbag(SBag_t *bag) {
    _parent_or_bag.setSBag(bag);
  }
  __attribute__((always_inline)) void set_pbag(PBag_t *bag) {
    _parent_or_bag.setPBag(bag);
  }

  /*
   * Union this disjoint set and that disjoint set.
   *
   * NOTE: Implicitly, in order to maintain the oldest _set_node, one should
   * always combine younger set into this set (defined by creation time).  Since
   * we union by rank, we may end up linking this set to the younger set.  To
   * make sure that we always return the oldest _node to represent the set, we
   * use an additional _set_node field to keep track of the oldest node and use
   * that to represent the set.
   *
   * @param that that (younger) disjoint set.
   */
  // Called "combine," because "union" is a reserved keyword in C.
  __attribute__((always_inline))
  DisjointSet_t *combine(DisjointSet_t *that) {
    assert_not_freed();

    cilksan_assert(that);
    cilksan_assert(this->find_set() != that->find_set());
    DisjointSet_t *root = this->find_set()->link(that->find_set());
    cilksan_assert(this->find_set() == that->find_set());
    return root;
  }

  static void cleanup() { disjoint_set_list.free_list(); }

  // Custom memory allocation for disjoint sets.
  struct DSSlab_t {
    // System-page size.
    static constexpr unsigned SYS_PAGE_SIZE = 4096;
    // Mask to get sub-system-page portion of a memory address.
    static constexpr uintptr_t SYS_PAGE_DATA_MASK = SYS_PAGE_SIZE - 1;
    // Mask to get the system page of a memory address.
    static constexpr uintptr_t SYS_PAGE_MASK = ~SYS_PAGE_DATA_MASK;

    static size_t PAGE_ALIGNED(size_t size) {
      return (size & SYS_PAGE_MASK) +
             ((size & SYS_PAGE_DATA_MASK) == 0 ? 0 : SYS_PAGE_SIZE);
    }

    DSSlab_t *Next = nullptr;
    DSSlab_t *Prev = nullptr;

    static constexpr int UsedMapSize = 2;
    uint64_t UsedMap[UsedMapSize] = { 0 };

    static const size_t NumDJSets =
      (SYS_PAGE_SIZE - (2 * sizeof(DSSlab_t *)) - sizeof(uint64_t[UsedMapSize]))
      / sizeof(DisjointSet_t);

    alignas(DisjointSet_t) char DJSets[NumDJSets * sizeof(DisjointSet_t)];

    DSSlab_t() { UsedMap[UsedMapSize - 1] |= ~((1UL << (NumDJSets % 64)) - 1); }

    // Returns true if this slab contains no free lines.
    bool isFull() const {
      for (int i = 0; i < UsedMapSize; ++i)
        if (UsedMap[i] != static_cast<uint64_t>(-1))
          return false;
      return true;
    }

    // Get a free disjoint set from the slab, marking that disjoint set as used
    // in the process.  Returns nullptr if no free disjoint set is available.
    DisjointSet_t *getFreeDJSet() __attribute__((malloc)) {
      for (int i = 0; i < UsedMapSize; ++i) {
        uint64_t UsedMapVal = UsedMap[i];
        if (UsedMapVal == static_cast<uint64_t>(-1))
          continue;

        // Get the free line.
        DisjointSet_t *DJSet = reinterpret_cast<DisjointSet_t *>(
            &DJSets[(64 * i + __builtin_ctzl(UsedMapVal + 1)) * sizeof(DisjointSet_t)]);

        // Mark the line as used.
        UsedMap[i] |= UsedMapVal + 1;

        return DJSet;
      }
      // No free lines in this slab.
      return nullptr;
    }

    // Returns a line to this slab, marking that line as available.
    void returnDJSet(__attribute__((noescape)) DisjointSet_t *DJSet) {
      uintptr_t DJSetPtr = reinterpret_cast<uintptr_t>(DJSet);
      cilksan_assert((DJSetPtr & SYS_PAGE_MASK) ==
                         reinterpret_cast<uintptr_t>(this) &&
                     "Disjoint set does not belong to this slab.");

      // Compute the index of this line in the array.
      uint64_t DJSetIdx = DJSetPtr & SYS_PAGE_DATA_MASK;
      DJSetIdx -= offsetof(DSSlab_t, DJSets);
      DJSetIdx /= sizeof(DisjointSet_t);

      // Mark the line as available in the map.
      uint64_t MapIdx = DJSetIdx / 64;
      uint64_t MapBit = DJSetIdx % 64;

      cilksan_assert(MapIdx < UsedMapSize && "Invalid MapIdx.");
      cilksan_assert(0 != (UsedMap[MapIdx] & (1UL << MapBit)) &&
                     "Disjoint set is not marked used.");
      UsedMap[MapIdx] &= ~(1UL << MapBit);
    }
  };

  class DSAllocator {
    static_assert(member_size(DisjointSet_t::DSSlab_t, UsedMap) * 8 >=
                      member_size(DisjointSet_t::DSSlab_t, DJSets) /
                          sizeof(DisjointSet_t),
                  "Bad size for DSSlab_t.UsedMap");
    static_assert((member_size(DisjointSet_t::DSSlab_t, UsedMap) * 8) - 64 <
                      member_size(DisjointSet_t::DSSlab_t, DJSets) /
                          sizeof(DisjointSet_t),
                  "Inefficient size for DSSlab_t.UsedMap");

    DSSlab_t *FreeSlabs = nullptr;
    DSSlab_t *FullSlabs = nullptr;

  public:
    DSAllocator() {
      FreeSlabs = new (my_aligned_alloc(
          DSSlab_t::SYS_PAGE_SIZE, DSSlab_t::PAGE_ALIGNED(sizeof(DSSlab_t))))
          DSSlab_t;
    }

    ~DSAllocator() {
      cilksan_assert(!FullSlabs && "Full slabs remaining.");
      // Destruct the free slabs and free their memory.
      DSSlab_t *Slab = FreeSlabs;
      DSSlab_t *PrevSlab = nullptr;
      while (Slab) {
        PrevSlab = Slab;
        Slab = Slab->Next;
        PrevSlab->~DSSlab_t();
        free(PrevSlab);
      }
      FreeSlabs = nullptr;
    }

    DisjointSet_t *getDJSet() __attribute__((malloc)) {
      DSSlab_t *Slab = FreeSlabs;
      DisjointSet_t *DJSet = Slab->getFreeDJSet();

      // If Slab is now full, move it to the Full list.
      if (Slab->isFull()) {
        if (!Slab->Next)
          // Allocate a new slab if necessary.
          FreeSlabs =
              new (my_aligned_alloc(DSSlab_t::SYS_PAGE_SIZE,
                                    DSSlab_t::PAGE_ALIGNED(sizeof(DSSlab_t))))
                  DSSlab_t;
        else {
          Slab->Next->Prev = nullptr;
          FreeSlabs = Slab->Next;
        }
        // Push slab to the beginning of the full list.
        Slab->Next = FullSlabs;
        if (FullSlabs)
          FullSlabs->Prev = Slab;
        FullSlabs = Slab;
      }

      cilksan_assert(DJSet && "No disjoinst set found.");
      return DJSet;
    }

    void freeDJSet(__attribute__((noescape)) void *Ptr) {
      // Derive the pointer to the slab.
      DSSlab_t *Slab = reinterpret_cast<DSSlab_t *>(
          reinterpret_cast<uintptr_t>(Ptr) & DSSlab_t::SYS_PAGE_MASK);

      if (Slab->isFull()) {
        // Slab is no longer full, so move it back to the free list.
        if (Slab->Prev)
          Slab->Prev->Next = Slab->Next;
        else
          FullSlabs = Slab->Next;

        // Make Slab's successor point to Slab's predecessor.
        if (Slab->Next)
          Slab->Next->Prev = Slab->Prev;

        // Push Slab to the start of the free list.
        Slab->Prev = nullptr;
        Slab->Next = FreeSlabs;
        FreeSlabs->Prev = Slab;
        FreeSlabs = Slab;
      } else if (FreeSlabs != Slab) {
        // Remove Slab from its place in FreeSlabs.
        Slab->Prev->Next = Slab->Next;
        if (Slab->Next)
          Slab->Next->Prev = Slab->Prev;

        // Move Slab to the start of FreeSlabs.
        Slab->Prev = nullptr;
        Slab->Next = FreeSlabs;
        FreeSlabs->Prev = Slab;
        FreeSlabs = Slab;
      }

      Slab->returnDJSet(static_cast<DisjointSet_t *>(Ptr));
    }
  };

  // // Simple free-list allocator to conserve space and time in managing
  // // DisjointSet_t objects.
  // static DisjointSet_t *free_list;
  static DSAllocator &Alloc;

  void *operator new(size_t size) {
    return Alloc.getDJSet();
    // if (free_list) {
    //   DisjointSet_t *new_node = free_list;
    //   free_list = free_list->_set_parent;
    //   return new_node;
    // }
    // return ::operator new(size);
  }

  void operator delete(__attribute__((noescape)) void *ptr) {
    Alloc.freeDJSet(ptr);
    // DisjointSet_t *del_node = reinterpret_cast<DisjointSet_t *>(ptr);
    // del_node->_set_parent = free_list;
    // free_list = del_node;
  }

  // static void cleanup_freelist() {
  //   DisjointSet_t *node = free_list;
  //   DisjointSet_t *next = nullptr;
  //   while (node) {
  //     next = node->_set_parent;
  //     ::operator delete(node);
  //     node = next;
  //   }
  // }
};

template <>
DisjointSet_t<call_stack_t>::DisjointSet_t(call_stack_t data, SBag_t *bag);
template <>
DisjointSet_t<call_stack_t>::DisjointSet_t(call_stack_t data, PBag_t *bag);

template <>
DisjointSet_t<call_stack_t>::DSAllocator &DisjointSet_t<call_stack_t>::Alloc;

template <>
DisjointSet_t<call_stack_t>::List_t
    &DisjointSet_t<call_stack_t>::disjoint_set_list;

#if CILKSAN_DEBUG
template<>
long DisjointSet_t<call_stack_t>::debug_count;
#endif

#endif // #ifndef _DISJOINTSET_H
