// -*- c++ -*-
#ifndef _HYPERTABLE_H
#define _HYPERTABLE_H

#include <cstdint>
#include "hyperobject_base.h"

// Helper methods for testing and setting keys.
static const uintptr_t KEY_EMPTY = 0UL;
static const uintptr_t KEY_DELETED = ~0UL;

static bool is_empty(uintptr_t key) { return key == KEY_EMPTY; }
static bool is_tombstone(uintptr_t key) { return key == KEY_DELETED; }
static inline bool is_valid(uintptr_t key) {
  return !is_empty(key) && !is_tombstone(key);
}

class CilkSanImpl_t;

// Hash table of reducers.  We don't need any locking or support for concurrent
// updates, since the hypertable is local.
class hyper_table {
public:
  using index_t = uint32_t;

  // An entry in the hash table.
  struct bucket {
    uintptr_t key = KEY_EMPTY; /* EMPTY, DELETED, or a user-provided pointer. */
    index_t hash = 0; /* hash of the key when inserted into the table. */
    reducer_base value;

    void make_tombstone() { key = KEY_DELETED; }
  };

private:
  // Data type for indexing the hash table.  This type is used for hashes as
  // well as the table's capacity.
  static constexpr int32_t MIN_CAPACITY = 4;
  static constexpr int32_t MIN_HT_CAPACITY = 8;

  // Some random numbers for the hash.
  /* uint64_t seed = 0xe803e76341ed6d51UL; */
  static constexpr uint64_t salt = 0x96b9af4f6a40de92UL;

  static inline index_t hash(uintptr_t key_in) {
    uint64_t x = key_in ^ salt;
    // mix, based on abseil's low-level hash, and convert 64-bit integers into
    // 32-bit integers.
    const size_t half_bits = sizeof(uintptr_t) * 4;
    const uintptr_t low_mask = ((uintptr_t)(1) << half_bits) - 1;
    uintptr_t v = (x & low_mask) * (x >> half_bits);
    return (v & low_mask) ^ (v >> half_bits);
  }

  static inline index_t get_table_entry(int32_t capacity, uintptr_t key) {
    // Assumes capacity is a power of 2.
    return hash(key) & (capacity - 1);
  }

  static inline index_t inc_index(index_t i, index_t capacity) {
    ++i;
    if (i == capacity)
      i = 0;
    return i;
  }

