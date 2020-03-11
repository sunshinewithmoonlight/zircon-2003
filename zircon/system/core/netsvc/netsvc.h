// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_NETSVC_NETSVC_H_
#define ZIRCON_SYSTEM_CORE_NETSVC_NETSVC_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

bool netbootloader();
const char* nodename();
bool all_features();

__END_CDECLS

#endif  // ZIRCON_SYSTEM_CORE_NETSVC_NETSVC_H_
