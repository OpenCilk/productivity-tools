// -*- C++ -*-
#ifndef __SIMPLE_SHADOW_MEM__
#define __SIMPLE_SHADOW_MEM__

#include "checking.h"
#include "cilksan_internal.h"
#include "debug_util.h"
#include "dictionary.h"
#include "locksets.h"
#include "shadow_mem_allocator.h"
#include "vector.h"
#include <cstdlib>
#include <sys/mman.h>

class SimpleShadowMem;

static const unsigned ReadMAAllocator = 0;
static const unsigned WriteMAAllocator = 1;
static const unsigned AllocMAAllocator = 2;

// A simple dictionary implementation that uses a two-level table structure.
// The table structure involves a table of pages, where each page represents a
// line of memory locations.  A line of memory accesses is represented as an
// array of MemoryAccess_t objects.
//
// Pages and lines in this representation do not necessarily correspond with
// OS or hardware notions of pages or cache lines.
//
// The template parameter identifies which memory allocator to use to allocate
// lines.
template <unsigned AllocIdx> class SimpleDictionary {
  friend class SimpleShadowMem;
private:
  // Constant parameters for the table structure.
  // log_2 of bytes per line.
  // static constexpr unsigned LG_LINE_SIZE = 3;
  static constexpr unsigned LG_LINE_SIZE = 9;
  // log_2 of lines per page.
  static constexpr unsigned LG_PAGE_SIZE = 30 - LG_LINE_SIZE;
  // log_2 of number of pages in the top-level table.
  static constexpr unsigned LG_TABLE_SIZE = 48 - LG_PAGE_SIZE - LG_LINE_SIZE;

  // Bytes per line.
  static constexpr uintptr_t LINE_SIZE = (1UL << LG_LINE_SIZE);
  // Low-order bit of address identifying the page.
  static constexpr uintptr_t PAGE_OFF = (1UL << (LG_PAGE_SIZE + LG_LINE_SIZE));

  // Mask to identify the byte within a line.
  static constexpr uintptr_t BYTE_MASK = (LINE_SIZE - 1);
  // Mask to identify the line.
  static constexpr uintptr_t LINE_MASK = ~BYTE_MASK;
  // Mask to identify the page.
  static constexpr uintptr_t PAGE_MASK = ~(PAGE_OFF - 1);
  // Mask to identify the index of a line in a page.
  static constexpr uintptr_t LINE_IDX_MASK = LINE_MASK ^ PAGE_MASK;

  // Helper methods to get the indices into the dictionary from a given address.
  // We used these helper methods, rather than a bitfiled struct, because the
  // language standard provides too few guarantees on the order of fields in a
  // bitfield struct.
  __attribute__((always_inline)) static uintptr_t byte(uintptr_t addr) {
    return addr & BYTE_MASK;
  }
  __attribute__((always_inline)) static uintptr_t line(uintptr_t addr) {
    return (addr & LINE_IDX_MASK) >> LG_LINE_SIZE;
  }
  __attribute__((always_inline)) static uintptr_t page(uintptr_t addr) {
    return (addr >> (LG_PAGE_SIZE + LG_LINE_SIZE));
  }

  // Helper methods for computing aligned addresses from a given address.  These
  // are used to iterate through the different parts of the shadow-memory
  // structure.
  __attribute__((always_inline)) static uintptr_t
  alignByPrevGrainsize(uintptr_t addr, unsigned lgGrainsize) {
    uintptr_t grainsize = 1 << lgGrainsize;
    uintptr_t mask = ~(grainsize - 1);
    return addr & mask;
  }
  __attribute__((always_inline)) static uintptr_t
  alignByNextGrainsize(uintptr_t addr, unsigned lgGrainsize) {
    uintptr_t grainsize = 1 << lgGrainsize;
    uintptr_t mask = ~(grainsize - 1);
    return (addr + grainsize) & mask;
  }
  __attribute__((always_inline)) static bool isLineStart(uintptr_t addr) {
    return byte(addr) == 0;
  }
  __attribute__((always_inline)) static bool isPageStart(uintptr_t addr) {
    return (addr & ~PAGE_MASK) == 0;
  }

  // Pair-like data structure to represent a continuous region of memory.
  struct Chunk_t {
    uintptr_t addr;
    size_t size;

    Chunk_t(uintptr_t addr, size_t size) : addr(addr), size(size) {}

    // Returns true if this chunk represents an empty region of memory.
    bool isEmpty() const { return 0 == size; }

    // Get the chunk after this chunk whose address is grainsize-aligned.
    __attribute__((always_inline)) Chunk_t next(unsigned lgGrainsize) const {
      cilksan_assert(((lgGrainsize == (LG_PAGE_SIZE + LG_LINE_SIZE)) ||
                      (lgGrainsize <= LG_LINE_SIZE)) &&
                     "Invalid lgGrainsize");

      uintptr_t nextAddr = alignByNextGrainsize(addr, lgGrainsize);
      size_t chunkSize = nextAddr - addr;
      if (chunkSize > size)
        return Chunk_t(nextAddr, 0);
      return Chunk_t(nextAddr, size - chunkSize);
    }

    // Returns true if this Chunk_t is entirely contained within the line.
    __attribute__((always_inline)) bool withinLine() const {
      uintptr_t nextLineAddr = alignByNextGrainsize(addr, LG_LINE_SIZE);
      return ((addr + size) < nextLineAddr);
    }

    // Returns the last byte address within this Chunk_t that lies within the
    // line.
    __attribute__((always_inline)) uintptr_t endAddrForLine() const {
      if (!withinLine())
        return alignByNextGrainsize(addr, LG_LINE_SIZE) - 1;
      return addr + size;
    }

    // Computes a LgGrainsize for this Chunk_t based on its start and end.
    __attribute__((always_inline)) uintptr_t getLgGrainsize() const {
      cilksan_assert(0 != addr && "Chunk defined on null address.");
      // Compute the lg grainsize implied by addr.
      unsigned lgGrainsize = __builtin_ctzl(addr);
      // Cap the lg grainsize at LG_LINE_SIZE.
      if (lgGrainsize > LG_LINE_SIZE)
        lgGrainsize = LG_LINE_SIZE;

      // Quick test to see if we need to check the end address of this chunk.
      if (size >= LINE_SIZE)
        return lgGrainsize;

      // Check if the end of the chunk is in the same line.
      if (withinLine()) {
        // Compute the lg grainsize implied by the end of the chunk.
        unsigned endLgGrainsize = __builtin_ctzl(addr + size);
        // Take the smaller of the two grainsizes.
        if (endLgGrainsize < lgGrainsize)
          lgGrainsize = endLgGrainsize;
      }
      return lgGrainsize;
    }

  private:
    // DEBUGGING: No default constructor for Chunk_t.
    Chunk_t() = delete;
  };

  // Helper methods to check if chunk startsat a line or page boundary.
  __attribute__((always_inline)) static bool isLineStart(Chunk_t chunk) {
    return isLineStart(chunk.addr);
  }
  __attribute__((always_inline)) static bool isPageStart(Chunk_t chunk) {
    return isPageStart(chunk.addr);
  }

  // Custom memory allocator for lines.
  static MALineAllocator &MAAlloc;

  struct MALineMethods {
    __attribute__((always_inline)) static MemoryAccess_t *
    allocate(size_t size) __attribute__((malloc)) {
      return MAAlloc.allocate(size);
    }
    __attribute__((always_inline)) static bool
    deallocate(__attribute__((noescape)) MemoryAccess_t *Ptr) {
      return MAAlloc.deallocate(Ptr);
    }
    __attribute__((always_inline)) static bool
    isValid(const MemoryAccess_t &MA) {
      return MA.isValid();
    }
    __attribute__((always_inline)) static void invalidate(MemoryAccess_t &MA) {
      MA.invalidate();
    }
  };

  struct MASetFn {
    // DisjointSet_t<SPBagInterface *> *func;
    DisjointSet_t<call_stack_t> *func;
    version_t version;
    csi_id_t acc_id;
    MAType_t type;

    __attribute__((always_inline)) void operator()(MemoryAccess_t &MA) const {
      MA.set(func, version, acc_id, type);
    }
    __attribute__((always_inline)) void checkValid() const {
      cilksan_assert(func && "Invalid MASetFn");
    }
  };

  template <typename LineData_t, class LineDataMethods, class LineDataSetFn>
  struct AbstractLine_t {
    // Expose template parameters for other classes to refer to
    using DataType = LineData_t;
    using DataMethods = LineDataMethods;
    using DataSetFn = LineDataSetFn;

  protected:
    static const uintptr_t DataMask = (1UL << 48) - 1;
    static const uintptr_t NumNonNullElsRShift = 48;
    static constexpr uintptr_t LgNumNonNullEls = 12;
    static_assert(LgNumNonNullEls > LG_LINE_SIZE,
                  "LINE_SIZE exceeds max count of non-null elements.");
    static const uintptr_t LgGrainsizeRShift = 48 + LgNumNonNullEls;
    static const uintptr_t NumNonNullMask = (1UL << LgNumNonNullEls) - 1;
    static const uintptr_t GrainsizeMask = (1UL << (16 - LgNumNonNullEls)) - 1;
    // The array of LineData_t objects in this line is allocated lazily.
    LineData_t *DataPtr = nullptr;

    __attribute__((always_inline)) LineData_t *getData() const {
      return reinterpret_cast<LineData_t *>(
          reinterpret_cast<uintptr_t>(DataPtr) & DataMask);
    }
    __attribute__((always_inline)) void setData(LineData_t *newData) {
      DataPtr = reinterpret_cast<LineData_t *>(
          (reinterpret_cast<uintptr_t>(DataPtr) & ~DataMask) |
          (reinterpret_cast<uintptr_t>(newData) & DataMask));
    }
    __attribute__((always_inline)) void
    setLgGrainsize(unsigned newLgGrainsize) {
      DataPtr = reinterpret_cast<LineData_t *>(
          (reinterpret_cast<uintptr_t>(DataPtr) &
           ~(GrainsizeMask << LgGrainsizeRShift)) |
          (static_cast<uintptr_t>(newLgGrainsize) << LgGrainsizeRShift));
    }
    __attribute__((always_inline)) void scaleNumNonNullEls(int replFactor) {
      DataPtr = reinterpret_cast<LineData_t *>(
          (reinterpret_cast<uintptr_t>(DataPtr) &
           ~(NumNonNullMask << NumNonNullElsRShift)) |
          ((reinterpret_cast<uintptr_t>(DataPtr) &
            (NumNonNullMask << NumNonNullElsRShift)) *
           replFactor));
    }

  public:
    AbstractLine_t(unsigned LgGrainsize) {
      cilksan_assert(LgGrainsize >= 0 && LgGrainsize <= LG_LINE_SIZE &&
                     "Invalid grainsize for Line_t");
      setLgGrainsize(LgGrainsize);
    }
    AbstractLine_t() {
      // By default, a AbstractLine_t contains entries of (1 << LG_LINE_SIZE) bytes.
      setLgGrainsize(LG_LINE_SIZE);
    }
    ~AbstractLine_t() {
      if (isMaterialized()) {
        LineDataMethods::deallocate(getData());
        DataPtr = nullptr;
      }
    }

    __attribute__((always_inline)) unsigned getLgGrainsize() const {
      return static_cast<unsigned>(
          (reinterpret_cast<uintptr_t>(DataPtr) >> LgGrainsizeRShift) &
          GrainsizeMask);
    }

    __attribute__((always_inline)) bool isEmpty() const {
      return noNonNullEls();
    }

    __attribute__((always_inline)) int getNumNonNullEls() const {
      return static_cast<int>(
          (reinterpret_cast<uintptr_t>(DataPtr) >> NumNonNullElsRShift) &
          NumNonNullMask);
    }
    __attribute__((always_inline)) bool noNonNullEls() const {
      return (0 == (reinterpret_cast<uintptr_t>(DataPtr) &
                    (NumNonNullMask << NumNonNullElsRShift)));
    }
    __attribute__((always_inline)) void zeroNumNonNullEls() {
      DataPtr = reinterpret_cast<LineData_t *>(
          reinterpret_cast<uintptr_t>(DataPtr) &
          ~(NumNonNullMask << NumNonNullElsRShift));
    }
    __attribute__((always_inline)) void incNumNonNullEls() {
      DataPtr = reinterpret_cast<LineData_t *>(
          reinterpret_cast<uintptr_t>(DataPtr) + (1UL << NumNonNullElsRShift));
    }
    __attribute__((always_inline)) void decNumNonNullEls() {
      cilksan_assert(!noNonNullEls() && "Decrementing NumNonNullEls below 0");
      DataPtr = reinterpret_cast<LineData_t *>(
          reinterpret_cast<uintptr_t>(DataPtr) - (1UL << NumNonNullElsRShift));
    }

    // Check if the array of LineData_t's has been allocated.
    __attribute__((always_inline)) bool isMaterialized() const {
      return (nullptr != getData());
    }

    // Allocate the array of LineData_t's for this line.
    void materialize() {
      cilksan_assert(!getData() && "Data already materialized.");
      int NumData = (1 << LG_LINE_SIZE) / (1 << getLgGrainsize());
      setData(LineDataMethods::allocate(NumData));
    }

    // Reduce the grainsize of this line to newLgGrainsize, which must fall
    // within [0, LgGrainsize].
    void refine(unsigned newLgGrainsize) {
      cilksan_assert(newLgGrainsize < getLgGrainsize() &&
                     "Invalid grainsize for refining Line_t.");
      // If Data hasn't been materialzed yet, then just update LgGrainsize.
      if (!isMaterialized()) {
        setLgGrainsize(newLgGrainsize);
        return;
      }

      LineData_t *Data = getData();
      // Create a new array of LineData_t's.
      int newNumDataEls = (1 << LG_LINE_SIZE) / (1 << newLgGrainsize);
      LineData_t *NewData = LineDataMethods::allocate(newNumDataEls);

      // Copy the old LineData_t's into the new array with replication.
      if (!noNonNullEls()) {
        unsigned LgGrainsize = getLgGrainsize();
        int oldNumDataEls = (1 << LG_LINE_SIZE) / (1 << LgGrainsize);
        int replFactor = (1 << LgGrainsize) / (1 << newLgGrainsize);
        for (int i = 0; i < oldNumDataEls; ++i)
          if (LineDataMethods::isValid(Data[i]))
            for (int j = replFactor * i; j < replFactor * (i + 1); ++j)
              NewData[j] = Data[i];
#if CILKSAN_DEBUG
        int oldNumNonNullEls = getNumNonNullEls();
#endif
        scaleNumNonNullEls(replFactor);
        WHEN_CILKSAN_DEBUG(cilksan_assert(oldNumNonNullEls * replFactor ==
                                          getNumNonNullEls()));
      }

      // Replace the old Data array and LgGrainsize value.
      LineDataMethods::deallocate(Data);
      setData(NewData);
      setLgGrainsize(newLgGrainsize);
    }

    // Reset this AbstractLine_t object with a default LgGrainsize and no valid
    // LineData_t's.
    void reset() {
      if (isMaterialized()) {
        LineDataMethods::deallocate(getData());
        DataPtr = nullptr;
      }
      setLgGrainsize(LG_LINE_SIZE);
    }

    // Helper method to convert a byte address into an index into this line.
    __attribute__((always_inline)) uintptr_t getIdx(uintptr_t byte) const {
      return byte >> getLgGrainsize();
    }

    // Access the LineData_t object in this line for the byte address.
    __attribute__((always_inline)) LineData_t &operator[](uintptr_t byte) {
      cilksan_level_assert(DEBUG_SHADOWMEM,
                           getData() && "Data not materialized");
      return getData()[getIdx(byte)];
    }
    __attribute__((always_inline)) const LineData_t &
    operator[](uintptr_t byte) const {
      cilksan_level_assert(DEBUG_SHADOWMEM,
                           getData() && "Data not materialized");
      return getData()[getIdx(byte)];
    }

    // Set all entries in this line covered by Accessed to be the LineData_t
    // formed by LineDataSetFn.  The func parameter must be valid.
    __attribute__((always_inline)) void set(Chunk_t &Accessed,
                                            LineDataSetFn SetFn) {
      SetFn.checkValid();
      // Get the grainsize of the access.
      unsigned AccessedLgGrainsize = Accessed.getLgGrainsize();

      // If we're overwritting the entire line, then we can coalesce the line.
      if (AccessedLgGrainsize == LG_LINE_SIZE) {
        // Reset the line if necessary.
        if (getLgGrainsize() != LG_LINE_SIZE)
          reset();

        // Materialize the line, if necessary.
        if (!isMaterialized())
          materialize();

        LineData_t *Data = getData();
        // Check if we're adding a new valid entry.
        if (!LineDataMethods::isValid(Data[0]))
          incNumNonNullEls();

        // Add the entry.
        SetFn(Data[0]);

        // Updated Accessed.
        Accessed = Accessed.next(AccessedLgGrainsize);
        return;
      }

      // We're updating the content of the line in a refined manner, meaning
      // that either Accessed or this AbstractLine_t store LineData_t's at a
      // finer granularity than the 2^LG_LINE_SIZE default.

      // Pick the smaller of the line's existing grainsize or the grainsize of
      // the access.
      unsigned LgGrainsize = getLgGrainsize();
      if (LgGrainsize > AccessedLgGrainsize) {
        // The access has a smaller grainsize, so first refine the line to match
        // that grainsize.
        refine(AccessedLgGrainsize);
      } else if (LgGrainsize < AccessedLgGrainsize) {
        AccessedLgGrainsize = LgGrainsize;
      }

      // Materialize the line, if necessary.
      if (!isMaterialized())
        materialize();

      // Update the accesses in the line, until we find a new non-null Entry.
      LineData_t *Data = getData();
      do {
        uintptr_t Idx = getIdx(byte(Accessed.addr));
        // Increase the count of non-null memory accesses, if necessary.
        if (!LineDataMethods::isValid(Data[Idx]))
          incNumNonNullEls();

        // Copy the memory access
        SetFn(Data[Idx]);

        // Get the next location.
        Accessed = Accessed.next(AccessedLgGrainsize);
        if (Accessed.isEmpty())
          return;

      } while (!isLineStart(Accessed));
    }

    // Starting from the first address in Accessed, insert the LineData_t formed
    // by LineDataSet into entries in this AbstractLine_t until either the end
    // of this line is reached or a change is detected in the LineData_t object.
    __attribute__((always_inline)) void
    insert(Chunk_t &Accessed, unsigned PrevIdx, LineDataSetFn SetFn) {
      SetFn.checkValid();
      // Get the grainsize of the access.
      unsigned AccessedLgGrainsize = Accessed.getLgGrainsize();

      // Materialize the line, if necessary.
      if (!isMaterialized())
        materialize();

      // If neither the line nor the access are refined, then we can optimize
      // the insert process.
      if ((AccessedLgGrainsize == LG_LINE_SIZE) &&
          (getLgGrainsize() == LG_LINE_SIZE)) {

        LineData_t *Data = getData();
        // Check if we're adding a new valid entry.
        if (!LineDataMethods::isValid(Data[0]))
          incNumNonNullEls();

        // Add the entry.
        SetFn(Data[0]);

        // Updated Accessed.
        Accessed = Accessed.next(AccessedLgGrainsize);
        return;
      }

      // We're updating the content of the line in a refined manner, meaning
      // that either Accessed or this AbstractLine_t store LineData_t's at a
      // finer granularity than the 2^LG_LINE_SIZE default.

      // Pick the smaller of the line's existing grainsize or the grainsize of
      // the access.
      unsigned LgGrainsize = getLgGrainsize();
      if (LgGrainsize > AccessedLgGrainsize) {
        // The access has a smaller grainsize, so first refine the line to match
        // that grainsize.
        refine(AccessedLgGrainsize);
      } else if (LgGrainsize < AccessedLgGrainsize) {
        AccessedLgGrainsize = LgGrainsize;
      }

      LineData_t *Data = getData();
      const LineData_t Previous = Data[PrevIdx];
      bool PrevIsValid = LineDataMethods::isValid(Previous);
      unsigned EntryIdx;
      // Update the accesses in the line, until we find a new non-null Entry.
      do {
        uintptr_t Idx = getIdx(byte(Accessed.addr));
        // Increase the count of non-null memory accesses, if necessary.
        if (!LineDataMethods::isValid(Data[Idx]))
          incNumNonNullEls();

        // Copy the memory access
        SetFn(Data[Idx]);

        // Get the next location.
        Accessed = Accessed.next(AccessedLgGrainsize);
        if (Accessed.isEmpty())
          return;

        // Exit early when we reach the end of the line
        if (isLineStart(Accessed))
          return;

        EntryIdx = getIdx(byte(Accessed.addr));
      } while (!LineDataMethods::isValid(Data[EntryIdx]) ||
               (PrevIsValid && (Previous == Data[EntryIdx])));
    }

    // Reset all the entries of this line covered by Accessed.
    __attribute__((always_inline)) void clear(Chunk_t &Accessed) {
      // Get the grainsize of the access.
      unsigned AccessedLgGrainsize = Accessed.getLgGrainsize();
      if (LG_LINE_SIZE == AccessedLgGrainsize) {
        if (!isEmpty())
          // Reset the line.
          reset();

        // Updated Accessed.
        Accessed = Accessed.next(AccessedLgGrainsize);
        return;
      }

      // Pick the smaller of the line's existing grainsize or the grainsize of
      // the access.
      unsigned LgGrainsize = getLgGrainsize();
      if (LgGrainsize > AccessedLgGrainsize) {
        // The access has a smaller grainsize, so first refine the line to match
        // that grainsize.
        refine(AccessedLgGrainsize);
      } else if (LgGrainsize < AccessedLgGrainsize) {
        AccessedLgGrainsize = LgGrainsize;
      }

      // If the line is already empty, then there's nothing to clear.
      if (isEmpty()) {
        Accessed = Accessed.next(LG_LINE_SIZE);
        return;
      }

      LineData_t *Data = getData();
      do {
        uintptr_t Idx = getIdx(byte(Accessed.addr));
        // If we find a valid LineData_t, invalidate it.
        if (LineDataMethods::isValid(Data[Idx])) {
          LineDataMethods::invalidate(Data[Idx]);

          // Decrement the number of non-null accesses.
          decNumNonNullEls();

          // Skip to the end of the line if it becomes empty.
          if (noNonNullEls()) {
            Accessed = Accessed.next(LG_LINE_SIZE);
            // Reset the line to forget about any refinement.
            if (getLgGrainsize() != LG_LINE_SIZE)
              reset();
          } else
            // Advance to the next entry in the line.
            Accessed = Accessed.next(AccessedLgGrainsize);
        } else
          // Advance to the next entry in the line.
          Accessed = Accessed.next(AccessedLgGrainsize);
      } while (!Accessed.isEmpty() && !isLineStart(Accessed.addr));
    }
  };

  // Data structure for a line of memory locations.  A line is represented as an
  // array of MemoryAccess_t objects, where each object represents an aligned
  // set of (1 << LgGrainsize) bytes.  LgGrainsize must be in
  // [0, LG_LINE_SIZE].
  using Line_t = AbstractLine_t<MemoryAccess_t, MALineMethods, MASetFn>;

  // A page is an array of lines.
  struct Page_t {
    using LineType = Line_t;
    // Bitmap identifying bytes in the page that were previously accessed in
    // the current strand.
    static constexpr unsigned LG_OCCUPANCY_PAGE_SIZE =
        LG_PAGE_SIZE + LG_LINE_SIZE;
    static constexpr size_t OCC_ARR_SIZE =
        (1UL << LG_OCCUPANCY_PAGE_SIZE) / (8 * sizeof(uint64_t));
    uint64_t occupancy[OCC_ARR_SIZE] = {0};

    // Memory-access entries for the page
    LineType lines[1UL << LG_PAGE_SIZE];

    // To accommodate their size and sparse access pattern, use mmap/munmap to
    // allocate and free Page_t's.
    void *operator new(size_t size) {
      CheckingRAII nocheck;
      return mmap(nullptr, sizeof(Page_t), PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    }
    void operator delete(void *ptr) {
      CheckingRAII nocheck;
      munmap(ptr, sizeof(Page_t));
    }

    // Operators for accessing lines
    LineType &operator[](uintptr_t line) { return lines[line]; }
    const LineType &operator[](uintptr_t line) const { return lines[line]; }

    // Constants for operating on occupancy bits
    static constexpr uintptr_t LG_OCCUPANCY_WORD_SIZE = 6;
    static constexpr uintptr_t OCCUPANCY_WORD_SIZE = 1UL
                                                     << LG_OCCUPANCY_WORD_SIZE;
    static_assert(
        OCCUPANCY_WORD_SIZE == (8 * sizeof(uint64_t)),
        "LG_OCCUPANCY_WORD_SIZE does not correspond with occupancy-word size");
    static constexpr uintptr_t OCCUPANCY_BIT_MASK = OCCUPANCY_WORD_SIZE - 1;
    static constexpr uintptr_t OCCUPANCY_WORD_MASK = ~OCCUPANCY_BIT_MASK;
    static constexpr uintptr_t OCCUPANCY_WORD_IDX =
        OCCUPANCY_WORD_MASK ^ ~((1UL << LG_OCCUPANCY_PAGE_SIZE) - 1);

    // Static helper methods for operating on occupancy bits
    __attribute__((always_inline)) static uintptr_t
    occupancyWord(uintptr_t addr) {
      return ((addr & OCCUPANCY_WORD_IDX) >> LG_OCCUPANCY_WORD_SIZE);
    }
    __attribute__((always_inline)) static uintptr_t
    occupancyWordStartBit(uintptr_t addr) {
      return (addr & OCCUPANCY_BIT_MASK);
    }
    __attribute__((always_inline)) static bool
    isOccupancyWordStart(uintptr_t addr) {
      return occupancyWordStartBit(addr) == 0;
    }

    // Get the chunk after this chunk whose address is grainsize-aligned.
    __attribute__((always_inline)) Chunk_t
    nextOccupancyWord(Chunk_t Accessed) const {
      uintptr_t nextAddr =
          alignByNextGrainsize(Accessed.addr, LG_OCCUPANCY_WORD_SIZE);
      size_t chunkSize = nextAddr - Accessed.addr;
      if (chunkSize > Accessed.size)
        return Chunk_t(nextAddr, 0);
      return Chunk_t(nextAddr, Accessed.size - chunkSize);
    }

    __attribute__((always_inline)) bool
    setOccupied(Chunk_t &Accessed, Vector_t<uintptr_t> &TouchedWords) {
      bool foundUnoccupied = false;
      while (!Accessed.isEmpty()) {
        uintptr_t addr = Accessed.addr;
        uint64_t mask;
        if (Accessed.size >= OCCUPANCY_WORD_SIZE)
          mask = (uint64_t)(-1);
        else
          mask = (1UL << Accessed.size) - 1;
        mask = (uint64_t)mask << (unsigned)occupancyWordStartBit(addr);

        uint64_t current = occupancy[occupancyWord(addr)];
        if (0UL == current)
          TouchedWords.push_back(addr);
        if (~current & mask)
          foundUnoccupied = true;

        occupancy[occupancyWord(addr)] |= mask;
        Accessed = nextOccupancyWord(Accessed);

        if (isPageStart(Accessed.addr))
          return foundUnoccupied;
      }
      return foundUnoccupied;
    }

    __attribute__((always_inline))
    bool setOccupiedFast(uintptr_t addr, size_t mem_size,
                         Vector_t<uintptr_t> &TouchedWords) {
      return true;
      bool foundUnoccupied = false;
      uint64_t mask = (1UL << mem_size) - 1;
      mask = (uint64_t)mask << (unsigned)occupancyWordStartBit(addr);
      uint64_t current = occupancy[occupancyWord(addr)];
      if (0UL == current)
        TouchedWords.push_back(addr);
      if (~current & mask)
        foundUnoccupied = true;
      occupancy[occupancyWord(addr)] |= mask;
      return foundUnoccupied;
    }

    __attribute__((always_inline)) void clear(uintptr_t wordAddr) {
      occupancy[occupancyWord(wordAddr)] = 0;
    }
  };

  struct LockerLineMethods {
    __attribute__((always_inline)) static LockerList_t *allocate(size_t size)
        __attribute__((malloc)) {
      return new LockerList_t[size];
    }
    __attribute__((always_inline)) static bool
    deallocate(__attribute__((noescape)) LockerList_t *Ptr) {
      delete[] Ptr;
      return true;
    }
    __attribute__((always_inline)) static bool isValid(const LockerList_t &LL) {
      return LL.isValid();
    }
    __attribute__((always_inline)) static void invalidate(LockerList_t &LL) {
      LL.invalidate();
    }
  };

  struct LockerSetFn {
    const LockSet_t &lockset;
    csi_id_t acc_id;
    MAType_t type;
    const FrameData_t *f;

    __attribute__((always_inline)) void operator()(LockerList_t &LL) const {
      // Scan the existing lockers to remove redundant lockers
      bool redundant = false;
      Locker_t *locker = LL.getHead();
      Locker_t **prevPtr = &LL.getHead();
      SBag_t *sbag = f->getSbagForAccess();
      DS_t *ds = sbag->get_ds();
      version_t version = sbag->get_version();
      while (locker) {
        IntersectionResult_t result =
            LockSet_t::intersect(locker->getLockSet(), lockset);
        if (!MemoryAccess_t::previousAccessInParallel(&locker->getAccess(),
                                                      f)) {
          if (result & L_SUPERSET_OF_R) {
            // The current lockset in the list is redundant with the lockset of
            // this access.

            // Splice the current lockset out of the list.
            *prevPtr = locker->getNext();
            Locker_t *next = locker->getNext();
            locker->setNext(nullptr);
            // Delete the current locker
            delete locker;
            // Move on to the next locker in the list.
            locker = next;
            continue;
          }
        } else {
          // Test if this lockset is redundant with one in the list
          if (result & L_SUBSET_OF_R)
            redundant = true;
        }
        prevPtr = &locker->getNext();
        locker = locker->getNext();
      }
      if (!redundant) {
        Locker_t *newLocker =
            new Locker_t(MemoryAccess_t(ds, version, acc_id, type), lockset);
        LL.insert(newLocker);
      }
    }
    __attribute__((always_inline)) void checkValid() const {}
  };

  // Data structure for a line of locker lists.  A line is represented as an
  // array of LockerList_t objects, where each object represents an aligned set
  // of (1 << LgGrainsize) bytes.  LgGrainsize must be in [0, LG_LINE_SIZE].
  using LockerLine_t =
      AbstractLine_t<LockerList_t, LockerLineMethods, LockerSetFn>;

  struct LockerPage_t {
    using LineType = LockerLine_t;

    LockerLine_t lines[1UL << LG_PAGE_SIZE];

    // To accommodate their size and sparse access pattern, use mmap/munmap to
    // allocate and free Page_t's.
    void *operator new(size_t size) {
      CheckingRAII nocheck;
      return mmap(nullptr, sizeof(LockerPage_t), PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    }
    void operator delete(void *ptr) {
      CheckingRAII nocheck;
      munmap(ptr, sizeof(LockerPage_t));
    }

    // Operators for accessing lines
    LockerLine_t &operator[](uintptr_t line) { return lines[line]; }
    const LockerLine_t &operator[](uintptr_t line) const { return lines[line]; }
  };

  // A table is an array of pages.
  Page_t *Table[1UL << LG_TABLE_SIZE] = {nullptr};
  LockerPage_t *LockerTable[1UL << LG_TABLE_SIZE] = {nullptr};

  // Vectors to track non-null values in the 2-level occupancy table.
  Vector_t<uintptr_t> TouchedWords;
  Vector_t<uintptr_t> AllocatedPages;
  bool LockerTableUsed = false;

  // Get a page of the appropriate type from the corresponding table.
  template <typename PageType>
  __attribute__((always_inline)) PageType *getPage(uintptr_t idx) const;
  template <>
  __attribute__((always_inline)) Page_t *getPage<Page_t>(uintptr_t idx) const {
    return Table[idx];
  }
  template <>
  __attribute__((always_inline)) LockerPage_t *
  getPage<LockerPage_t>(uintptr_t idx) const {
    return LockerTable[idx];
  }

  // Set a page in the corresponding table.
  template <typename PageType>
  __attribute__((always_inline)) void setPage(uintptr_t idx, PageType *Page);
  template <>
  __attribute__((always_inline)) void setPage<Page_t>(uintptr_t idx,
                                                      Page_t *Page) {
    Table[idx] = Page;
  }
  template <>
  __attribute__((always_inline)) void
  setPage<LockerPage_t>(uintptr_t idx, LockerPage_t *Page) {
    LockerTableUsed = true;
    LockerTable[idx] = Page;
  }

  __attribute__((always_inline)) static unsigned lgMemSize(size_t mem_size) {
    switch (mem_size) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    case 8:
      return 3;
    case 16:
      return 4;
    case 32:
      return 5;
    // case 64:
    //   return 6;
    // case 128:
    //   return 7;
    // case 256:
    //   return 8;
    // case 512:
    //   return 9;
    default:
      return __builtin_ctzl(mem_size);
    }
  }

public:
  static unsigned getLgSmallAccessSize() { return LG_LINE_SIZE; }

  SimpleDictionary() {}
  ~SimpleDictionary() {
    freePages();
    for (int64_t i = 0; i < (1L << LG_TABLE_SIZE); ++i)
      if (Table[i]) {
        delete Table[i];
        Table[i] = nullptr;
      }
    if (LockerTableUsed)
      for (int64_t i = 0; i < (1L << LG_TABLE_SIZE); ++i)
        if (LockerTable[i]) {
          delete LockerTable[i];
          LockerTable[i] = nullptr;
        }
  }

  // Helper class to store a particular location in the dictionary.  This class
  // makes it easy to re-retrieve the MemoryAccess_t object at a given location,
  // even if the underlying line structure in the shadow memory might change.
  template<typename PageType>
  struct Entry_t {
    using LineType = typename PageType::LineType;
    using DataType = typename LineType::DataType;
    using DataMethods = typename LineType::DataMethods;

    uintptr_t Address;
    PageType *Page = nullptr;

    Entry_t() {}
    Entry_t(const SimpleDictionary &D, uintptr_t Address) : Address(Address) {
      Page = D.getPage<PageType>(page(Address));
    }

    // Get the DataType object at this location.  Returns nullptr if no valid
    // DataType object exists at the current address.
    __attribute__((always_inline)) const DataType *get() const {
      // If there's no page, return nullptr.
      if (!Page)
        return nullptr;

      // If the line is empty, return nullptr.
      if ((*Page)[line(Address)].isEmpty())
        return nullptr;

      // Return the DataType object at this address if it's valid, nullptr
      // otherwise.
      const DataType *Acc = &((*Page)[line(Address)][byte(Address)]);
      if (!DataMethods::isValid(*Acc))
        return nullptr;
      return Acc;
    }
  };

  template <typename PageType>
  __attribute__((always_inline)) typename PageType::LineType *
  getLine(uintptr_t addr, size_t mem_size) {
    using LineType = typename PageType::LineType;
    PageType *Page = getPage<PageType>(page(addr));
    if (!Page)
      return nullptr;

    LineType *Line = &(*Page)[line(addr)];

    return Line;
  }

  template <typename PageType>
  __attribute__((always_inline)) typename PageType::LineType *
  getLineMustExist(uintptr_t addr, size_t mem_size) {
    using LineType = typename PageType::LineType;
    unsigned AccessLgGrainsize = lgMemSize(mem_size);
    PageType *Page = getPage<PageType>(page(addr));
    LineType *Line;
    Line = &(*Page)[line(addr)];
    // If the line's grainsize is larger than that of the access, go ahead and
    // refine the line,
    if (Line->getLgGrainsize() > AccessLgGrainsize)
      Line->refine(AccessLgGrainsize);
    return Line;
  }

  // Iterator class for querying the entries of the shadow memory corresponding
  // to a given accessed chunk.
  template <typename PageType> class Query_iterator {
    using LineType = typename PageType::LineType;
    using DataType = typename LineType::DataType;
    using DataMethods = typename LineType::DataMethods;
    using Entry_t = Entry_t<PageType>;

    const SimpleDictionary &Dict;
    Chunk_t Accessed;
    PageType *Page = nullptr;
    LineType *Line = nullptr;
    Entry_t Entry;

  public:
    Query_iterator(const SimpleDictionary &Dict, Chunk_t Accessed)
        : Dict(Dict), Accessed(Accessed) {
      // Initialize the iterator to point to the first valid entry covered by
      // Accessed.
      if (Accessed.isEmpty())
        return;

      // Get the first non-null page for this access.
      if (!nextPage())
        return;

      // Get the first non-null line for this access.
      if (!nextLine())
        return;

      Entry = Entry_t(Dict, Accessed.addr);
    }

    // Returns true if this iterator has reached the end of the chunk Accessed.
    __attribute__((always_inline)) bool isEnd() const {
      return Accessed.isEmpty();
    }

    // Get the DataType object at the current address.  Returns nullptr if no
    // valid DataType object exists at the current address.
    __attribute__((always_inline)) const DataType *get() const {
      if (isEnd())
        return nullptr;

      cilksan_assert(Line && "Null Line for Query_iterator not at end.");
      if (Line->isEmpty())
        return nullptr;

      const DataType *Access = &(*Line)[byte(Accessed.addr)];
      if (!DataMethods::isValid(*Access))
        return nullptr;
      return Access;
    }

    // Get the current starting address being queried.
    uintptr_t getAddress() const { return Accessed.addr; }

    // Scan the entries from Accessed until an entry with a new non-null
    // DataType object is found.
    __attribute__((always_inline)) void next() {
      cilksan_assert(!isEnd() &&
                     "Cannot call next() on an empty Line iterator");
      const Entry_t Previous = Entry;
      const DataType *PrevData = Previous.get();
      const DataType *EntryData = nullptr;
      do {
        if (Line->isEmpty())
          Accessed = Accessed.next(LG_LINE_SIZE);
        else
          Accessed = Accessed.next(Line->getLgGrainsize());

        if (Accessed.isEmpty())
          return;

        // Update the page, if necessary
        if (isPageStart(Accessed.addr))
          if (!nextPage())
            return;

        // Update the line, if necessary
        if (isLineStart(Accessed.addr))
          if (!nextLine())
            return;

        Entry = Entry_t(Dict, Accessed.addr);
        EntryData = Entry.get();
      } while (!EntryData || (PrevData && (*PrevData == *EntryData)));
    }

  private:
    // Helper method to get the next non-null page covered by Accessed.  Returns
    // true if a page is found, false otherwise.
    bool nextPage() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextPage() on an empty Line iterator");
      // Scan to find the non-null page.
      Page = Dict.getPage<PageType>(page(Accessed.addr));
      while (!Page) {
        Accessed = Accessed.next(LG_PAGE_SIZE + LG_LINE_SIZE);
        // Return early if the access becomes empty.
        if (Accessed.isEmpty())
          return false;
        Page = Dict.getPage<PageType>(page(Accessed.addr));
      }
      return true;
    }

    // Helper method to get the next non-null line covered by Accessed.  Returns
    // true if a line is found, false otherwise.
    __attribute__((always_inline))
    bool nextLine() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextLine() on an empty Line iterator");
      cilksan_assert(Page && "nextLine() called with null page");
      // Scan to find the non-null line.
      Line = &(*Page)[line(Accessed.addr)];
      while (!Line || Line->isEmpty()) {
        Accessed = Accessed.next(LG_LINE_SIZE);
        // Return early if the access becomes empty.
        if (Accessed.isEmpty())
          return false;

        // If this search reaches the end of the page, get the next page.
        if (isPageStart(Accessed.addr))
          if (!nextPage())
            return false;

        Line = &(*Page)[line(Accessed.addr)];
      }
      return true;
    }
  };

  // Iterator class for updating the entries of the shadow memory corresponding
  // to a given chunk Accessed.
  template <typename PageType> class Update_iterator {
    using LineType = typename PageType::LineType;
    using DataType = typename LineType::DataType;
    using DataMethods = typename LineType::DataMethods;
    using DataSetFn = typename LineType::DataSetFn;
    using Entry_t = Entry_t<PageType>;

    SimpleDictionary &Dict;
    Chunk_t Accessed;
    PageType *Page = nullptr;
    LineType *Line = nullptr;
    Entry_t Entry;

  public:
    Update_iterator(SimpleDictionary &Dict, Chunk_t Accessed)
        : Dict(Dict), Accessed(Accessed) {
      // Initialize the iterator to point to the first page and line entries for
      // Accessed.
      if (Accessed.isEmpty())
        return;

      // // Get the page for this access.
      // if (!nextPage())
      //   return;

      nextPage();

      nextLine();

      Entry = Entry_t(Dict, Accessed.addr);
    }

    // Returns true if this iterator has reached the end of the chunk Accessed.
    __attribute__((always_inline)) bool isEnd() const {
      return Accessed.isEmpty();
    }

    // Get the DataType object at the current address.  Returns nullptr if
    // no valid DataType object exists at the current address.
    __attribute__((always_inline)) DataType *get() const {
      if (isEnd() || !Page)
        return nullptr;

      if (Line->isEmpty())
        return nullptr;

      DataType *Access = &(*Line)[byte(Accessed.addr)];
      if (!DataMethods::isValid(*Access))
        return nullptr;
      return Access;
    }

    // Get the current starting address being queried.
    __attribute__((always_inline)) uintptr_t getAddress() const {
      return Accessed.addr;
    }

    // Scan the entries from Accessed until we find a location with an invalid
    // DataType object or a DataType object that does not match the previous
    // one.
    __attribute__((always_inline)) void next() {
      cilksan_assert(!isEnd() &&
                     "Cannot call next() on an empty Line iterator");
      cilksan_assert(Page && "Cannot call next() with null Page");
      cilksan_assert(Line && "Cannot call next() with null Line");

      // Remember the previous Entry.
      const Entry_t Previous = Entry;
      do {
        Accessed = Accessed.next(Line->getLgGrainsize());
        if (Accessed.isEmpty())
          return;

        // Update the page, if necessary
        if (isPageStart(Accessed.addr))
          if (!nextPage())
            return;

        // Update the line, if necessary
        if (isLineStart(Accessed.addr))
          if (!nextLine())
            return;

        Entry = Entry_t(Dict, Accessed.addr);
      } while (Entry.get() && Previous.get() &&
               *Previous.get() == *Entry.get());
    }

    // Set all entries from Accessed to the DataObject formed by SetFn.
    __attribute__((always_inline)) void set(DataSetFn SetFn) {
      do {
        // Create a new page, if necessary.
        if (!Page) {
          Page = new PageType;
          Dict.setPage(page(Accessed.addr), Page);
          Line = &(*Page)[line(Accessed.addr)];
          assert(!Line->isMaterialized() &&
                 "Materialized line found in new page");
        }

        // Set DataType objects in the current line.
        Line->set(Accessed, SetFn);

        // Return early if we've handled the whole access.
        if (Accessed.isEmpty())
          return;

        // Update the current page, if necessary.
        if (isPageStart(Accessed))
          nextPage();

        // Update the current line, if necessary.
        if (isLineStart(Accessed))
          nextLine();

      } while (true);
    }

    // Insert DataType objects into all entries from Accessed until a new valid
    // DataType object is discovered.
    __attribute__((always_inline)) void insert(DataSetFn SetFn) {
      // Copy the object at the previous entry.  In case this method changes the
      // object at this previous entry, this copy ensures that comparisons use
      // the previous object's value.
      const DataType Previous(Entry.get() ? *Entry.get() : DataType());
      bool PrevIsValid = DataMethods::isValid(Previous);
      do {
        // Create a new page, if necessary.
        if (!Page) {
          Page = new PageType;
          Dict.setPage<PageType>(page(Accessed.addr), Page);
          Line = &(*Page)[line(Accessed.addr)];
          assert(!Line->isMaterialized() &&
                 "Materialized line found in new page");
        }

        // Set the object in the current line.
        Line->insert(Accessed, Line->getIdx(byte(Accessed.addr)), SetFn);

        // Return early if we've handled the whole access.
        if (Accessed.isEmpty())
          return;

        // Update the current page, if necessary.
        if (isPageStart(Accessed))
          nextPage();

        // Update the current line, if necessary.
        if (isLineStart(Accessed))
          nextLine();

        Entry = Entry_t(Dict, Accessed.addr);
      } while (!Entry.get() || (PrevIsValid && (Previous == *Entry.get())));
    }

    // Clear all entries covered by Accessed.
    __attribute__((always_inline)) void clear() {
      do {
        // Scan for a non-null Page.
        if (!nextNonNullPage())
          return;

        // Scan for a non-null Line.
        if (!nextNonNullLine())
          return;

        Line->clear(Accessed);

        // Return early if we've handled the whole access.
        if (Accessed.isEmpty())
          return;
      } while (true);
    }

  private:
    // In contrast to Query iterators, Update iterators should typically get
    // pointers to null pages and lines, not skip them.
    bool nextPage() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextPage() on an empty Line iterator");
      Page = Dict.getPage<PageType>(page(Accessed.addr));
      return true;
    }
    __attribute__((always_inline))
    bool nextLine() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextLine() on an empty Line iterator");
      if (!Page) {
        Line = nullptr;
        return false;
      }
      Line = &(*Page)[line(Accessed.addr)];
      return true;
    }

    // Helper method to get the next non-null page, similar to the nextPage
    // method for Query_iterators.
    bool nextNonNullPage() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextPage() on an empty Line iterator");
      // Scan to find the non-null page.
      Page = Dict.getPage<PageType>(page(Accessed.addr));
      while (!Page) {
        Accessed = Accessed.next(LG_PAGE_SIZE + LG_LINE_SIZE);
        // Return early if the access becomes empty.
        if (Accessed.isEmpty())
          return false;
        Page = Dict.getPage<PageType>(page(Accessed.addr));
      }
      return true;
    }

    // Helper method to get the next non-null line, similar to the nextLine
    // method for Query_iterators.
    __attribute__((always_inline))
    bool nextNonNullLine() {
      cilksan_assert(!isEnd() &&
                     "Cannot call nextLine() on an empty Line iterator");
      cilksan_assert(Page && "nextLine() called with null page");
      // Scan to find the non-null line.
      Line = &(*Page)[line(Accessed.addr)];
      while (!Line || Line->isEmpty()) {
        Accessed = Accessed.next(LG_LINE_SIZE);
        // Return early if the access becomes empty.
        if (Accessed.isEmpty())
          return false;

        // If this search reaches the end of the page, get the next page.
        if (isPageStart(Accessed.addr))
          if (!nextNonNullPage())
            return false;

        Line = &(*Page)[line(Accessed.addr)];
      }
      return true;
    }
  };

  // High-level method to set the occupancy of the shadow memory
  __attribute__((always_inline))
  bool setOccupied(uintptr_t addr, size_t mem_size) {
    assert(AllocIdx != AllocMAAllocator &&
           "Called setOccupied on Alloc shadow memory");

    Chunk_t Accessed(addr, mem_size);
    bool foundUnoccupied = false;
    while (!Accessed.isEmpty()) {
      Page_t *Page = Table[page(Accessed.addr)];
      if (__builtin_expect(!Page, false)) {
        foundUnoccupied = true;
        Page = new Page_t;
        AllocatedPages.push_back(page(Accessed.addr));
        Table[page(Accessed.addr)] = Page;
      }
      foundUnoccupied |= Page->setOccupied(Accessed, TouchedWords);
    }
    return foundUnoccupied;
  }

  // High-level fast-path method to set the occupancy of the shadow memory for a
  // small, aligned access.
  __attribute__((always_inline)) bool setOccupiedFast(uintptr_t addr,
                                                      size_t mem_size) {
    assert(AllocIdx != AllocMAAllocator &&
           "Called setOccupied on Alloc shadow memory");

    Page_t *Page = Table[page(addr)];
    if (__builtin_expect(!Page, false)) {
      Page = new Page_t;
      AllocatedPages.push_back(page(addr));
      Table[page(addr)] = Page;
    }
    return Page->setOccupiedFast(addr, mem_size, TouchedWords);
  }

  // High-level method to clear any occupancy information recorded.
  void clearOccupied() {
    for (uintptr_t wordAddr : TouchedWords)
      Table[page(wordAddr)]->clear(wordAddr);
    TouchedWords.clear();
  }

  // Free pages of shadow memory.
  void freePages() {
    TouchedWords.clear();
    for (uintptr_t Addr : AllocatedPages) {
      delete Table[Addr];
      Table[Addr] = nullptr;
    }
    AllocatedPages.clear();
  }

  // High-level method to find a MemoryAccess_t object at the specified address.
  const MemoryAccess_t *find(uintptr_t addr) const {
    Query_iterator<Page_t> QI(*this, Chunk_t(addr, 1));
    return QI.get();
  }

  // High-level method to set shadow of the specified chunk of memory to match
  // the MemoryAccess_t formed by (func, acc_id, type).
  void set(uintptr_t addr, size_t size, DisjointSet_t<call_stack_t> *func,
           version_t version, csi_id_t acc_id, MAType_t type) {
    Update_iterator<Page_t> UI(*this, Chunk_t(addr, size));
    UI.set(MASetFn({func, version, acc_id, type}));
  }

  // Get a query iterator for the specified chunk of memory.
  Query_iterator<Page_t> getQueryIterator(uintptr_t addr, size_t size) const {
    return Query_iterator<Page_t>(*this, Chunk_t(addr, size));
  }

  // Get an update iterator for the specified chunk of memory.
  Update_iterator<Page_t> getUpdateIterator(uintptr_t addr, size_t size) {
    return Update_iterator<Page_t>(*this, Chunk_t(addr, size));
  }

  // Get a query iterator for the lockers for a specified chunk of memory.
  Query_iterator<LockerPage_t> getLockerQueryIterator(uintptr_t addr,
                                                      size_t size) const {
    return Query_iterator<LockerPage_t>(*this, Chunk_t(addr, size));
  }

  // Get an update iterator for the lockers for a specified chunk of memory.
  Update_iterator<LockerPage_t> getLockerUpdateIterator(uintptr_t addr,
                                                        size_t size) {
    return Update_iterator<LockerPage_t>(*this, Chunk_t(addr, size));
  }

  // Clear all entries for the specified chunk of memory.
  __attribute__((always_inline)) void clear(uintptr_t addr, size_t size) {
    Update_iterator<Page_t> UI(*this, Chunk_t(addr, size));
    UI.clear();

    // Also clear the locker table, if it is used.
    if (LockerTableUsed) {
      Update_iterator<LockerPage_t> DRUI(*this, Chunk_t(addr, size));
      DRUI.clear();
    }
  }
};

