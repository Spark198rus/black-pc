// POSIX Memory Management and Contiguous Physical Allocator for Xbox kernel
#pragma once

#include "loader/src/platform/windefs.h"

#include <cstdint>

namespace xposix {

void memory_init();

// Pool Allocator (Heap)
void *ExAllocatePoolWithTag(uint32_t pool_type, uint32_t number_of_bytes, uint32_t tag);
void  ExFreePool(void *ptr);

// Contiguous Physical RAM Allocator (0x80000000)
void *MmAllocateContiguousMemory(uint32_t number_of_bytes);
void *MmAllocateContiguousMemoryEx(uint32_t number_of_bytes, uint32_t lowest_acceptable_address,
                                  uint32_t highest_acceptable_address, uint32_t alignment, uint32_t protect);
void  MmFreeContiguousMemory(void *base_address);
uint32_t MmGetPhysicalAddress(void *base_address);

// Virtual Memory APIs
NTSTATUS NtAllocateVirtualMemory(void **base_address, uint32_t zero_bits, uint32_t *region_size,
                                uint32_t allocation_type, uint32_t protect);
NTSTATUS NtFreeVirtualMemory(void **base_address, uint32_t *region_size, uint32_t free_type);

} // namespace xposix
