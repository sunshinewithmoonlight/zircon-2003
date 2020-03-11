// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_NELSON_NELSON_H_
#define ZIRCON_SYSTEM_DEV_BOARD_NELSON_NELSON_H_

#include <threads.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/iommu.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/macros.h>
#include <soc/aml-s905d2/s905d2-gpio.h>

namespace nelson {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB,
  BTI_DISPLAY,
  BTI_EMMC,
  BTI_MALI,
  BTI_VIDEO,
  BTI_SDIO,
  BTI_CANVAS,
  BTI_AUDIO_IN,
  BTI_AUDIO_OUT,
  BTI_TEE,
  BTI_SYSMEM,
  BTI_AML_SECURE_MEM,
};

// MAC address metadata indices
enum {
  MACADDR_WIFI = 0,
  MACADDR_BLUETOOTH = 1,
};

typedef struct {
  zx_device_t* parent;
  pbus_protocol_t pbus;
  gpio_impl_protocol_t gpio;
  iommu_protocol_t iommu;
} aml_bus_t;

// These should match the mmio table defined in nelson-i2c.cc
enum {
  NELSON_I2C_A0_0,
  NELSON_I2C_2,
  NELSON_I2C_3,
};

// Nelson SPI bus arbiters (should match spi_channels[] in nelson-spi.cc).
enum {
  NELSON_SPICC0,
  NELSON_SPICC1,
};

// Nelson Board Revs
enum {
  BOARD_REV_P1 = 0,
  BOARD_REV_P2 = 1,
  BOARD_REV_EVT_1 = 2,
  BOARD_REV_EVT_2 = 3,
  BOARD_REV_DVT = 4,
  BOARD_REV_PVT = 5,

  MAX_SUPPORTED_REV,  // This must be last entry
};

// Nelson GPIO Pins used for board rev detection
constexpr uint32_t GPIO_HW_ID0 = (S905D2_GPIOZ(7));
constexpr uint32_t GPIO_HW_ID1 = (S905D2_GPIOZ(8));
constexpr uint32_t GPIO_HW_ID2 = (S905D2_GPIOZ(3));

/* Nelson I2C Devices */
constexpr uint8_t I2C_BACKLIGHT_ADDR = (0x2C);
constexpr uint8_t I2C_FOCALTECH_TOUCH_ADDR = (0x38);
constexpr uint8_t I2C_AMBIENTLIGHT_ADDR = (0x39);
constexpr uint8_t I2C_AUDIO_CODEC_ADDR = (0x31);     // For Nelson P1.
constexpr uint8_t I2C_AUDIO_CODEC_ADDR_P2 = (0x2D);  // For Nelson P2.
constexpr uint8_t I2C_GOODIX_TOUCH_ADDR = (0x5d);

class Nelson;
using NelsonType = ddk::Device<Nelson>;

// This is the main class for the Nelson platform bus driver.
class Nelson : public NelsonType {
 public:
  explicit Nelson(zx_device_t* parent, pbus_protocol_t* pbus, iommu_protocol_t* iommu)
      : NelsonType(parent), pbus_(pbus), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Nelson);

  zx_status_t AudioInit();
  zx_status_t BluetoothInit();
  zx_status_t ButtonsInit();
  zx_status_t CanvasInit();
  zx_status_t ClkInit();
  zx_status_t DisplayInit();
  zx_status_t EmmcInit();
  zx_status_t GpioInit();
  zx_status_t I2cInit();
  zx_status_t LightInit();
  zx_status_t MaliInit();
  zx_status_t PwmInit();
  zx_status_t SdioInit();
  zx_status_t Start();
  zx_status_t SecureMemInit();
  zx_status_t SpiInit();
  zx_status_t SysmemInit();
  zx_status_t TeeInit();
  zx_status_t ThermalInit();
  zx_status_t TouchInit();
  zx_status_t UsbInit();
  zx_status_t VideoInit();
  zx_status_t BacklightInit();
  zx_status_t CpuInit();
  int Thread();

  uint32_t GetBoardRev(void);
  zx_status_t EnableWifi32K(void);
  zx_status_t SdEmmcConfigurePortB(void);

  ddk::PBusProtocolClient pbus_;
  ddk::IommuProtocolClient iommu_;
  ddk::GpioImplProtocolClient gpio_impl_;
  ddk::ClockImplProtocolClient clk_impl_;

  thrd_t thread_;
};

}  // namespace nelson

#endif  // ZIRCON_SYSTEM_DEV_BOARD_NELSON_NELSON_H_
