// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_INCLUDE_LIB_CMPCTMALLOC_H_
#define ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_INCLUDE_LIB_CMPCTMALLOC_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <stdbool.h>
#include <stddef.h>
#include <zircon/compiler.h>

#include <kernel/mutex.h>

DECLARE_SINGLETON_MUTEX(TheHeapLock);

void* cmpct_alloc(size_t) TA_EXCL(TheHeapLock::Get());
void* cmpct_realloc(void*, size_t) TA_EXCL(TheHeapLock::Get());
void cmpct_free(void*) TA_EXCL(TheHeapLock::Get());
void* cmpct_memalign(size_t size, size_t alignment) TA_EXCL(TheHeapLock::Get());

void cmpct_init(void) TA_EXCL(TheHeapLock::Get());
void cmpct_dump(bool panic_time) TA_EXCL(TheHeapLock::Get());
void cmpct_get_info(size_t* size_bytes, size_t* free_bytes) TA_EXCL(TheHeapLock::Get());
void cmpct_test(void) TA_EXCL(TheHeapLock::Get());
void cmpct_trim(void) TA_EXCL(TheHeapLock::Get());

#endif  // ZIRCON_KERNEL_LIB_HEAP_CMPCTMALLOC_INCLUDE_LIB_CMPCTMALLOC_H_
