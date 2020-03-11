// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/pager.h>

#include "../blob-verifier.h"

namespace blobfs {

// Info required by the user pager to read in and verify pages.
// Initialized by the PageWatcher and passed on to the UserPager.
struct UserPagerInfo {
  // Unique identifier used by UserPager to identify the data source on the underlying block
  // device.
  uint32_t identifier;
  // Block offset (in bytes) the data starts at. Used to inform the UserPager of the offset it
  // should start issuing reads from.
  uint64_t data_start_bytes;
  // Total length of the data. The |verifier| must be set up to verify this length.
  uint64_t data_length_bytes;
  // Used to verify the pages as they are read in.
  // TODO(44742): Make BlobVerifier movable, unwrap from unique_ptr.
  std::unique_ptr<BlobVerifier> verifier;
};

// The size of a transfer buffer for reading from storage.
//
// The decision to use a single global transfer buffer is arbitrary; a pool of them could also be
// available in the future for more fine-grained access. Moreover, the blobfs pager uses a single
// thread at the moment, so a global buffer should be sufficient.
//
// 256 MB; but the size is arbitrary, since pages will become decommitted as they are moved to
// destination VMOS.
constexpr uint64_t kTransferBufferSize = 256 * (1 << 20);

// Abstract class that encapsulates a user pager, its associated thread and transfer buffer. The
// child class will need to define the interface required to populate the transfer buffer with
// blocks read from storage.
class UserPager {
 public:
  UserPager() = default;
  virtual ~UserPager() = default;

  // Returns the pager handle.
  const zx::pager& Pager() const { return pager_; }

  // Returns the pager dispatcher.
  async_dispatcher_t* Dispatcher() const { return pager_loop_.dispatcher(); }

  // Invoked by the |PageWatcher| on a read request. Reads in the requested byte range
  // [|offset|, |offset| + |length|) for the inode associated with |map_index| into the
  // |transfer_buffer_|, and then moves those pages to the destination |vmo|. If |verifier_info| is
  // not null, uses it to verify the pages prior to transferring them to the destination vmo.
  zx_status_t TransferPagesToVmo(uint64_t offset, uint64_t length, const zx::vmo& vmo,
                                 UserPagerInfo* info);

 protected:
  // Sets up the transfer buffer, creates the pager and starts the pager thread.
  zx_status_t InitPager();

  // Protected for unit test access
  zx::pager pager_;

 private:
  // Virtual function to attach the transfer buffer to the underlying block device, so that blocks
  // can be read into it from storage.
  virtual zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) = 0;

  // Virtual function to read data for the inode corresponding to |map_index| into the
  // transfer buffer for the byte range specified by [|offset|, |offset| + |length|).
  virtual zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length,
                                          UserPagerInfo* info) = 0;

  // Virtual function to verify the data read in to |transfer_vmo| (i.e. the transfer buffer) via
  // |PopulateTransferVmo|. Data in the range [|offset|, |offset| + |length|) is verified using the
  // |verifier_info| provided.
  virtual zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length,
                                        const zx::vmo& transfer_vmo, UserPagerInfo* info) = 0;

  // Virtual function for aligning the requested read range to include the minimum number of nodes
  // (per the Merkle tree) required to verify the range. The range needs to be determined before
  // calling the user pager to populate the pages, as absent pages will cause page faults during
  // verification on the pager thread, causing it to block against itself indefinitely.
  virtual zx_status_t AlignForVerification(uint64_t* offset, uint64_t* length,
                                           UserPagerInfo* info) = 0;

  // Scratch buffer for pager transfers.
  // NOTE: Per the constraints imposed by |zx_pager_supply_pages|, this needs to be unmapped before
  // calling |zx_pager_supply_pages|. Map this only when an explicit address is required, e.g. for
  // verification, and unmap it immediately after.
  zx::vmo transfer_buffer_;

  // Async loop for pager requests.
  async::Loop pager_loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_USER_PAGER_H_