class SimpleShadowMem {
private:
  CilkSanImpl_t &CilkSanImpl;
  // The shadow memory involves three dictionaries to separately handle reads,
  // writes, and allocations.  The template parameter allows each dictionary to
  // use a different memory allocator.
  SimpleDictionary<ReadMAAllocator> Reads;
  SimpleDictionary<WriteMAAllocator> Writes;
  SimpleDictionary<AllocMAAllocator> Allocs;

  using RLine_t = SimpleDictionary<ReadMAAllocator>::Line_t;
  using WLine_t = SimpleDictionary<WriteMAAllocator>::Line_t;

  void freePages() {
    Reads.freePages();
    Writes.freePages();
  }

  __attribute__((always_inline)) static bool
  previousAccessInParallel(MemoryAccess_t *PrevAccess, const FrameData_t *f) {
    return MemoryAccess_t::previousAccessInParallel(PrevAccess, f);
  }
  __attribute__((always_inline)) static bool
  previousAccessInParallel(const MemoryAccess_t *PrevAccess,
                           const FrameData_t *f) {
    return MemoryAccess_t::previousAccessInParallel(PrevAccess, f);
  }

  // Logic to try to get memory-allocation information on the given address
  __attribute__((always_inline)) AccessLoc_t
  findAllocLoc(uintptr_t addr) const {
    // Try to get information on the allocation for this memory access.
    if (auto AllocFind = Allocs.find(addr))
      return AllocFind->getLoc();
    return AccessLoc_t();
  }

