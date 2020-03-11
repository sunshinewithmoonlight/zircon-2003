// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abr-client.h"

#include <endian.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <string.h>

#include <string_view>

#include "device-partitioner.h"
#include "partition-client.h"
#include "pave-logging.h"
#include "zircon/errors.h"

namespace abr {

namespace {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;

zx_status_t QueryBootConfig(const zx::channel& svc_root, Configuration* out) {
  zx::channel local, remote;
  if (zx_status_t status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    return status;
  }
  auto status = fdio_service_connect_at(svc_root.get(), ::llcpp::fuchsia::boot::Arguments::Name,
                                        remote.release());
  if (status != ZX_OK) {
    return status;
  }
  ::llcpp::fuchsia::boot::Arguments::SyncClient client(std::move(local));
  auto result = client.GetString(::fidl::StringView{"zvb.current_slot"});
  if (!result.ok()) {
    return result.status();
  }

  const auto response = result.Unwrap();
  if (response->value.is_null()) {
    ERROR("Kernel cmdline param zvb.current_slot not found!\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto slot = std::string_view{response->value.data(), response->value.size()};
  // Some bootloaders prefix slot with dash or underscore. We strip them for consistency.
  slot.remove_prefix(std::min(slot.find_first_not_of("_-"), slot.size()));
  if (slot.compare("a") == 0) {
    *out = Configuration::A;
  } else if (slot.compare("b") == 0) {
    *out = Configuration::B;
  } else if (slot.compare("r") == 0) {
    *out = Configuration::RECOVERY;
  } else {
    ERROR("Invalid value `%.*s` found in zvb.current_slot!\n", static_cast<int>(slot.size()),
          slot.data());
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t SupportsVerifiedBoot(const zx::channel& svc_root) {
  Configuration config;
  if (zx_status_t status = QueryBootConfig(svc_root, &config); status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Implementation of abr::Client which works with a contiguous partition storing abr::Data.
class PartitionClient : public Client {
 public:
  // |partition| should contain abr::Data with no offset.
  static zx_status_t Create(std::unique_ptr<paver::PartitionClient> partition,
                            std::unique_ptr<abr::Client>* out);

  zx_status_t Persist(abr::Data data) override;

  const abr::Data& Data() const override { return data_; }

 private:
  PartitionClient(std::unique_ptr<paver::PartitionClient> partition, zx::vmo vmo, size_t block_size,
                  const abr::Data& data)
      : partition_(std::move(partition)),
        vmo_(std::move(vmo)),
        block_size_(block_size),
        data_(data) {}

  std::unique_ptr<paver::PartitionClient> partition_;
  zx::vmo vmo_;
  size_t block_size_;
  abr::Data data_;
};

zx_status_t PartitionClient::Create(std::unique_ptr<paver::PartitionClient> partition,
                                    std::unique_ptr<abr::Client>* out) {
  size_t block_size;
  if (zx_status_t status = partition->GetBlockSize(&block_size); status != ZX_OK) {
    return status;
  }

  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(fbl::round_up(block_size, ZX_PAGE_SIZE), 0, &vmo);
      status != ZX_OK) {
    return status;
  }

  if (zx_status_t status = partition->Read(vmo, block_size); status != ZX_OK) {
    return status;
  }

  abr::Data data;
  if (zx_status_t status = vmo.read(&data, 0, sizeof(data)); status != ZX_OK) {
    return status;
  }

  out->reset(new PartitionClient(std::move(partition), std::move(vmo), block_size, data));
  return ZX_OK;
}

zx_status_t PartitionClient::Persist(abr::Data data) {
  UpdateCrc(&data);
  if (memcmp(&data, &data_, sizeof(data)) == 0) {
    return ZX_OK;
  }
  if (zx_status_t status = vmo_.write(&data, 0, sizeof(data)); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = partition_->Write(vmo_, block_size_); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = partition_->Flush(); status != ZX_OK) {
    return status;
  }

  data_ = data;
  return ZX_OK;
}

}  // namespace

zx_status_t Client::Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                           std::unique_ptr<abr::Client>* out) {
  if (zx_status_t status = SupportsVerifiedBoot(svc_root); status != ZX_OK) {
    return status;
  }

  if (AstroClient::Create(devfs_root.duplicate(), out) == ZX_OK ||
      SherlockClient::Create(std::move(devfs_root), out) == ZX_OK) {
    return ZX_OK;
  }
  return ZX_ERR_NOT_FOUND;
}

bool Client::IsValid() const {
  return memcmp(Data().magic, kMagic, kMagicLen) == 0 && Data().version_major == kMajorVersion &&
         Data().version_minor == kMinorVersion && Data().slots[0].priority <= kMaxPriority &&
         Data().slots[1].priority <= kMaxPriority &&
         Data().slots[0].tries_remaining <= kMaxTriesRemaining &&
         Data().slots[1].tries_remaining <= kMaxTriesRemaining &&
         Data().crc32 == htobe32(crc32(0, reinterpret_cast<const uint8_t*>(&Data()),
                                       sizeof(abr::Data) - sizeof(uint32_t)));
}

void Client::UpdateCrc(abr::Data* data) {
  data->crc32 = htobe32(
      crc32(0, reinterpret_cast<const uint8_t*>(data), sizeof(abr::Data) - sizeof(uint32_t)));
}

zx_status_t AstroClient::Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  zx_status_t status = paver::AstroPartitioner::Initialize(std::move(devfs_root), &partitioner);
  if (status != ZX_OK) {
    return status;
  }

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  std::unique_ptr<paver::PartitionClient> partition;
  if (zx_status_t status =
          partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta), &partition);
      status != ZX_OK) {
    return status;
  }

  return PartitionClient::Create(std::move(partition), out);
}

zx_status_t SherlockClient::Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  zx_status_t status =
      paver::SherlockPartitioner::Initialize(std::move(devfs_root), std::nullopt, &partitioner);
  if (status != ZX_OK) {
    return status;
  }

  // ABR metadata has no need of a content type since it's always local rather
  // than provided in an update package, so just use the default content type.
  std::unique_ptr<paver::PartitionClient> partition;
  if (zx_status_t status =
          partitioner->FindPartition(paver::PartitionSpec(paver::Partition::kAbrMeta), &partition);
      status != ZX_OK) {
    return status;
  }

  return PartitionClient::Create(std::move(partition), out);
}

}  // namespace abr
