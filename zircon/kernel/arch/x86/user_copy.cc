// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/user_copy.h"

#include <assert.h>
#include <lib/code_patching.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

CODE_TEMPLATE(kStacInstruction, "stac")
CODE_TEMPLATE(kClacInstruction, "clac")
static const uint8_t kNopInstruction = 0x90;

extern "C" {

void fill_out_stac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kStacInstructionEnd - kStacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kStacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}

void fill_out_clac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kClacInstructionEnd - kClacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kClacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}

void x86_usercopy_select(const CodePatchInfo* patch) {
  const ptrdiff_t kSize = 19;
  DEBUG_ASSERT(patch->dest_size == kSize);
  extern char _x86_usercopy_erms;
  extern char _x86_usercopy_erms_end;
  DEBUG_ASSERT(&_x86_usercopy_erms_end - &_x86_usercopy_erms <= kSize);
  extern char _x86_usercopy_quad;
  extern char _x86_usercopy_quad_end;
  DEBUG_ASSERT(&_x86_usercopy_quad_end - &_x86_usercopy_quad <= kSize);

  memset(patch->dest_addr, kNopInstruction, kSize);
  if (x86_feature_test(X86_FEATURE_ERMS) ||
      (x86_get_microarch_config()->x86_microarch == X86_MICROARCH_AMD_ZEN)) {
    memcpy(patch->dest_addr, &_x86_usercopy_erms, &_x86_usercopy_erms_end - &_x86_usercopy_erms);
  } else {
    memcpy(patch->dest_addr, &_x86_usercopy_quad, &_x86_usercopy_quad_end - &_x86_usercopy_quad);
  }
}
}

static inline bool ac_flag(void) { return x86_save_flags() & X86_FLAGS_AC; }

static bool can_access(const void* base, size_t len) {
  LTRACEF("can_access: base %p, len %zu\n", base, len);

  // We don't care about whether pages are actually mapped or what their
  // permissions are, as long as they are in the user address space.  We
  // rely on a page fault occurring if an actual permissions error occurs.
  DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
  return is_user_address_range(reinterpret_cast<vaddr_t>(base), len);
}

static X64CopyToFromUserRet _arch_copy_from_user(void* dst, const void* src, size_t len,
                                                 uint64_t fault_return_mask) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(src, len))
    return (X64CopyToFromUserRet){.status = ZX_ERR_INVALID_ARGS, .pf_flags = 0, .pf_va = 0};

  // Spectre V1 - force resolution of can_access() before attempting to copy from user memory.
  // A poisoned conditional branch predictor can be used to force the kernel to read any kernel
  // address (speculatively); dependent operations can leak the values read-in.
  __asm__ __volatile__("lfence" ::: "memory");

  Thread* thr = Thread::Current::Get();
  X64CopyToFromUserRet ret =
      _x86_copy_to_or_from_user(dst, src, len, &thr->arch_.page_fault_resume, fault_return_mask);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return ret;
}

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  return _arch_copy_from_user(dst, src, len, X86_USER_COPY_DO_FAULTS).status;
}

zx_status_t arch_copy_from_user_capture_faults(void* dst, const void* src, size_t len,
                                               vaddr_t* pf_va, uint* pf_flags) {
  X64CopyToFromUserRet ret = _arch_copy_from_user(dst, src, len, X86_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}

static X64CopyToFromUserRet _arch_copy_to_user(void* dst, const void* src, size_t len,
                                               uint64_t fault_return_mask) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(dst, len))
    return (X64CopyToFromUserRet){.status = ZX_ERR_INVALID_ARGS, .pf_flags = 0, .pf_va = 0};

  Thread* thr = Thread::Current::Get();
  X64CopyToFromUserRet ret =
      _x86_copy_to_or_from_user(dst, src, len, &thr->arch_.page_fault_resume, fault_return_mask);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return ret;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  return _arch_copy_to_user(dst, src, len, X86_USER_COPY_DO_FAULTS).status;
}

zx_status_t arch_copy_to_user_capture_faults(void* dst, const void* src, size_t len, vaddr_t* pf_va,
                                             uint* pf_flags) {
  X64CopyToFromUserRet ret = _arch_copy_to_user(dst, src, len, X86_USER_COPY_CAPTURE_FAULTS);
  // If a fault didn't occur, and ret.status == ZX_OK, this will copy garbage data. It is the
  // responsibility of the caller to check the status and ignore.
  *pf_va = ret.pf_va;
  *pf_flags = ret.pf_flags;
  return ret.status;
}
