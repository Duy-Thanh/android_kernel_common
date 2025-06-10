// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

use kernel::{
    alloc::AllocError,
    list::ListArc,
    prelude::*,
    rbtree::{self, RBTreeNodeReservation},
    seq_file::SeqFile,
    seq_print,
    sync::{Arc, UniqueArc},
    uaccess::UserSliceReader,
};

use crate::{
    defs::*, node::Node, process::Process, thread::Thread, BinderReturnWriter, DArc, DLArc,
    DTRWrap, DeliverToRead,
};

use core::mem;

#[derive(Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
pub(crate) struct FreezeCookie(u64);

/// Represents a listener for changes to the frozen state of a process.
pub(crate) struct FreezeListener {
    /// The node we are listening for.
    pub(crate) node: DArc<Node>,
    /// The cookie of this freeze listener.
    cookie: FreezeCookie,
    /// What value of `is_frozen` did we most recently tell userspace about?
    last_is_frozen: Option<bool>,
    /// We sent a `BR_FROZEN_BINDER` and we are waiting for `BC_FREEZE_NOTIFICATION_DONE` before
    /// sending any other commands.
    is_pending: bool,
    /// Userspace sent `BC_CLEAR_FREEZE_NOTIFICATION` and we need to reply with
    /// `BR_CLEAR_FREEZE_NOTIFICATION_DONE` as soon as possible. If `is_pending` is set, then we
    /// must wait for it to be unset before we can reply.
    is_clearing: bool,
}

type UninitFM = UniqueArc<core::mem::MaybeUninit<DTRWrap<FreezeMessage>>>;

/// Represents a notification that the freeze state has changed.
pub(crate) struct FreezeMessage {
    cookie: FreezeCookie,
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for FreezeMessage {
        untracked;
    }
}

impl FreezeMessage {
    fn new(ua: UninitFM, cookie: FreezeCookie) -> DLArc<FreezeMessage> {
        match ua.pin_init_with(DTRWrap::new(FreezeMessage { cookie })) {
            Ok(msg) => ListArc::from(msg),
            Err(err) => match err {},
        }
    }
}

impl DeliverToRead for FreezeMessage {
    fn do_work(
        self: DArc<Self>,
        thread: &Thread,
        writer: &mut BinderReturnWriter<'_>,
    ) -> Result<bool> {
        let _removed_listener;
        let mut node_refs = thread.process.node_refs.lock();
        let Some(mut freeze_entry) = node_refs.freeze_listeners.find_mut(&self.cookie) else {
            return Ok(true);
        };
        let freeze = freeze_entry.get_mut();

        if freeze.is_pending {
            return Ok(true);
        }
        if freeze.is_clearing {
            _removed_listener = freeze_entry.remove_node();
            drop(node_refs);
            writer.write_code(BR_CLEAR_FREEZE_NOTIFICATION_DONE)?;
            writer.write_payload(&self.cookie.0)?;
            Ok(true)
        } else {
            let is_frozen = freeze.node.owner.inner.lock().is_frozen;
            if freeze.last_is_frozen == Some(is_frozen) {
                return Ok(true);
            }

            let mut state_info = BinderFrozenStateInfo::default();
            state_info.is_frozen = is_frozen as u32;
            state_info.cookie = freeze.cookie.0;
            freeze.is_pending = true;
            freeze.last_is_frozen = Some(is_frozen);
            drop(node_refs);

            writer.write_code(BR_FROZEN_BINDER)?;
            writer.write_payload(&state_info)?;
            // BR_FROZEN_BINDER notifications can cause transactions
            Ok(false)
        }
    }

    fn cancel(self: DArc<Self>) {}
    fn on_thread_selected(&self, _thread: &Thread) {}

    fn should_sync_wakeup(&self) -> bool {
        false
    }

    #[inline(never)]
    fn debug_print(&self, m: &SeqFile, prefix: &str, _tprefix: &str) -> Result<()> {
        seq_print!(m, "{}has frozen binder\n", prefix);
        Ok(())
    }
}

impl FreezeListener {
    pub(crate) fn on_process_exit(&self, proc: &Arc<Process>) {
        if !self.is_clearing {
            self.node.remove_freeze_listener(proc);
        }
    }
}

impl Process {
    pub(crate) fn request_freeze(self: &Arc<Self>, reader: &mut UserSliceReader) -> Result<()> {
        let hc = reader.read::<BinderHandleCookie>()?;
        let handle = hc.handle;
        let cookie = FreezeCookie(hc.cookie);

        let msg = UniqueArc::new_uninit(GFP_KERNEL)?;
        let alloc = RBTreeNodeReservation::new(GFP_KERNEL)?;

        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let listener_entry = match node_refs.freeze_listeners.entry(cookie) {
            rbtree::Entry::Vacant(entry) => entry,
            rbtree::Entry::Occupied(_) => {
                pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION duplicate cookie\n");
                return Err(EINVAL);
            }
        };
        let Some(info) = node_refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION invalid ref {}\n", handle);
            return Err(EINVAL);
        };
        if info.freeze().is_some() {
            pr_warn!("BC_REQUEST_FREEZE_NOTIFICATION already set\n");
            return Err(EINVAL);
        }
        let node_ref = info.node_ref();
        node_ref.node.add_freeze_listener(self, GFP_KERNEL)?;

