// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_IHDA_ZIRCON_DEVICE_H_
#define ZIRCON_SYSTEM_UAPP_IHDA_ZIRCON_DEVICE_H_

#include <lib/zx/channel.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/intel-hda.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace audio {
namespace intel_hda {

class ZirconDevice {
 public:
  zx_status_t Connect();
  void Disconnect();

  const char* dev_name() const { return dev_name_ ? dev_name_ : "<unknown>"; }

  using EnumerateCbk = zx_status_t (*)(void* ctx, uint32_t id, const char* const str);
  static zx_status_t Enumerate(void* ctx, const char* const dev_path, EnumerateCbk cbk);

  enum class Type { Controller, Codec };

  explicit ZirconDevice(const char* const dev_name, Type type)
      : type_(type), dev_name_(::strdup(dev_name)) {}

  ~ZirconDevice() {
    Disconnect();
    if (dev_name_)
      ::free(dev_name_);
  }

  template <typename ReqType, typename RespType>
  zx_status_t CallDevice(const ReqType& req, RespType* resp, zx::duration timeout = zx::msec(100)) {
    if (!resp)
      return ZX_ERR_INVALID_ARGS;

    zx_status_t res = Connect();
    if (res != ZX_OK)
      return res;

    zx_channel_call_args_t args;
    memset(&args, 0, sizeof(args));

    // TODO(johngro) : get rid of this const cast
    args.wr_bytes = const_cast<ReqType*>(&req);
    args.wr_num_bytes = sizeof(req);
    args.rd_bytes = resp;
    args.rd_num_bytes = sizeof(*resp);

    return CallDevice(args, timeout);
  }

  template <typename ReqType>
  static void InitRequest(ReqType* req, ihda_cmd_t cmd) {
    ZX_DEBUG_ASSERT(req != nullptr);
    memset(req, 0, sizeof(*req));
    do {
      req->hdr.transaction_id = ++transaction_id_;
    } while (req->hdr.transaction_id == IHDA_INVALID_TRANSACTION_ID);
    req->hdr.cmd = cmd;
  }

 private:
  const Type type_;
  char* dev_name_;
  zx::channel dev_channel_;

  zx_status_t CallDevice(const zx_channel_call_args_t& args, zx::duration timeout);
  static uint32_t transaction_id_;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // ZIRCON_SYSTEM_UAPP_IHDA_ZIRCON_DEVICE_H_
