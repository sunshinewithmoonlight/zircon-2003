// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/thread_annotations.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

#include <unittest/unittest.h>

static mtx_t big_lock;

int block_forever(void *arg) TA_ACQ(&big_lock) {
  mtx_lock(&big_lock);
  return 0;
}

bool mutex_block(void) TA_ACQ(&big_lock) {
  BEGIN_TEST;
  mtx_init(&big_lock, mtx_plain);
  mtx_lock(&big_lock);

  thrd_t thread;
  thrd_create_with_name(&thread, block_forever, NULL, "block_forever");
  thrd_detach(thread);
  END_TEST;
}

BEGIN_TEST_CASE(hard_to_exit)
RUN_TEST(mutex_block)
END_TEST_CASE(hard_to_exit)
