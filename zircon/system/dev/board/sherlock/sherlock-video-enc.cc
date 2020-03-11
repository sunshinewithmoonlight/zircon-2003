// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static pbus_mmio_t sherlock_video_enc_mmios[] = {
    {
        .base = T931_CBUS_BASE,
        .length = T931_CBUS_LENGTH,
    },
    {
        .base = T931_DOS_BASE,
        .length = T931_DOS_LENGTH,
    },
    {
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

constexpr pbus_bti_t sherlock_video_enc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_VIDEO_ENC,
    },
};

constexpr pbus_irq_t sherlock_video_enc_irqs[] = {
    {
        .irq = T931_DOS_MBOX_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t canvas_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_AMLOGIC_CANVAS),
};
constexpr device_component_part_t sysmem_component[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_component_part_t canvas_component[] = {
    {countof(root_match), root_match},
    {countof(canvas_match), canvas_match},
};
constexpr device_component_t components[] = {
    {countof(sysmem_component), sysmem_component},
    {countof(canvas_component), canvas_component},
};

static pbus_dev_t video_enc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-video-enc";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_VIDEO_ENC;
  dev.mmio_list = sherlock_video_enc_mmios;
  dev.mmio_count = countof(sherlock_video_enc_mmios);
  dev.bti_list = sherlock_video_enc_btis;
  dev.bti_count = countof(sherlock_video_enc_btis);
  dev.irq_list = sherlock_video_enc_irqs;
  dev.irq_count = countof(sherlock_video_enc_irqs);
  return dev;
}();

zx_status_t Sherlock::VideoEncInit() {
  zxlogf(INFO, "video-enc init\n");

  zx_status_t status =
      pbus_.CompositeDeviceAdd(&video_enc_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Sherlock::VideoEncInit: CompositeDeviceAdd() failed for video: %d\n", status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
