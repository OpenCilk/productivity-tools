// -*- C++ -*-
#ifndef __RACE_DETECT_UPDATE__
#define __RACE_DETECT_UPDATE__

#include "simple_shadow_mem.h"
#include <csi/csi.h>

// Check races on memory [addr, addr+mem_size) with this read access.  Once done
// checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory);

// Check races on memory [addr, addr+mem_size) with this write access.  Once
// done checking, update shadow_memory with this new read access.  Very similar
// to check_races_and_update_with_read function.
__attribute__((always_inline)) void check_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, SimpleShadowMem &shadow_memory);

// Check races on memory [addr, addr+mem_size) with this memory access.  Once
// done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_races_and_update(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size, FrameData_t *f,
                            SimpleShadowMem &shadow_memory);

// Fast-path check for races on memory [addr, addr+mem_size) with this memory
// access.  Once done checking, update shadow_memory with the new access.
// Assumes that mem_size is small and addr is aligned based on mem_size.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
__attribute__((always_inline)) void
check_races_and_update_fast(const csi_id_t acc_id, MAType_t type,
                            uintptr_t addr, size_t mem_size, FrameData_t *f,
                            SimpleShadowMem &shadow_memory);

// Check data races on memory [addr, addr+mem_size) with this read access.  Once
// done checking, update shadow_memory with this new read access.
__attribute__((always_inline)) void check_data_races_and_update_with_read(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory);

// Check data races on memory [addr, addr+mem_size) with this write access. Once
// done checking, update shadow_memory with this new read access.  Very similar
// to check_data_races_and_update_with_read function.
__attribute__((always_inline)) void check_data_races_and_update_with_write(
    const csi_id_t acc_id, MAType_t type, uintptr_t addr, size_t mem_size,
    FrameData_t *f, const LockSet_t &lockset, SimpleShadowMem &shadow_memory);

// Check data races on memory [addr, addr+mem_size) with this memory access.
// Once done checking, update shadow_memory with the new access.
//
// is_read: whether or not this access reads memory
// acc_id: ID of the memory-access instruction
// type: type of memory access, e.g., a read/write, an allocation, a free
// addr: memory address accessed
// mem_size: number of bytes accessed, starting at addr
// f: pointer to current frame on the shadow stack
// shadow_memory: shadow memory recording memory access information
template <bool is_read>
void check_data_races_and_update(const csi_id_t acc_id, MAType_t type,
                                 uintptr_t addr, size_t mem_size, FrameData_t *f,
                                 const LockSet_t &lockset,
                                 SimpleShadowMem &shadow_memory);

#endif // __RACE_DETECT_UPDATE__
