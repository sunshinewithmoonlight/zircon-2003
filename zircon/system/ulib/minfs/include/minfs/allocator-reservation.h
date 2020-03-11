// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#ifndef MINFS_ALLOCATOR_RESERVATION_H_
#define MINFS_ALLOCATOR_RESERVATION_H_

#include <fbl/function.h>
#include <fbl/macros.h>
#include <minfs/format.h>
#include <minfs/mutex.h>
#include <minfs/pending-work.h>
#include <minfs/superblock.h>

#ifdef __Fuchsia__
#include <fuchsia/minfs/c/fidl.h>
#endif

namespace minfs {

// Forward declaration for a reference to the internal allocator.
class Allocator;

// This class represents a reservation from an Allocator to save a particular number of reserved
// elements for later allocation. Allocation for reserved elements must be done through the
// AllocatorReservation class.
// This class is thread-compatible.
// This class is not assignable, copyable, or moveable.
class AllocatorReservation {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AllocatorReservation);

  AllocatorReservation() {}
  ~AllocatorReservation();

  // Returns |ZX_OK| when |allocator| reserves |reserved| elements and |this| is successfully
  // initialized. Returns an error if not enough elements are available for reservation,
  // |allocator| is null, or |this| was previously initialized.
  zx_status_t Initialize(PendingWork* transaction, size_t reserved, Allocator* allocator);

  bool IsInitialized() const { return allocator_ != nullptr; }

  // Allocate a new item in allocator_. Return the index of the newly allocated item.
  // A call to Allocate() is effectively the same as a call to Swap(0) + SwapCommit(), but under
  // the hood completes these operations more efficiently as additional state doesn't need to be
  // stored between the two.
  size_t Allocate(PendingWork* transaction);

  // Unreserve all currently reserved items.
  void Cancel();

#ifdef __Fuchsia__
  // Swap the element currently allocated at |old_index| for a new index.
  // If |old_index| is 0, a new block will still be allocated, but no blocks will be de-allocated.
  // The swap will not be persisted until a call to SwapCommit is made.
  size_t Swap(size_t old_index);

  // Commit any pending swaps, allocating new indices and de-allocating old indices.
  void SwapCommit(PendingWork* transaction);

  // Remove |requested| reserved elements and give them to |other_reservation|.
  // The reserved count belonging to the Allocator does not change.
  void GiveBlocks(size_t requested, AllocatorReservation* other_reservation);

  size_t GetReserved() const { return reserved_; }
#endif
 private:
  Allocator* allocator_ = nullptr;
  size_t reserved_ = 0;

  // TODO(planders): Optionally store swap info in AllocatorReservation,
  //                 to ensure we only swap the current reservation's blocks on SwapCommit.
};

}  // namespace minfs

#endif  // MINFS_ALLOCATOR_RESERVATION_H_
