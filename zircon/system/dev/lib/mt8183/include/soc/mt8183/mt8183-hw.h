// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_MT8183_INCLUDE_SOC_MT8183_MT8183_HW_H_
#define ZIRCON_SYSTEM_DEV_LIB_MT8183_INCLUDE_SOC_MT8183_MT8183_HW_H_

#define MT8183_MCUCFG_BASE 0x0c530000
#define MT8183_MCUCFG_SIZE 0x2000

#define MT8183_GPIO_BASE 0x10005000
#define MT8183_GPIO_SIZE 0x1000

#define MT8183_EINT_BASE 0x1000b000
#define MT8183_EINT_SIZE 0x1000

#define MT8183_SPI0_BASE 0x1100a000
#define MT8183_SPI1_BASE 0x11010000
#define MT8183_SPI2_BASE 0x11012000
#define MT8183_SPI3_BASE 0x11013000
#define MT8183_SPI4_BASE 0x11018000
#define MT8183_SPI5_BASE 0x11019000
#define MT8183_SPI_SIZE 0x1000

#define MT8183_MSDC0_BASE 0x11230000
#define MT8183_MSDC0_SIZE 0x1000

// MCU config interrupt polarity registers start
#define MT8183_MCUCFG_INT_POL_CTL0 0xa80

// GIC interrupt numbers
#define MT8183_IRQ_MSDC0 109
#define MT8183_IRQ_EINT 209

// GPIOs
#define MT8183_GPIO_SPI2_CSB 0
#define MT8183_GPIO_SPI2_MO 1
#define MT8183_GPIO_SPI2_CLK 2
#define MT8183_GPIO_SPI1_B_MI 7
#define MT8183_GPIO_SPI1_B_CSB 8
#define MT8183_GPIO_SPI1_B_MO 9
#define MT8183_GPIO_SPI1_B_CLK 10
#define MT8183_GPIO_SPI5_MI 13
#define MT8183_GPIO_SPI5_CSB 14
#define MT8183_GPIO_SPI5_MO 15
#define MT8183_GPIO_SPI5_CLK 16
#define MT8183_GPIO_SPI4_MI 17
#define MT8183_GPIO_SPI4_CSB 18
#define MT8183_GPIO_SPI4_MO 19
#define MT8183_GPIO_SPI4_CLK 20
#define MT8183_GPIO_SPI3_MI 21
#define MT8183_GPIO_SPI3_CSB 22
#define MT8183_GPIO_SPI3_MO 23
#define MT8183_GPIO_SPI3_CLK 24
#define MT8183_GPIO_SPI0_MI 85
#define MT8183_GPIO_SPI0_CSB 86
#define MT8183_GPIO_SPI0_MO 87
#define MT8183_GPIO_SPI0_CLK 88
#define MT8183_GPIO_SPI2_MI 94
#define MT8183_GPIO_MSDC0_RST 133
#define MT8183_GPIO_SPI1_A_MI 161
#define MT8183_GPIO_SPI1_A_CSB 162
#define MT8183_GPIO_SPI1_A_MO 163
#define MT8183_GPIO_SPI1_A_CLK 164

#endif  // ZIRCON_SYSTEM_DEV_LIB_MT8183_INCLUDE_SOC_MT8183_MT8183_HW_H_
