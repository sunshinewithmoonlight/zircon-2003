// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon threads.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Profile, Status};

use fuchsia_zircon_sys as sys;

/// An object representing a Zircon thread.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Thread(Handle);
impl_handle_based!(Thread);

impl Thread {
    /// Cause the thread to begin execution.
    ///
    /// Wraps the
    /// [zx_thread_start](https://fuchsia.dev/fuchsia-src/reference/syscalls/thread_start.md)
    /// syscall.
    pub fn start(
        &self,
        thread_entry: usize,
        stack: usize,
        arg1: usize,
        arg2: usize,
    ) -> Result<(), Status> {
        let thread_raw = self.raw_handle();
        let status = unsafe { sys::zx_thread_start(thread_raw, thread_entry, stack, arg1, arg2) };
        ok(status)
    }

    /// Apply a scheduling profile to a thread.
    ///
    /// Wraps the
    /// [zx_object_set_profile](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_set_profile) syscall.
    pub fn set_profile(&self, profile: Profile, options: u32) -> Result<(), Status> {
        let thread_raw = self.raw_handle();
        let profile_raw = profile.raw_handle();
        let status = unsafe { sys::zx_object_set_profile(thread_raw, profile_raw, options) };
        ok(status)
    }

    /// Terminate the current running thread.
    ///
    /// Extreme caution should be used-- this is basically always UB in Rust.
    /// There's almost no "normal" program code where this is okay to call.
    /// Users should take care that no references could possibly exist to
    /// stack variables on this thread, and that any destructors, closure
    /// suffixes, or other "after this thing runs" code is waiting to run
    /// in order for safety.
    pub unsafe fn exit() {
        sys::zx_thread_exit()
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_zircon::{Handle, Profile, Status};

    #[test]
    fn set_profile_invalid() {
        let thread = fuchsia_runtime::thread_self();
        let profile = Profile::from(Handle::invalid());
        assert_eq!(thread.set_profile(profile, 0), Err(Status::BAD_HANDLE));
    }
}
