// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ctype.h>
#include <inttypes.h>
#include <lib/crashlog.h>
#include <lib/version.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <zircon/boot/crash-reason.h>

#include <kernel/thread.h>
#include <vm/vm.h>

crashlog_t crashlog = {};

size_t crashlog_to_string(char* out, const size_t out_len, zircon_crash_reason_t reason) {
  char* buf = out;
  size_t remain = out_len;
  const bool is_oom = (reason == ZirconCrashReason::Oom);

  buf[0] = '\0';

  const uintptr_t bias = KERNEL_BASE - reinterpret_cast<uintptr_t>(__code_start);

  // Note that (2, 3) is used instead of (1, 2) because the lambda adds an
  // implicit argument for the captures.
  auto buf_printf = [&buf, &remain](const char* fmt, ...) __PRINTFLIKE(2, 3) {
    va_list args;
    va_start(args, fmt);
    size_t len = vsnprintf(buf, remain, fmt, args);
    remain -= len;
    buf += len;
  };

  buf_printf("ZIRCON REBOOT REASON (%s)\n\n", is_oom ? "OOM" : "KERNEL PANIC");
  if (remain <= 0) {
    return out_len;
  }

  buf_printf("UPTIME (ms)\n%" PRIi64 "\n\n", current_time() / ZX_MSEC(1));
  if (remain <= 0) {
    return out_len;
  }

  // Keep the format and values in sync with the symbolizer.
  // Print before the registers (KASLR offset).
#if defined(__x86_64__)
  const char* arch = "x86_64";
#elif defined(__aarch64__)
  const char* arch = "aarch64";
#endif
  buf_printf(
      "VERSION\narch: %s\nbuild_id: %s\ndso: id=%s base=%#lx "
      "name=zircon.elf\n\n",
      arch, version_string(), elf_build_id_string(), is_oom ? 0u : crashlog.base_address);
  if (remain <= 0) {
    return out_len;
  }

  if (is_oom) {
    // If OOM, then including a backtrace doesn't make sense, return early.
    return out_len - remain;
  }

  print_module(buf_printf, elf_build_id_string());
  if (remain <= 0) {
    return out_len;
  }

  print_mmap(buf_printf, bias, __code_start, __code_end, "rx");
  if (remain <= 0) {
    return out_len;
  }

  print_mmap(buf_printf, bias, __rodata_start, __rodata_end, "r");
  if (remain <= 0) {
    return out_len;
  }

  print_mmap(buf_printf, bias, __data_start, __data_end, "rw");
  if (remain <= 0) {
    return out_len;
  }

  print_mmap(buf_printf, bias, __bss_start, _end, "rw");
  if (remain <= 0) {
    return out_len;
  }

  if (crashlog.iframe) {
#if defined(__aarch64__)
    // clang-format off
    buf_printf(
        "REGISTERS\n"
        "  x0: %#18" PRIx64 "\n"
        "  x1: %#18" PRIx64 "\n"
        "  x2: %#18" PRIx64 "\n"
        "  x3: %#18" PRIx64 "\n"
        "  x4: %#18" PRIx64 "\n"
        "  x5: %#18" PRIx64 "\n"
        "  x6: %#18" PRIx64 "\n"
        "  x7: %#18" PRIx64 "\n"
        "  x8: %#18" PRIx64 "\n"
        "  x9: %#18" PRIx64 "\n"
        " x10: %#18" PRIx64 "\n"
        " x11: %#18" PRIx64 "\n"
        " x12: %#18" PRIx64 "\n"
        " x13: %#18" PRIx64 "\n"
        " x14: %#18" PRIx64 "\n"
        " x15: %#18" PRIx64 "\n"
        " x16: %#18" PRIx64 "\n"
        " x17: %#18" PRIx64 "\n"
        " x18: %#18" PRIx64 "\n"
        " x19: %#18" PRIx64 "\n"
        " x20: %#18" PRIx64 "\n"
        " x21: %#18" PRIx64 "\n"
        " x22: %#18" PRIx64 "\n"
        " x23: %#18" PRIx64 "\n"
        " x24: %#18" PRIx64 "\n"
        " x25: %#18" PRIx64 "\n"
        " x26: %#18" PRIx64 "\n"
        " x27: %#18" PRIx64 "\n"
        " x28: %#18" PRIx64 "\n"
        " x29: %#18" PRIx64 "\n"
        "  lr: %#18" PRIx64 "\n"
        " usp: %#18" PRIx64 "\n"
        " elr: %#18" PRIx64 "\n"
        "spsr: %#18" PRIx64 "\n"
        "\n",
        crashlog.iframe->r[0], crashlog.iframe->r[1], crashlog.iframe->r[2],
        crashlog.iframe->r[3], crashlog.iframe->r[4], crashlog.iframe->r[5],
        crashlog.iframe->r[6], crashlog.iframe->r[7], crashlog.iframe->r[8],
        crashlog.iframe->r[9], crashlog.iframe->r[10], crashlog.iframe->r[11],
        crashlog.iframe->r[12], crashlog.iframe->r[13], crashlog.iframe->r[14],
        crashlog.iframe->r[15], crashlog.iframe->r[16], crashlog.iframe->r[17],
        crashlog.iframe->r[18], crashlog.iframe->r[19], crashlog.iframe->r[20],
        crashlog.iframe->r[21], crashlog.iframe->r[22], crashlog.iframe->r[23],
        crashlog.iframe->r[24], crashlog.iframe->r[25], crashlog.iframe->r[26],
        crashlog.iframe->r[27], crashlog.iframe->r[28], crashlog.iframe->r[29],
        crashlog.iframe->lr, crashlog.iframe->usp, crashlog.iframe->elr,
        crashlog.iframe->spsr);
    // clang-format on
    if (remain <= 0) {
      return out_len;
    }
#elif defined(__x86_64__)
    // clang-format off
    buf_printf("REGISTERS\n"
               "  CS: %#18" PRIx64 "\n"
               " RIP: %#18" PRIx64 "\n"
               " EFL: %#18" PRIx64 "\n"
               " CR2: %#18lx\n"
               " RAX: %#18" PRIx64 "\n"
               " RBX: %#18" PRIx64 "\n"
               " RCX: %#18" PRIx64 "\n"
               " RDX: %#18" PRIx64 "\n"
               " RSI: %#18" PRIx64 "\n"
               " RDI: %#18" PRIx64 "\n"
               " RBP: %#18" PRIx64 "\n"
               " RSP: %#18" PRIx64 "\n"
               "  R8: %#18" PRIx64 "\n"
               "  R9: %#18" PRIx64 "\n"
               " R10: %#18" PRIx64 "\n"
               " R11: %#18" PRIx64 "\n"
               " R12: %#18" PRIx64 "\n"
               " R13: %#18" PRIx64 "\n"
               " R14: %#18" PRIx64 "\n"
               " R15: %#18" PRIx64 "\n"
               "errc: %#18" PRIx64 "\n"
               "\n",
               crashlog.iframe->cs, crashlog.iframe->ip, crashlog.iframe->flags,
               x86_get_cr2(), crashlog.iframe->rax, crashlog.iframe->rbx,
               crashlog.iframe->rcx, crashlog.iframe->rdx, crashlog.iframe->rsi,
               crashlog.iframe->rdi, crashlog.iframe->rbp,
               crashlog.iframe->user_sp, crashlog.iframe->r8,
               crashlog.iframe->r9, crashlog.iframe->r10, crashlog.iframe->r11,
               crashlog.iframe->r12, crashlog.iframe->r13, crashlog.iframe->r14,
               crashlog.iframe->r15, crashlog.iframe->err_code);
    // clang-format on
    if (remain <= 0) {
      return out_len;
    }
#endif
  }

  buf_printf("BACKTRACE (up to 16 calls)\n");
  if (remain <= 0) {
    return out_len;
  }

  size_t len = Thread::Current::AppendCurrentBacktrace(buf, remain);
  if (len > remain) {
    return out_len;
  }
  remain -= len;
  buf += len;

  buf_printf("\n");
  if (remain <= 0) {
    return out_len;
  }

  return out_len - remain;
}
