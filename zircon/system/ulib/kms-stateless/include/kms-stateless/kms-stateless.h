#ifndef KMS_STATELESS_KMS_STATELESS_H_
#define KMS_STATELESS_KMS_STATELESS_H_

#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/function.h>

namespace kms_stateless {
const size_t kExpectedKeyInfoSize = 32;

// The callback called when a hardware key is successfully derived. Arguments to the callback
// is a unique_ptr of the key buffer and the key size.
using GetHardwareDerivedKeyCallback =
    fbl::Function<zx_status_t(std::unique_ptr<uint8_t[]>, size_t)>;

// Get a hardware derived key using
zx_status_t GetHardwareDerivedKey(GetHardwareDerivedKeyCallback callback,
                                  uint8_t key_info[kExpectedKeyInfoSize]);
}  // namespace kms_stateless

#endif  // KMS_STATELESS_KMS_STATELESS_H_
