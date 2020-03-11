// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/sdmmc.h>
#include <hw/reg.h>
#include <soc/aml-a113/a113-hw.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
  ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const pbus_mmio_t emmc_mmios[] = {{
    .base = 0xD0074000,
    .length = 0x2000,
}};

static const pbus_irq_t emmc_irqs[] = {
    {
        .irq = 250,
        // c++ initialization error
        .mode = 0
        // c++ initialization error
    },
};

static const pbus_bti_t emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_EMMC,
    },
};

static aml_sd_emmc_config_t config = {
    // As per AMlogic, on S912 chipset, HS400 mode can be operated at 125MHZ or low.
    .supports_dma = true,
    .min_freq = 400000,
    .max_freq = 100000000,
    .version_3 = false,
    .prefs = SDMMC_HOST_PREFS_DISABLE_HS400,
};

static const pbus_metadata_t emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_EMMC_CONFIG,
        .data_buffer = &config,
        .data_size = sizeof(config),
    },
};

static const pbus_boot_metadata_t emmc_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_PARTITION_MAP,
        .zbi_extra = 0,
    },
};

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, S912_EMMC_RST),
};
static const device_component_part_t gpio_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_match), gpio_match},
};
static const device_component_t components[] = {
    {countof(gpio_component), gpio_component},
};

zx_status_t Vim::EmmcInit() {
  zx_status_t status;

  pbus_dev_t emmc_dev = {};
  emmc_dev.name = "aml_emmc";
  emmc_dev.vid = PDEV_VID_AMLOGIC;
  emmc_dev.pid = PDEV_PID_GENERIC;
  emmc_dev.did = PDEV_DID_AMLOGIC_SD_EMMC_C;
  emmc_dev.mmio_list = emmc_mmios;
  emmc_dev.mmio_count = countof(emmc_mmios);
  emmc_dev.irq_list = emmc_irqs;
  emmc_dev.irq_count = countof(emmc_irqs);
  emmc_dev.bti_list = emmc_btis;
  emmc_dev.bti_count = countof(emmc_btis);
  emmc_dev.metadata_list = emmc_metadata;
  emmc_dev.metadata_count = countof(emmc_metadata);
  emmc_dev.boot_metadata_list = emmc_boot_metadata;
  emmc_dev.boot_metadata_count = countof(emmc_boot_metadata);

  // set alternate functions to enable EMMC
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D0, S912_EMMC_NAND_D0_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D1, S912_EMMC_NAND_D1_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D2, S912_EMMC_NAND_D2_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D3, S912_EMMC_NAND_D3_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D4, S912_EMMC_NAND_D4_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D5, S912_EMMC_NAND_D5_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D6, S912_EMMC_NAND_D6_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_NAND_D7, S912_EMMC_NAND_D7_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_CLK, S912_EMMC_CLK_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_RST, S912_EMMC_RST_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_CMD, S912_EMMC_CMD_FN);
  gpio_impl_.SetAltFunction(S912_EMMC_DS, S912_EMMC_DS_FN);

  status = pbus_.CompositeDeviceAdd(&emmc_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "SdEmmcInit could not add emmc_dev: %d\n", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
