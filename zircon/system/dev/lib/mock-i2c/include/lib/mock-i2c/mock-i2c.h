// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace mock_i2c {

// This class mocks an addressed I2C device by providing an i2c_protocol_t. Users can set
// expectations that either return specified data on read or verify data on write. After the test,
// use VerifyAndClear to reset the object and verify that all expectations were satisfied. See the
// following example test:
//
// mock_i2c::MockI2c i2c;
// i2c
//     .ExpectWrite({0x18})
//     .ExpectReadStop({0x3a, 0x07})
//     .ExpectWriteStop({0x18, 0x57});
//
// SomeDriver dut(i2c.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// EXPECT_TRUE(i2c.VerifyAndClear());

class MockI2c : ddk::I2cProtocol<MockI2c> {
 public:
  MockI2c() : proto_{&i2c_protocol_ops_, this} {}

  const i2c_protocol_t* GetProto() { return &proto_; }

  // Checks that the next operation is a read and returns the contents of buf.
  MockI2c& ExpectRead(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), true, false};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a read followed by a stop and returns the contents of buf.
  MockI2c& ExpectReadStop(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), true, true};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a write and that the data matches the contents of buf.
  MockI2c& ExpectWrite(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), false, false};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  // Checks that the next operation is a write followed by a stop and that the data matches the
  // contents of buf.
  MockI2c& ExpectWriteStop(fbl::Vector<uint8_t> buf) {
    I2cExpectation exp{std::move(buf), false, true};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  void VerifyAndClear() {
    EXPECT_EQ(expectations_index_, expectations_.size(), "There are more transactions expected");
    expectations_.reset();
    expectations_index_ = 0;
  }

  // These are used by ddk::I2cProtocol but are not intended for use by tests.
  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie) {
    fbl::Vector<i2c_op_t> read_ops;
    for (size_t i = 0; i < op_count; i++) {
      CheckI2cOp(op_list[i], &read_ops);
    }

    callback(cookie, ZX_OK, read_ops.data(), read_ops.size());
  }

  zx_status_t I2cGetMaxTransferSize(size_t* out_size) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  struct I2cExpectation {
    fbl::Vector<uint8_t> data;
    bool is_read;
    bool stop;
  };

  void CheckI2cOp(const i2c_op_t& op, fbl::Vector<i2c_op_t>* read_ops) {
    ASSERT_FALSE(expectations_.is_empty(), "No more transactions are expected");
    ASSERT_LT(expectations_index_, expectations_.size(), "No more transactions are expected");

    const I2cExpectation& exp = expectations_[expectations_index_++];
    EXPECT_EQ(exp.stop, op.stop, "Transaction stop flag mismatch");
    ASSERT_EQ(exp.is_read, op.is_read, "Transaction read flag mismatch");
    ASSERT_EQ(exp.data.size(), op.data_size, "Transaction size mismatch");

    if (exp.is_read) {
      read_ops->push_back({.data_buffer = exp.data.data(),
                           .data_size = exp.data.size(),
                           .is_read = true,
                           .stop = exp.stop});
    } else {
      EXPECT_EQ(0, memcmp(exp.data.data(), op.data_buffer, exp.data.size()),
                "Transaction data mismatch");
    }
  }

  const i2c_protocol_t proto_;
  fbl::Vector<I2cExpectation> expectations_;
  size_t expectations_index_ = 0;
};

}  // namespace mock_i2c
