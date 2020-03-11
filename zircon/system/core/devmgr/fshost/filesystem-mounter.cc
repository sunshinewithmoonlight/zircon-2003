// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <lib/zx/process.h>
#include <zircon/status.h>

#include <fs-management/mount.h>
#include <minfs/minfs.h>

#include "../shared/fdio.h"
#include "fshost-fs-provider.h"
#include "pkgfs-launcher.h"

namespace devmgr {

zx_status_t FilesystemMounter::LaunchFs(int argc, const char** argv, zx_handle_t* hnd,
                                        uint32_t* ids, size_t len) {
  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  return launcher.Launch(*zx::job::default_job(), argv[0], argv, nullptr, -1,
                         /* TODO(fxb/32044) */ zx::resource(), hnd, ids, len, nullptr,
                         FS_FOR_FSPROC);
}

zx_status_t FilesystemMounter::MountFilesystem(const char* mount_path, const char* binary,
                                               const mount_options_t& options,
                                               zx::channel block_device_client) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }

  constexpr size_t kNumHandles = 2;
  zx_handle_t handles[kNumHandles] = {
      server.release(),
      block_device_client.release(),
  };
  uint32_t ids[kNumHandles] = {
      FS_HANDLE_ROOT_ID,
      FS_HANDLE_BLOCK_DEVICE_ID,
  };
  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options.readonly) {
    argv.push_back("--readonly");
  }
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }
  if (options.collect_metrics) {
    argv.push_back("--metrics");
  }
  if (options.enable_journal) {
    argv.push_back("--journal");
  }
  if (options.enable_pager) {
    argv.push_back("--pager");
  }
  if (options.write_uncompressed) {
    argv.push_back("--write-uncompressed");
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  status = LaunchFs(static_cast<int>(argv.size() - 1), argv.data(), handles, ids, kNumHandles);

  if (status != ZX_OK) {
    return status;
  }

  zx_signals_t observed = 0;
  status =
      client.wait_one(ZX_USER_SIGNAL_0 | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed);
  if ((status != ZX_OK) || (observed & ZX_CHANNEL_PEER_CLOSED)) {
    status = (status != ZX_OK) ? status : ZX_ERR_BAD_STATE;
    return status;
  }

  return InstallFs(mount_path, std::move(client));
}

zx_status_t FilesystemMounter::MountData(zx::channel block_device, const mount_options_t& options) {
  if (data_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx_status_t status =
      MountFilesystem(PATH_DATA, "/boot/bin/minfs", options, std::move(block_device));
  if (status != ZX_OK) {
    return status;
  }

  data_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountInstall(zx::channel block_device,
                                            const mount_options_t& options) {
  if (install_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx_status_t status =
      MountFilesystem(PATH_INSTALL, "/boot/bin/minfs", options, std::move(block_device));
  if (status != ZX_OK) {
    return status;
  }

  install_mounted_ = true;
  return ZX_OK;
}

zx_status_t FilesystemMounter::MountBlob(zx::channel block_device, const mount_options_t& options) {
  if (blob_mounted_) {
    return ZX_ERR_ALREADY_BOUND;
  }

  zx_status_t status =
      MountFilesystem(PATH_BLOB, "/boot/bin/blobfs", options, std::move(block_device));
  if (status != ZX_OK) {
    return status;
  }

  blob_mounted_ = true;
  return ZX_OK;
}

void FilesystemMounter::TryMountPkgfs() {
  // Pkgfs waits for the following to mount before initializing:
  //   - Blobfs. Pkgfs is launched from blobfs, so this is a hard requirement.
  //   - Minfs. Pkgfs and other components want minfs to exist, so although they
  //   could launch and query for it later, this synchronization point means that
  //   subsequent clients will no longer need to query.
  //
  // TODO(fxb/38621): In the future, this mechanism may be replaced with a feed-forward
  // design to the mounted filesystems.
  if (!pkgfs_mounted_ && blob_mounted_ && (data_mounted_ || !WaitForData())) {
    LaunchPkgfs(this);
    pkgfs_mounted_ = true;
  }
}

}  // namespace devmgr
