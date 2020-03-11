// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file defines:
// * Initialization code for kernel/object module
// * Singleton instances and global locks
// * Helper functions

#include <inttypes.h>
#include <lib/cmdline.h>
#include <lib/crashlog.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <lk/init.h>
#include <object/diagnostics.h>
#include <object/event_dispatcher.h>
#include <object/executor.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <platform/crashlog.h>
#include <platform/halt_helper.h>

static Executor gExecutor;

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() { return gExecutor.GetRootJobDispatcher(); }

enum PressureLevel : uint8_t {
  kOutOfMemory = 0,
  kCritical,
  kWarning,
  kNormal,
  kNumLevels,
};

static const char* PressureLevelToString(PressureLevel level) {
  switch (level) {
    case PressureLevel::kOutOfMemory:
      return "OutOfMemory";
    case PressureLevel::kCritical:
      return "Critical";
    case PressureLevel::kWarning:
      return "Warning";
    case PressureLevel::kNormal:
      return "Normal";
    default:
      return "Unknown";
  }
}

// Kernel-owned events used to signal userspace at different levels of memory pressure.
static ktl::array<fbl::RefPtr<EventDispatcher>, PressureLevel::kNumLevels> mem_pressure_events;

// Event used for communicating memory state between the mem_avail_state_updated_cb callback and the
// oom thread.
static Event mem_state_signal(EVENT_FLAG_AUTOUNSIGNAL);

static ktl::atomic<PressureLevel> mem_event_idx = PressureLevel::kNormal;
static PressureLevel prev_mem_event_idx = mem_event_idx;

fbl::RefPtr<EventDispatcher> GetMemPressureEvent(uint32_t kind) {
  switch (kind) {
    case ZX_SYSTEM_EVENT_OUT_OF_MEMORY:
      return mem_pressure_events[PressureLevel::kOutOfMemory];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL:
      return mem_pressure_events[PressureLevel::kCritical];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING:
      return mem_pressure_events[PressureLevel::kWarning];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL:
      return mem_pressure_events[PressureLevel::kNormal];
    default:
      return nullptr;
  }
}

// Callback used with |pmm_init_reclamation|.
// This is a very minimal save idx and signal an event as we are called under the pmm lock and must
// avoid causing any additional allocations.
static void mem_avail_state_updated_cb(uint8_t idx) {
  mem_event_idx = PressureLevel(idx);
  mem_state_signal.Signal();
}

// Helper called by the oom thread when low memory mode is entered.
static void on_oom() {
  const char* oom_behavior_str = gCmdline.GetString("kernel.oom.behavior");

  // Default to reboot if not set or set to an unexpected value. See fxbug.dev/33429 for the product
  // details on when this path vs. the reboot should be used.
  enum class OomBehavior {
    kReboot,
    kJobKill,
  } oom_behavior = OomBehavior::kReboot;

  if (oom_behavior_str && strcmp(oom_behavior_str, "jobkill") == 0) {
    oom_behavior = OomBehavior::kJobKill;
  }

  switch (oom_behavior) {
    case OomBehavior::kJobKill:

      if (!gExecutor.KillJobWithKillOnOOM()) {
        printf("OOM: no alive job has a kill bit\n");
      }

      // Since killing is asynchronous, sleep for a short period for the system to quiesce. This
      // prevents us from rapidly killing more jobs than necessary. And if we don't find a
      // killable job, don't just spin since the next iteration probably won't find a one either.
      Thread::Current::SleepRelative(ZX_MSEC(500));
      break;

    case OomBehavior::kReboot:
      const int kSleepSeconds = 8;
      printf("OOM: pausing for %ds after low mem signal\n", kSleepSeconds);
      zx_status_t status = Thread::Current::SleepRelative(ZX_SEC(kSleepSeconds));
      if (status != ZX_OK) {
        printf("OOM: sleep failed: %d\n", status);
      }
      printf("OOM: rebooting\n");

      // Tell the oom_tests host test that we are about to generate an OOM
      // crashlog to keep it happy.  Without these messages present in a
      // specific order in the log, the test will fail.
      printf("stowing crashlog\nZIRCON REBOOT REASON (OOM)\n");

      platform_graceful_halt_helper(HALT_ACTION_REBOOT, ZirconCrashReason::Oom);
  }
}

static int oom_thread(void* unused) {
  while (true) {
    // Get a local copy of the atomic. It's possible by the time we read this that we've already
    // exited the last observed state, but that's fine as we don't necessarily need to signal every
    // transient state.
    PressureLevel idx = mem_event_idx;
    printf("OOM: memory availability state %s\n", PressureLevelToString(idx));

    // Unsignal the last event that was signaled.
    zx_status_t status =
        mem_pressure_events[prev_mem_event_idx]->user_signal_self(ZX_EVENT_SIGNALED, 0);
    if (status != ZX_OK) {
      panic("OOM: unsignal memory event %s failed: %d\n", PressureLevelToString(prev_mem_event_idx),
            status);
    }

    // Signal event corresponding to the new memory state.
    status = mem_pressure_events[idx]->user_signal_self(0, ZX_EVENT_SIGNALED);
    if (status != ZX_OK) {
      panic("OOM: signal memory event %s failed: %d\n", PressureLevelToString(idx), status);
    }
    prev_mem_event_idx = idx;

    // If we're below the out-of-memory watermark, trigger OOM behavior.
    if (idx == 0) {
      on_oom();
    }

    // Wait for the memory state to change again.
    mem_state_signal.Wait(Deadline::infinite());
  }
}

static void memory_pressure_init() {
  for (uint8_t i = 0; i < PressureLevel::kNumLevels; i++) {
    auto level = PressureLevel(i);
    KernelHandle<EventDispatcher> event;
    zx_rights_t rights;
    zx_status_t status = EventDispatcher::Create(0, &event, &rights);
    if (status != ZX_OK) {
      panic("mem pressure event %s create: %d\n", PressureLevelToString(level), status);
    }
    mem_pressure_events[i] = event.release();
  }

  if (gCmdline.GetBool("kernel.oom.enable", true)) {
    constexpr auto kNumWatermarks = PressureLevel::kNumLevels - 1;
    ktl::array<uint64_t, kNumWatermarks> mem_watermarks;

    // TODO(rashaeqbal): The watermarks chosen below are arbitrary. Tune them based on memory usage
    // patterns. Consider moving to percentages of total memory instead of absolute numbers - will
    // be easier to maintain across platforms.
    mem_watermarks[PressureLevel::kOutOfMemory] =
        gCmdline.GetUInt64("kernel.oom.outofmemory-mb", 50) * MB;
    mem_watermarks[PressureLevel::kCritical] =
        gCmdline.GetUInt64("kernel.oom.critical-mb", 150) * MB;
    mem_watermarks[PressureLevel::kWarning] = gCmdline.GetUInt64("kernel.oom.warning-mb", 300) * MB;

    zx_status_t status = pmm_init_reclamation(&mem_watermarks[PressureLevel::kOutOfMemory],
                                              kNumWatermarks, MB, mem_avail_state_updated_cb);
    if (status != ZX_OK) {
      panic("failed to initialize pmm reclamation: %d\n", status);
    }

    auto thread = Thread::Create("oom-thread", oom_thread, nullptr, HIGH_PRIORITY);
    DEBUG_ASSERT(thread);
    thread->Detach();
    thread->Resume();
  }
}

static void object_glue_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
  Handle::Init();
  PortDispatcher::Init();

  gExecutor.Init();

  memory_pressure_init();
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING)
