// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_LUIS_LUIS_H_
#define ZIRCON_SYSTEM_DEV_BOARD_LUIS_LUIS_H_

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_luis {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_EMMC,
};

class Luis : public ddk::Device<Luis> {
 public:
  Luis(zx_device_t* parent, const ddk::PBusProtocolClient& pbus,
        const pdev_board_info_t& board_info)
      : ddk::Device<Luis>(parent), pbus_(pbus), board_info_(board_info) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

 private:
  zx_status_t Start();
  int Thread();

  zx_status_t EmmcInit();
  zx_status_t GpioInit();

  const ddk::PBusProtocolClient pbus_;
  const pdev_board_info_t board_info_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_luis

#endif  // ZIRCON_SYSTEM_DEV_BOARD_LUIS_LUIS_H_
