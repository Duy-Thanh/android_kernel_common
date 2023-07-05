// SPDX-License-Identifier: GPL-2.0

use core::sync::atomic::{AtomicBool, Ordering};
use kernel::{
    io_buffer::IoBufferWriter,
    linked_list::Links,
    prelude::*,
    sync::Arc,
    task::Kuid,
    types::{Either, ScopeGuard},
    user_ptr::UserSlicePtrWriter,
};

use crate::{
    defs::*,
    error::{BinderError, BinderResult},
    node::{Node, NodeRef},
    process::Process,
    ptr_align,
    thread::{PushWorkRes, Thread},
    DeliverToRead,
};

pub(crate) struct Transaction {
    target_node: Option<Arc<Node>>,
    stack_next: Option<Arc<Transaction>>,
    pub(crate) from: Arc<Thread>,
    to: Arc<Process>,
    free_allocation: AtomicBool,
    code: u32,
    pub(crate) flags: u32,
    data_size: usize,
    data_address: usize,
    links: Links<dyn DeliverToRead>,
    sender_euid: Kuid,
    txn_security_ctx_off: Option<usize>,
}

impl Transaction {
    pub(crate) fn new(
        node_ref: NodeRef,
        stack_next: Option<Arc<Transaction>>,
        from: &Arc<Thread>,
        tr: &BinderTransactionDataSg,
    ) -> BinderResult<Arc<Self>> {
        let trd = &tr.transaction_data;
        let txn_security_ctx = node_ref.node.flags & FLAT_BINDER_FLAG_TXN_SECURITY_CTX != 0;
        let mut txn_security_ctx_off = if txn_security_ctx { Some(0) } else { None };
        let to = node_ref.node.owner.clone();
        let mut alloc = match from.copy_transaction_data(&to, tr, txn_security_ctx_off.as_mut()) {
            Ok(alloc) => alloc,
            Err(err) => {
                if !err.is_dead() {
                    pr_warn!("Failure in copy_transaction_data: {:?}", err);
                }
                return Err(err);
            }
        };
        if trd.flags & TF_ONE_WAY != 0 {
            if stack_next.is_some() {
                pr_warn!("Oneway transaction should not be in a transaction stack.");
                return Err(EINVAL.into());
            }
        }
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        let target_node = node_ref.node.clone();
        alloc.set_info_target_node(node_ref);
        let data_address = alloc.ptr;
        alloc.keep_alive();

        Ok(Arc::try_new(Transaction {
            target_node: Some(target_node),
            stack_next,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            data_address,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            txn_security_ctx_off,
        })?)
    }

    pub(crate) fn new_reply(
        from: &Arc<Thread>,
        to: Arc<Process>,
        tr: &BinderTransactionDataSg,
    ) -> BinderResult<Arc<Self>> {
        let trd = &tr.transaction_data;
        let mut alloc = match from.copy_transaction_data(&to, tr, None) {
            Ok(alloc) => alloc,
            Err(err) => {
                pr_warn!("Failure in copy_transaction_data: {:?}", err);
                return Err(err);
            }
        };
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        let data_address = alloc.ptr;
        alloc.keep_alive();
        Ok(Arc::try_new(Transaction {
            target_node: None,
            stack_next: None,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            data_address,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            txn_security_ctx_off: None,
        })?)
    }

    /// Determines if the transaction is stacked on top of the given transaction.
    pub(crate) fn is_stacked_on(&self, onext: &Option<Arc<Self>>) -> bool {
        match (&self.stack_next, onext) {
            (None, None) => true,
            (Some(stack_next), Some(next)) => Arc::ptr_eq(stack_next, next),
            _ => false,
        }
    }

    /// Returns a pointer to the next transaction on the transaction stack, if there is one.
    pub(crate) fn clone_next(&self) -> Option<Arc<Self>> {
        Some(self.stack_next.as_ref()?.clone())
    }

    /// Searches in the transaction stack for a thread that belongs to the target process. This is
    /// useful when finding a target for a new transaction: if the node belongs to a process that
    /// is already part of the transaction stack, we reuse the thread.
    fn find_target_thread(&self) -> Option<Arc<Thread>> {
        let mut it = &self.stack_next;
        while let Some(transaction) = it {
            if Arc::ptr_eq(&transaction.from.process, &self.to) {
                return Some(transaction.from.clone());
            }
            it = &transaction.stack_next;
        }
        None
    }

