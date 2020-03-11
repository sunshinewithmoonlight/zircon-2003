// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/light-sensor.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::LightInit() {
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  constexpr zx_bind_inst_t gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LIGHT_INTERRUPT),
  };
  constexpr zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x39),
  };
  const device_component_part_t gpio_component[] = {
      {countof(root_match), root_match},
      {countof(gpio_match), gpio_match},
  };
  const device_component_part_t i2c_component[] = {
      {countof(root_match), root_match},
      {countof(i2c_match), i2c_match},
  };
  const device_component_t components[] = {
      {countof(i2c_component), i2c_component},
      {countof(gpio_component), gpio_component},
  };

  metadata::LightSensorParams params = {};
  // TODO(kpt): Insert the right parameters here.
  params.lux_constant_coefficient = 0;
  params.lux_linear_coefficient = .29f;
  params.integration_time_ms = 615;

  device_metadata_t metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &params,
          .length = sizeof(params),
      },
  };
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMS},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMS_TCS3400},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMS_LIGHT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .components = components,
      .components_count = countof(components),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = metadata,
      .metadata_count = countof(metadata),
  };

  zx_status_t status = DdkAddComposite("SherlockLightSensor", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d\n", __func__, status);
    return status;
  }

  // Lights
  // Instructions: include components in this order
  //     GPIO component
  //     BRIGHTNESS capable--include PWM component
  //     RGB capable--include RGB component
  //   Set GPIO alternative function here!
  using LightName = char[ZX_MAX_NAME_LEN];
  constexpr LightName kLightNames[] = {"AMBER_LED", "GREEN_LED"};
  constexpr LightsConfig kConfigs[] = {
      {.brightness = true, .rgb = false, .init_on = true, .group_id = -1},
      {.brightness = true, .rgb = false, .init_on = false, .group_id = -1},
  };
  static const pbus_metadata_t light_metadata[] = {
      {
          .type = DEVICE_METADATA_NAME,
          .data_buffer = &kLightNames,
          .data_size = sizeof(kLightNames),
      },
      {
          .type = DEVICE_METADATA_LIGHTS,
          .data_buffer = &kConfigs,
          .data_size = sizeof(kConfigs),
      },
  };

  constexpr zx_bind_inst_t amber_led_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AMBER_LED),
  };
  constexpr zx_bind_inst_t amber_led_pwm_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
      BI_MATCH_IF(EQ, BIND_PWM_ID, T931_PWM_AO_A),
  };
  constexpr zx_bind_inst_t green_led_gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_GREEN_LED),
  };
  constexpr zx_bind_inst_t green_led_pwm_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
      BI_MATCH_IF(EQ, BIND_PWM_ID, T931_PWM_F),
  };
  const device_component_part_t amber_led_gpio_component[] = {
      {countof(root_match), root_match},
      {countof(amber_led_gpio_match), amber_led_gpio_match},
  };
  const device_component_part_t amber_led_pwm_component[] = {
      {countof(root_match), root_match},
      {countof(amber_led_pwm_match), amber_led_pwm_match},
  };
  const device_component_part_t green_led_gpio_component[] = {
      {countof(root_match), root_match},
      {countof(green_led_gpio_match), green_led_gpio_match},
  };
  const device_component_part_t green_led_pwm_component[] = {
      {countof(root_match), root_match},
      {countof(green_led_pwm_match), green_led_pwm_match},
  };
  const device_component_t light_components[] = {
      {countof(amber_led_gpio_component), amber_led_gpio_component},
      {countof(amber_led_pwm_component), amber_led_pwm_component},
      {countof(green_led_gpio_component), green_led_gpio_component},
      {countof(green_led_pwm_component), green_led_pwm_component},
  };

  static const pbus_dev_t light_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "gpio-light";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_GPIO_LIGHT;
    dev.metadata_list = light_metadata;
    dev.metadata_count = countof(light_metadata);
    return dev;
  }();

  // Enable the Amber LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_AMBER_LED, 3);  // Set as GPIO.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d\n", __func__, status);
  }
  status = gpio_impl_.ConfigOut(GPIO_AMBER_LED, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d\n", __func__, status);
  }
  // Enable the Green LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_GREEN_LED, 4);  // Set as PWM.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d\n", __func__, status);
  }
  status = gpio_impl_.ConfigOut(GPIO_GREEN_LED, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d\n", __func__, status);
  }

  status =
      pbus_.CompositeDeviceAdd(&light_dev, light_components, countof(light_components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
