// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_
#define ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_

#include <limits.h>

namespace vs680 {

// SDIO Registers
constexpr uint32_t kEmmc0Base = 0xf7aa'0000;
constexpr uint32_t kEmmc0Size = fbl::round_up<uint32_t, uint32_t>(0x1000, PAGE_SIZE);
constexpr uint32_t kEmmc0Irq = (13 + 32);

}  // namespace vs680

#endif  // ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_VS680_VS680_HW_H_