  // Logic to check for a data race with the given previous accesses.
  __attribute__((always_inline)) static bool
  dataRaceWithPreviousAccesses(LockerList_t *PrevAccesses, const FrameData_t *f,
                               const LockSet_t &LS) {
    Locker_t *locker = PrevAccesses->getHead();
    while (locker) {
      if (previousAccessInParallel(&locker->getAccess(), f)) {
        if (IntersectionResult_t::EMPTY ==
            LockSet_t::intersect(locker->getLockSet(), LS))
          return true;
      }
      locker = locker->getNext();
    }
    return false;
  }
  __attribute__((always_inline)) static bool
  dataRaceWithPreviousAccesses(const LockerList_t *PrevAccesses,
                               const FrameData_t *f, const LockSet_t &LS) {
    return dataRaceWithPreviousAccesses(
        const_cast<LockerList_t *>(PrevAccesses), f, LS);
  }

public:
  static int getLgSmallAccessSize() {
    return SimpleDictionary<ReadMAAllocator>::getLgSmallAccessSize();
  }

  SimpleShadowMem(CilkSanImpl_t &CilkSanImpl) : CilkSanImpl(CilkSanImpl) {}
  ~SimpleShadowMem() {}

  // Set the occupancy bits in the appropriate dictionary.  Returns true if some
  // location in [addr, add+mem_size) was not already occupied, false otherwise.
  __attribute__((always_inline)) bool setOccupied(bool is_read, uintptr_t addr,
                                                  size_t mem_size) {
    if (is_read)
      return Reads.setOccupied(addr, mem_size);
    else
      return Writes.setOccupied(addr, mem_size);
  }

