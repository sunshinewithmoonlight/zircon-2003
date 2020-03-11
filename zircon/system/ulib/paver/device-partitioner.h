// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_PAVER_DEVICE_PARTITIONER_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_DEVICE_PARTITIONER_H_

#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>
#include <stdbool.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/function.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <gpt/gpt.h>

#include "abr.h"
#include "partition-client.h"

namespace paver {
using gpt::GptDevice;
using ::llcpp::fuchsia::paver::Configuration;

enum class Partition {
  kUnknown,
  kBootloader,
  kZirconA,
  kZirconB,
  kZirconR,
  kVbMetaA,
  kVbMetaB,
  kVbMetaR,
  kAbrMeta,
  kFuchsiaVolumeManager,
};

const char* PartitionName(Partition type);

enum class Arch {
  kX64,
  kArm64,
};

// Operations on a specific partition take two identifiers, a partition type
// and a content type.
//
// The first is the conceptual partition type. This may not necessarily map 1:1
// with on-disk partitions. For example, some devices have multiple bootloader
// stages which live in different partitions on-disk but which would both have
// type kBootloader.
//
// The second is a device-specific string identifying the contents the caller
// wants to read/write. The backend uses this to decide which on-disk partitions
// to use and where the content lives in them. The default content type is
// null or empty.
struct PartitionSpec {
 public:
  // Creates a spec with the given partition and default (null) content type.
  explicit PartitionSpec(Partition partition) : PartitionSpec(partition, std::string_view()) {}

  // Creates a spec with the given partition and content type.
  PartitionSpec(Partition partition, std::string_view content_type)
      : partition(partition), content_type(content_type) {}

  // Returns a descriptive string for logging.
  fbl::String ToString() const;

  Partition partition;
  std::string_view content_type;
};

// Abstract device partitioner definition.
// This class defines common APIs for interacting with a device partitioner.
class DevicePartitioner {
 public:
  // Factory method which automatically returns the correct DevicePartitioner
  // implementation. Returns nullptr on failure.
  // |block_device| is root block device whichs contains the logical partitions we wish to operate
  // against. It's only meaningful for EFI and CROS devices which may have multiple storage devices.
  static std::unique_ptr<DevicePartitioner> Create(fbl::unique_fd devfs_root, zx::channel svc_root,
                                                   Arch arch,
                                                   zx::channel block_device = zx::channel());

  virtual ~DevicePartitioner() = default;

  // Whether or not the Fuchsia Volume Manager exists within an FTL.
  virtual bool IsFvmWithinFtl() const = 0;

  // Checks if the device supports the given partition spec.
  //
  // This is the only function that will definitively say whether a spec is
  // supported or not. Other partition functions may return ZX_ERR_NOT_SUPPORTED
  // on unsupported spec, but they may also return it for other reasons such as
  // a lower-level driver error. They also may return other errors even if given
  // an unsupported spec if something else goes wong.
  virtual bool SupportsPartition(const PartitionSpec& spec) const = 0;

  // Returns a PartitionClient matching |spec|, creating the partition.
  // Assumes that the partition does not already exist.
  virtual zx_status_t AddPartition(const PartitionSpec& spec,
                                   std::unique_ptr<PartitionClient>* out_partition) const = 0;

  // Returns a PartitionClient matching |spec| if one exists.
  virtual zx_status_t FindPartition(const PartitionSpec& spec,
                                    std::unique_ptr<PartitionClient>* out_partition) const = 0;

  // Finalizes the PartitionClient matching |spec| after it has been written.
  virtual zx_status_t FinalizePartition(const PartitionSpec& spec) const = 0;

  // Wipes Fuchsia Volume Manager partition.
  virtual zx_status_t WipeFvm() const = 0;

  // Initializes partition tables.
  virtual zx_status_t InitPartitionTables() const = 0;

  // Wipes partition tables.
  virtual zx_status_t WipePartitionTables() const = 0;

  // Determine if the given data file is a valid image for this device.
  //
  // This analysis is best-effort only, providing only basic safety checks.
  virtual zx_status_t ValidatePayload(const PartitionSpec& spec,
                                      fbl::Span<const uint8_t> data) const = 0;
};

// Useful for when a GPT table is available (e.g. x86 devices). Provides common
// utility functions.
class GptDevicePartitioner {
 public:
  using FilterCallback = fbl::Function<bool(const gpt_partition_t&)>;

  // Find and initialize a GPT based device.
  //
  // If block_device is provided, then search is skipped, and block_device is used
  // directly. If it is not provided, we search for a device with a valid GPT,
  // with an entry for an FVM. If multiple devices with valid GPT containing
  // FVM entries are found, an error is returned.
  static zx_status_t InitializeGpt(fbl::unique_fd devfs_root,
                                   std::optional<fbl::unique_fd> block_device,
                                   std::unique_ptr<GptDevicePartitioner>* gpt_out);

  // Returns block info for a specified block device.
  zx_status_t GetBlockInfo(::llcpp::fuchsia::hardware::block::BlockInfo* block_info) const {
    memcpy(block_info, &block_info_, sizeof(*block_info));
    return ZX_OK;
  }

