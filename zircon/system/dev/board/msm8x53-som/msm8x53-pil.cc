// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/fw.h>
#include <soc/msm8x53/msm8x53-clock.h>
#include <zircon/syscalls/smc.h>

#include "msm8x53.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::PilInit() {
  constexpr pbus_smc_t smcs[] = {{
      .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
      .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
      .exclusive = true,
  }};
  constexpr pbus_bti_t btis[] = {{
      .iommu_index = 0,
      .bti_id = BTI_PIL,
  }};

  struct {
    const char* name;
    uint8_t id;
  } local_fw_list[] = {
      {"adsp", 1},
  };
  constexpr pbus_mmio_t fw_mmios[] = {
      {
          .base = 0x8840'0000,
          .length = 32 * 1024 * 1024,
      },
  };
  static_assert(countof(local_fw_list) == countof(fw_mmios));

  metadata::Firmware fw_list[countof(local_fw_list)];
  for (size_t i = 0; i < countof(local_fw_list); ++i) {
    strncpy(fw_list[i].name, local_fw_list[i].name, metadata::kMaxNameLen);
    fw_list[i].name[metadata::kMaxNameLen - 1] = 0;
    fw_list[i].id = local_fw_list[i].id;
    fw_list[i].pa = fw_mmios[i].base;
  }

  pbus_metadata_t metadata[] = {{
      .type = DEVICE_METADATA_PRIVATE,
      .data_buffer = &fw_list,
      .data_size = sizeof(fw_list),
  }};

  pbus_dev_t dev = {};
  dev.name = "msm8x53-pil";
  dev.vid = PDEV_VID_QUALCOMM;
  dev.did = PDEV_DID_QUALCOMM_PIL;
  dev.smc_list = smcs;
  dev.smc_count = countof(smcs);
  dev.bti_list = btis;
  dev.bti_count = countof(btis);
  dev.metadata_list = metadata;
  dev.metadata_count = countof(metadata);
  dev.mmio_list = fw_mmios;
  dev.mmio_count = countof(fw_mmios);

  constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
  };
  constexpr zx_bind_inst_t clk_crypto_ahb_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, msm8x53::kCryptoAhbClk),
  };
  constexpr zx_bind_inst_t clk_crypto_axi_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, msm8x53::kCryptoAxiClk),
  };
  constexpr zx_bind_inst_t clk_crypto_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, msm8x53::kCryptoClk),
  };
  const device_component_part_t clk_crypto_ahb_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(clk_crypto_ahb_match), clk_crypto_ahb_match},
  };
  const device_component_part_t clk_crypto_axi_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(clk_crypto_axi_match), clk_crypto_axi_match},
  };
  const device_component_part_t clk_crypto_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(clk_crypto_match), clk_crypto_match},
  };
  const device_component_t components[] = {
    {fbl::count_of(clk_crypto_ahb_component), clk_crypto_ahb_component},
    {fbl::count_of(clk_crypto_axi_component), clk_crypto_axi_component},
    {fbl::count_of(clk_crypto_component), clk_crypto_component},
  };

  auto status = pbus_.CompositeDeviceAdd(&dev, components, fbl::count_of(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not add dev %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_msm8x53
