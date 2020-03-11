// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/ldsvc/c/fidl.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/watcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/hermetic-decompressor/hermetic-decompressor.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <zircon/boot/image.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <memory>

#include <cobalt-client/cpp/collector.h>
#include <fbl/unique_fd.h>
#include <fs/remote_dir.h>
#include <fs/service.h>
#include <loader-service/loader-service.h>
#include <ramdevice-client/ramdisk.h>

#include "block-watcher.h"
#include "fs-manager.h"
#include "metrics.h"

namespace devmgr {
namespace {

// local_storage project ID as defined in cobalt-analytics projects.yaml.
constexpr uint32_t kCobaltProjectId = 3676913920;

FsHostMetrics MakeMetrics() {
  return FsHostMetrics(std::make_unique<cobalt_client::Collector>(kCobaltProjectId));
}

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

// Get ramdisk from the boot items service.
zx_status_t get_ramdisk(zx::vmo* ramdisk_vmo) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect(kItemsPath, remote.release());
  if (status != ZX_OK) {
    return status;
  }
  uint32_t length;
  return fuchsia_boot_ItemsGet(local.get(), ZBI_TYPE_STORAGE_RAMDISK, 0,
                               ramdisk_vmo->reset_and_get_address(), &length);
}

zx_status_t MiscDeviceAdded(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE || strcmp(fn, "ramctl") != 0) {
    return ZX_OK;
  }

  zx::vmo ramdisk_vmo = std::move(*static_cast<zx::vmo*>(cookie));

  zbi_header_t header;
  zx_status_t status = ramdisk_vmo.read(&header, 0, sizeof(header));
  if (status != ZX_OK) {
    printf("fshost: cannot read ZBI_TYPE_STORAGE_RAMDISK item header: %s\n",
           zx_status_get_string(status));
    return ZX_ERR_STOP;
  }
  if (!(header.flags & ZBI_FLAG_VERSION) || header.magic != ZBI_ITEM_MAGIC ||
      header.type != ZBI_TYPE_STORAGE_RAMDISK) {
    printf("fshost: invalid ZBI_TYPE_STORAGE_RAMDISK item header\n");
    return ZX_ERR_STOP;
  }

  zx::vmo vmo;
  if (header.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
    status = zx::vmo::create(header.extra, 0, &vmo);
    if (status != ZX_OK) {
      printf("fshost: cannot create VMO for uncompressed RAMDISK: %s\n",
             zx_status_get_string(status));
      return ZX_ERR_STOP;
    }
    HermeticDecompressor decompressor;
    status = decompressor(ramdisk_vmo, sizeof(zbi_header_t), header.length, vmo, 0, header.extra);
    if (status != ZX_OK) {
      printf("fshost: failed to decompress RAMDISK: %s\n", zx_status_get_string(status));
      return ZX_ERR_STOP;
    }
  } else {
    // TODO(ZX-4824): The old code ignored uncompressed items too, and
    // silently.  Really the protocol should be cleaned up so the VMO arrives
    // without the header in it and then it could just be used here directly
    // if uncompressed (or maybe bootsvc deals with decompression in the first
    // place so the uncompressed VMO is always what we get).
    printf("fshost: ignoring uncompressed RAMDISK item in ZBI\n");
    return ZX_ERR_STOP;
  }

  ramdisk_client* client;
  status = ramdisk_create_from_vmo(vmo.release(), &client);
  if (status != ZX_OK) {
    printf("fshost: failed to create ramdisk from ZBI_TYPE_STORAGE_RAMDISK\n");
  } else {
    printf("fshost: ZBI_TYPE_STORAGE_RAMDISK attached\n");
  }
  return ZX_ERR_STOP;
}

int RamctlWatcher(void* arg) {
  fbl::unique_fd dirfd(open("/dev/misc", O_DIRECTORY | O_RDONLY));
  if (!dirfd) {
    printf("fshost: failed to open /dev/misc: %s\n", strerror(errno));
    return -1;
  }
  fdio_watch_directory(dirfd.get(), &MiscDeviceAdded, ZX_TIME_INFINITE, arg);
  return 0;
}

// Setup the loader service to be used by all processes spawned by devmgr.
// TODO(fxb/34633): this loader service is deprecated and should be deleted.
loader_service_t* setup_loader_service() {
  loader_service_t* svc;
  zx_status_t status = loader_service_create_fs(nullptr, &svc);
  if (status != ZX_OK) {
    fprintf(stderr, "fshost: failed to create loader service %d\n", status);
  }
  auto defer = fit::defer([svc] { loader_service_release(svc); });
  zx_handle_t fshost_loader;
  status = loader_service_connect(svc, &fshost_loader);
  if (status != ZX_OK) {
    fprintf(stderr, "fshost: failed to connect to loader service: %d\n", status);
    return nullptr;
  }
  zx_handle_close(dl_set_loader_service(fshost_loader));
  return svc;
}

