// -*- C++ -*-
#ifndef _ADDR_MAP_H
#define _ADDR_MAP_H

#include <cstdint>
#include <sys/mman.h>

#include "checking.h"
#include "debug_util.h"

template <typename DATA_T>
class AddrMap_t {
  // log_2 of lines per page.
  static constexpr unsigned LG_PAGE_SIZE = 24;
  // log_2 of number of pages in the top-level table.
  static constexpr unsigned LG_TABLE_SIZE = 48 - LG_PAGE_SIZE;

  // Mask to identify the byte within a page.
  static constexpr uintptr_t BYTE_MASK = ((1UL << LG_PAGE_SIZE) - 1);

  // Helper methods to get the indices into the dictionary from a given address.
  __attribute__((always_inline)) static uintptr_t byte(uintptr_t addr) {
    return addr & BYTE_MASK;
  }
  __attribute__((always_inline)) static uintptr_t page(uintptr_t addr) {
    return addr >> LG_PAGE_SIZE;
  }

  struct Page_t {
    // Bitmap identifying bytes in the page that were previously accessed in
    // the current strand.
    static constexpr size_t VALID_ARR_SIZE =
        (1UL << LG_PAGE_SIZE) / (8 * sizeof(uint64_t));
    uint64_t Valid[VALID_ARR_SIZE] = {0};

    DATA_T Bytes[1UL << LG_PAGE_SIZE];

    // To accommodate the size and sparse access pattern of a Page_t, use
    // mmap/munmap to allocate and free Page_t's.
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
    DATA_T &operator[](uintptr_t byteaddr) { return Bytes[byteaddr]; }
    const DATA_T &operator[](uintptr_t byteaddr) const {
      return Bytes[byteaddr];
    }

    // Constants for operating on valid bits
    static constexpr uintptr_t LG_VALID_WORD_SIZE = 6;
    static constexpr uintptr_t VALID_WORD_SIZE = 1UL << LG_VALID_WORD_SIZE;
    static_assert(
        VALID_WORD_SIZE == (8 * sizeof(uint64_t)),
        "LG_VALID_WORD_SIZE does not correspond with valid-word size");
    static constexpr uintptr_t VALID_BIT_MASK = VALID_WORD_SIZE - 1;
    static constexpr uintptr_t VALID_WORD_MASK = ~VALID_BIT_MASK;
    static constexpr uintptr_t VALID_WORD_IDX =
        VALID_WORD_MASK ^ ~((1UL << LG_PAGE_SIZE) - 1);

    // Static helper methods for operating on valid bits
    __attribute__((always_inline)) static uintptr_t validWord(uintptr_t addr) {
      return (addr & VALID_WORD_IDX) >> LG_VALID_WORD_SIZE;
    }
    __attribute__((always_inline)) static uintptr_t
    validBit(uintptr_t addr) {
      return addr & VALID_BIT_MASK;
    }

    bool isValid(uintptr_t byteaddr) const {
      return Valid[validWord(byteaddr)] & (1UL << validBit(byteaddr));
    }
    void setValid(uintptr_t byteaddr) {
      Valid[validWord(byteaddr)] |= 1UL << validBit(byteaddr);
    }
    void clearValid(uintptr_t byteaddr) {
      Valid[validWord(byteaddr)] &= ~(1UL << validBit(byteaddr));
    }
  };

  Page_t *Table[1UL << LG_TABLE_SIZE] = {nullptr};

  Page_t *getPage(uintptr_t addr) const {
    return Table[page(addr)];
  }

  Page_t *getOrCreatePage(uintptr_t addr) {
    Page_t *Page = getPage(addr);
    if (!Page) {
      Page = new Page_t;
      Table[page(addr)] = Page;
    }
    return Page;
  }

public:

  ~AddrMap_t() {
    for (uintptr_t i = 0; i < (1UL << LG_TABLE_SIZE); ++i) {
      if (Table[i]) {
        delete Table[i];
        Table[i] = nullptr;
      }
    }
  }

  bool contains(uintptr_t addr) const {
    if (const Page_t *Page = getPage(addr))
      return Page->isValid(byte(addr));
    return false;
  }

  const DATA_T *get(uintptr_t addr) const {
    if (const Page_t *Page = getPage(addr))
      if (Page->isValid(byte(addr)))
        return &(*Page)[byte(addr)];
    return nullptr;
  }

  void insert(uintptr_t addr, const DATA_T &data) {
    Page_t *Page = getOrCreatePage(addr);
    (*Page)[byte(addr)] = data;
    Page->setValid(byte(addr));
  }

  void remove(uintptr_t addr) {
    if (Page_t *Page = getPage(addr))
      Page->clearValid(byte(addr));
  }
};

#endif // _ADDR_MAP_H
