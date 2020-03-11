// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <soc/aml-s905d3/s905d3-gpio.h>

namespace nelson {

#define GPIO_BACKLIGHT_ENABLE S905D3_GPIOA(10)
#define GPIO_LCD_RESET S905D3_GPIOZ(13)
#define GPIO_PANEL_DETECT S905D3_GPIOZ(11)
#define GPIO_TOUCH_INTERRUPT S905D3_GPIOZ(4)
#define GPIO_TOUCH_RESET S905D3_GPIOZ(9)
#define GPIO_LIGHT_INTERRUPT S905D3_GPIOAO(5)
#define GPIO_AUDIO_SOC_FAULT_L S905D3_GPIOA(12)
#define GPIO_SOC_AUDIO_EN S905D3_GPIOA(5)
#define GPIO_VOLUME_UP S905D3_GPIOZ(5)
#define GPIO_VOLUME_DOWN S905D3_GPIOZ(6)
#define GPIO_VOLUME_BOTH S905D3_GPIOAO(10)
#define GPIO_MIC_PRIVACY S905D3_GPIOZ(2)
#define GPIO_EMMC_RESET S905D3_GPIOBOOT(12)
#define GPIO_WIFI_REG_ON S905D3_GPIOX(6)
#define GPIO_SPICC1_SS0 S905D3_GPIOH(6)
#define GPIO_SOC_WIFI_LPO_32k768 S905D3_GPIOX(16)
#define GPIO_SOC_BT_REG_ON S905D3_GPIOX(17)

}  // namespace nelson