  GptDevice* GetGpt() const { return gpt_.get(); }
  zx::unowned_channel Channel() const { return caller_.channel(); }

  // Find the first spot that has at least |bytes_requested| of space.
  //
  // Returns the |start_out| block and |length_out| blocks, indicating
  // how much space was found, on success. This may be larger than
  // the number of bytes requested.
  zx_status_t FindFirstFit(size_t bytes_requested, size_t* start_out, size_t* length_out) const;

  // Creates a partition, adds an entry to the GPT, and returns a file descriptor to it.
  // Assumes that the partition does not already exist.
  zx_status_t AddPartition(const char* name, const uint8_t* type, size_t minimum_size_bytes,
                           size_t optional_reserve_bytes,
                           std::unique_ptr<PartitionClient>* out_partition) const;

  // Returns a file descriptor to a partition which can be paved,
  // if one exists.
  zx_status_t FindPartition(FilterCallback filter, std::unique_ptr<PartitionClient>* out_partition,
                            gpt_partition_t** out = nullptr) const;

  // Wipes a specified partition from the GPT, and overwrites first 8KiB with
  // nonsense.
  zx_status_t WipeFvm() const;

  // Removes all partitions from GPT.
  zx_status_t WipePartitionTables() const;

  // Wipes all partitions meeting given criteria.
  zx_status_t WipePartitions(FilterCallback filter) const;

  const fbl::unique_fd& devfs_root() { return devfs_root_; }

 private:
  using GptDevices = std::vector<std::pair<std::string, fbl::unique_fd>>;

  // Find all block devices which could contain a GPT.
  static bool FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out);

  // Initializes GPT for a device which was explicitly provided. If |gpt_device| doesn't have a
  // valid GPT, it will initialize it with a valid one.
  static zx_status_t InitializeProvidedGptDevice(fbl::unique_fd devfs_root,
                                                 fbl::unique_fd gpt_device,
                                                 std::unique_ptr<GptDevicePartitioner>* gpt_out);

  GptDevicePartitioner(fbl::unique_fd devfs_root, fbl::unique_fd fd, std::unique_ptr<GptDevice> gpt,
                       ::llcpp::fuchsia::hardware::block::BlockInfo block_info)
      : devfs_root_(std::move(devfs_root)),
        caller_(std::move(fd)),
        gpt_(std::move(gpt)),
        block_info_(block_info) {}

  zx_status_t CreateGptPartition(const char* name, const uint8_t* type, uint64_t offset,
                                 uint64_t blocks, uint8_t* out_guid) const;

  fbl::unique_fd devfs_root_;
  fdio_cpp::FdioCaller caller_;
  mutable std::unique_ptr<GptDevice> gpt_;
  ::llcpp::fuchsia::hardware::block::BlockInfo block_info_;
};

// DevicePartitioner implementation for EFI based devices.
class EfiDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root, Arch arch,
                                std::optional<fbl::unique_fd> block_device,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override;

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  EfiDevicePartitioner(Arch arch, std::unique_ptr<GptDevicePartitioner> gpt)
      : gpt_(std::move(gpt)), arch_(arch) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
  Arch arch_;
};

// DevicePartitioner implementation for ChromeOS devices.
class CrosDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root, Arch arch,
                                std::optional<fbl::unique_fd> block_device,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override;

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  CrosDevicePartitioner(std::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
};

// DevicePartitioner implementation for devices which have fixed partition maps (e.g. ARM
// devices). It will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class FixedDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  FixedDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  fbl::unique_fd devfs_root_;
};

// DevicePartitioner implementation for devices which have fixed partition maps, but do not expose a
// block device interface. Instead they expose devices with skip-block IOCTL interfaces. Like the
// FixedDevicePartitioner, it will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class SkipBlockDevicePartitioner {
 public:
  SkipBlockDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  zx_status_t FindPartition(const uint8_t* guid,
                            std::unique_ptr<PartitionClient>* out_partition) const;

  zx_status_t FindFvmPartition(std::unique_ptr<PartitionClient>* out_partition) const;

  zx_status_t WipeFvm() const;

  fbl::unique_fd& devfs_root() { return devfs_root_; }

 private:
  fbl::unique_fd devfs_root_;
};

class AstroPartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return true; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  AstroPartitioner(std::unique_ptr<SkipBlockDevicePartitioner> skip_block)
      : skip_block_(std::move(skip_block)) {}

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block_;
};

class As370Partitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return true; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  As370Partitioner(std::unique_ptr<SkipBlockDevicePartitioner> skip_block)
      : skip_block_(std::move(skip_block)) {}

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block_;
};

class SherlockPartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                std::optional<fbl::unique_fd> block_device,
                                std::unique_ptr<DevicePartitioner>* partitioner);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx_status_t AddPartition(const PartitionSpec& spec,
                           std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FindPartition(const PartitionSpec& spec,
                            std::unique_ptr<PartitionClient>* out_partition) const override;

  zx_status_t FinalizePartition(const PartitionSpec& spec) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t InitPartitionTables() const override;

  zx_status_t WipePartitionTables() const override;

  zx_status_t ValidatePayload(const PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override;

 private:
  SherlockPartitioner(std::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
};

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_DEVICE_PARTITIONER_H_