  // Fast path to set the occupancy bits in the appropriate dictionary.  Returns
  // true if some location in [addr, add+mem_size) was not already occupied,
  // false otherwise.
  __attribute__((always_inline)) bool setOccupiedFast(bool is_read, uintptr_t addr,
                                                      size_t mem_size) {
    if (is_read)
      return Reads.setOccupiedFast(addr, mem_size);
    else
      return Writes.setOccupiedFast(addr, mem_size);
  }

  __attribute__((always_inline)) void clearOccupied() {
    Reads.clearOccupied();
    Writes.clearOccupied();
  }

  // Core routine for checking for a determinacy race, using the given
  // Query_iterator QI.
  template <typename QITy, bool prev_read, bool is_read>
  __attribute__((always_inline)) void
  check_race(QITy &QI, const csi_id_t acc_id, MAType_t type,
             const FrameData_t *f) const {
    // Repeat as long as the Query_iterator has more memory locations to check.
    while (!QI.isEnd()) {
      // Find a previous access
      const MemoryAccess_t *PrevAccess = QI.get();
      if (PrevAccess && PrevAccess->isValid()) {
        // If the previous access was in parallel, then we have a race
        if (__builtin_expect(previousAccessInParallel(PrevAccess, f), false)) {
          uintptr_t AccAddr = QI.getAddress();

          // Report the race
          if (prev_read)
            CilkSanImpl.report_race(
                PrevAccess->getLoc(),
                AccessLoc_t(acc_id, type, CilkSanImpl.get_current_call_stack()),
                findAllocLoc(AccAddr), AccAddr, RW_RACE);
          else {
            if (is_read)
              CilkSanImpl.report_race(
                  PrevAccess->getLoc(),
                  AccessLoc_t(acc_id, type,
                              CilkSanImpl.get_current_call_stack()),
                  findAllocLoc(AccAddr), AccAddr, WR_RACE);
            else
              CilkSanImpl.report_race(
                  PrevAccess->getLoc(),
                  AccessLoc_t(acc_id, type,
                              CilkSanImpl.get_current_call_stack()),
                  findAllocLoc(AccAddr), AccAddr, WW_RACE);
          }
        }
      }
      // Get the next location to check.
      QI.next();
    }
  }

