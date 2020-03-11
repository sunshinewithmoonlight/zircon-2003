// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/llcpp/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-power.h>
#include <soc/as370/as370-thermal.h>

#include "as370.h"

namespace board_as370 {

using llcpp::fuchsia::hardware::thermal::OperatingPoint;
using llcpp::fuchsia::hardware::thermal::OperatingPointEntry;
using llcpp::fuchsia::hardware::thermal::PowerDomain;
using llcpp::fuchsia::hardware::thermal::ThermalDeviceInfo;

zx_status_t As370::ThermalInit() {
  constexpr pbus_mmio_t thermal_mmios[] = {
      {
          .base = as370::kThermalBase,
          .length = as370::kThermalSize,
      },
  };

  constexpr ThermalDeviceInfo kThermalDeviceInfo = {
      .active_cooling = false,
      .passive_cooling = true,
      .gpu_throttling = false,
      .num_trip_points = 0,
      .big_little = false,
      .critical_temp_celsius = 0.0f,
      .trip_point_info = {},
      .opps =
          fidl::Array<OperatingPoint, 2>{
              OperatingPoint{
                  .opp =
                      fidl::Array<OperatingPointEntry, 16>{
                          // clang-format off
                          OperatingPointEntry{.freq_hz =   400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz =   800'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'200'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'500'000'000, .volt_uv = 900'000},
                          OperatingPointEntry{.freq_hz = 1'800'000'000, .volt_uv = 900'000},
                          // clang-format on
                      },
                  .latency = 0,
                  .count = 6,
              },
              {
                  .opp = {},
                  .latency = 0,
                  .count = 0,
              },
          },
  };

  const pbus_metadata_t thermal_metadata[] = {
      {
          .type = DEVICE_METADATA_THERMAL_CONFIG,
          .data_buffer = &kThermalDeviceInfo,
          .data_size = sizeof(kThermalDeviceInfo),
      },
  };

  static constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  static constexpr zx_bind_inst_t cpu_clock_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
      BI_MATCH_IF(EQ, BIND_CLOCK_ID, as370::kClkCpu),
  };
  static const device_component_part_t cpu_clock_component[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(cpu_clock_match), cpu_clock_match},
  };

  static constexpr zx_bind_inst_t cpu_power_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
      BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, kBuckSoC),
  };
  static const device_component_part_t cpu_power_component[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(cpu_power_match), cpu_power_match},
  };

  static const device_component_t components[] = {
      {fbl::count_of(cpu_clock_component), cpu_clock_component},
      {fbl::count_of(cpu_power_component), cpu_power_component},
  };

  pbus_dev_t thermal_dev = {};
  thermal_dev.name = "thermal";
  thermal_dev.vid = PDEV_VID_SYNAPTICS;
  thermal_dev.did = PDEV_DID_AS370_THERMAL;
  thermal_dev.mmio_list = thermal_mmios;
  thermal_dev.mmio_count = countof(thermal_mmios);
  thermal_dev.metadata_list = thermal_metadata;
  thermal_dev.metadata_count = countof(thermal_metadata);

  zx_status_t status =
      pbus_.CompositeDeviceAdd(&thermal_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
