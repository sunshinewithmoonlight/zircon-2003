// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef SYSROOT_BITS_NULL_H_
#define SYSROOT_BITS_NULL_H_

// The compiler's <stddef.h> defines NULL without defining anything
// else if __need_NULL is defined first.
#define __need_NULL
#include <stddef.h>

#endif  // SYSROOT_BITS_NULL_H_
