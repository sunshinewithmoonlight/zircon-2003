// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <region-alloc/region-alloc.h>
#include <zxtest/zxtest.h>

#include "common.h"

TEST(RegionAllocCApiTestCase, RegionPools) {
  // Make a pool for the bookkeeping.  Do not allow it to be very large.
  // Require that this succeeds, we will not be able to run the tests without
  // it.
  ralloc_pool_t* pool;
  ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
  ASSERT_NOT_NULL(pool);

  // Create an allocator.
  ralloc_allocator_t* alloc;
  ASSERT_OK(ralloc_create_allocator(&alloc));
  ASSERT_NOT_NULL(alloc);

  {
    // Make sure that it refuses to perform any operations because it has no
    // RegionPool assigned to it yet.
    const ralloc_region_t tmp = {.base = 0u, .size = 1u};
    const ralloc_region_t* out;

    EXPECT_EQ(ZX_ERR_BAD_STATE, ralloc_add_region(alloc, &tmp, false));
    EXPECT_EQ(ZX_ERR_BAD_STATE, ralloc_get_sized_region_ex(alloc, 1u, 1u, &out));
    EXPECT_EQ(ZX_ERR_BAD_STATE, ralloc_get_specific_region_ex(alloc, &tmp, &out));
    EXPECT_NULL(ralloc_get_sized_region(alloc, 1u, 1u));
    EXPECT_NULL(ralloc_get_specific_region(alloc, &tmp));
  }

  // Assign our pool to our allocator, but hold onto the pool for now.
  EXPECT_OK(ralloc_set_region_pool(alloc, pool));

  // Release our pool reference.  The allocator should be holding onto its own
  // reference at this point.
  ralloc_release_pool(pool);
  pool = NULL;

  // Add some regions to our allocator.
  for (size_t i = 0; i < countof(GOOD_REGIONS); ++i)
    EXPECT_OK(ralloc_add_region(alloc, &GOOD_REGIONS[i], false));

  // Make a new pool and try to assign it to the allocator.  This should fail
  // because the allocator is currently using resources from its currently
  // assigned pool.
  ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
  ASSERT_NOT_NULL(pool);
  EXPECT_EQ(ZX_ERR_BAD_STATE, ralloc_set_region_pool(alloc, pool));

  // Add a bunch of adjacent regions to our pool.  Try to add so many
  // that we would normally run out of bookkeeping space.  We should not
  // actually run out, however, because the regions should get merged as they
  // get added.
  {
    ralloc_region_t tmp = {.base = GOOD_MERGE_REGION_BASE, .size = GOOD_MERGE_REGION_SIZE};
    for (size_t i = 0; i < OOM_RANGE_LIMIT; ++i) {
      ASSERT_OK(ralloc_add_region(alloc, &tmp, false));
      tmp.base += tmp.size;
    }
  }

  // Attempt (and fail) to add some bad regions (regions which overlap,
  // regions which wrap the address space)
  for (size_t i = 0; i < countof(BAD_REGIONS); ++i)
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, ralloc_add_region(alloc, &BAD_REGIONS[i], false));

  // Force the region bookkeeping pool to run out of memory by adding more and
  // more regions until we eventually run out of room.  Make sure that the
  // regions are not adjacent, or the internal bookkeeping will just merge
  // them.
  {
    size_t i;
    ralloc_region_t tmp = {.base = BAD_MERGE_REGION_BASE, .size = BAD_MERGE_REGION_SIZE};
    for (i = 0; i < OOM_RANGE_LIMIT; ++i) {
      zx_status_t res;

      res = ralloc_add_region(alloc, &tmp, false);
      if (res != ZX_OK) {
        EXPECT_EQ(ZX_ERR_NO_MEMORY, res);
        break;
      }

      tmp.base += tmp.size + 1;
    }

    EXPECT_LT(i, OOM_RANGE_LIMIT);
  }

  // Reset allocator.  All of the existing available regions we had previously
  // added will be returned to the pool.
  ralloc_reset_allocator(alloc);

  // Now assign the second pool to the allocator.  Now that the allocator is
  // no longer using any resources, this should succeed.
  EXPECT_OK(ralloc_set_region_pool(alloc, pool));

  // Release our pool reference.
  ralloc_release_pool(pool);

  // Destroy our allocator.
  ralloc_destroy_allocator(alloc);
}

