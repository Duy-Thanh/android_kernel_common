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
    allocation::TranslatedFds,
    defs::*,
    error::{BinderError, BinderResult},
    node::{Node, NodeRef},
    process::{Process, ProcessInner},
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
    is_outstanding: AtomicBool,
    code: u32,
    pub(crate) flags: u32,
    data_size: usize,
    offsets_size: usize,
    data_address: usize,
    links: Links<dyn DeliverToRead>,
    sender_euid: Kuid,
    txn_security_ctx_off: Option<usize>,
    pub(crate) oneway_spam_detected: bool,
}

impl Transaction {
    pub(crate) fn new(
        node_ref: NodeRef,
        stack_next: Option<Arc<Transaction>>,
        from: &Arc<Thread>,
        tr: &BinderTransactionDataSg,
    ) -> BinderResult<Arc<Self>> {
        let trd = &tr.transaction_data;
        let allow_fds = node_ref.node.flags & FLAT_BINDER_FLAG_ACCEPTS_FDS != 0;
        let txn_security_ctx = node_ref.node.flags & FLAT_BINDER_FLAG_TXN_SECURITY_CTX != 0;
        let mut txn_security_ctx_off = if txn_security_ctx { Some(0) } else { None };
        let to = node_ref.node.owner.clone();
        let mut alloc =
            match from.copy_transaction_data(&to, tr, allow_fds, txn_security_ctx_off.as_mut()) {
                Ok(alloc) => alloc,
                Err(err) => {
                    if !err.is_dead() {
                        pr_warn!("Failure in copy_transaction_data: {:?}", err);
                    }
                    return Err(err);
                }
            };
        let oneway_spam_detected = alloc.oneway_spam_detected;
        if trd.flags & TF_ONE_WAY != 0 {
            if stack_next.is_some() {
                pr_warn!("Oneway transaction should not be in a transaction stack.");
                return Err(EINVAL.into());
            }
            alloc.set_info_oneway_node(node_ref.node.clone());
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
            sender_euid: from.process.cred.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            offsets_size: trd.offsets_size as _,
            data_address,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            is_outstanding: AtomicBool::new(false),
            txn_security_ctx_off,
            oneway_spam_detected,
        })?)
    }

    pub(crate) fn new_reply(
        from: &Arc<Thread>,
        to: Arc<Process>,
        tr: &BinderTransactionDataSg,
        allow_fds: bool,
    ) -> BinderResult<Arc<Self>> {
        let trd = &tr.transaction_data;
        let mut alloc = match from.copy_transaction_data(&to, tr, allow_fds, None) {
            Ok(alloc) => alloc,
            Err(err) => {
                pr_warn!("Failure in copy_transaction_data: {:?}", err);
                return Err(err);
            }
        };
        let oneway_spam_detected = alloc.oneway_spam_detected;
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
            offsets_size: trd.offsets_size as _,
            data_address,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            is_outstanding: AtomicBool::new(false),
            txn_security_ctx_off: None,
            oneway_spam_detected,
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

    pub(crate) fn set_outstanding(&self, to_process: &mut ProcessInner) {
        // No race because this method is only called once.
        if !self.is_outstanding.load(Ordering::Relaxed) {
            self.is_outstanding.store(true, Ordering::Relaxed);
            to_process.add_outstanding_txn();
        }
    }

    /// Decrement `outstanding_txns` in `to` if it hasn't already been decremented.
    fn drop_outstanding_txn(&self) {
        // No race because this is called at most twice, and one of the calls are in the
        // destructor, which is guaranteed to not race with any other operations on the
        // transaction. It also cannot race with `set_outstanding`, since submission happens
        // before delivery.
        if self.is_outstanding.load(Ordering::Relaxed) {
            self.is_outstanding.store(false, Ordering::Relaxed);
            self.to.drop_outstanding_txn();
        }
    }

    /// Submits the transaction to a work queue. Uses a thread if there is one in the transaction
    /// stack, otherwise uses the destination process.
    ///
    /// Not used for replies.
    pub(crate) fn submit(self: Arc<Self>) -> BinderResult {
        // Defined before `process_inner` so that the destructor runs after releasing the lock.
        let mut _t_outdated = None;

        let oneway = self.flags & TF_ONE_WAY != 0;
        let process = self.to.clone();
        let mut process_inner = process.inner.lock();

        self.set_outstanding(&mut *process_inner);

        if oneway {
            if let Some(target_node) = self.target_node.clone() {
                if process_inner.is_frozen {
                    process_inner.async_recv = true;
                    if self.flags & TF_UPDATE_TXN != 0 {
                        _t_outdated =
                            target_node.take_outdated_transaction(&self, &mut process_inner);
                    }
                }
                target_node.submit_oneway(self, &mut process_inner)?;
                return Ok(());
            } else {
                pr_err!("Failed to submit oneway transaction to node.");
            }
        }

        if process_inner.is_frozen {
            process_inner.sync_recv = true;
            return Err(BinderError::new_frozen());
        }

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

    /// Check whether one oneway transaction can supersede another.
    pub(crate) fn can_replace(&self, old: &Transaction) -> bool {
        if self.from.process.task.pid() != old.from.process.task.pid() {
            return false;
        }

        if self.flags & old.flags & (TF_ONE_WAY | TF_UPDATE_TXN) != (TF_ONE_WAY | TF_UPDATE_TXN) {
            return false;
        }

        let target_node_match = match (self.target_node.as_ref(), old.target_node.as_ref()) {
            (None, None) => true,
            (Some(tn1), Some(tn2)) => Arc::ptr_eq(tn1, tn2),
            _ => false,
        };

        self.code == old.code && self.flags == old.flags && target_node_match
    }

    fn prepare_file_list(&self) -> Result<TranslatedFds> {
        let mut alloc = self.to.buffer_get(self.data_address).ok_or(ESRCH)?;

        match alloc.translate_fds() {
            Ok(translated) => {
                alloc.keep_alive();
                Ok(translated)
            }
            Err(err) => {
                // Free the allocation eagerly, and don't attempt to free it again when we drop the
                // transaction.
                drop(alloc);
                self.free_allocation.store(false, Ordering::Relaxed);
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
            self.drop_outstanding_txn();
        });
        let files = if let Ok(list) = self.prepare_file_list() {
            list
        } else {
            // On failure to process the list, we send a reply back to the sender and ignore the
            // transaction on the recipient.
            return Ok(true);
        };

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
        tr.offsets_size = self.offsets_size as _;
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

        // Scope to make borrow-checker see `alloc_for_close_on_free` go out of scope at the right
        // time.
        {
            let alloc_for_close_on_free = if files.needs_close_on_free() {
                Some(self.to.buffer_get(self.data_address).ok_or(ESRCH)?)
            } else {
                None
            };

            // Dismiss the completion of transaction with a failure. No failure paths are allowed from
            // here on out.
            send_failed_reply.dismiss();

            // Commit files, and set FDs in FDA to be closed on buffer free.
            let close_on_free = files.commit();
            if let Some(mut alloc) = alloc_for_close_on_free {
                alloc.set_info_close_on_free(close_on_free);
                alloc.keep_alive();
            } else if !close_on_free.is_empty() {
                pr_warn!("Failed to attach close_on_free fds.");
            }
        }

        // When `drop` is called, we don't want the allocation to be freed because it is now the
        // user's reponsibility to free it.
        //
        // `drop` is guaranteed to see this relaxed store because `Arc` guarantess that everything
        // that happens when an object is referenced happens-before the eventual `drop`.
        self.free_allocation.store(false, Ordering::Relaxed);

        self.drop_outstanding_txn();

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

        self.drop_outstanding_txn();
    }

    fn get_links(&self) -> &Links<dyn DeliverToRead> {
        &self.links
    }

    fn should_sync_wakeup(&self) -> bool {
        self.flags & TF_ONE_WAY == 0
    }

    fn downcast_transaction(&self) -> Option<&Transaction> {
        Some(self)
    }
}

impl Drop for Transaction {
    fn drop(&mut self) {
        if self.free_allocation.load(Ordering::Relaxed) {
            self.to.buffer_get(self.data_address);
        }

        self.drop_outstanding_txn();
    }
}
