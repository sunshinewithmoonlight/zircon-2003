// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pwm.h>

#define DRIVER_NAME "test-pwm"

namespace pwm {

namespace {

struct mode_config_magic {
  uint32_t magic;
};

struct mode_config {
  uint32_t mode;
  union {
    struct mode_config_magic magic;
  };
};

}  // namespace

class TestPwmDevice;
using DeviceType = ddk::Device<TestPwmDevice, ddk::UnbindableDeprecated>;

class TestPwmDevice : public DeviceType,
                      public ddk::PwmImplProtocol<TestPwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestPwmDevice(zx_device_t* parent) : DeviceType(parent) {}

  // Methods required by the ddk mixins
  void DdkUnbindDeprecated();
  void DdkRelease();

  zx_status_t PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config);
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config);
  zx_status_t PwmImplEnable(uint32_t idx);
  zx_status_t PwmImplDisable(uint32_t idx);
};

zx_status_t TestPwmDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestPwmDevice>(parent);
  zx_status_t status;

  zxlogf(INFO, "TestPwmDevice::Create: %s \n", DRIVER_NAME);

  status = dev->DdkAdd("test-pwm");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d\n", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto unused = dev.release();

  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config) {
  if (idx || !out_config || (out_config->mode_config_size != sizeof(mode_config))) {
    return ZX_ERR_INVALID_ARGS;
  }

  out_config->polarity = false;
  out_config->period_ns = 1000;
  out_config->duty_cycle = 39.0;
  auto mode_cfg = static_cast<mode_config*>(out_config->mode_config_buffer);
  mode_cfg->mode = 0;
  mode_cfg->magic.magic = 12345;

  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (config->polarity || (config->period_ns != 1000) || (config->duty_cycle != 39.0) ||
      (config->mode_config_size != sizeof(mode_config)) ||
      (static_cast<const mode_config*>(config->mode_config_buffer)->mode != 0) ||
      (static_cast<const mode_config*>(config->mode_config_buffer)->magic.magic != 12345)) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplEnable(uint32_t idx) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplDisable(uint32_t idx) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void TestPwmDevice::DdkUnbindDeprecated() {}

void TestPwmDevice::DdkRelease() { delete this; }

zx_status_t test_pwm_bind(void* ctx, zx_device_t* parent) { return TestPwmDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_pwm_bind;
  return driver_ops;
}();

}  // namespace pwm

// clang-format off
ZIRCON_DRIVER_BEGIN(test_pwm, pwm::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_PWM),
ZIRCON_DRIVER_END(test_pwm)
    // clang-format on
