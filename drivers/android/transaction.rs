// SPDX-License-Identifier: GPL-2.0

use core::sync::atomic::{AtomicBool, Ordering};
use kernel::{
    bindings,
    file::{File, FileDescriptorReservation},
    io_buffer::IoBufferWriter,
    linked_list::{GetLinks, Links, List},
    prelude::*,
    sync::{Arc, SpinLock},
    task::Kuid,
    types::{ARef, Either, ScopeGuard},
    user_ptr::UserSlicePtrWriter,
};

use crate::{
    defs::*,
    node::{Node, NodeRef},
    process::Process,
    ptr_align,
    thread::{BinderError, BinderResult, Thread},
    DeliverToRead,
};

struct TransactionInner {
    file_list: List<Box<FileInfo>>,
    /// True if this transaction is still counted in `outstanding_txns`.
    is_outstanding: bool,
}

#[pin_data(PinnedDrop)]
pub(crate) struct Transaction {
    #[pin]
    inner: SpinLock<TransactionInner>,
    target_node: Option<Arc<Node>>,
    stack_next: Option<Arc<Transaction>>,
    pub(crate) from: Arc<Thread>,
    to: Arc<Process>,
    free_allocation: AtomicBool,
    code: u32,
    pub(crate) flags: u32,
    data_size: usize,
    offsets_size: usize,
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
        if trd.flags & TF_ONE_WAY != 0 {
            if stack_next.is_some() {
                pr_warn!("Oneway transaction should not be in a transaction stack.");
                return Err(BinderError::new_failed());
            }
            alloc.set_info_oneway_node(node_ref.node.clone());
        }
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        let target_node = node_ref.node.clone();
        alloc.set_info_target_node(node_ref);
        let data_address = alloc.ptr;
        let file_list = alloc.take_file_list();
        alloc.keep_alive();