        // From now on we added it to the node's list, so we can't fail.
        let msg = FreezeMessage::new(msg, cookie);
        listener_entry.insert(
            FreezeListener {
                cookie,
                node: node_ref.node.clone(),
                last_is_frozen: None,
                is_pending: false,
                is_clearing: false,
            },
            alloc,
        );
        *info.freeze() = Some(cookie);
        drop(node_refs_guard);
        let _ = self.push_work(msg);
        Ok(())
    }

    pub(crate) fn freeze_done(self: &Arc<Self>, reader: &mut UserSliceReader) -> Result<()> {
        let cookie = FreezeCookie(reader.read()?);
        let alloc = UniqueArc::new_uninit(GFP_KERNEL)?;
        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let Some(freeze) = node_refs.freeze_listeners.get_mut(&cookie) else {
            pr_warn!("BC_FREEZE_NOTIFICATION_DONE {:016x} not found\n", cookie.0);
            return Err(EINVAL);
        };
        if !freeze.is_pending {
            pr_warn!(
                "BC_FREEZE_NOTIFICATION_DONE {:016x} not pending\n",
                cookie.0
            );
            return Err(EINVAL);
        }
        let mut clear_msg = None;
        if freeze.is_clearing {
            // Immediately send another FreezeMessage for BR_CLEAR_FREEZE_NOTIFICATION_DONE.
            clear_msg = Some(FreezeMessage::new(alloc, cookie));
        }
        freeze.is_pending = false;
        drop(node_refs_guard);
        if let Some(clear_msg) = clear_msg {
            let _ = self.push_work(clear_msg);
        }
        Ok(())
    }

    pub(crate) fn clear_freeze(self: &Arc<Self>, reader: &mut UserSliceReader) -> Result<()> {
        let hc = reader.read::<BinderHandleCookie>()?;
        let handle = hc.handle;
        let cookie = FreezeCookie(hc.cookie);

        let alloc = UniqueArc::new_uninit(GFP_KERNEL)?;
        let mut node_refs_guard = self.node_refs.lock();
        let node_refs = &mut *node_refs_guard;
        let Some(info) = node_refs.by_handle.get_mut(&handle) else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION invalid ref {}\n", handle);
            return Err(EINVAL);
        };
        let Some(info_cookie) = info.freeze() else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION freeze notification not active\n");
            return Err(EINVAL);
        };
        if *info_cookie != cookie {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION freeze notification cookie mismatch\n");
            return Err(EINVAL);
        }
        let Some(listener) = node_refs.freeze_listeners.get_mut(&cookie) else {
            pr_warn!("BC_CLEAR_FREEZE_NOTIFICATION invalid cookie {}\n", handle);
            return Err(EINVAL);
        };
        listener.is_clearing = true;
        listener.node.remove_freeze_listener(self);
        *info.freeze() = None;
        let mut msg = None;
        if !listener.is_pending {
            msg = Some(FreezeMessage::new(alloc, cookie));
        }
        drop(node_refs_guard);

        if let Some(msg) = msg {
            let _ = self.push_work(msg);
        }
        Ok(())
    }

    fn get_freeze_cookie(&self, node: &DArc<Node>) -> Option<FreezeCookie> {
        let node_refs = &mut *self.node_refs.lock();
        let handle = node_refs.by_node.get(&node.global_id())?;
        let node_ref = node_refs.by_handle.get_mut(handle)?;
        *node_ref.freeze()
    }

    /// Prepare allocations for sending freeze messages.
    pub(crate) fn prepare_freeze_messages(&self) -> Result<FreezeMessages, AllocError> {
        let mut batch = KVVec::with_capacity(8, GFP_KERNEL)?;
        let mut procs = KVVec::<Arc<Process>>::with_capacity(8, GFP_KERNEL)?;

        let mut inner = self.inner.lock();
        let mut nodes = mem::take(&mut inner.nodes);
        let mut curr = nodes.cursor_front();
        while let Some(cursor) = curr {
            let (key, node) = cursor.current();
            let key = *key;
            let list = node.freeze_list(&inner);
            let len = list.len();

            if procs.capacity() < len {
                inner.nodes = nodes;
                drop(inner);
                procs.reserve(len, GFP_KERNEL)?;
                inner = self.inner.lock();
                nodes = mem::take(&mut inner.nodes);
                curr = nodes.cursor_lower_bound(&key);
            } else if len != 0 {
                for proc in list {
                    procs
                        .push_within_capacity(proc.clone())
                        .map_err(|_| AllocError)?;
                }
                let node = node.clone();
                inner.nodes = nodes;
                drop(inner);
                batch.reserve(procs.len(), GFP_KERNEL)?;
                for proc in procs.drain_all() {
                    let msg_alloc = UniqueArc::new_uninit(GFP_KERNEL)?;
                    let Some(cookie) = proc.get_freeze_cookie(&node) else {
                        continue;
                    };
                    let msg = FreezeMessage::new(msg_alloc, cookie);
                    batch.push((proc, msg), GFP_KERNEL)?;
                }
                drop(node);
                inner = self.inner.lock();
                nodes = mem::take(&mut inner.nodes);
                curr = key
                    .checked_add(1)
                    .and_then(|kp1| nodes.cursor_lower_bound(&kp1));
            } else {
                curr = cursor.move_next();
            }
        }
        inner.nodes = nodes;
        Ok(FreezeMessages { batch })
    }
}

pub(crate) struct FreezeMessages {
    batch: KVVec<(Arc<Process>, DLArc<FreezeMessage>)>,
}

impl FreezeMessages {
    pub(crate) fn send_messages(self) {
        for (proc, msg) in self.batch {
            let _ = proc.push_work(msg);
        }
    }
}
