// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
#define ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <acpica/acpi.h>
#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>
#include <fbl/vector.h>

class AcpiCrOsEc : public fbl::RefCounted<AcpiCrOsEc> {
 public:
  static zx_status_t Create(fbl::RefPtr<AcpiCrOsEc>* out);
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* out,
                           size_t outsize, void* in, size_t insize, size_t* actual);

  bool supports_motion_sense() const {
    return features_.flags[0] & EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE);
  }

  bool supports_motion_sense_fifo() const {
    return features_.flags[0] & EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE_FIFO);
  }

  ~AcpiCrOsEc();

 private:
  AcpiCrOsEc();
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEc);

  fbl::Mutex io_lock_;
  struct ec_response_get_features features_;
};

// TODO(teisenbe): Define motionsense interface

class AcpiCrOsEcMotionDevice;
using DeviceType = ddk::Device<AcpiCrOsEcMotionDevice>;

// Properties for a single MotionSense sensor.
struct SensorInfo {
  bool valid;

  enum motionsensor_type type;
  enum motionsensor_location loc;
  uint32_t min_sampling_freq;
  uint32_t max_sampling_freq;
  uint32_t fifo_max_event_count;

  // For MOTIONSENSE_TYPE_ACCEL, value is in Gs
  //     MOTIONSENSE_TYPE_GYRO, value is in deg/s
  //     MOTIONSENSE_TYPE_MAG, value is in multiples of 1/16 uT
  //     MOTIONSENSE_TYPE_LIGHT, value is in lux?
  int32_t phys_min;
  int32_t phys_max;
};

// CrOS EC protocol to HID protocol translator for device motion sensors
class AcpiCrOsEcMotionDevice
    : public DeviceType,
      public ddk::HidbusProtocol<AcpiCrOsEcMotionDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(fbl::RefPtr<AcpiCrOsEc> ec, zx_device_t* parent,
                            ACPI_HANDLE acpi_handle, std::unique_ptr<AcpiCrOsEcMotionDevice>* out);

  // hidbus protocol implementation
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  void DdkRelease();
  ~AcpiCrOsEcMotionDevice();

 private:
  AcpiCrOsEcMotionDevice(fbl::RefPtr<AcpiCrOsEc> ec, zx_device_t* parent, ACPI_HANDLE acpi_handle);
  DISALLOW_COPY_ASSIGN_AND_MOVE(AcpiCrOsEcMotionDevice);

  static void NotifyHandler(ACPI_HANDLE handle, UINT32 value, void* ctx);

  // Hardware commands
  zx_status_t QueryNumSensors(uint8_t* count);
  zx_status_t QuerySensorInfo(uint8_t sensor_num, SensorInfo* info);
  zx_status_t SetEcSamplingRate(uint8_t sensor_num, uint32_t milliseconds);
  zx_status_t SetSensorOutputDataRate(uint8_t sensor_num, uint32_t freq_millihertz);
  zx_status_t GetSensorRange(uint8_t sensor_num, int32_t* range);
  zx_status_t GetKbWakeAngle(int32_t* angle);
  zx_status_t SetKbWakeAngle(int16_t angle);
  zx_status_t FifoInterruptEnable(bool enable);
  zx_status_t FifoRead(struct ec_response_motion_sensor_data* data);

  // Guard against concurrent use of the HID interfaces
  fbl::Mutex hid_lock_;
  void QueueHidReportLocked(const uint8_t* data, size_t len);
  zx_status_t ConsumeFifoLocked();

  // Chat with hardware to build up |sensors_|
  zx_status_t ProbeSensors();

  fbl::RefPtr<AcpiCrOsEc> ec_;

  const ACPI_HANDLE acpi_handle_;

  // Interface the driver is currently bound to
  ddk::HidbusIfcProtocolClient client_;

  fbl::Vector<SensorInfo> sensors_;

  fbl::Array<uint8_t> hid_descriptor_ = fbl::Array<uint8_t>();
};

// Build a HID descriptor reporting information about the given set of sensors.
zx_status_t BuildHidDescriptor(fbl::Span<const SensorInfo> sensors, fbl::Array<uint8_t>* result);

#endif  // ZIRCON_SYSTEM_DEV_BOARD_X86_ACPI_DEV_DEV_CROS_EC_DEV_H_
