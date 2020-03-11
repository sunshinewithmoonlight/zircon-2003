// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "test.h"

namespace {

// Composite binding rules for power domain 1
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t power_impl_driver_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
};
constexpr device_component_part_t power_impl_component[] = {
    {countof(root_match), root_match},
    {countof(power_impl_driver_match), power_impl_driver_match},
};
zx_device_prop_t props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

constexpr device_component_t power_domain_1_components[] = {
    {countof(power_impl_component), power_impl_component},
};
static const power_domain_t power_domain_1[] = {
    {1},
};

static const device_metadata_t power_metadata_1[] = {{
    .type = DEVICE_METADATA_POWER_DOMAINS,
    .data = &power_domain_1,
    .length = sizeof(power_domain_1),
}};
const composite_device_desc_t power_domain_1_desc = {
    .props = props,
    .props_count = countof(props),
    .components = power_domain_1_components,
    .components_count = countof(power_domain_1_components),
    .coresident_device_index = 0,
    .metadata_list = power_metadata_1,
    .metadata_count = countof(power_metadata_1),
};

// Composite binding rules for power domain 3
static const zx_bind_inst_t parent_domain_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
    BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, 1),
};
constexpr device_component_part_t parent_domain_component[] = {
    {countof(root_match), root_match},
    {countof(parent_domain_match), parent_domain_match},
};

constexpr device_component_t power_domain_3_components[] = {
    {countof(power_impl_component), power_impl_component},
    {countof(parent_domain_component), parent_domain_component},
};
static const power_domain_t power_domain_3[] = {
    {3},
};

static const device_metadata_t power_metadata_3[] = {{
    .type = DEVICE_METADATA_POWER_DOMAINS,
    .data = &power_domain_3,
    .length = sizeof(power_domain_3),
}};
const composite_device_desc_t power_domain_3_desc = {
    .props = props,
    .props_count = countof(props),
    .components = power_domain_3_components,
    .components_count = countof(power_domain_3_components),
    .coresident_device_index = 0,
    .metadata_list = power_metadata_3,
    .metadata_count = countof(power_metadata_3),
};

}  // namespace

namespace board_test {

zx_status_t TestBoard::PowerInit() {
  pbus_dev_t power_dev = {};
  power_dev.name = "power";
  power_dev.vid = PDEV_VID_TEST;
  power_dev.pid = PDEV_PID_PBUS_TEST;
  power_dev.did = PDEV_DID_TEST_POWER;

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_POWER_IMPL, &power_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }
  status = DdkAddComposite("composite-pd-1", &power_domain_1_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite for power domain 1 failed: %d \n", __FUNCTION__, status);
    return status;
  }

  status = DdkAddComposite("composite-pd-3", &power_domain_3_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite for power domain 3 failed: %d\n", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
