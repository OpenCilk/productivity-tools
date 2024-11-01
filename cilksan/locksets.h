/* -*- Mode: C++ -*- */

#ifndef _LOCKSETS_H
#define _LOCKSETS_H

#include "debug_util.h"
#include "dictionary.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

enum IntersectionResult_t : uint8_t {
  EMPTY = 0x0,
  NONEMPTY = 0x1,
  L_SUBSET_OF_R = 0x2,
  L_SUPERSET_OF_R = 0x4,
  L_EQUAL_R = L_SUBSET_OF_R | L_SUPERSET_OF_R,
};

using LockID_t = uint64_t;

// Class representing a set of locks held during an access.
class LockSet_t {
private:
  LockID_t *IDs = nullptr;
  size_t end = 0;
  size_t capacity = 1;

  void resize(size_t new_capacity) {
    // If we haven't yet allocated the IDs array, create a new array.
    if (!IDs) {
      IDs = new LockID_t[new_capacity];
      capacity = new_capacity;
      return;
    }

    // Save a pointer to the old IDs array.
    LockID_t *oldIDs = IDs;

    // Allocate a new IDs array;
    IDs = new LockID_t[new_capacity];

    // Copy from oldIDs into the new IDs array.
    size_t copy_end = capacity > new_capacity ? new_capacity : capacity;
    for (size_t i = 0; i < copy_end; ++i)
      IDs[i] = oldIDs[i];

    // Update the capacity
    capacity = new_capacity;

    // Delete the old IDs array
    delete[] oldIDs;
  }
public:
  // Default constructor
  LockSet_t() {
    IDs = new LockID_t[capacity];
  }
  // Copy constructor
  LockSet_t(const LockSet_t &copy)
      : end(copy.end), capacity(copy.capacity) {
    IDs = new LockID_t[capacity];
    for (size_t i = 0; i < end; ++i)
      IDs[i] = copy.IDs[i];
  }
  // Move constructor
  LockSet_t(const LockSet_t &&move)
      : IDs(move.IDs), end(move.end), capacity(move.capacity) {}

  // Destructor
  ~LockSet_t() {
    if (IDs) {
      delete[] IDs;
      IDs = nullptr;
    }
  }

  // Return true if the lockset is empty, false otherwise.
  bool isEmpty() const { return 0 == end; }

  // Return the number of elements in this lockset.
  size_t size() const { return end; }

  // Get the lock ID at index i in this lockset.
  LockID_t &operator[](size_t i) const { return IDs[i]; }

  // Insert a new lock ID into this lockset.
  void insert(LockID_t new_lock_id) {
    if (end == capacity)
      resize(2 * capacity);

    // Scan the IDs until we find where to insert the new ID to maintain the
    // sorted order.
    size_t i = 0;
    while (i < end && IDs[i] < new_lock_id)
      ++i;

    // If we found the new lock ID already in this lockset, return early.
    if (i < end && IDs[i] == new_lock_id) {
      return;
    }

    // Move IDs at position >= i forward by 1.
    for (size_t j = end; j > i; --j)
      IDs[j] = IDs[j-1];

    // Insert the new ID
    IDs[i] = new_lock_id;

    // Update size
    ++end;
  }

  // Remove the specified lock ID from this lockset.
  void remove(LockID_t lock_id) {
    // Scan the IDs until we find the given lock ID.
    size_t i = 0;
    while (i < end && IDs[i] < lock_id)
      ++i;

    // cilksan_assert((IDs[i] == lock_id) &&
    //                "Lock ID to remove is not in this lockset.");
    if (IDs[i] != lock_id)
      fprintf(stderr, "  Lock ID to remove is not in this lockset.\n");

    // Move the IDs greater than the given lock ID back by 1.
    for (size_t j = i; j < end - 1; ++j)
      IDs[j] = IDs[j+1];

    // Update size
    --end;
  }

  static IntersectionResult_t intersect(const LockSet_t &LHS,
                                        const LockSet_t &RHS) {
    // If both LockSets are empty, return EMPTY.
    if (LHS.isEmpty() || RHS.isEmpty())
      return EMPTY;

    size_t Lsize = LHS.size(), Rsize = RHS.size();
    size_t Li = 0, Ri = 0;
    IntersectionResult_t result = L_EQUAL_R;
    do {
      // Advance through the LHS lockset until we reach a lock that is greater
      // than or equal to the lock at the start of RHS.
      if (Ri < Rsize) {
        while (Li < Lsize && LHS[Li] < RHS[Ri]) {
          // We found a lock in LHS that is not in RHS, so L is not a subset of
          // R.
          result = static_cast<IntersectionResult_t>(
              static_cast<uint8_t>(result) &
              ~static_cast<uint8_t>(L_SUBSET_OF_R));
          // Return early when we find the locksets share a lock but also both
          // contain distinct locks.
          if (NONEMPTY == result)
            return result;
          ++Li;
        }
      }

      // Advance through the RHS lockset until we reach a lock that is greater
      // than or equal to the lock at the start of LHS.
      if (Li < Lsize) {
        while (Ri < Rsize && RHS[Ri] < LHS[Li]) {
          // We found a lock in RHS that is not in LHS, so R is not a subset of
          // L.
          result = static_cast<IntersectionResult_t>(
              static_cast<uint8_t>(result) &
              ~static_cast<uint8_t>(L_SUPERSET_OF_R));
          // Return early when we find the locksets share a lock but also both
          // contain distinct locks.
          if (NONEMPTY == result)
            return result;
          ++Ri;
        }
      }

      // Check if we have found that both locksets contain the same lock.
      if (Li < Lsize && Ri < Rsize && LHS[Li] == RHS[Ri]) {
        // Mark that we found a lock in common.
        result = static_cast<IntersectionResult_t>(
            static_cast<uint8_t>(result) | static_cast<uint8_t>(NONEMPTY));
        // Return early when we find the locksets share a lock but also both
        // contain distinct locks.
        if (NONEMPTY == result)
          return result;
        ++Li;
        ++Ri;
      }
    } while (Li < Lsize && Ri < Rsize);

    return result;
  }

