// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/algorithm.h>

namespace msm8x53 {

constexpr uint32_t kSdc1CoreBase = 0x07824000;
constexpr uint32_t kSdc1CoreSize = fbl::round_up<uint32_t, uint32_t>(0x800, PAGE_SIZE);

constexpr uint32_t kSdc1HcBase = 0x07824900;
constexpr uint32_t kSdc1HcSize = fbl::round_up<uint32_t, uint32_t>(0x200, PAGE_SIZE);

}  // namespace msm8x53
