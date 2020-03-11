// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
#define ZIRCON_SYSTEM_DEV_LIB_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/llcpp/fidl.h>

#include "zxtest/zxtest.h"

namespace mock_sysmem {

class MockBufferCollection : public llcpp::fuchsia::sysmem::BufferCollection::Interface {
 public:
  void SetEventSink(::zx::channel events, SetEventSinkCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void Sync(SyncCompleter::Sync _completer) override { EXPECT_TRUE(false); }
  void SetConstraints(bool has_constraints,
                      llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void CloseSingleBuffer(uint64_t buffer_index,
                         CloseSingleBufferCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void AllocateSingleBuffer(uint64_t buffer_index,
                            AllocateSingleBufferCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForSingleBufferAllocated(
      uint64_t buffer_index, WaitForSingleBufferAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckSingleBufferAllocated(uint64_t buffer_index,
                                  CheckSingleBufferAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void Close(CloseCompleter::Sync _completer) override { EXPECT_TRUE(false); }
};

}  // namespace mock_sysmem

#endif  // ZIRCON_SYSTEM_DEV_LIB_MOCK_SYSMEM_INCLUDE_LIB_MOCK_SYSMEM_MOCK_BUFFER_COLLECTION_H_
