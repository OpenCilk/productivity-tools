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

// An entry in the hash table.
struct bucket {
  uintptr_t key = KEY_EMPTY; /* EMPTY, DELETED, or a user-provided pointer. */
  reducer_base value;

  void make_tombstone() { key = KEY_DELETED; }
};

// Hash table of reducers.  We don't need any locking or support for concurrent
// updates, since the hypertable is local.
class hyper_table {

  using index_t = uint32_t;

  // Data type for indexing the hash table.  This type is used for hashes as
  // well as the table's capacity.
  static constexpr int32_t MIN_CAPACITY = 4;
  static constexpr int32_t MIN_HT_CAPACITY = 8;

  // Some random numbers for the hash.
  /* uint64_t seed = 0xe803e76341ed6d51UL; */
  static constexpr uint64_t salt = 0x96b9af4f6a40de92UL;

  static inline index_t hash(uintptr_t key_in) {
    uint64_t x = key_in ^ salt;
    // mix64 from SplitMix.
    x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdUL;
    x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53UL;
    return x;
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

  static inline bool continue_search(index_t tgt, index_t hash, index_t i,
                                     uintptr_t tgt_key, uintptr_t key) {
    // Continue the search if the current hash is smaller than the target or if
    // the hashes match and the current key is smaller than the target key.
    //
    // It's possible that the current hash is larger than the target because it
    // belongs to a run that wraps from the end of the table to the beginning.
    // We want to treat such hashes as smaller than the target, unless the
    // target itself is part of such a wrapping run.  To detect such cases,
    // check that the target is smaller than the current index i --- meaning the
    // search has not wrapped --- and the current hash is larger than i ---
    // meaning the current hash is part of a wrapped run.
    return hash <= tgt || (tgt < i && hash > i);
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
      } else
        ++tombstone_idx;
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
    int32_t capacity = this->capacity;
    if (capacity < MIN_HT_CAPACITY) {
      // If the table is small enough, just scan the array.
      struct bucket *buckets = this->buckets;
      int32_t occupancy = this->occupancy;

      // Scan the array backwards, since inserts add new entries to the end of
      // the array, and we anticipate that the program will exhibit locality of
      // reference.
      for (int32_t i = occupancy - 1; i >= 0; --i)
        if (buckets[i].key == key)
          return &buckets[i];

      return nullptr;
    }

    // Target hash
    index_t tgt = get_table_entry(capacity, key);
    struct bucket *buckets = this->buckets;
    // Start the search at the target hash
    index_t i = tgt;
    do {
      uintptr_t curr_key = buckets[i].key;
      // If we find the key, return that bucket.
      // TODO: Consider moving this bucket to the front of the run.
      if (key == curr_key)
        return &buckets[i];

      // If we find an empty entry, the search failed.
      if (is_empty(curr_key))
        return nullptr;

      // If we find a tombstone, continue the search.
      if (is_tombstone(curr_key)) {
        i = inc_index(i, capacity);
        continue;
      }

      // Otherwise we have another valid key that does not match.  Compare the
      // hashes to decide whether or not to continue the search.
      index_t curr_hash = get_table_entry(capacity, curr_key);
      if (continue_search(tgt, curr_hash, i, key, curr_key)) {
        i = inc_index(i, capacity);
        continue;
      }

      // If none of the above cases match, then the search failed to find the
      // key.
      return nullptr;
    } while (i != tgt);

    // The search failed to find the key.
    return NULL;
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

        // The key is not aleady in the table.  Append the bucket.
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
    index_t tgt = get_table_entry(capacity, b.key);

    // If we find an empty entry, insert the bucket there.
    if (is_empty(buckets[tgt].key)) {
      buckets[tgt] = b;
      ++this->occupancy;
      ++this->ins_rm_count;
      return true;
    }

    // Search for the place to insert b.
    index_t i = tgt;
    do {
      uintptr_t curr_key = buckets[i].key;
      // If we find the key, overwrite that bucket.
      // TODO: Reconsider what we do in this case.
      if (b.key == curr_key) {
        buckets[i].value = b.value;
        return true;
      }

      // If we find an empty entry, insert b there.
      if (is_empty(curr_key)) {
        buckets[i] = b;
        ++this->occupancy;
        ++this->ins_rm_count;
        return true;
      }

      // If we find a tombstone, check whether to insert b here, and finish the
      // insert if so.
      if (is_tombstone(curr_key)) {
        // Check that the search would not continue through the next index.
        index_t next_i = inc_index(i, capacity);
        index_t next_key = buckets[next_i].key;
        if (is_empty(next_key)) {
          // If the next entry is empty, then the search would stop.  Go ahead
          // and insert the bucket.
          buckets[i] = b;
          ++this->occupancy;
          ++this->ins_rm_count;
          return true;
        } else if (is_tombstone(next_key)) {
          // If the next entry is a tombstone, then the search would continue.
          i = next_i;
          continue;
        }
        index_t next_hash = get_table_entry(capacity, next_key);
        if (!continue_search(tgt, next_hash, next_i, b.key, next_key)) {
          // This location is appropriate for inserting the bucket.
          buckets[i] = b;
          ++this->occupancy;
          ++this->ins_rm_count;
          return true;
        }
        // This location is not appropriate for this bucket.  Continue the
        // search.
        i = next_i;
        continue;
      }

      // Otherwise we have another valid key that does not match.  Compare the
      // hashes to decide whether or not to continue the search.
      index_t curr_hash = get_table_entry(capacity, curr_key);
      if (continue_search(tgt, curr_hash, i, b.key, curr_key)) {
        i = inc_index(i, capacity);
        continue;
      }

      // This is an appropriate location to insert the bucket.  Stop the search.
      break;
    } while (i != tgt);

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
      struct bucket tmp = buckets[i];
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
      struct bucket *buckets = this->buckets;
      int32_t occupancy = this->occupancy;

      for (int32_t i = 0; i < occupancy; ++i) {
        if (buckets[i].key == key) {
          if (i == occupancy - 1)
            // Set this entry's key to empty.  This code is here primarily to
            // handle the case where occupancy == 1.
            buckets[i].key = KEY_EMPTY;
          else
            // Remove this entry by swapping it with the last entry.
            buckets[i] = buckets[occupancy - 1];
          // Decrement the occupancy.
          --this->occupancy;
          return true;
        }
      }
      return false;
    }