  // For theoretical and practical efficiency, the hash table implements
  // ordered linear probing --- consecutive hashes in the table are
  // always stored in sorted order --- in a circular buffer.
  // Intuitively, this ordering means any hash-table probe for a target
  // T can stop when it encounters an element in the table whose hash is
  // greater than T.
  //
  // Implementing ordered linear probing on a circular buffer, however,
  // leads to several tricky cases when probing for an element or its
  // insertion point.  These cases depend on whether the probe or the
  // run --- the ordered sequence of hashes in the table --- wraps
  // around from the end to the beginning of the buffer's allocated
  // memory.  In general, there are four cases:
  //
  // Example case 1: No wrapping (common case)
  //     Index:  ... | 3 | 4 | 5 | 6 | ...
  //     Hashes: ... | 3 | 3 | 3 | 5 | ...
  //     Target: 4
  //   The probe starts at index 4 and scans increasing indices, stopping when
  //   it sees hash = 5 at index 6.
  //
  // Example case 2: Probe and run both wrap
  //     Index:  | 0 | 1 | 2 | ... | 6 | 7 |
  //     Hashes: | 6 | 7 | 0 | ... | 6 | 6 |
  //     Target: 7
  //   The run of 6's wraps around, as does the probe for 7.
  //
  // Example case 3: Probe does not wrap, run does wrap
  //     Index:  | 0 | 1 | 2 | ... | 6 | 7 |
  //     Hashes: | 6 | 7 | 0 | ... | 6 | 6 |
  //     Target: 0
  //   The run of 6's and 7's wrap around.  The probe for 0 starts in the middle
  //   of this wrapped run and must continue past it, even though the hashes in
  //   the run are larger than the target.
  //
  // Example case 4: Probe wraps, run does not wrap
  //     Index:  | 0 | 1 | 2 | ... | 6 | 7 |
  //     Hashes: | 6 | 0 | 1 | ... | 6 | 6 |
  //     Target: 7
  //   After the wrapped run of 6's is a run starting at 0, which does not wrap.
  //   The probe for 7 wraps around before encountering the 0.  The probe should
  //   stop at that point, even though 0 is smaller than 7.
  //
  // We characterize these four cases in terms of the following variables:
  //
  // - T: The target hash value being probed for.
  // - i: The current index in the table being examined in the probe.
  // - H[i]: The hash value of the key at index i, assuming that table entry is
  //   occupied.
  //
  // We can identify cases where the probe or the run wraps around the end of
  // the circular buffer by comparing i to T (for the probe) and i to H[i] (for
  // the run).  A probe starts at i == T and proceeds to scan increasing values
  // of i (mod table size).  Therefore, we typically expect i >= T and i >=
  // H[i].  But when wrapping occurs, i will be smaller than the hash, that is,
  // i < T when the probe wraps and i < H[i] when the run wraps.
  //
  // We can describe these four cases in terms of these variables as follows:
  //   Normal Probe, Normal Run (NP+NR):   T <= i and H[i] <= i
  //     The probe _terminates_ at i where T < H[i].
  //   Wrapped Probe, Wrapped Run (WP+WR): T > i and H[i] > i
  //     The probe _terminates_ at i where T < H[i].
  //   Normal Probe, Wrapped Run (NP+WR):  T <= i and H[i] > i
  //     The probe _must continue_ even though T < H[i].
  //   Wrapped Probe, Normal Run (WP+NR):  T > i and H[i] <= i
  //     The probe _must terminate_ even though T > H[i].
  //
  // The table uses the following bit trick to handle all of these cases simply:
  //
  //   Continue the probe if and only if i-T <= i-H[i], using an _unsigned
  //   integer_ comparison.
  //
  // Intuitively, this trick makes the case of wrapping around the table
  // coincide with unsigned integer overflow, allowing the same
  // comparison to be used in all cases.
  //
  // We can justify this bit trick in all cases:
  //
  //   NP+NR and WP+WR: The original termination condition, T < H[i], implies
  //   that -T > -H[i].  Adding i to both sides does not affect the comparison.
  //
  //   NP+WR: The wrapped run, H[i] > i, implies that i-H[i] is negative, which
  //   becomes are large positive unsigned integer.  Meanwhile, i-T is a small
  //   positive unsigned integer, because i > T.  Hence, i-T < i-H[i], which
  //   correctly implies that the probe must continue.
  //
  //   WP+NR: The wrapped probe, T > i, implies that i-T is negative, which
  //   becomes a large positive unsigned integer.  Meanwhile, i >= H[i],
  //   implying that i-H[i] is a small positive unsigned integer.  Hence, i-T >
  //   i-H[i], which correctly implies that the probe should stop.
  //
  // Note: One can formulate this bit trick as T-i >= H[i]-i instead, preserving
  // the direction of the inequality.  I formulate the trick this way simply
  // because I prefer that the common case involve comparisons of small positive
  // integers.

  static inline bool continue_probe(index_t tgt, index_t hash, index_t idx) {
    // NOTE: index_t must be unsigned for this check to work.
    return (idx - tgt) <= (idx - hash);
  }

  // Constant used to determine the target maximum load factor.  The table will
  // aim for a maximum load factor of 1 - (1 / LOAD_FACTOR_CONSTANT).
  static constexpr int32_t LOAD_FACTOR_CONSTANT = 16;

