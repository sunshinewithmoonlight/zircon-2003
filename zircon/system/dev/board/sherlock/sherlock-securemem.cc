// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_bti_t sherlock_secure_mem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_AML_SECURE_MEM,
    },
};

static const pbus_dev_t secure_mem_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-secure-mem";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_SECURE_MEM;
  dev.bti_list = sherlock_secure_mem_btis;
  dev.bti_count = countof(sherlock_secure_mem_btis);
  return dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t tee_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEE),
};
constexpr device_component_part_t sysmem_component[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_component_part_t tee_component[] = {
    {countof(root_match), root_match},
    {countof(tee_match), tee_match},
};
constexpr device_component_t components[] = {
    {countof(sysmem_component), sysmem_component},
    {countof(tee_component), tee_component},
};

zx_status_t Sherlock::SecureMemInit() {
  zx_status_t status =
      pbus_.CompositeDeviceAdd(&secure_mem_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d\n", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