  // Instantiate check_race to check for a determinacy race with a previous read
  // access.
  __attribute__((always_inline)) void
  check_race_with_prev_read(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size,
                            const FrameData_t *f) const {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using QITy = RDict::Query_iterator<RDict::Page_t>;
    QITy QI = Reads.getQueryIterator(addr, mem_size);
    // The second argument does not matter here.
    check_race<QITy, true, false>(QI, acc_id, type, f);
  }

  // Instantiate check_race to check for a determinacy race with a previous
  // write access.
  template <bool is_read>
  __attribute__((always_inline)) void
  check_race_with_prev_write(const csi_id_t acc_id, MAType_t type,
                             uintptr_t addr, size_t mem_size,
                             const FrameData_t *f) const {
    using WDict = SimpleDictionary<WriteMAAllocator>;
    using QITy = WDict::Query_iterator<WDict::Page_t>;
    QITy QI = Writes.getQueryIterator(addr, mem_size);
    check_race<QITy, false, is_read>(QI, acc_id, type, f);
  }

  // Core routine for updating the entries of a dictionary, using the given
  // Update_iterator UI.
  template <typename UITy, class MASetFn>
  __attribute__((always_inline)) void
  update(UITy &UI, const csi_id_t acc_id, MAType_t type, const FrameData_t *f) {
    SBag_t *sbag = f->getSbagForAccess();
    DS_t *ds = sbag->get_ds();
    version_t version = sbag->get_version();
    // Repeat as long as the Update_iterator has more memory locations to
    // update.
    while (!UI.isEnd()) {
      // Find a previous access
      MemoryAccess_t *PrevAccess = UI.get();
      if (!PrevAccess || !PrevAccess->isValid()) {
        // This is the first access to this location.  Record the memory access.
        UI.insert(MASetFn({ds, version, acc_id, type}));
      } else {
        // If the previous access was in series, update it.  Otherwise, get the
        // next location to check
        if (!previousAccessInParallel(PrevAccess, f)) {
          UI.insert(MASetFn({ds, version, acc_id, type}));
        } else {
          // Nothing to update; get the next location.
          UI.next();
        }
      }
    }
  }

