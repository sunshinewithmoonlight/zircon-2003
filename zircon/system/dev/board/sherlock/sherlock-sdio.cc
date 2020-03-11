// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <lib/zx/handle.h>

#include <optional>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/init-step.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/algorithm.h>
#include <hw/reg.h>
#include <hwreg/bitfields.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <wifi/wifi-config.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr uint32_t kGpioBase = fbl::round_down<uint32_t, uint32_t>(T931_GPIO_BASE, PAGE_SIZE);
constexpr uint32_t kGpioBaseOffset = T931_GPIO_BASE - kGpioBase;

class PadDsReg2A : public hwreg::RegisterBase<PadDsReg2A, uint32_t> {
 public:
  static constexpr uint32_t kDriveStrength3mA = 2;

  static auto Get() { return hwreg::RegisterAddr<PadDsReg2A>((0xd2 * 4) + kGpioBaseOffset); }

  DEF_FIELD(1, 0, gpiox_0_select);
  DEF_FIELD(3, 2, gpiox_1_select);
  DEF_FIELD(5, 4, gpiox_2_select);
  DEF_FIELD(7, 6, gpiox_3_select);
  DEF_FIELD(9, 8, gpiox_4_select);
  DEF_FIELD(11, 10, gpiox_5_select);
};

constexpr pbus_boot_metadata_t wifi_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    },
};

constexpr pbus_mmio_t sd_emmc_mmios[] = {
    {
        .base = T931_SD_EMMC_A_BASE,
        .length = T931_SD_EMMC_A_LENGTH,
    },
};

constexpr pbus_irq_t sd_emmc_irqs[] = {
    {
        .irq = T931_SD_EMMC_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t sd_emmc_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

constexpr aml_sd_emmc_config_t sd_emmc_config = {
    .supports_dma = false,
    .min_freq = 500'000,      // 500KHz
    .max_freq = 208'000'000,  // 208MHz
    .version_3 = true,
    .prefs = 0,
};

constexpr wifi_config_t wifi_config = {
    .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    .iovar_table =
        {
            {IOVAR_STR_TYPE, {"ampdu_ba_wsize"}, 32},
            {IOVAR_STR_TYPE, {"stbc_tx"}, 1},
            {IOVAR_STR_TYPE, {"stbc_rx"}, 1},
            {IOVAR_CMD_TYPE, {{BRCMF_C_SET_PM}}, 0},
            {IOVAR_CMD_TYPE, {{BRCMF_C_SET_FAKEFRAG}}, 1},
            {IOVAR_LIST_END_TYPE, {{0}}, 0},
        },
    .cc_table =
        {
            {"WW", 1},   {"AU", 923}, {"CA", 901}, {"US", 843}, {"GB", 889}, {"BE", 889},
            {"BG", 889}, {"CZ", 889}, {"DK", 889}, {"DE", 889}, {"EE", 889}, {"IE", 889},
            {"GR", 889}, {"ES", 889}, {"FR", 889}, {"HR", 889}, {"IT", 889}, {"CY", 889},
            {"LV", 889}, {"LT", 889}, {"LU", 889}, {"HU", 889}, {"MT", 889}, {"NL", 889},
            {"AT", 889}, {"PL", 889}, {"PT", 889}, {"RO", 889}, {"SI", 889}, {"SK", 889},
            {"FI", 889}, {"SE", 889}, {"EL", 889}, {"IS", 889}, {"LI", 889}, {"TR", 889},
            {"CH", 889}, {"NO", 889}, {"JP", 2},   {"KR", 2},   {"TW", 2},   {"IN", 2},
            {"SG", 2},   {"MX", 2},   {"NZ", 2},   {"", 0},
        },
};

constexpr pbus_metadata_t sd_emmc_metadata[] = {
    {
        .type = DEVICE_METADATA_EMMC_CONFIG,
        .data_buffer = &sd_emmc_config,
        .data_size = sizeof(sd_emmc_config),
    },
    {
        .type = DEVICE_METADATA_WIFI_CONFIG,
        .data_buffer = &wifi_config,
        .data_size = sizeof(wifi_config),
    },
};

const pbus_dev_t sdio_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "sherlock-sd-emmc";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_SD_EMMC_A;
  dev.mmio_list = sd_emmc_mmios;
  dev.mmio_count = countof(sd_emmc_mmios);
  dev.bti_list = sd_emmc_btis;
  dev.bti_count = countof(sd_emmc_btis);
  dev.irq_list = sd_emmc_irqs;
  dev.irq_count = countof(sd_emmc_irqs);
  dev.metadata_list = sd_emmc_metadata;
  dev.metadata_count = countof(sd_emmc_metadata);
  dev.boot_metadata_list = wifi_boot_metadata;
  dev.boot_metadata_count = countof(wifi_boot_metadata);
  return dev;
}();

// Composite binding rules for wifi driver.
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sdio_fn1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO), BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 1),           BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
constexpr zx_bind_inst_t sdio_fn2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SDIO), BI_ABORT_IF(NE, BIND_SDIO_VID, 0x02d0),
    BI_ABORT_IF(NE, BIND_SDIO_FUNCTION, 2),           BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4345),
    BI_MATCH_IF(EQ, BIND_SDIO_PID, 0x4359),
};
constexpr zx_bind_inst_t oob_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, T931_WIFI_HOST_WAKE),
};
constexpr device_component_part_t sdio_fn1_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(sdio_fn1_match), sdio_fn1_match},
};
constexpr device_component_part_t sdio_fn2_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(sdio_fn2_match), sdio_fn2_match},
};
constexpr device_component_part_t oob_gpio_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(oob_gpio_match), oob_gpio_match},
};
constexpr device_component_t wifi_composite[] = {
    {fbl::count_of(sdio_fn1_component), sdio_fn1_component},
    {fbl::count_of(sdio_fn2_component), sdio_fn2_component},
    {fbl::count_of(oob_gpio_component), oob_gpio_component},
};

