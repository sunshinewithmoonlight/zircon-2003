// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_SHERLOCK_SHERLOCK_GPIOS_H_
#define ZIRCON_SYSTEM_DEV_BOARD_SHERLOCK_SHERLOCK_GPIOS_H_

#include <soc/aml-t931/t931-gpio.h>

#define GPIO_BACKLIGHT_ENABLE T931_GPIOA(10)
#define GPIO_LCD_RESET T931_GPIOH(6)
#define GPIO_PANEL_DETECT T931_GPIOH(0)
#define GPIO_TOUCH_INTERRUPT T931_GPIOZ(1)
#define GPIO_TOUCH_RESET T931_GPIOZ(9)
#define GPIO_AUDIO_SOC_FAULT_L T931_GPIOZ(8)
#define GPIO_SOC_AUDIO_EN T931_GPIOH(7)
#define GPIO_VANA_ENABLE T931_GPIOA(6)
#define GPIO_VDIG_ENABLE T931_GPIOZ(12)
#define GPIO_CAM_RESET T931_GPIOZ(0)
#define GPIO_LIGHT_INTERRUPT T931_GPIOAO(5)
#define GPIO_SPICC0_SS0 T931_GPIOC(2)
#define GPIO_VOLUME_UP T931_GPIOZ(4)
#define GPIO_VOLUME_DOWN T931_GPIOZ(5)
#define GPIO_VOLUME_BOTH T931_GPIOZ(13)
#define GPIO_MIC_PRIVACY T931_GPIOH(3)
#define GPIO_OT_RADIO_RESET T931_GPIOA(2)
#define GPIO_OT_RADIO_INTERRUPT T931_GPIOA(4)
#define GPIO_OT_RADIO_BOOTLOADER T931_GPIOA(5)
#define GPIO_AMBER_LED T931_GPIOAO(11)
#define GPIO_GREEN_LED T931_GPIOH(5)
#define GPIO_SOC_WIFI_LPO_32k768 T931_GPIOX(16)
#define GPIO_SOC_BT_REG_ON T931_GPIOX(17)

#endif  // ZIRCON_SYSTEM_DEV_BOARD_SHERLOCK_SHERLOCK_GPIOS_H_