        let inner = TransactionInner {
            file_list,
            is_outstanding: false,
        };
        Ok(Arc::pin_init(pin_init!(Transaction {
            inner <- kernel::new_spinlock!(inner, "Transaction::inner"),
            target_node: Some(target_node),
            stack_next,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            data_address,
            offsets_size: trd.offsets_size as _,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            txn_security_ctx_off,
        }))?)
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
        if trd.flags & TF_CLEAR_BUF != 0 {
            alloc.set_info_clear_on_drop();
        }
        let data_address = alloc.ptr;
        let file_list = alloc.take_file_list();
        alloc.keep_alive();
        let inner = TransactionInner {
            file_list,
            is_outstanding: false,
        };
        Ok(Arc::pin_init(pin_init!(Transaction {
            inner <- kernel::new_spinlock!(inner, "Transaction::inner"),
            target_node: None,
            stack_next: None,
            sender_euid: from.process.task.euid(),
            from: from.clone(),
            to,
            code: trd.code,
            flags: trd.flags,
            data_size: trd.data_size as _,
            data_address,
            offsets_size: trd.offsets_size as _,
            links: Links::new(),
            free_allocation: AtomicBool::new(true),
            txn_security_ctx_off: None,
        }))?)
    }

    #[inline(never)]
    pub(crate) fn debug_print(&self, m: &mut crate::debug::SeqFile) {
        let from_pid = self.from.process.task.pid_in_current_ns();
        let to_pid = self.to.task.pid_in_current_ns();
        let from_tid = self.from.id;
        match self.target_node.as_ref() {
            Some(target_node) => {
                let node_id = target_node.global_id;
                seq_print!(
                    m,
                    "{}(tid:{})->{}(nid:{})",
                    from_pid,
                    from_tid,
                    to_pid,
                    node_id
                );
            }
            None => {
                seq_print!(m, "{}(tid:{})->{}(nid:_)", from_pid, from_tid, to_pid);
            }
        }
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
        let next = self.stack_next.as_ref()?;
        Some(next.clone())
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

    /// Submits the transaction to a work queue. Use a thread if there is one in the transaction
    /// stack, otherwise use the destination process.
    ///
    /// Not used for replies.
    pub(crate) fn submit(self: Arc<Self>) -> BinderResult {
        // Defined before `process_inner` so that the destructor runs after releasing the lock.
        let mut _t_outdated = None;

        let oneway = self.flags & TF_ONE_WAY != 0;
        let process = self.to.clone();
        let mut process_inner = process.inner.lock();

        {
            let mut inner = self.inner.lock();
            if inner.is_outstanding == false {
                inner.is_outstanding = true;
                drop(inner);
                process_inner.add_outstanding_txn();
            }
        }

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
            thread.push_work(self).map(|_bool| ())
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

    /// Prepares the file list for delivery to the caller.
    fn prepare_file_list(&self) -> Result<List<Box<FileInfo>>> {
        // Get list of files that are being transferred as part of the transaction.
        let mut file_list = core::mem::replace(&mut self.inner.lock().file_list, List::new());

        // If the list is non-empty, prepare the buffer.
        if !file_list.is_empty() {
            let alloc = self.to.buffer_get(self.data_address).ok_or(ESRCH)?;
            let cleanup = ScopeGuard::new(|| {
                self.free_allocation.store(false, Ordering::Relaxed);
            });

            let mut it = file_list.cursor_front_mut();
            while let Some(file_info) = it.current() {
                let reservation = FileDescriptorReservation::new(bindings::O_CLOEXEC)?;
                alloc.write(file_info.buffer_offset, &reservation.reserved_fd())?;
                file_info.reservation = Some(reservation);
                it.move_next();
            }

            alloc.keep_alive();
            cleanup.dismiss();
        }

        Ok(file_list)
    }

    /// Decrement `outstanding_txns` in `to` if it hasn't already been decremented.
    fn drop_outstanding_txn(&self) {
        let should_drop = core::mem::replace(&mut self.inner.lock().is_outstanding, false);
        if should_drop {
            self.to.drop_outstanding_txn();
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
        let mut file_list = if let Ok(list) = self.prepare_file_list() {
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

        // Add BINDER_TYPE_FDA fds to the allocation for cleanup.
        {
            let mut fda_count = 0;
            let mut it = file_list.cursor_front();
            while let Some(file_info) = it.current() {
                fda_count += file_info.part_of_fda as usize;
                it.move_next();
            }

            // If there are any, store them in an `DeferredFdClose` in the allocation.
            //
            // If we succeed in setting the `fda_cleanup` field, then there are no more failure
            // paths, so we will definitely need to clean up BINDER_TYPE_FDA fds after that.
            //
            // We only submit the `DeferredFdClose` if we set the `fda_cleanup` field, so it's
            // ok to fail before that.
            if fda_count > 0 {
                let mut deferred = crate::allocation::DeferredFdClose::new(fda_count)?;
                it = file_list.cursor_front();
                while let Some(file_info) = it.current() {
                    if file_info.part_of_fda {
                        if let Some(reservation) = file_info.reservation.as_ref() {
                            deferred.push_fd(reservation.reserved_fd())?;
                        }
                    }
                    it.move_next();
                }

                let mut alloc = self.to.buffer_get(self.data_address).ok_or(ESRCH)?;
                alloc.set_fda_cleanup(deferred);
                alloc.keep_alive();
            }
        }

        // Dismiss the completion of transaction with a failure. No failure paths are allowed from
        // here on out.
        send_failed_reply.dismiss();

        // Commit all files.
        {
            let mut it = file_list.cursor_front_mut();
            while let Some(file_info) = it.current() {
                if let Some(reservation) = file_info.reservation.take() {
                    if let Some(file) = file_info.file.take() {
                        reservation.commit(file);
                    }
                }

                it.move_next();
            }
        }

        // When `drop` is called, we don't want the allocation to be freed because it is now the
        // user's reponsibility to free it.
        //
        // `drop` is guaranteed to see this relaxed store because `Arc` guarantess that everything
        // that happens when an object is referenced happens-before the eventual `drop`.
        self.free_allocation.store(false, Ordering::Relaxed);

        self.drop_outstanding_txn();

        // When this is not a reply and not an async transaction, update `current_transaction`. If
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

#[pinned_drop]
impl PinnedDrop for Transaction {
    fn drop(self: Pin<&mut Self>) {
        if self.free_allocation.load(Ordering::Relaxed) {
            self.to.buffer_get(self.data_address);
        }

        self.drop_outstanding_txn()
    }
}

pub(crate) struct FileInfo {
    links: Links<FileInfo>,

    /// The file for which a descriptor will be created in the recipient process.
    file: Option<ARef<File>>,

    /// The file descriptor reservation on the recipient process.
    reservation: Option<FileDescriptorReservation>,

    /// The offset in the buffer where the file descriptor is stored.
    buffer_offset: usize,

    /// Is this in a BINDER_TYPE_FD or BINDER_TYPE_FDA?
    part_of_fda: bool,
}

impl FileInfo {
    pub(crate) fn new(file: ARef<File>, buffer_offset: usize, part_of_fda: bool) -> Self {
        Self {
            file: Some(file),
            reservation: None,
            buffer_offset,
            links: Links::new(),
            part_of_fda,
        }
    }
}

impl GetLinks for FileInfo {
    type EntryType = Self;

    fn get_links(data: &Self::EntryType) -> &Links<Self::EntryType> {
        &data.links
    }
}