  // Comparison operators for sorting
  bool operator<(const LockSet_t &RHS) const {
    // If this is empty, then this < RHS as long as RHS is not empty
    if (isEmpty())
      return !RHS.isEmpty();

    if (RHS.isEmpty())
      // this is non-empty, but RHS is empty, so this > RHS
      return false;

    size_t Lsize = size(), Rsize = RHS.size();
    size_t Li = 0, Ri = 0;
    while (Li < Lsize && Ri < Rsize) {
      if ((*this)[Li] < RHS[Ri])
        return true;
      if ((*this)[Li] > RHS[Ri])
        return false;
      // These locks are equal.  Go to the next lock
      ++Li;
      ++Ri;
    }

    // this is a prefix of RHS or vice versa.  Return true if this is smaller than
    // RHS, false otherwise.
    return Lsize < Rsize;
  }

  bool operator==(const LockSet_t &RHS) const {
    // If either lockset is empty, then the locksets are equal only if they are
    // both empty.
    if (isEmpty() || RHS.isEmpty())
      return (isEmpty() && RHS.isEmpty());

    size_t Lsize = size(), Rsize = RHS.size();
    size_t Li = 0, Ri = 0;
    while (Li < Lsize && Ri < Rsize) {
      if ((*this)[Li] != RHS[Ri])
        return false;
      // These locks are equal.  Go to the next lock
      ++Li;
      ++Ri;
    }

    // this is a prefix of RHS or vice versa.  Return true if this is smaller than
    // RHS, false otherwise.
    return Lsize == Rsize;
  }

  bool operator!=(const LockSet_t &RHS) const {
    return !(*this == RHS);
  }
};

// Class representing a locker, which consists of a lock set and a
// MemoryAccess_t describing the corresponding memory access.
class Locker_t {
public:
  MemoryAccess_t access;
  LockSet_t lockset;
  Locker_t *next = nullptr;

  // Constructor
  Locker_t(const MemoryAccess_t &access, const LockSet_t &lockset,
           Locker_t *next = nullptr)
      : access(access), lockset(lockset), next(next) {}
  // Destructor
  ~Locker_t() {
    if (next) {
      delete next;
      next = nullptr;
    }
  }

  // Get the disjoint-set node for the function associated with this locker
  const MemoryAccess_t &getAccess() const { return access; }
  MemoryAccess_t &getAccess() { return access; }

  // Get the lockset for this locker
  const LockSet_t &getLockSet() const { return lockset; }
  LockSet_t &getLockSet() { return lockset; }

  Locker_t *&getNext() { return next; }
  void setNext(Locker_t *locker) { next = locker; }

  bool operator==(const Locker_t &that) const {
    return (access == that.access) && (lockset == that.lockset);
  }
  bool operator!=(const Locker_t &that) const { return !(*this == that); }
  bool operator<(const Locker_t &that) const {
    uintptr_t thisFunc = reinterpret_cast<uintptr_t>(access.getFunc());
    uintptr_t thatFunc = reinterpret_cast<uintptr_t>(that.access.getFunc());
    return (thisFunc < thatFunc) ||
           ((thisFunc == thatFunc) && (lockset < that.lockset));
  }
};

// Type definition for a list of lockers.
class LockerList_t {
public:
  Locker_t *head = nullptr;

  LockerList_t() = default;
  LockerList_t(const LockerList_t &copy) {
    // Perform a deep copy of the list of lockers
    Locker_t *next_locker = copy.head;
    Locker_t **prev = &head;
    while (next_locker) {
      *prev = new Locker_t(next_locker->getAccess(), next_locker->getLockSet());
      prev = &(*prev)->next;
      next_locker = next_locker->getNext();
    }
  }
  ~LockerList_t() {
    if (head) {
      delete head;
      head = nullptr;
    }
  }

  LockerList_t &operator=(const LockerList_t &copy) {
    if (head)
      delete head;

    // Perform a deep copy of the list of lockers
    Locker_t *next_locker = copy.head;
    Locker_t **prev = &head;
    while (next_locker) {
      *prev = new Locker_t(next_locker->getAccess(), next_locker->getLockSet());
      prev = &(*prev)->next;
      next_locker = next_locker->getNext();
    }

    return *this;
  }

  Locker_t *&getHead() { return head; }
  void setHead(Locker_t *locker) { head = locker; }

  bool isValid() const { return nullptr != head; }
  void invalidate() {
    if (head) {
      delete head;
      head = nullptr;
    }
  }

  bool operator==(const LockerList_t &that) const {
    // Check the equality of all lockers in the list
    Locker_t *thisLocker = head;
    Locker_t *thatLocker = that.head;
    while (thisLocker && thatLocker) {
      if (*thisLocker != *thatLocker)
        return false;
      thisLocker = thisLocker->getNext();
      thatLocker = thatLocker->getNext();
    }

    return (nullptr == thisLocker) && (nullptr == thatLocker);
  }

  void insert(Locker_t *locker) {
    cilksan_assert(nullptr == locker->next);
    Locker_t *current = head;
    Locker_t **prev = &head;
    while (current && *current < *locker) {
      prev = &(current->next);
      current = current->next;
    }
    locker->next = current;
    *prev = locker;
  }
};

#endif // #define _LOCKSETS_H
