// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t clk1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, kClkRgSlowMfg),
};
static const zx_bind_inst_t clk2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, kClkRgAxiMfg),
};
static const zx_bind_inst_t clk3_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, kClkMfgMm),
};
static const device_component_part_t clk1_component[] = {
    { countof(root_match), root_match },
    { countof(clk1_match), clk1_match },
};
static const device_component_part_t clk2_component[] = {
    { countof(root_match), root_match },
    { countof(clk2_match), clk2_match },
};
static const device_component_part_t clk3_component[] = {
    { countof(root_match), root_match },
    { countof(clk3_match), clk3_match },
};
static const device_component_t components[] = {
    { countof(clk1_component), clk1_component },
    { countof(clk2_component), clk2_component },
    { countof(clk3_component), clk3_component },
};

zx_status_t Mt8167::GpuInit() {
  const pbus_mmio_t gpu_mmios[] = {
      {
          // Actual GPU registers
          .base = MT8167_MFG_BASE,
          .length = MT8167_MFG_SIZE,
      },
      {
          .base = MT8167_MFG_TOP_CONFIG_BASE,
          .length = MT8167_MFG_TOP_CONFIG_SIZE,
      },
      {
          // Power registers
          .base = MT8167_SCPSYS_BASE,
          .length = MT8167_SCPSYS_SIZE,
      },
      {
          // Clock registers
          .base = MT8167_XO_BASE,
          .length = MT8167_XO_SIZE,
      },
  };

  const pbus_irq_t gpu_irqs[] = {{
      .irq = MT8167_IRQ_RGX,
      .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
  }};

  const pbus_bti_t gpu_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_GPU,
      },
  };
  pbus_dev_t gpu_dev = {};
  gpu_dev.name = "mt8167s_gpu";
  gpu_dev.vid = PDEV_VID_MEDIATEK;
  gpu_dev.did = PDEV_DID_MEDIATEK_GPU;
  gpu_dev.mmio_list = gpu_mmios;
  gpu_dev.mmio_count = countof(gpu_mmios);
  gpu_dev.irq_list = gpu_irqs;
  gpu_dev.irq_count = countof(gpu_irqs);
  gpu_dev.bti_list = gpu_btis;
  gpu_dev.bti_count = countof(gpu_btis);

  auto status = pbus_.CompositeDeviceAdd(&gpu_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