  static bool is_overloaded(int32_t occupancy, int32_t capacity) {
    // Set the upper load threshold to be 15/16ths of the capacity.
    return occupancy >
           (LOAD_FACTOR_CONSTANT - 1) * capacity / LOAD_FACTOR_CONSTANT;
  }

  static bool is_underloaded(int32_t occupancy, int32_t capacity) {
    // Set the lower load threshold to be 7/16ths of the capacity.
    return (capacity > MIN_CAPACITY) &&
           (occupancy <=
            ((LOAD_FACTOR_CONSTANT / 2) - 1) * capacity / LOAD_FACTOR_CONSTANT);
  }

  // After enough insertions and deletions have occurred, rebuild the table to
  // fix up tombstones.
  static constexpr int32_t MIN_REBUILD_OP_COUNT = 8;
  static bool time_to_rebuild(int32_t ins_rm_count, int32_t capacity) {
    return (ins_rm_count > MIN_REBUILD_OP_COUNT) &&
           (ins_rm_count > capacity / (4 * LOAD_FACTOR_CONSTANT));
  }

  // Initialize an array of buckets of the given size.
  bucket *bucket_array_create(int32_t array_size) {
    bucket *buckets = new bucket[array_size];
    if (array_size < MIN_HT_CAPACITY) {
      return buckets;
    }
    int32_t tombstone_idx = 0;
    for (int32_t i = 0; i < array_size; ++i) {
      // Graveyard hashing: Insert tombstones at regular intervals.
      // TODO: Check if it's bad for the insertions to rebuild a table to use
      // these tombstones.
      if (tombstone_idx == 2 * LOAD_FACTOR_CONSTANT) {
        buckets[i].make_tombstone();
        tombstone_idx -= 2 * LOAD_FACTOR_CONSTANT;
      } else {
        ++tombstone_idx;
      }
    }
    return buckets;
  }

  // Rebuild the table with the specified capacity.
  bucket *rebuild(int32_t new_capacity) {
    bucket *old_buckets = buckets;
    int32_t old_capacity = capacity;
    int32_t old_occupancy = occupancy;

    buckets = bucket_array_create(new_capacity);
    capacity = new_capacity;
    occupancy = 0;
    // Set count of insertions and removals to prevent insertions into
    // new table from triggering another rebuild.
    ins_rm_count = -old_occupancy;

    for (int32_t i = 0; i < old_capacity; ++i) {
      if (is_valid(old_buckets[i].key)) {
        bool success = insert(old_buckets[i]);
        assert(success && "Failed to insert when resizing table.");
        (void)success;
      }
    }

    assert(occupancy == old_occupancy &&
           "Mismatched occupancy after resizing table.");
    delete[] old_buckets;
    return buckets;
  }

  bucket *find_linear(uintptr_t key) const {
    // Scan the array backwards, since inserts add new entries to
    // the end of the array, and we anticipate that the program
    // will exhibit locality of reference.
    for (int32_t i = occupancy - 1; i >= 0; --i)
      if (buckets[i].key == key)
        return &buckets[i];

    return nullptr;
  }

  bucket *find_hash(uintptr_t key) const {
    int32_t capacity = this->capacity;

    // Target hash
    index_t tgt = get_table_entry(capacity, key);
    bucket *buckets = this->buckets;
    // Start the probe at the target hash
    index_t i = tgt;
    do {
      uintptr_t curr_key = buckets[i].key;
      // Found the key?  Return that bucket.
      // TODO: Consider moving this bucket to the front of the run.
      if (key == curr_key)
        return &buckets[i];

      // Found an empty entry?  The probe failed.
      if (is_empty(curr_key))
        return nullptr;

      // Found a tombstone?  Continue the probe.
      if (is_tombstone(curr_key)) {
        i = inc_index(i, capacity);
        continue;
      }

      // Otherwise, buckets[i] is another valid key that does not match.
      index_t curr_hash = buckets[i].hash;

      if (continue_probe(tgt, curr_hash, i)) {
        i = inc_index(i, capacity);
        continue;
      }

      // If none of the above cases match, then the probe failed to
      // find the key.
      return nullptr;
    } while (i != tgt);

    // The probe failed to find the key.
    return nullptr;
  }

public:
  index_t capacity = MIN_CAPACITY;
  int32_t occupancy = 0;
  int32_t ins_rm_count = 0;
  bucket *buckets = nullptr;