    /// Searches in the transaction stack for a transaction originating at the given thread.
    pub(crate) fn find_from(&self, thread: &Thread) -> Option<Arc<Transaction>> {
        let mut it = &self.stack_next;
        while let Some(transaction) = it {
            if core::ptr::eq(thread, transaction.from.as_ref()) {
                return Some(transaction.clone());
            }

            it = &transaction.stack_next;
        }
        None
    }

    /// Submits the transaction to a work queue. Uses a thread if there is one in the transaction
    /// stack, otherwise uses the destination process.
    ///
    /// Not used for replies.
    pub(crate) fn submit(self: Arc<Self>) -> BinderResult {
        let process = self.to.clone();
        let mut process_inner = process.inner.lock();

        let res = if let Some(thread) = self.find_target_thread() {
            match thread.push_work(self) {
                PushWorkRes::Ok => Ok(()),
                PushWorkRes::AlreadyInList => Ok(()),
                PushWorkRes::FailedDead(me) => Err((BinderError::new_dead(), me)),
            }
        } else {
            process_inner.push_work(self)
        };
        drop(process_inner);

        match res {
            Ok(()) => Ok(()),
            Err((err, work)) => {
                // Drop work after releasing process lock.
                drop(work);
                Err(err)
            }
        }
    }
}

impl DeliverToRead for Transaction {
    fn do_work(self: Arc<Self>, thread: &Thread, writer: &mut UserSlicePtrWriter) -> Result<bool> {
        let send_failed_reply = ScopeGuard::new(|| {
            if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
                let reply = Either::Right(BR_FAILED_REPLY);
                self.from.deliver_reply(reply, &self);
            }
        });

        let mut tr_sec = BinderTransactionDataSecctx::default();
        let tr = tr_sec.tr_data();
        if let Some(target_node) = &self.target_node {
            let (ptr, cookie) = target_node.get_id();
            tr.target.ptr = ptr as _;
            tr.cookie = cookie as _;
        };
        tr.code = self.code;
        tr.flags = self.flags;
        tr.data_size = self.data_size as _;
        tr.data.ptr.buffer = self.data_address as _;
        tr.offsets_size = 0;
        if tr.offsets_size > 0 {
            tr.data.ptr.offsets = (self.data_address + ptr_align(self.data_size)) as _;
        }
        tr.sender_euid = self.sender_euid.into_uid_in_current_ns();
        tr.sender_pid = 0;
        if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
            // Not a reply and not one-way.
            tr.sender_pid = self.from.process.task.pid_in_current_ns();
        }
        let code = if self.target_node.is_none() {
            BR_REPLY
        } else {
            if self.txn_security_ctx_off.is_some() {
                BR_TRANSACTION_SEC_CTX
            } else {
                BR_TRANSACTION
            }
        };

        // Write the transaction code and data to the user buffer.
        writer.write(&code)?;
        if let Some(off) = self.txn_security_ctx_off {
            tr_sec.secctx = (self.data_address + off) as u64;
            writer.write(&tr_sec)?;
        } else {
            writer.write(&*tr)?;
        }

        // Dismiss the completion of transaction with a failure. No failure paths are allowed from
        // here on out.
        send_failed_reply.dismiss();

        // When `drop` is called, we don't want the allocation to be freed because it is now the
        // user's reponsibility to free it.
        //
        // `drop` is guaranteed to see this relaxed store because `Arc` guarantess that everything
        // that happens when an object is referenced happens-before the eventual `drop`.
        self.free_allocation.store(false, Ordering::Relaxed);

        // When this is not a reply and not a oneway transaction, update `current_transaction`. If
        // it's a reply, `current_transaction` has already been updated appropriately.
        if self.target_node.is_some() && tr_sec.transaction_data.flags & TF_ONE_WAY == 0 {
            thread.set_current_transaction(self);
        }

        Ok(false)
    }

    fn cancel(self: Arc<Self>) {
        // If this is not a reply or oneway transaction, then send a dead reply.
        if self.target_node.is_some() && self.flags & TF_ONE_WAY == 0 {
            let reply = Either::Right(BR_DEAD_REPLY);
            self.from.deliver_reply(reply, &self);
        }
    }

    fn get_links(&self) -> &Links<dyn DeliverToRead> {
        &self.links
    }

    fn should_sync_wakeup(&self) -> bool {
        self.flags & TF_ONE_WAY == 0
    }
}

impl Drop for Transaction {
    fn drop(&mut self) {
        if self.free_allocation.load(Ordering::Relaxed) {
            self.to.buffer_get(self.data_address);
        }
    }
}
