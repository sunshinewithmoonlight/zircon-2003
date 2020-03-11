// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BOARD_QEMU_ARM64_QEMU_BUS_H_
#define ZIRCON_SYSTEM_DEV_BOARD_QEMU_ARM64_QEMU_BUS_H_

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_qemu_arm64 {

// BTI IDs for our devices
enum {
  BTI_SYSMEM,
};

class QemuArm64 : public ddk::Device<QemuArm64> {
 public:
  QemuArm64(zx_device_t* parent, const ddk::PBusProtocolClient& pbus)
      : ddk::Device<QemuArm64>(parent), pbus_(pbus) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

 private:
  zx_status_t Start();
  int Thread();

  static zx_status_t PciInit();
  zx_status_t PciAdd();
  zx_status_t RtcInit();
  zx_status_t SysmemInit();
  zx_status_t DisplayInit();

  const ddk::PBusProtocolClient pbus_;
  thrd_t thread_;
};

}  // namespace board_qemu_arm64

#endif  // ZIRCON_SYSTEM_DEV_BOARD_QEMU_ARM64_QEMU_BUS_H_
