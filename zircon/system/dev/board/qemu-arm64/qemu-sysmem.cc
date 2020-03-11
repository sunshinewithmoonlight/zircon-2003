// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/sysmem.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "qemu-bus.h"
#include "qemu-virt.h"

namespace board_qemu_arm64 {

zx_status_t QemuArm64::SysmemInit() {
  constexpr pbus_bti_t kSysmemBtis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_SYSMEM,
      },
  };

  constexpr sysmem_metadata_t kSysmemMetadata = {
      .vid = PDEV_VID_QEMU,
      .pid = PDEV_PID_QEMU,
      .protected_memory_size = 0,
      .contiguous_memory_size = 0,
  };

  const pbus_metadata_t kSysmemMetadataList[] = {{
      .type = SYSMEM_METADATA,
      .data_buffer = &kSysmemMetadata,
      .data_size = sizeof(kSysmemMetadata),
  }};

  pbus_dev_t sysmem_dev = {};
  sysmem_dev.name = "sysmem";
  sysmem_dev.vid = PDEV_VID_GENERIC;
  sysmem_dev.pid = PDEV_PID_GENERIC;
  sysmem_dev.did = PDEV_DID_SYSMEM;
  sysmem_dev.bti_list = kSysmemBtis;
  sysmem_dev.bti_count = countof(kSysmemBtis);
  sysmem_dev.metadata_list = kSysmemMetadataList;
  sysmem_dev.metadata_count = countof(kSysmemMetadataList);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_qemu_arm64