// Composite binding rules for SDIO.
constexpr zx_bind_inst_t wifi_pwren_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, T931_WIFI_REG_ON),
};
constexpr zx_bind_inst_t pwm_e_match[] = {
    BI_MATCH_IF(EQ, BIND_INIT_STEP, BIND_INIT_STEP_PWM),
};
constexpr device_component_part_t wifi_pwren_gpio_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(wifi_pwren_gpio_match), wifi_pwren_gpio_match},
};
constexpr device_component_part_t pwm_e_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(pwm_e_match), pwm_e_match},
};
constexpr device_component_t sdio_components[] = {
    {fbl::count_of(wifi_pwren_gpio_component), wifi_pwren_gpio_component},
    {fbl::count_of(pwm_e_component), pwm_e_component},
};

}  // namespace

zx_status_t Sherlock::SdioInit() {
  zx_status_t status;

  // Configure eMMC-SD soc pads.
  if (((status = gpio_impl_.SetAltFunction(T931_SDIO_D0, T931_SDIO_D0_FN)) != ZX_OK) ||
      ((status = gpio_impl_.SetAltFunction(T931_SDIO_D1, T931_SDIO_D1_FN)) != ZX_OK) ||
      ((status = gpio_impl_.SetAltFunction(T931_SDIO_D2, T931_SDIO_D2_FN)) != ZX_OK) ||
      ((status = gpio_impl_.SetAltFunction(T931_SDIO_D3, T931_SDIO_D3_FN)) != ZX_OK) ||
      ((status = gpio_impl_.SetAltFunction(T931_SDIO_CLK, T931_SDIO_CLK_FN)) != ZX_OK) ||
      ((status = gpio_impl_.SetAltFunction(T931_SDIO_CMD, T931_SDIO_CMD_FN)) != ZX_OK)) {
    return status;
  }

  std::optional<ddk::MmioBuffer> buf;
  zx::unowned_resource res(get_root_resource());
  status = ddk::MmioBuffer::Create(kGpioBase, kGpioBaseOffset + T931_GPIO_LENGTH, *res,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &buf);

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ddk::MmioBuffer::Create() error: %d\n", __func__, status);
    return status;
  }

  PadDsReg2A::Get()
      .ReadFrom(&(*buf))
      .set_gpiox_0_select(PadDsReg2A::kDriveStrength3mA)
      .set_gpiox_1_select(PadDsReg2A::kDriveStrength3mA)
      .set_gpiox_2_select(PadDsReg2A::kDriveStrength3mA)
      .set_gpiox_3_select(PadDsReg2A::kDriveStrength3mA)
      .set_gpiox_4_select(PadDsReg2A::kDriveStrength3mA)
      .set_gpiox_5_select(PadDsReg2A::kDriveStrength3mA)
      .WriteTo(&(*buf));

  status = gpio_impl_.SetAltFunction(T931_WIFI_REG_ON, T931_WIFI_REG_ON_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(T931_WIFI_HOST_WAKE, T931_WIFI_HOST_WAKE_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = pbus_.CompositeDeviceAdd(&sdio_dev, sdio_components, fbl::count_of(sdio_components),
                                    UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd() error: %d\n", __func__, status);
    return status;
  }

  // Add a composite device for wifi driver.
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_BROADCOM},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_BCM43458},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_BCM_WIFI},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .components = wifi_composite,
      .components_count = countof(wifi_composite),
      .coresident_device_index = 0,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("wifi", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add_composite failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
