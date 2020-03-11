// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ot-radio/ot-radio.h>
#include <limits.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

static const uint32_t device_id = kOtDeviceNrf52840;
static const pbus_metadata_t nrf52840_radio_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data_buffer = &device_id, .data_size = sizeof(device_id)},
};

// Composite binding rules for openthread radio driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static constexpr zx_bind_inst_t ot_dev_match[] = {
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NORDIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_NORDIC_NRF52840),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_NORDIC_THREAD),
};
static constexpr zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_INTERRUPT),
};
static constexpr zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_RESET),
};
static constexpr zx_bind_inst_t gpio_bootloader_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_OT_RADIO_BOOTLOADER),
};
static constexpr device_component_part_t ot_dev_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(ot_dev_match), ot_dev_match},
};
static constexpr device_component_part_t gpio_int_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(gpio_int_match), gpio_int_match},
};
static constexpr device_component_part_t gpio_reset_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(gpio_reset_match), gpio_reset_match},
};
static constexpr device_component_part_t gpio_bootloader_component[] = {
    {fbl::count_of(root_match), root_match},
    {fbl::count_of(gpio_bootloader_match), gpio_bootloader_match},
};
static constexpr device_component_t ot_components[] = {
    {fbl::count_of(ot_dev_component), ot_dev_component},
    {fbl::count_of(gpio_int_component), gpio_int_component},
    {fbl::count_of(gpio_reset_component), gpio_reset_component},
    {fbl::count_of(gpio_bootloader_component), gpio_bootloader_component},
};

zx_status_t Sherlock::OtRadioInit() {
  pbus_dev_t dev = {};
  dev.name = "nrf52840-radio";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_SHERLOCK;
  dev.did = PDEV_DID_OT_RADIO;
  dev.metadata_list = nrf52840_radio_metadata;
  dev.metadata_count = fbl::count_of(nrf52840_radio_metadata);

  zx_status_t status =
      pbus_.CompositeDeviceAdd(&dev, ot_components, fbl::count_of(ot_components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(nrf52840): DeviceAdd failed: %d\n", __func__, status);
  } else {
    zxlogf(INFO, "%s(nrf52840): DeviceAdded\n", __func__);
  }
  return status;
}

}  // namespace sherlock
