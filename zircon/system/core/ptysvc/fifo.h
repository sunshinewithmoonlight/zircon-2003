// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_PTYSVC_FIFO_H_
#define ZIRCON_SYSTEM_CORE_PTYSVC_FIFO_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/compiler.h>

#include <array>

// A basic FIFO of bytes.  This class is not thread-safe.
class Fifo {
 public:
  Fifo() = default;
  ~Fifo() = default;

  Fifo(const Fifo&) = delete;
  Fifo(Fifo&&) = delete;
  Fifo& operator=(const Fifo&) = delete;
  Fifo& operator=(Fifo&&) = delete;

  // Returns the number of bytes read.
  [[nodiscard]] size_t Read(void* data, size_t len);

  // Returns the number of bytes written.  If |atomic| is true, no partial
  // writes will occur.
  [[nodiscard]] size_t Write(const void* data, size_t len, bool atomic);

  [[nodiscard]] bool is_empty() const { return head_ == tail_; }
  [[nodiscard]] bool is_full() const { return head_ - tail_ == kSize; }

  // The size of the FIFO.
  static constexpr uint32_t kSize = 4096;

 private:
  uint32_t head_ = 0;
  uint32_t tail_ = 0;
  std::array<uint8_t, kSize> data_ = {};
};

#endif  // ZIRCON_SYSTEM_CORE_PTYSVC_FIFO_H_