    // Find the key in the table.
    bucket *entry = find(key);

    // If entry is NULL, the search did not find the key.
    if (NULL == entry)
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

  static hyper_table *merge_two_hyper_tables(hyper_table *__restrict__ left,
                                             hyper_table *__restrict__ right) {
    // In the trivial case of an empty hyper_table, return the other
    // hyper_table.
    if (!left || left->occupancy == 0)
      return right;
    if (!right || right->occupancy == 0)
      return left;

    // Pick the smaller hyper_table to be the source to iterate over.
    bool left_dst;
    hyper_table *src, *dst;
    if (left->occupancy >= right->occupancy) {
      src = right;
      dst = left;
      left_dst = true;
    } else {
      src = left;
      dst = right;
      left_dst = false;
    }

    int32_t src_capacity =
        (src->capacity < MIN_HT_CAPACITY) ? src->occupancy : src->capacity;
    bucket *src_buckets = src->buckets;
    // Iterate over the contents of the source hyper_table.
    for (int32_t i = 0; i < src_capacity; ++i) {
      struct bucket b = src_buckets[i];
      if (!is_valid(b.key))
        continue;

      // For each valid key in the source table, lookup that key in the
      // destination table.
      bucket *dst_bucket = dst->find(b.key);

      if (NULL == dst_bucket) {
        // The destination table does not contain this key.  Insert the
        // key-value pair from the source table into the destination.
        dst->insert(b);
      } else {
        // Merge the two views in the source and destination buckets, being sure
        // to preserve left-to-right ordering.  Free the right view when done.
        reducer_base dst_rb = dst_bucket->value;
        if (left_dst) {
          dst_rb.reduce_fn(dst_rb.view, b.value.view);
          free(b.value.view);
        } else {
          dst_rb.reduce_fn(b.value.view, dst_rb.view);
          free(dst_rb.view);
          dst_rb.view = b.value.view;
        }
      }
    }

    // Destroy the source hyper_table, and return the destination.
    delete src;
    return dst;
  }
};

#endif // _HYPERTABLE_H