  // Instantiate update to update the read dictionary with a new read access.
  __attribute__((always_inline)) void
  update_with_read(const csi_id_t acc_id, MAType_t type, uintptr_t addr,
                   size_t mem_size, const FrameData_t *f) {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using UITy = RDict::Update_iterator<RDict::Page_t>;
    UITy UI = Reads.getUpdateIterator(addr, mem_size);
    update<UITy, RDict::MASetFn>(UI, acc_id, type, f);
  }

  // Core routine that combines the checking and updating of the write
  // dictionary with a new write access.
  __attribute__((always_inline)) void
  check_and_update_write(const csi_id_t acc_id, MAType_t type, uintptr_t addr,
                         size_t mem_size, const FrameData_t *f) {
    // Create an Update_iterator for the new write access
    using WDict = SimpleDictionary<WriteMAAllocator>;
    using UITy = WDict::Update_iterator<WDict::Page_t>;
    UITy UI = Writes.getUpdateIterator(addr, mem_size);

    SBag_t *sbag = f->getSbagForAccess();
    DS_t *ds = sbag->get_ds();
    version_t version = sbag->get_version();
    // Repeat as long as there are more memory locations to check and update.
    while (!UI.isEnd()) {
      // Find a previous access
      MemoryAccess_t *PrevAccess = UI.get();
      if (!PrevAccess || !PrevAccess->isValid()) {
        // This is the first access to this location.  Record the access.
        UI.insert(WDict::MASetFn({ds, version, acc_id, type}));
      } else {
        // If the previous access was in parallel, we have a race.
        if (__builtin_expect(previousAccessInParallel(PrevAccess, f), false)) {
          uintptr_t AccAddr = UI.getAddress();
          // Report the race
          CilkSanImpl.report_race(
              PrevAccess->getLoc(),
              AccessLoc_t(acc_id, type, CilkSanImpl.get_current_call_stack()),
              findAllocLoc(AccAddr), AccAddr, WW_RACE);

          // Get the next location to check
          UI.next();
        } else {
          // Otherwise, the previous was in series, so update it
          UI.insert(WDict::MASetFn({ds, version, acc_id, type}));
        }
      }
    }
  }

