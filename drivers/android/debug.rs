// SPDX-License-Identifier: GPL-2.0
#![allow(unused_imports)]

use core::marker::PhantomData;
use kernel::{
    bindings::{self, seq_file},
    c_str,
    file::{File, Operations, PollTable},
    io_buffer::IoBufferWriter,
    linked_list::{GetLinks, GetLinksWrapped, Links},
    prelude::*,
    str::CStr,
    sync::Arc,
    types::ForeignOwnable,
    user_ptr::UserSlicePtrWriter,
};

pub(crate) struct SeqFile {
    m: *mut seq_file,
}

impl SeqFile {
    /// SAFETY: Pointer must be valid for lifetime of this struct, and generic type must be type of
    /// private data.
    pub(crate) unsafe fn new(m: *mut seq_file) -> SeqFile {
        Self { m }
    }

    pub(crate) fn call_printf(&mut self, args: core::fmt::Arguments<'_>) {
        unsafe {
            bindings::seq_printf(
                self.m,
                c_str!("%pA").as_char_ptr(),
                &args as *const _ as *const core::ffi::c_void,
            );
        }
    }
}

macro_rules! seq_print {
    ($m:expr, $($arg:tt)+) => (
        $m.call_printf(format_args!($($arg)+))
    );
}

macro_rules! define_show_method {
    ($( fn $show_name:ident($m:ident : SeqFile) { $($tok:tt)* } )*) => {
        $(
        #[no_mangle]
        unsafe extern "C" fn $show_name(m: *mut seq_file) -> core::ffi::c_int {
            #[inline(always)]
            #[allow(unused_mut)]
            fn show_impl(mut $m: SeqFile) {
                $($tok)*
            }

            let m = unsafe { SeqFile::new(m) };
            show_impl(m);
            return 0;
        }
        )*
    }
}

fn rust_binder_state_show_impl(m: &mut SeqFile) -> Result<()> {
    let contexts = crate::context::get_all_contexts()?;
    for ctx in contexts {
        let procs = ctx.get_all_procs()?;
        seq_print!(m, "context {}: ({} processes)\n", &*ctx.name, procs.len());
        for proc in procs {
            proc.debug_print(m)?;
            seq_print!(m, "\n");
        }
    }

    Ok(())
}

define_show_method! {
    fn rust_binder_stats_show(m: SeqFile) {
        seq_print!(m, "Hello world!\n");
    }

    fn rust_binder_state_show(m: SeqFile) {
        if let Err(err) = rust_binder_state_show_impl(&mut m) {
            seq_print!(m, "failed to generate state: {:?}", err);
        }
    }

    fn rust_binder_transactions_show(m: SeqFile) {
        seq_print!(m, "Hello world!\n");
    }

    fn rust_binder_transaction_log_show(m: SeqFile) {
        seq_print!(m, "Hello world!\n");
    }
}