  hyper_table() { buckets = bucket_array_create(capacity); }
  ~hyper_table() { delete[] buckets; }

  // Returns a pointer to the bucket associated with key, if it exists, or
  // nullptr if no bucket is associated with key.
  bucket *find(uintptr_t key) const {
    if (capacity < MIN_HT_CAPACITY) {
        return find_linear(key);
    } else {
        return find_hash(key);
    }
  }

  // Insert the given bucket into the table.  Might move other buckets around
  // inside the table or cause the table to be rebuilt.
  bool insert(bucket b) {
    int32_t capacity = this->capacity;
    bucket *buckets = this->buckets;
    if (capacity < MIN_HT_CAPACITY) {
      // If the table is small enough, just scan the array.
      int32_t occupancy = this->occupancy;

      if (occupancy < capacity) {
        for (int32_t i = 0; i < occupancy; ++i) {
          if (buckets[i].key == b.key) {
            // The key is already in the table.  Overwrite.
            buckets[i] = b;
            return true;
          }
        }

        // The key is not already in the table.  Append the bucket.
        buckets[occupancy] = b;
        ++this->occupancy;
        return true;
      }

      // The small table is full.  Increase its capacity, convert it to a hash
      // table, and fall through to insert the new bucket into that hash table.
      capacity *= 2;
      buckets = rebuild(capacity);
    }

    // If the occupancy is already too high, rebuild the table.
    if (is_overloaded(this->occupancy, capacity)) {
      capacity *= 2;
      buckets = rebuild(capacity);
    } else if (time_to_rebuild(this->ins_rm_count, capacity)) {
      buckets = rebuild(capacity);
    }

    // Target hash
    const index_t tgt = get_table_entry(capacity, b.key);
    b.hash = tgt;

    // If we find an empty entry, insert the bucket there.
    if (is_empty(buckets[tgt].key)) {
      buckets[tgt] = b;
      ++this->occupancy;
      ++this->ins_rm_count;
      return true;
    }

    // Probe for the place to insert b.
    index_t i = tgt;

    const index_t probe_end = tgt;
    do {
      uintptr_t curr_key = buckets[i].key;
      // Found the key?  Overwrite that bucket.
      // TODO: Reconsider what to do in this case.
      if (b.key == curr_key) {
        buckets[i].value = b.value;
        return true;
      }

      // Found an empty entry?  Insert b there.
      if (is_empty(curr_key)) {
        buckets[i] = b;
        ++this->occupancy;
        ++this->ins_rm_count;
        return true;
      }

      // Found a tombstone?
      if (is_tombstone(curr_key)) {
        index_t current_tomb = i;
        // Scan consecutive tombstones from i.
        index_t next_i = inc_index(i, capacity);
        uintptr_t tomb_end = buckets[next_i].key;
        while (next_i != probe_end && is_tombstone(tomb_end)) {
          next_i = inc_index(next_i, capacity);
          tomb_end = buckets[next_i].key;
        }

        // If the next entry is empty, then the probe would stop.  It's
        // safe to insert the bucket at the tombstone at i.
        if (is_empty(tomb_end)) {
          buckets[current_tomb] = b;
          ++this->occupancy;
          ++this->ins_rm_count;
          return true;
        }

        // Check if the hash at the end of this run of tombstones would
        // terminate the probe or if the probe has traversed the whole
        // table.
        index_t tomb_end_hash = buckets[next_i].hash;
        if (next_i == probe_end ||
            !continue_probe(tgt, tomb_end_hash, next_i)) {
          // It's safe to insert b at the current tombstone.
          buckets[current_tomb] = b;
          ++this->occupancy;
          ++this->ins_rm_count;
          return true;
        }

        // None of the locations among these consecutive tombstones are
        // appropriate for this bucket.  Continue the probe.
        i = next_i;
        continue;
      }

      // Otherwise this entry contains another valid key that does
      // not match.  Compare the hashes to decide whether or not to
      // continue the probe.
      index_t curr_hash = buckets[i].hash;
      if (continue_probe(tgt, curr_hash, i)) {
        i = inc_index(i, capacity);
        continue;
      }

      // This is an appropriate location to insert the bucket.  Stop
      // the probe.
      break;
    } while (i != probe_end);

    index_t insert_tgt = i;
    // The search found a place to insert the bucket, but it's occupied.  Insert
    // the bucket here and shift the subsequent entries.
    do {
      // If this entry is empty or a tombstone, insert the current bucket at
      // this location and terminate.
      if (!is_valid(buckets[i].key)) {
        buckets[i] = b;
        ++this->occupancy;
        ++this->ins_rm_count;
        return true;
      }

      // Swap b with the current bucket.
      bucket tmp = buckets[i];
      buckets[i] = b;
      b = tmp;

      // Continue onto the next index.
      i = inc_index(i, capacity);
    } while (i != insert_tgt);

    assert(i != insert_tgt && "Insertion failed.");
    return false;
  }