  // Implement a fast-path check for a determinacy race against a new read
  // access.  This fast path is tailored for small (mem_size <= 2^LG_LINE_SIZE),
  // aligned memory accesses.
  __attribute__((always_inline)) void
  check_read_fast(const csi_id_t acc_id, MAType_t type, uintptr_t addr,
                  size_t mem_size, const FrameData_t *f) {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using WDict = SimpleDictionary<WriteMAAllocator>;
    // Get the line storing the previous write to this location, if any.
    const WLine_t *__restrict__ write_line =
        Writes.getLine<WDict::Page_t>(addr, mem_size);
    // Since we only need to query the previous write access, we can still
    // handle this read even if we don't have a previous write access.
    bool need_check = write_line && !write_line->isEmpty();
    if (need_check &&
        (1 << write_line->getLgGrainsize()) != (unsigned)mem_size) {
      // This access touches more than one entry in the line.  Handle it via the
      // slow path.
      check_race_with_prev_write<true>(acc_id, type, addr, mem_size, f);
      need_check = false;
    }

    // Get the line storing the previous write to this location, if any.
    RLine_t *__restrict__ read_line =
        Reads.getLineMustExist<RDict::Page_t>(addr, mem_size);
    bool need_update = true;
    if ((1 << read_line->getLgGrainsize()) != (unsigned)mem_size) {
      // This access touches more than one entry in the line.  Handle it via the
      // slow path.
      update_with_read(acc_id, type, addr, mem_size, f);
      need_update = false;
    }

    // We're now committed to handling this check.  Insert the read access
    // first, then check against the write access.

    // Update the read dictionary with this new access, if need be.
    if (need_update) {
      // Materialize the read line if necessary
      if (!read_line->isMaterialized())
        read_line->materialize();
      // Get the read MemoryAccess_t entry to update
      MemoryAccess_t *read_ma = &(*read_line)[Reads.byte(addr)];
      if (!read_ma->isValid()) {
        // If we're inserting a new read, increment the count of non-null
        // accesses in this line
        read_line->incNumNonNullEls();

        // Update the read MemoryAccess_t
        SBag_t *sbag = f->getSbagForAccess();
        DS_t *ds = sbag->get_ds();
        version_t version = sbag->get_version();
        read_ma->set(ds, version, acc_id, type);
      } else {
        // Otherwise, only insert the new read if it is in series with the
        // previous read.
        if (!previousAccessInParallel(read_ma, f)) {
          // This read access is in series with the previous access, so update
          // the shadow memory.
          SBag_t *sbag = f->getSbagForAccess();
          DS_t *ds = sbag->get_ds();
          version_t version = sbag->get_version();
          read_ma->set(ds, version, acc_id, type);
        }
      }
    }

    // If need be, check the previous write access for a race.
    if (need_check) {
      // Get the write MemoryAccess_t to query.
      const MemoryAccess_t &write_ma = (*write_line)[Writes.byte(addr)];
      if (write_ma.isValid()) {
        // If the previous access is in parallel, then we have a race
        if (__builtin_expect(previousAccessInParallel(&write_ma, f), false)) {
          // Report the race
          CilkSanImpl.report_race(
              write_ma.getLoc(),
              AccessLoc_t(acc_id, type, CilkSanImpl.get_current_call_stack()),
              findAllocLoc(addr), addr, WR_RACE);
        }
      }
    }
  }

  // Implement a fast-path check for a determinacy race against a new write
  // access.  This fast path is tailored for small (mem_size <= 2^LG_LINE_SIZE),
  // aligned memory accesses.
  __attribute__((always_inline)) void
  check_write_fast(const csi_id_t acc_id, MAType_t type, uintptr_t addr,
                   size_t mem_size, const FrameData_t *f) {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using WDict = SimpleDictionary<WriteMAAllocator>;
    // Get the line storing the previous read to this location, if any.
    const RLine_t *__restrict__ read_line =
        Reads.getLine<RDict::Page_t>(addr, mem_size);
    // Since we only need to query the previous write access, we can still
    // handle this read even if we don't have a previous write access.
    bool need_read_check = read_line && !read_line->isEmpty();
    if (need_read_check && (1 << read_line->getLgGrainsize()) != (unsigned)mem_size) {
      // This access touches more than one entry in the line.  Handle it via the
      // slow path.
      check_race_with_prev_read(acc_id, type, addr, mem_size, f);
      need_read_check = false;
    }

    // Get the line storing the previous write to this location, if any.
    WLine_t *__restrict__ write_line =
        Writes.getLineMustExist<WDict::Page_t>(addr, mem_size);
    bool need_update = true;
    if ((1 << write_line->getLgGrainsize()) != (unsigned)mem_size) {
      // This access touches more than one entry in the line.  Handle it via the
      // slow path.
      check_and_update_write(acc_id, type, addr, mem_size, f);
      need_update = false;
    }

    // We're now committed to handling this check.  Check and update the write
    // first, then check against the read.

    // Perform a combined check-and-update of the write dictionary for this
    // access, if need be.
    if (need_update) {
      // Materialize the write line if necessary
      if (!write_line->isMaterialized())
        write_line->materialize();
      // Get the write MemoryAccess_t entry to update
      MemoryAccess_t *write_ma = &(*write_line)[Writes.byte(addr)];
      if (!write_ma->isValid()) {
        // If we're inserting a new write, increment the count of non-null
        // accesses in this line.
        write_line->incNumNonNullEls();

        // Update the write MemoryAccess_t
        SBag_t *sbag = f->getSbagForAccess();
        DS_t *ds = sbag->get_ds();
        version_t version = sbag->get_version();
        write_ma->set(ds, version, acc_id, type);
      } else {
        // Otherwise, check against the existing write.
        if (previousAccessInParallel(write_ma, f)) {
          // Report the race
          CilkSanImpl.report_race(
              write_ma->getLoc(),
              AccessLoc_t(acc_id, type, CilkSanImpl.get_current_call_stack()),
              findAllocLoc(addr), addr, WW_RACE);
        } else {
          // This write access is in series with the previous access, so update
          // the shadow memory.
          SBag_t *sbag = f->getSbagForAccess();
          DS_t *ds = sbag->get_ds();
          version_t version = sbag->get_version();
          write_ma->set(ds, version, acc_id, type);
        }
      }
    }

    // Check the previous read access for a race, if need be.
    if (need_read_check) {
      // Get the read MemoryAccess_t to query.
      const MemoryAccess_t &read_ma = (*read_line)[Reads.byte(addr)];
      if (__builtin_expect(read_ma.isValid(), true)) {
        // If the previous access was in parallel, then we have a race
        if (previousAccessInParallel(&read_ma, f)) {
          // Report the race
          CilkSanImpl.report_race(
              read_ma.getLoc(),
              AccessLoc_t(acc_id, type, CilkSanImpl.get_current_call_stack()),
              findAllocLoc(addr), addr, RW_RACE);
        }
      }
    }
  }

