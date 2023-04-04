// SPDX-License-Identifier: GPL-2.0

use kernel::{
    bindings,
    linked_list::{GetLinks, Links, List},
    prelude::*,
    security,
    str::{CStr, CString},
    sync::{Arc, Guard, Mutex, MutexBackend},
};

use crate::{
    node::NodeRef,
    process::Process,
    thread::{BinderError, BinderResult},
};

use core::cell::UnsafeCell;
use core::mem::MaybeUninit;

// This type only exists to allow for lazy initialization of the mutex.
// TODO: Once `kernel::sync` has support for mutexes in globals, remove this type.
pub(crate) struct Contexts {
    inner: UnsafeCell<MaybeUninit<Mutex<ContextList>>>,
}

impl Contexts {
    pub(crate) fn init(&self) {
        unsafe {
            let ptr = self.inner.get() as *mut Mutex<ContextList>;
            let init = kernel::new_mutex!(ContextList { list: List::new() }, "ContextList");
            match init.__pinned_init(ptr) {
                Ok(()) => {}
                Err(e) => match e {},
            }
        }
    }

    fn lock(&self) -> Guard<'_, ContextList, MutexBackend> {
        // SAFETY: The `init` method is called during initialization of the binder module, so the
        // mutex is initialized at this point.
        unsafe {
            let ptr = self.inner.get() as *const Mutex<ContextList>;
            (&*ptr).lock()
        }
    }
}

unsafe impl Send for Contexts {}
unsafe impl Sync for Contexts {}

pub(crate) static CONTEXTS: Contexts = Contexts {
    inner: UnsafeCell::new(MaybeUninit::uninit()),
};

struct ContextList {
    list: List<Arc<Context>>,
}

#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl Send for ContextList {}
unsafe impl Sync for ContextList {}

pub(crate) fn get_all_contexts() -> Result<Vec<Arc<Context>>> {
    let mut lock = CONTEXTS.lock();

    // TODO: The current cursor API gives me a `&Context` rather than a `&Arc<Context>`, so I
    // can't call clone to get a `Arc<Context>` via a cursor.
    let mut ctxs = Vec::new();
    while let Some(cur) = lock.list.pop_front() {
        ctxs.try_push(cur)?;
    }
    for ctx in &ctxs {
        lock.list.push_back(ctx.clone());
    }
    Ok(ctxs)
}

struct Manager {
    node: Option<NodeRef>,
    uid: Option<bindings::kuid_t>,
    all_procs: List<Arc<Process>>,
}

/// There is one context per binder file (/dev/binder, /dev/hwbinder, etc)
#[pin_data]
pub(crate) struct Context {
    #[pin]
    manager: Mutex<Manager>,
    pub(crate) name: CString,
    links: Links<Context>,
}

#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl Send for Context {}
unsafe impl Sync for Context {}

impl GetLinks for Context {
    type EntryType = Context;
    fn get_links(data: &Context) -> &Links<Context> {
        &data.links
    }
}

impl Context {
    pub(crate) fn new(name: &CStr) -> Result<Arc<Self>> {
        let name = CString::try_from_cstr(name)?;
        let ctx = Arc::pin_init(pin_init!(Context {
            name,
            links: Links::new(),
            manager <- kernel::new_mutex!(Manager {
                all_procs: List::new(),
                node: None,
                uid: None,
            }, "Context::manager"),
        }))?;

        CONTEXTS.lock().list.push_back(ctx.clone());

        Ok(ctx)
    }

    /// Called when the file for this context is unlinked.
    ///
    /// No-op if called twice.
    pub(crate) fn deregister(self: &Arc<Self>) {
        // SAFETY: We never add the context to any other linked list than this one, so it is either
        // in this list, or not in any list.
        unsafe {
            CONTEXTS.lock().list.remove(self);
        }
    }

    pub(crate) fn register_process(self: &Arc<Self>, proc: Arc<Process>) {
        if !Arc::ptr_eq(self, &proc.ctx) {
            pr_err!("Context::register_process called on the wrong context.");
            return;
        }
        self.manager.lock().all_procs.push_back(proc);
    }

    pub(crate) fn deregister_process(self: &Arc<Self>, proc: &Arc<Process>) {
        if !Arc::ptr_eq(self, &proc.ctx) {
            pr_err!("Context::deregister_process called on the wrong context.");
            return;
        }
        // SAFETY: We just checked that this is the right list.
        unsafe {
            self.manager.lock().all_procs.remove(proc);
        }
    }

    pub(crate) fn get_all_procs(&self) -> Result<Vec<Arc<Process>>> {
        let mut lock = self.manager.lock();

        // TODO: The current cursor API gives me a `&Process` rather than a `&Arc<Process>`, so I
        // can't call clone to get a `Arc<Process>` via a cursor.
        let mut procs = Vec::new();
        while let Some(cur) = lock.all_procs.pop_front() {
            procs.try_push(cur)?;
        }
        for proc in &procs {
            lock.all_procs.push_back(proc.clone());
        }
        Ok(procs)
    }

    pub(crate) fn set_manager_node(&self, node_ref: NodeRef) -> Result {
        let mut manager = self.manager.lock();
        if manager.node.is_some() {
            pr_warn!("BINDER_SET_CONTEXT_MGR already set");
            return Err(EBUSY);
        }
        security::binder_set_context_mgr(&node_ref.node.owner.cred)?;

        // TODO: Get the actual caller id.
        let caller_uid = bindings::kuid_t::default();
        if let Some(ref uid) = manager.uid {
            if uid.val != caller_uid.val {
                return Err(EPERM);
            }
        }

        manager.node = Some(node_ref);
        manager.uid = Some(caller_uid);
        Ok(())
    }

    pub(crate) fn unset_manager_node(&self) {
        let node_ref = self.manager.lock().node.take();
        drop(node_ref);
    }

    pub(crate) fn get_manager_node(&self, strong: bool) -> BinderResult<NodeRef> {
        self.manager
            .lock()
            .node
            .as_ref()
            .ok_or_else(BinderError::new_dead)?
            .clone(strong)
    }

    pub(crate) fn for_each_proc<F>(&self, mut func: F)
    where
        F: FnMut(&Process),
    {
        let lock = self.manager.lock();

        let mut cursor = lock.all_procs.cursor_front();
        while let Some(proc) = cursor.current() {
            func(proc);
            cursor.move_next();
        }
    }

    pub(crate) fn get_procs_with_pid(&self, pid: i32) -> Result<Vec<Arc<Process>>> {
        let mut lock = self.manager.lock();

        // TODO: The current cursor API gives me a `&Process` rather than a `&Arc<Process>`, so I
        // can't call clone to get a `Arc<Process>` via a cursor.
        //
        // When the linked list is fixed, we can simplify this a lot.
        let mut procs = Vec::new();
        while let Some(cur) = lock.all_procs.pop_front() {
            match procs.try_push(cur) {
                Ok(()) => {}
                Err(err) => {
                    for proc in procs {
                        lock.all_procs.push_back(proc);
                    }
                    return Err(err.into());
                }
            }
        }
        for proc in &procs {
            lock.all_procs.push_back(proc.clone());
        }
        procs.retain(|proc| proc.task.pid() == pid);
        Ok(procs)
    }
}
