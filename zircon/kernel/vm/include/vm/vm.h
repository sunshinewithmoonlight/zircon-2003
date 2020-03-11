// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_

#include <arch.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <arch/kernel_aspace.h>
#include <vm/arch_vm_aspace.h>

#define PAGE_ALIGN(x) ALIGN((x), PAGE_SIZE)
#define ROUNDUP_PAGE_SIZE(x) ROUNDUP((x), PAGE_SIZE)
#define IS_PAGE_ALIGNED(x) IS_ALIGNED((x), PAGE_SIZE)

// kernel address space
static_assert(KERNEL_ASPACE_BASE + (KERNEL_ASPACE_SIZE - 1) > KERNEL_ASPACE_BASE, "");

static inline bool is_kernel_address(vaddr_t va) {
  return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
          va - (vaddr_t)KERNEL_ASPACE_BASE < (vaddr_t)KERNEL_ASPACE_SIZE);
}

// user address space, defaults to below kernel space with a 16MB guard gap on either side
static_assert(USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1) > USER_ASPACE_BASE, "");

static inline bool is_user_address(vaddr_t va) {
  return (va >= USER_ASPACE_BASE && va <= (USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1)));
}

static inline bool is_user_address_range(vaddr_t va, size_t len) {
  return va + len >= va && is_user_address(va) && (len == 0 || is_user_address(va + len - 1));
}

// linker script provided variables for various virtual kernel addresses
extern char __code_start[];
extern char __code_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char _end[];

// Prints a single mapping required to interpret backtraces.
template <class F>
void print_mmap(const F &f, uintptr_t bias, const void *begin, const void *end, const char *perm) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(begin);
  const size_t size = reinterpret_cast<uintptr_t>(end) - start;
  f("{{{mmap:%#lx:%#lx:load:0:%s:%#lx}}}\n", start, size, perm, start + bias);
}

template <class F>
void print_module(const F &f, const char *build_id) {
  f("{{{module:0:kernel:elf:%s}}}\n", build_id);
}

// return the physical address corresponding to _start
static inline paddr_t get_kernel_base_phys(void) {
  extern paddr_t kernel_base_phys;

  return kernel_base_phys;
}

static inline size_t get_kernel_size(void) { return _end - __code_start; }

__BEGIN_CDECLS

// C friendly opaque handle to the internals of the VMM.
// Never defined, just used as a handle for C apis.
typedef struct vmm_aspace vmm_aspace_t;

// internal kernel routines below, do not call directly

// internal routine by the scheduler to swap mmu contexts
void vmm_context_switch(vmm_aspace_t *oldspace, vmm_aspace_t *newaspace);

// set the current user aspace as active on the current thread.
// NULL is a valid argument, which unmaps the current user address space
void vmm_set_active_aspace(vmm_aspace_t *aspace);

// specialized version of above function that must be called with the thread_lock already held.
// This is only intended for use by panic handlers.
void vmm_set_active_aspace_locked(vmm_aspace_t *aspace);

struct kernel_region {
  const char *name;
  vaddr_t base;
  size_t size;
  uint arch_mmu_flags;
};

// List of kernel program's various segments.
static kernel_region kernel_regions[] = {
    {
        .name = "kernel_code",
        .base = (vaddr_t)__code_start,
        .size = ROUNDUP((uintptr_t)__code_end - (uintptr_t)__code_start, PAGE_SIZE),
        .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE,
    },
    {
        .name = "kernel_rodata",
        .base = (vaddr_t)__rodata_start,
        .size = ROUNDUP((uintptr_t)__rodata_end - (uintptr_t)__rodata_start, PAGE_SIZE),
        .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ,
    },
    {
        .name = "kernel_data",
        .base = (vaddr_t)__data_start,
        .size = ROUNDUP((uintptr_t)__data_end - (uintptr_t)__data_start, PAGE_SIZE),
        .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
    },
    {
        .name = "kernel_bss",
        .base = (vaddr_t)__bss_start,
        .size = ROUNDUP((uintptr_t)_end - (uintptr_t)__bss_start, PAGE_SIZE),
        .arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
    },
};

__END_CDECLS

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_