  // Methods for checking for data races and updating lockers.

  template <typename DictTy, typename QITy, typename LQITy, bool prev_read,
            bool is_read>
  __attribute__((always_inline)) void
  check_data_race(const DictTy &Dict, QITy &QI, const csi_id_t acc_id,
                  MAType_t type, const FrameData_t *f,
                  const LockSet_t &LS) const {
    while (!QI.isEnd()) {
      // Find a previous access
      const MemoryAccess_t *PrevAccess = QI.get();
      if (PrevAccess && PrevAccess->isValid()) {
        // If the previous access was in parallel, then we have a race
        if (__builtin_expect(previousAccessInParallel(PrevAccess, f), false)) {
          uintptr_t StartAddr = QI.getAddress();
          // Get the next location to check
          QI.next();
          uintptr_t EndAddr = QI.getAddress();
          cilksan_assert(EndAddr > StartAddr);

          // Create a locker query iterator for the range of addresses
          // [StartAddr, EndAddr) examined by the Query_iterator..
          LQITy LQI =
              Dict.getLockerQueryIterator(StartAddr, EndAddr - StartAddr);
          while (!LQI.isEnd()) {
            // Find lockers for previous accesses
            const LockerList_t *PrevAccesses = LQI.get();
            if (!PrevAccesses || !PrevAccesses->isValid() ||
                dataRaceWithPreviousAccesses(PrevAccesses, f, LS)) {
              // Report the race
              uintptr_t AccAddr = LQI.getAddress();

              if (prev_read)
                CilkSanImpl.report_race(
                    PrevAccess->getLoc(),
                    AccessLoc_t(acc_id, type,
                                CilkSanImpl.get_current_call_stack()),
                    findAllocLoc(AccAddr), AccAddr, RW_RACE);
              else {
                if (is_read)
                  CilkSanImpl.report_race(
                      PrevAccess->getLoc(),
                      AccessLoc_t(acc_id, type,
                                  CilkSanImpl.get_current_call_stack()),
                      findAllocLoc(AccAddr), AccAddr, WR_RACE);
                else
                  CilkSanImpl.report_race(
                      PrevAccess->getLoc(),
                      AccessLoc_t(acc_id, type,
                                  CilkSanImpl.get_current_call_stack()),
                      findAllocLoc(AccAddr), AccAddr, WW_RACE);
              }
            }
            LQI.next();
          }
          continue;
        }
      }
      QI.next();
    }
  }

  __attribute__((always_inline)) void check_data_race_with_prev_read(
      const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
      const FrameData_t *f, const LockSet_t &LS) const {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using QITy = RDict::Query_iterator<RDict::Page_t>;
    using LQITy = RDict::Query_iterator<RDict::LockerPage_t>;
    QITy QI = Reads.getQueryIterator(addr, mem_size);
    // The second argument does not matter here.
    check_data_race<RDict, QITy, LQITy, true, false>(Reads, QI, acc_id, type, f,
                                                     LS);
  }

  template <bool is_read>
  __attribute__((always_inline)) void check_data_race_with_prev_write(
      const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
      const FrameData_t *f, const LockSet_t &LS) const {
    using WDict = SimpleDictionary<WriteMAAllocator>;
    using QITy = WDict::Query_iterator<WDict::Page_t>;
    using LQITy = WDict::Query_iterator<WDict::LockerPage_t>;
    QITy QI = Writes.getQueryIterator(addr, mem_size);
    check_data_race<WDict, QITy, LQITy, false, is_read>(Writes, QI, acc_id,
                                                        type, f, LS);
  }

  template <typename UITy, class LockerSetFn>
  __attribute__((always_inline)) void
  update_lockers(UITy &UI, const csi_id_t acc_id, MAType_t type,
                 const FrameData_t *f, const LockSet_t &LS) {
    while (!UI.isEnd()) {
      UI.insert(LockerSetFn({LS, acc_id, type, f}));
    }
  }

  __attribute__((always_inline)) void
  update_lockers_with_read(const csi_id_t acc_id, MAType_t type, uintptr_t addr,
                           size_t mem_size, const FrameData_t *f,
                           const LockSet_t &LS) {
    using RDict = SimpleDictionary<ReadMAAllocator>;
    using UITy = RDict::Update_iterator<RDict::LockerPage_t>;
    UITy UI = Reads.getLockerUpdateIterator(addr, mem_size);
    update_lockers<UITy, RDict::LockerSetFn>(UI, acc_id, type, f, LS);
  }

  __attribute__((always_inline)) void
  check_data_race_and_update_write(const csi_id_t acc_id, MAType_t type,
                                   uintptr_t addr, size_t mem_size,
                                   const FrameData_t *f, const LockSet_t &LS) {
    using WDict = SimpleDictionary<WriteMAAllocator>;
    using UITy = WDict::Update_iterator<WDict::Page_t>;
    using LUITy = WDict::Update_iterator<WDict::LockerPage_t>;
    UITy UI = Writes.getUpdateIterator(addr, mem_size);

    SBag_t *sbag = f->getSbagForAccess();
    DS_t *ds = sbag->get_ds();
    version_t version = sbag->get_version();

    while (!UI.isEnd()) {
      // Find a previous access
      MemoryAccess_t *PrevAccess = UI.get();
      if (!PrevAccess || !PrevAccess->isValid()) {
        // This is the first access to this location.
        uintptr_t StartAddr = UI.getAddress();
        UI.insert(WDict::MASetFn({ds, version, acc_id, type}));
        uintptr_t EndAddr = UI.getAddress();
        cilksan_assert(EndAddr > StartAddr);

        // Update the lockers over the same range of addresses just updated.
        LUITy LUI =
            Writes.getLockerUpdateIterator(StartAddr, EndAddr - StartAddr);
        update_lockers<LUITy, WDict::LockerSetFn>(LUI, acc_id, type, f, LS);
      } else {
        // If the previous access was in parallel, check for a data race.
        if (__builtin_expect(previousAccessInParallel(PrevAccess, f), false)) {
          uintptr_t StartAddr = UI.getAddress();
          // Get the next location to check
          UI.next();
          uintptr_t EndAddr = UI.getAddress();
          cilksan_assert(EndAddr > StartAddr);

          // Create a locker update iterator for this range of addresses
          LUITy LUI =
              Writes.getLockerUpdateIterator(StartAddr, EndAddr - StartAddr);
          // Check and update the lockers in this range of addresses
          while (!LUI.isEnd()) {
            // Find lockers for previous accesses
            LockerList_t *PrevAccesses = LUI.get();
            // Check for a data race
            if (!PrevAccesses || !PrevAccesses->isValid() ||
                dataRaceWithPreviousAccesses(PrevAccesses, f, LS)) {
              // Report the race
              uintptr_t AccAddr = LUI.getAddress();
              CilkSanImpl.report_race(
                  PrevAccess->getLoc(),
                  AccessLoc_t(acc_id, type,
                              CilkSanImpl.get_current_call_stack()),
                  findAllocLoc(AccAddr), AccAddr, WW_RACE);
            }
            // Insert the new locker
            LUI.insert(WDict::LockerSetFn({LS, acc_id, type, f}));
          }
        } else {
          // Otherwise, the previous was in series, so update it
          uintptr_t StartAddr = UI.getAddress();
          UI.insert(WDict::MASetFn({ds, version, acc_id, type}));
          uintptr_t EndAddr = UI.getAddress();
          cilksan_assert(EndAddr > StartAddr);

          // Update the lockers over the same range of addresses just updated.
          LUITy LUI =
              Writes.getLockerUpdateIterator(StartAddr, EndAddr - StartAddr);
          update_lockers<LUITy, WDict::LockerSetFn>(LUI, acc_id, type, f, LS);
        }
      }
    }
  }

  __attribute__((always_inline)) void clear(size_t start, size_t size) {
    Reads.clear(start, size);
    Writes.clear(start, size);
  }

  void record_alloc(size_t start, size_t size, FrameData_t *f,
                    csi_id_t alloca_id) {
    SBag_t *sbag = f->getSbagForAccess();
    DS_t *ds = sbag->get_ds();
    version_t version = sbag->get_version();
    Allocs.set(start, size, ds, version, alloca_id, MAType_t::ALLOC);
  }

  void record_free(size_t start, size_t size, FrameData_t *f, csi_id_t free_id,
                   MAType_t type) {
    Allocs.clear(start, size);
    SBag_t *sbag = f->getSbagForAccess();
    DS_t *ds = sbag->get_ds();
    version_t version = sbag->get_version();
    Writes.set(start, size, ds, version, free_id, type);
  }

  void clear_alloc(size_t start, size_t size) { Allocs.clear(start, size); }
};

#endif // __SIMPLE_SHADOW_MEM__