  // Remove the bucket associated with key from the table.  Might cause the
  // table to be rebuilt.
  bool remove(uintptr_t key) {
    if (this->capacity < MIN_HT_CAPACITY) {
      // If the table is small enough, just scan the array.
      bucket *buckets = this->buckets;
      int32_t occupancy = this->occupancy;

      for (int32_t i = 0; i < occupancy; ++i) {
        if (buckets[i].key == key) {
          if (i == occupancy - 1)
            // Set this entry's key to empty.  This code is here primarily to
            // handle the case where occupancy == 1.
            buckets[i].key = KEY_EMPTY;
          else {
            // Remove this entry by swapping it with the last entry.
            buckets[i] = buckets[occupancy - 1];
            buckets[occupancy - 1].key = KEY_EMPTY;
          }
          // Decrement the occupancy.
          --this->occupancy;
          return true;
        }
      }
      return false;
    }

    // Find the key in the table.
    bucket *entry = find(key);

    // If entry is nullptr, the search did not find the key.
    if (nullptr == entry)
      return false;

    // The search found the key and returned a pointer to the entry.  Replace
    // the entry with a tombstone and decrement the occupancy.
    entry->make_tombstone();
    --this->occupancy;
    ++this->ins_rm_count;

    int32_t capacity = this->capacity;
    if (is_underloaded(this->occupancy, capacity))
      rebuild(capacity / 2);
    else if (time_to_rebuild(this->ins_rm_count, capacity))
      rebuild(capacity);

    return true;
  }

  // Clear all buckets from the table.
  void clear() {
    assert(occupancy > 0);
    if (capacity == MIN_CAPACITY) {
      // If the table is still minimum size, just clear all keys.
      for (int32_t i = 0; i < occupancy; ++i)
        buckets[i].key = KEY_EMPTY;
      occupancy = 0;
      return;
    }
    // Otherwise, replace the old buckets array with a new, empty array.
    delete[] buckets;
    buckets = bucket_array_create(MIN_CAPACITY);
    capacity = MIN_CAPACITY;
    occupancy = 0;
    ins_rm_count = 0;
  }

  static hyper_table *merge_two_hyper_tables(CilkSanImpl_t *__restrict__ tool,
                                             hyper_table *__restrict__ left,
                                             hyper_table *__restrict__ right);
};

#endif // _HYPERTABLE_H