TEST(RegionAllocCApiTestCase, AllocBySize) {
  // Make a pool and attach it to an allocator.  Then add the test regions to it.
  ralloc_allocator_t* alloc = NULL;
  {
    ralloc_pool_t* pool;
    ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
    ASSERT_NOT_NULL(pool);

    // Create an allocator and add our region pool to it.
    ASSERT_OK(ralloc_create_allocator(&alloc));
    ASSERT_NOT_NULL(alloc);
    ASSERT_OK(ralloc_set_region_pool(alloc, pool));

    // Release our pool reference.  The allocator should be holding onto its own
    // reference at this point.
    ralloc_release_pool(pool);
  }

  for (size_t i = 0; i < countof(ALLOC_BY_SIZE_REGIONS); ++i)
    EXPECT_OK(ralloc_add_region(alloc, &ALLOC_BY_SIZE_REGIONS[i], false));

  // Run the alloc by size tests.  Hold onto the regions it allocates so they
  // can be cleaned up properly when the test finishes.
  const ralloc_region_t* regions[countof(ALLOC_BY_SIZE_TESTS)];
  memset(regions, 0, sizeof(regions));

  for (size_t i = 0; i < countof(ALLOC_BY_SIZE_TESTS); ++i) {
    const alloc_by_size_alloc_test_t* TEST = ALLOC_BY_SIZE_TESTS + i;
    zx_status_t res = ralloc_get_sized_region_ex(alloc, TEST->size, TEST->align, regions + i);

    // Make sure we get the test result we were expecting.
    EXPECT_EQ(TEST->res, res);

    // If the allocation claimed to succeed, we should have gotten
    // back a non-null region.  Otherwise, we should have gotten a
    // null region back.
    if (res == ZX_OK) {
      ASSERT_NOT_NULL(regions[i]);
    } else {
      EXPECT_NULL(regions[i]);
    }

    // If the allocation succeeded, and we expected it to succeed,
    // the allocation should have come from the test region we
    // expect and be aligned in the way we asked.
    if ((res == ZX_OK) && (TEST->res == ZX_OK)) {
      ASSERT_LT(TEST->region, countof(ALLOC_BY_SIZE_TESTS));
      EXPECT_TRUE(region_contains_region(ALLOC_BY_SIZE_REGIONS + TEST->region, regions[i]), "");
      EXPECT_EQ(0u, regions[i]->base & (TEST->align - 1));
    }
  }

  // Put the regions we have allocated back in the allocator.
  for (size_t i = 0; i < countof(regions); ++i)
    if (regions[i])
      ralloc_put_region(regions[i]);

  // Destroy our allocator.
  ralloc_destroy_allocator(alloc);
}

TEST(RegionAllocCApiTestCase, AllocSpecific) {
  // Make a pool and attach it to an allocator.  Then add the test regions to it.
  ralloc_allocator_t* alloc = NULL;
  {
    ralloc_pool_t* pool;
    ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
    ASSERT_NOT_NULL(pool);

    // Create an allocator and add our region pool to it.
    ASSERT_OK(ralloc_create_allocator(&alloc));
    ASSERT_NOT_NULL(alloc);
    ASSERT_OK(ralloc_set_region_pool(alloc, pool));

    // Release our pool reference.  The allocator should be holding onto its own
    // reference at this point.
    ralloc_release_pool(pool);
  }

  for (size_t i = 0; i < countof(ALLOC_SPECIFIC_REGIONS); ++i)
    EXPECT_OK(ralloc_add_region(alloc, &ALLOC_SPECIFIC_REGIONS[i], false));

  // Run the alloc by size tests.  Hold onto the regions it allocates so they
  // can be cleaned up properly when the test finishes.
  const ralloc_region_t* regions[countof(ALLOC_SPECIFIC_TESTS)];
  memset(regions, 0, sizeof(regions));

  for (size_t i = 0; i < countof(ALLOC_SPECIFIC_TESTS); ++i) {
    const alloc_specific_alloc_test_t* TEST = ALLOC_SPECIFIC_TESTS + i;
    zx_status_t res = ralloc_get_specific_region_ex(alloc, &TEST->req, regions + i);

    // Make sure we get the test result we were expecting.
    EXPECT_EQ(TEST->res, res);

    // If the allocation claimed to succeed, we should have gotten back a
    // non-null region which exactly matches our requested region.
    if (res == ZX_OK) {
      ASSERT_NOT_NULL(regions[i]);
      EXPECT_EQ(TEST->req.base, regions[i]->base);
      EXPECT_EQ(TEST->req.size, regions[i]->size);
    } else {
      EXPECT_NULL(regions[i]);
    }
  }

  // Put the regions we have allocated back in the allocator.
  for (size_t i = 0; i < countof(regions); ++i)
    if (regions[i])
      ralloc_put_region(regions[i]);

  // Destroy our allocator.
  ralloc_destroy_allocator(alloc);
}

TEST(RegionAllocCApiTestCase, AddOverlap) {
  // Make a pool and attach it to an allocator.
  ralloc_allocator_t* alloc = NULL;
  {
    ralloc_pool_t* pool;
    ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
    ASSERT_NOT_NULL(pool);

    // Create an allocator and add our region pool to it.
    ASSERT_OK(ralloc_create_allocator(&alloc));
    ASSERT_NOT_NULL(alloc);
    ASSERT_OK(ralloc_set_region_pool(alloc, pool));

    // Release our pool reference.  The allocator should be holding onto its own
    // reference at this point.
    ralloc_release_pool(pool);
  }

  // Add each of the regions specified by the test and check the expected results.
  for (size_t i = 0; i < countof(ADD_OVERLAP_TESTS); ++i) {
    const alloc_add_overlap_test_t* TEST = ADD_OVERLAP_TESTS + i;

    zx_status_t res = ralloc_add_region(alloc, &TEST->reg, TEST->ovl);

    EXPECT_EQ(TEST->res, res);
    EXPECT_EQ(TEST->cnt, ralloc_get_available_region_count(alloc));
  }

  // Destroy our allocator.
  ralloc_destroy_allocator(alloc);
}