// Initialize the fshost namespace.
//
// |fs_root_client| is mapped to "/fs", and represents the filesystem of devmgr.
zx_status_t BindNamespace(zx::channel fs_root_client) {
  fdio_ns_t* ns;
  zx_status_t status;
  if ((status = fdio_ns_get_installed(&ns)) != ZX_OK) {
    printf("fshost: cannot get namespace: %d\n", status);
    return status;
  }

  // Bind "/fs".
  if ((status = fdio_ns_bind(ns, "/fs", fs_root_client.release())) != ZX_OK) {
    printf("fshost: cannot bind /fs to namespace: %d\n", status);
    return status;
  }

  // Bind "/system".
  {
    zx::channel client, server;
    if ((status = zx::channel::create(0, &client, &server)) != ZX_OK) {
      return status;
    }
    if ((status = fdio_open("/fs/system",
                            ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE | ZX_FS_RIGHT_ADMIN,
                            server.release())) != ZX_OK) {
      printf("fshost: cannot open connection to /system: %d\n", status);
      return status;
    }
    if ((status = fdio_ns_bind(ns, "/system", client.release())) != ZX_OK) {
      printf("fshost: cannot bind /system to namespace: %d\n", status);
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace
}  // namespace devmgr

int main(int argc, char** argv) {
  bool disable_block_watcher = false;

  enum {
    kDisableBlockWatcher,
  };
  option options[] = {
      {"disable-block-watcher", no_argument, nullptr, kDisableBlockWatcher},
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "", options, nullptr)) != -1) {
    switch (opt) {
      case kDisableBlockWatcher:
        printf("fshost: received --disable-block-watcher\n");
        disable_block_watcher = true;
        break;
    }
  }

  // Setup the devmgr loader service.
  loader_service_t* loader_svc = devmgr::setup_loader_service();
  if (loader_svc == nullptr) {
    return ZX_ERR_INTERNAL;
  }

  // Initialize the local filesystem in isolation.
  zx::channel dir_request(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  std::unique_ptr<devmgr::FsManager> fs_manager;
  zx_status_t status = devmgr::FsManager::Create(loader_svc, std::move(dir_request),
                                                 devmgr::MakeMetrics(), &fs_manager);
  if (status != ZX_OK) {
    printf("fshost: Cannot create FsManager: %s\n", zx_status_get_string(status));
    return status;
  }

  // Serve the root filesystems in our own namespace.
  zx::channel fs_root_client, fs_root_server;
  status = zx::channel::create(0, &fs_root_client, &fs_root_server);
  if (status != ZX_OK) {
    return ZX_OK;
  }
  status = fs_manager->ServeRoot(std::move(fs_root_server));
  if (status != ZX_OK) {
    printf("fshost: Cannot serve devmgr's root filesystem\n");
    return status;
  }

  // Initialize namespace, and begin monitoring for a termination event.
  status = devmgr::BindNamespace(std::move(fs_root_client));
  if (status != ZX_OK) {
    printf("fshost: cannot bind namespace\n");
    return status;
  }
  // TODO(dgonyeo): call WatchExit from inside FsManager, instead of doing it
  // here.
  fs_manager->WatchExit();

  // If there is a ramdisk, setup the ramctl filesystems.
  zx::vmo ramdisk_vmo;
  status = devmgr::get_ramdisk(&ramdisk_vmo);
  if (status != ZX_OK) {
    printf("fshost: failed to get ramdisk: %s\n", zx_status_get_string(status));
  } else if (ramdisk_vmo.is_valid()) {
    thrd_t t;
    int err = thrd_create_with_name(&t, &devmgr::RamctlWatcher, &ramdisk_vmo, "ramctl-filesystems");
    if (err != thrd_success) {
      printf("fshost: failed to start ramctl-filesystems: %d\n", err);
    }
    thrd_detach(t);
  }

  // Start the block watcher or sleep forever
  if (!disable_block_watcher) {
    // Check relevant boot arguments
    devmgr::BlockWatcherOptions options = {};
    options.netboot = fs_manager->boot_args()->netboot();
    options.check_filesystems = fs_manager->boot_args()->check_filesystems();
    options.wait_for_data = fs_manager->boot_args()->wait_for_data();

    if (options.netboot) {
      printf("fshost: disabling automount\n");
    }

    BlockDeviceWatcher(std::move(fs_manager), options);
  } else {
    // Keep the process alive so that this component doesn't appear to have
    // terminated and so that it keeps serving services started on other async
    // loops/threads
    zx::nanosleep(zx::time::infinite());
  }
  printf("fshost: terminating (block device filesystems finished?)\n");
  return 0;
}
