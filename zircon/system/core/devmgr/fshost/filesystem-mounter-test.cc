// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filesystem-mounter.h"

#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <cobalt-client/cpp/in_memory_logger.h>
#include <zxtest/zxtest.h>

#include "fs-manager.h"
#include "fshost-fs-provider.h"
#include "metrics.h"

namespace devmgr {
namespace {

FsHostMetrics MakeMetrics() {
  return FsHostMetrics(std::make_unique<cobalt_client::Collector>(
      std::make_unique<cobalt_client::InMemoryLogger>()));
}

class FilesystemMounterHarness : public zxtest::Test {
 public:
  void SetUp() override {
    zx::channel dir_request;
    ASSERT_OK(FsManager::Create(nullptr, std::move(dir_request), MakeMetrics(), &manager_));
    manager_->WatchExit();
  }

  std::unique_ptr<FsManager> TakeManager() { return std::move(manager_); }

 private:
  std::unique_ptr<FsManager> manager_;
};

using MounterTest = FilesystemMounterHarness;

TEST_F(MounterTest, CreateFilesystemManager) {}

TEST_F(MounterTest, CreateFilesystemMounter) {
  BlockWatcherOptions options = {};
  FilesystemMounter mounter(TakeManager(), options);
}

TEST_F(MounterTest, PkgfsWillNotMountBeforeBlobAndData) {
  BlockWatcherOptions options = {};
  FilesystemMounter mounter(TakeManager(), options);

  ASSERT_FALSE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

enum class FilesystemType {
  kBlobfs,
  kMinfs,
};

class TestMounter : public FilesystemMounter {
 public:
  template <typename... Args>
  TestMounter(Args&&... args) : FilesystemMounter(std::forward<Args>(args)...) {}

  void ExpectFilesystem(FilesystemType fs) { expected_filesystem_ = fs; }

  zx_status_t LaunchFs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                       size_t len) final {
    if (argc != 3) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (len != 2) {
      return ZX_ERR_INVALID_ARGS;
    }

    switch (expected_filesystem_) {
      case FilesystemType::kBlobfs:
        EXPECT_STR_EQ(argv[0], "/boot/bin/blobfs");
        break;
      case FilesystemType::kMinfs:
        EXPECT_STR_EQ(argv[0], "/boot/bin/minfs");
        break;
      default:
        ADD_FAILURE("Unexpected filesystem type");
    }

    EXPECT_STR_EQ(argv[1], "--journal");
    EXPECT_STR_EQ(argv[2], "mount");

    EXPECT_EQ(ids[0], FS_HANDLE_ROOT_ID);
    EXPECT_EQ(ids[1], FS_HANDLE_BLOCK_DEVICE_ID);

    zx::channel* server = nullptr;
    switch (expected_filesystem_) {
      case FilesystemType::kBlobfs:
        server = &blobfs_server_;
        break;
      case FilesystemType::kMinfs:
        server = &minfs_server_;
        break;
      default:
        ADD_FAILURE("Unexpected filesystem type");
    }

    server->reset(hnd[0]);
    EXPECT_OK(server->signal_peer(0, ZX_USER_SIGNAL_0));
    EXPECT_OK(zx_handle_close(hnd[1]));
    return ZX_OK;
  }

 private:
  FilesystemType expected_filesystem_ = FilesystemType::kBlobfs;
  zx::channel blobfs_server_;
  zx::channel minfs_server_;
};

TEST_F(MounterTest, PkgfsWillNotMountBeforeData) {
  BlockWatcherOptions block_options = {};
  block_options.wait_for_data = true;
  TestMounter mounter(TakeManager(), block_options);

  mount_options_t options = default_mount_options;
  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), options));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

TEST_F(MounterTest, PkgfsWillNotMountBeforeDataUnlessExplicitlyRequested) {
  BlockWatcherOptions block_options = {};
  block_options.wait_for_data = false;
  TestMounter mounter(TakeManager(), block_options);

  mount_options_t options = default_mount_options;
  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), options));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_FALSE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_TRUE(mounter.PkgfsMounted());
}

TEST_F(MounterTest, PkgfsWillNotMountBeforeBlob) {
  BlockWatcherOptions block_options = {};
  block_options.wait_for_data = true;
  TestMounter mounter(TakeManager(), block_options);

  mount_options_t options = default_mount_options;
  mounter.ExpectFilesystem(FilesystemType::kMinfs);
  ASSERT_OK(mounter.MountData(zx::channel(), options));

  ASSERT_FALSE(mounter.BlobMounted());
  ASSERT_TRUE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_FALSE(mounter.PkgfsMounted());
}

TEST_F(MounterTest, PkgfsMountsWithBlobAndData) {
  BlockWatcherOptions block_options = {};
  block_options.wait_for_data = true;
  TestMounter mounter(TakeManager(), block_options);

  mount_options_t options = default_mount_options;
  mounter.ExpectFilesystem(FilesystemType::kBlobfs);
  ASSERT_OK(mounter.MountBlob(zx::channel(), options));
  mounter.ExpectFilesystem(FilesystemType::kMinfs);
  ASSERT_OK(mounter.MountData(zx::channel(), options));

  ASSERT_TRUE(mounter.BlobMounted());
  ASSERT_TRUE(mounter.DataMounted());
  mounter.TryMountPkgfs();
  EXPECT_TRUE(mounter.PkgfsMounted());
}

}  // namespace
}  // namespace devmgr