TEST(RegionAllocCApiTestCase, Subtract) {
  // Make a pool and attach it to an allocator.
  ralloc_allocator_t* alloc = NULL;
  {
    ralloc_pool_t* pool;
    ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
    ASSERT_NOT_NULL(pool);

    // Create an allocator and add our region pool to it.
    ASSERT_OK(ralloc_create_allocator(&alloc));
    ASSERT_NOT_NULL(alloc);
    ASSERT_OK(ralloc_set_region_pool(alloc, pool));

    // Release our pool reference.  The allocator should be holding onto its own
    // reference at this point.
    ralloc_release_pool(pool);
  }

  // Run the test sequence, adding and subtracting regions and verifying the results.
  for (size_t i = 0; i < countof(SUBTRACT_TESTS); ++i) {
    const alloc_subtract_test_t* TEST = SUBTRACT_TESTS + i;

    zx_status_t res;
    if (TEST->add)
      res = ralloc_add_region(alloc, &TEST->reg, false);
    else
      res = ralloc_sub_region(alloc, &TEST->reg, TEST->incomplete);

    EXPECT_EQ(TEST->res ? ZX_OK : ZX_ERR_INVALID_ARGS, res);
    EXPECT_EQ(TEST->cnt, ralloc_get_available_region_count(alloc));
  }

  // Destroy our allocator.
  ralloc_destroy_allocator(alloc);
}

static bool ralloc_walk_cb(const ralloc_region_t* r, void* ctx) {
  ralloc_walk_test_ctx_t* ctx_ = (ralloc_walk_test_ctx_t*)ctx;

  // Make sure the region matches what we expect.  If not, tell the callback
  // to exit the walk operation early.
  check_region_match(r, &ctx_->regions[ctx_->i]);
  if (CURRENT_TEST_HAS_FATAL_FAILURES()) {
    return false;
  }

  ctx_->i++;

  // attempt to exit early if end is set to a value >= 0
  return (ctx_->end == -1) ? true : (ctx_->i != ctx_->end);
}

TEST(RegionAllocCApiTestCase, AllocatedWalk) {
  const ralloc_region_t test_regions[] = {
      {.base = 0x00000000, .size = 1 << 20}, {.base = 0x10000000, .size = 1 << 20},
      {.base = 0x20000000, .size = 1 << 20}, {.base = 0x30000000, .size = 1 << 20},
      {.base = 0x40000000, .size = 1 << 20}, {.base = 0x50000000, .size = 1 << 20},
      {.base = 0x60000000, .size = 1 << 20}, {.base = 0x70000000, .size = 1 << 20},
      {.base = 0x80000000, .size = 1 << 20}, {.base = 0x90000000, .size = 1 << 20},
  };
  const size_t r_cnt = countof(test_regions);

  // Create the pool for our allocator
  ralloc_pool_t* pool;
  ASSERT_OK(ralloc_create_pool(REGION_POOL_MAX_SIZE, &pool));
  ASSERT_NOT_NULL(pool);

  // Create an allocator and add our region pool to it.
  ralloc_allocator_t* alloc;
  ASSERT_OK(ralloc_create_allocator(&alloc));
  ASSERT_NOT_NULL(alloc);
  ASSERT_OK(ralloc_set_region_pool(alloc, pool));
  ralloc_region_t full_region = {.base = 0, .size = UINT64_MAX};
  ASSERT_OK(ralloc_add_region(alloc, &full_region, false));

  // Pull each region defined above out of the allocator and stash their UPtrs
  // for the time being.  Then the lambda can walk the allocated regions and
  // verify that they are in-order and match the expected values.
  const ralloc_region_t* tmp_regions[r_cnt];
  for (unsigned i = 0; i < r_cnt; i++) {
    tmp_regions[i] = ralloc_get_specific_region(alloc, &test_regions[i]);
  }

  ralloc_walk_test_ctx_t ctx = {0, -1, test_regions};
  ASSERT_NO_FATAL_FAILURES(ralloc_walk_allocated_regions(alloc, ralloc_walk_cb, (void*)&ctx));
  EXPECT_EQ(r_cnt, (size_t)ctx.i);

  // Test that exiting early works, no matter where we are in the region list.
  // Every time the function is called we increment the counter and then at
  // the end ensure we've only been called as many times as expected, within
  // the bounds of [1, r_cnt].
  for (size_t cnt = 0; cnt < 1024; cnt++) {
    ctx.i = 0;
    ctx.end = (int)(rand() % r_cnt) + 1;
    ralloc_walk_allocated_regions(alloc, ralloc_walk_cb, (void*)&ctx);
    ASSERT_EQ(ctx.i, ctx.end);
  }

  // Clean up the allocated regions, pool, then allocator
  for (size_t i = 0; i < r_cnt; i++) {
    ralloc_put_region(tmp_regions[i]);
  }
  ralloc_release_pool(pool);
  ralloc_destroy_allocator(alloc);
}
