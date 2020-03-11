// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_PAVER_VMO_READER_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_VMO_READER_H_

#include <fuchsia/mem/llcpp/fidl.h>
#include <lib/zx/vmo.h>

#include <algorithm>

namespace paver {

class VmoReader {
 public:
  VmoReader(::llcpp::fuchsia::mem::Buffer buffer)
      : vmo_(std::move(buffer.vmo)), size_(buffer.size) {}

  zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) {
    if (offset_ >= size_) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    const auto size = std::min(size_ - offset_, buf_size);
    auto status = vmo_.read(buf, offset_, size);
    if (status != ZX_OK) {
      return status;
    }
    offset_ += size;
    *size_actual = size;
    return ZX_OK;
  }

 private:
  zx::vmo vmo_;
  size_t size_;
  zx_off_t offset_ = 0;
};

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_VMO_READER_H_
