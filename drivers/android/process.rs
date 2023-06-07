// SPDX-License-Identifier: GPL-2.0

//! This module defines the `Process` type, which represents a process using a particular binder
//! context.
//!
//! The `Process` object keeps track of all of the resources that this process owns in the binder
//! context.
//!
//! There is one `Process` object for each binder fd that a process has opened, so processes using
//! several binder contexts have several `Process` objects. This ensures that the contexts are
//! fully separated.

use kernel::{
    bindings,
    cred::Credential,
    file::{self, File, IoctlCommand, IoctlHandler, PollTable},
    io_buffer::{IoBufferReader, IoBufferWriter},
    linked_list::{GetLinks, Links, List},
    mm,
    prelude::*,
    rbtree::RBTree,
    sync::{Arc, ArcBorrow, Guard, SpinLock, Mutex},
    task::Task,
    types::{ARef, Either},
    user_ptr::{UserSlicePtr, UserSlicePtrReader},
    workqueue::{self, Work},
};

use crate::{
    context::Context,
    defs::*,
    error::BinderError,
    node::{Node, NodeRef},
    thread::Thread,
    DeliverToReadListAdapter,
    DeliverToRead,
};

use core::mem::take;

const PROC_DEFER_FLUSH: u8 = 1;
const PROC_DEFER_RELEASE: u8 = 2;

/// The fields of `Process` protected by the spinlock.
pub(crate) struct ProcessInner {
    is_manager: bool,
    pub(crate) is_dead: bool,
    threads: RBTree<i32, Arc<Thread>>,
    ready_threads: List<Arc<Thread>>,
    work: List<DeliverToReadListAdapter>,
    nodes: RBTree<usize, Arc<Node>>,

    /// The number of requested threads that haven't registered yet.
    requested_thread_count: u32,
    /// The maximum number of threads used by the process thread pool.
    max_threads: u32,
    /// The number of threads the started and registered with the thread pool.
    started_thread_count: u32,

    /// Bitmap of deferred work to do.
    defer_work: u8,
}

impl ProcessInner {
    fn new() -> Self {
        Self {
            is_manager: false,
            is_dead: false,
            threads: RBTree::new(),
            ready_threads: List::new(),
            nodes: RBTree::new(),
            work: List::new(),
            requested_thread_count: 0,
            max_threads: 0,
            started_thread_count: 0,
            defer_work: 0,
        }
    }

    /// Schedule the work item for execution on this process.
    ///
    /// If any threads are ready for work, then the work item is given directly to that thread and
    /// it is woken up. Otherwise, it is pushed to the process work list.
    ///
    /// This call can fail only if the process is dead. In this case, the work item is returned to
    /// the caller so that the caller can drop it after releasing the inner process lock. This is
    /// necessary since the destructor of `Transaction` will take locks that can't necessarily be
    /// taken while holding the inner process lock.
    pub(crate) fn push_work(
        &mut self,
        work: Arc<dyn DeliverToRead>,
    ) -> Result<(), (BinderError, Arc<dyn DeliverToRead>)> {
        // Try to find a ready thread to which to push the work.
        if let Some(thread) = self.ready_threads.pop_front() {
            // Push to thread while holding state lock. This prevents the thread from giving up
            // (for example, because of a signal) when we're about to deliver work.
            if !thread.push_work(work) {
                // We failed to push to the list since the work item is already in another
                // list. In this case, we put the thread back into the list of ready threads.
                // We don't return an error since the work item being in another list means
                // that it will be executed soon regardless.
                //
                // We don't need to worry about the destructor of the work item, because
                // `push_work` can only fail in this way when the work item is an `Arc<Node>`,
                // which doesn't do anything in its destructor.
                //
                // TODO: Make this more robust so we don't rely on the destructor always being
                // safe to run while holding a mutex.
                self.ready_threads.push_back(thread);
            }
            Ok(())
        } else if self.is_dead {
            Err((BinderError::new_dead(), work))
        } else {
            let sync = work.should_sync_wakeup();
            // There are no ready threads. Push work to process queue.
            self.work.push_back(work);
            // Wake up polling threads, if any.
            for thread in self.threads.values() {
                thread.notify_if_poll_ready(sync);
            }
            Ok(())
        }
    }

    pub(crate) fn remove_node(&mut self, ptr: usize) {
        self.nodes.remove(&ptr);
    }

    /// Updates the reference count on the given node.
    pub(crate) fn update_node_refcount(
        &mut self,
        node: &Arc<Node>,
        inc: bool,
        strong: bool,
        count: usize,
        othread: Option<&Thread>,
    ) {
        let push = node.update_refcount_locked(inc, strong, count, self);

        // If we decided that we need to push work, push either to the process or to a thread if
        // one is specified.
        if push {
            if let Some(thread) = othread {
                thread.push_work_deferred(node.clone());
            } else {
                let _ = self.push_work(node.clone());
                // Nothing to do: `push_work` may fail if the process is dead, but that's ok as in
                // that case, it doesn't care about the notification.
            }
        }
    }

    pub(crate) fn new_node_ref(
        &mut self,
        node: Arc<Node>,
        strong: bool,
        thread: Option<&Thread>,
    ) -> NodeRef {
        self.update_node_refcount(&node, true, strong, 1, thread);
        let strong_count = if strong { 1 } else { 0 };
        NodeRef::new(node, strong_count, 1 - strong_count)
    }

    /// Returns an existing node with the given pointer and cookie, if one exists.
    ///
    /// Returns an error if a node with the given pointer but a different cookie exists.
    fn get_existing_node(&self, ptr: usize, cookie: usize) -> Result<Option<Arc<Node>>> {
        match self.nodes.get(&ptr) {
            None => Ok(None),
            Some(node) => {
                let (_, node_cookie) = node.get_id();
                if node_cookie == cookie {
                    Ok(Some(node.clone()))
                } else {
                    Err(EINVAL)
                }
            }
        }
    }

    /// Returns a reference to an existing node with the given pointer and cookie. It requires a
    /// mutable reference because it needs to increment the ref count on the node, which may
    /// require pushing work to the work queue (to notify userspace of 0 to 1 transitions).
    fn get_existing_node_ref(
        &mut self,
        ptr: usize,
        cookie: usize,
        strong: bool,
        thread: Option<&Thread>,
    ) -> Result<Option<NodeRef>> {
        Ok(self
            .get_existing_node(ptr, cookie)?
            .map(|node| self.new_node_ref(node, strong, thread)))
    }

    fn register_thread(&mut self) -> bool {
        if self.requested_thread_count == 0 {
            return false;
        }

        self.requested_thread_count -= 1;
        self.started_thread_count += 1;
        true
    }
}

struct NodeRefInfo {
    node_ref: NodeRef,
}

impl NodeRefInfo {
    fn new(node_ref: NodeRef) -> Self {
        Self {
            node_ref,
        }
    }
}

struct ProcessNodeRefs {
    by_handle: RBTree<u32, NodeRefInfo>,
    by_global_id: RBTree<u64, u32>,
}

impl ProcessNodeRefs {
    fn new() -> Self {
        Self {
            by_handle: RBTree::new(),
            by_global_id: RBTree::new(),
        }
    }
}

/// A process using binder.
///
/// Strictly speaking, there can be multiple of these per process. There is one for each binder fd
/// that a process has opened, so processes using several binder contexts have several `Process`
/// objects. This ensures that the contexts are fully separated.
#[pin_data]
pub(crate) struct Process {
    pub(crate) ctx: Arc<Context>,

    // The task leader (process).
    pub(crate) task: ARef<Task>,

    // Credential associated with file when `Process` is created.
    pub(crate) cred: ARef<Credential>,

    #[pin]
    pub(crate) inner: SpinLock<ProcessInner>,

    // Node references are in a different lock to avoid recursive acquisition when
    // incrementing/decrementing a node in another process.
    #[pin]
    node_refs: Mutex<ProcessNodeRefs>,

    // Work node for deferred work item.
    #[pin]
    defer_work: Work<Process>,

    // Links for process list in Context.
    links: Links<Process>,
}

kernel::impl_has_work! {
    impl HasWork<Process> for Process { self.defer_work }
}

impl GetLinks for Process {
    type EntryType = Process;
    fn get_links(data: &Process) -> &Links<Process> {
        &data.links
    }
}

impl workqueue::WorkItem for Process {
    type Pointer = Arc<Process>;

    fn run(me: Arc<Self>) {
        let defer;
        {
            let mut inner = me.inner.lock();
            defer = inner.defer_work;
            inner.defer_work = 0;
        }

        if defer & PROC_DEFER_FLUSH != 0 {
            me.deferred_flush();
        }
        if defer & PROC_DEFER_RELEASE != 0 {
            me.deferred_release();
        }
    }
}

impl Process {
    fn new(ctx: Arc<Context>, cred: ARef<Credential>) -> Result<Arc<Self>> {
        let process = Arc::pin_init(pin_init!(Process {
            ctx,
            cred,
            inner <- kernel::new_spinlock!(ProcessInner::new(), "Process::inner"),
            node_refs <- kernel::new_mutex!(ProcessNodeRefs::new(), "Process::node_refs"),
            task: kernel::current!().group_leader().into(),
            defer_work <- Work::new(),
            links: Links::new(),
        }))?;

        process.ctx.register_process(process.clone());

        Ok(process)
    }

    /// Attempts to fetch a work item from the process queue.
    pub(crate) fn get_work(&self) -> Option<Arc<dyn DeliverToRead>> {
        self.inner.lock().work.pop_front()
    }

    /// Attempts to fetch a work item from the process queue. If none is available, it registers the
    /// given thread as ready to receive work directly.
    ///
    /// This must only be called when the thread is not participating in a transaction chain; when
    /// it is, work will always be delivered directly to the thread (and not through the process
    /// queue).
    pub(crate) fn get_work_or_register<'a>(
        &'a self,
        thread: &'a Arc<Thread>,
    ) -> Either<Arc<dyn DeliverToRead>, Registration<'a>> {
        let mut inner = self.inner.lock();
        // Try to get work from the process queue.
        if let Some(work) = inner.work.pop_front() {
            return Either::Left(work);
        }

        // Register the thread as ready.
        Either::Right(Registration::new(self, thread, &mut inner))
    }

    fn get_thread(self: ArcBorrow<'_, Self>, id: i32) -> Result<Arc<Thread>> {
        {
            let inner = self.inner.lock();
            if let Some(thread) = inner.threads.get(&id) {
                return Ok(thread.clone());
            }
        }

        // Allocate a new `Thread` without holding any locks.
        let ta = Thread::new(id, self.into())?;
        let node = RBTree::try_allocate_node(id, ta.clone())?;

        let mut inner = self.inner.lock();

        // Recheck. It's possible the thread was created while we were not holding the lock.
        if let Some(thread) = inner.threads.get(&id) {
            return Ok(thread.clone());
        }

        inner.threads.insert(node);
        Ok(ta)
    }

    fn set_as_manager(
        self: ArcBorrow<'_, Self>,
        info: Option<FlatBinderObject>,
        thread: &Thread,
    ) -> Result {
        let (ptr, cookie, flags) = if let Some(obj) = info {
            (
                // SAFETY: The object type for this ioctl is implicitly `BINDER_TYPE_BINDER`, so it
                // is safe to access the `binder` field.
                unsafe { obj.__bindgen_anon_1.binder },
                obj.cookie,
                obj.flags,
            )
        } else {
            (0, 0, 0)
        };
        let node_ref = self.get_node(ptr as _, cookie as _, flags as _, true, Some(thread))?;
        let node = node_ref.node.clone();
        self.ctx.set_manager_node(node_ref)?;
        self.inner.lock().is_manager = true;

        // Force the state of the node to prevent the delivery of acquire/increfs.
        let mut owner_inner = node.owner.inner.lock();
        node.force_has_count(&mut owner_inner);
        Ok(())
    }

    pub(crate) fn get_node(
        self: ArcBorrow<'_, Self>,
        ptr: usize,
        cookie: usize,
        flags: u32,
        strong: bool,
        thread: Option<&Thread>,
    ) -> Result<NodeRef> {
        // Try to find an existing node.
        {
            let mut inner = self.inner.lock();
            if let Some(node) = inner.get_existing_node_ref(ptr, cookie, strong, thread)? {
                return Ok(node);
            }
        }

        // Allocate the node before reacquiring the lock.
        let node = Arc::try_new(Node::new(ptr, cookie, flags, self.into()))?;
        let rbnode = RBTree::try_allocate_node(ptr, node.clone())?;
        let mut inner = self.inner.lock();
        if let Some(node) = inner.get_existing_node_ref(ptr, cookie, strong, thread)? {
            return Ok(node);
        }

        inner.nodes.insert(rbnode);
        Ok(inner.new_node_ref(node, strong, thread))
    }

    pub(crate) fn insert_or_update_handle(
        &self,
        node_ref: NodeRef,
        is_mananger: bool,
    ) -> Result<u32> {
        {
            let mut refs = self.node_refs.lock();

            // Do a lookup before inserting.
            if let Some(handle_ref) = refs.by_global_id.get(&node_ref.node.global_id) {
                let handle = *handle_ref;
                let info = refs.by_handle.get_mut(&handle).unwrap();
                info.node_ref.absorb(node_ref);
                return Ok(handle);
            }
        }

        // Reserve memory for tree nodes.
        let reserve1 = RBTree::try_reserve_node()?;
        let reserve2 = RBTree::try_reserve_node()?;

        let mut refs = self.node_refs.lock();

        // Do a lookup again as node may have been inserted before the lock was reacquired.
        if let Some(handle_ref) = refs.by_global_id.get(&node_ref.node.global_id) {
            let handle = *handle_ref;
            let info = refs.by_handle.get_mut(&handle).unwrap();
            info.node_ref.absorb(node_ref);
            return Ok(handle);
        }

        // Find id.
        let mut target: u32 = if is_mananger { 0 } else { 1 };
        for handle in refs.by_handle.keys() {
            if *handle > target {
                break;
            }
            if *handle == target {
                target = target.checked_add(1).ok_or(ENOMEM)?;
            }
        }

        // Ensure the process is still alive while we insert a new reference.
        let inner = self.inner.lock();
        if inner.is_dead {
            return Err(ESRCH);
        }
        refs.by_global_id
            .insert(reserve1.into_node(node_ref.node.global_id, target));
        refs.by_handle
            .insert(reserve2.into_node(target, NodeRefInfo::new(node_ref)));
        Ok(target)
    }

    pub(crate) fn get_node_from_handle(&self, handle: u32, strong: bool) -> Result<NodeRef> {
        self.node_refs
            .lock()
            .by_handle
            .get(&handle)
            .ok_or(ENOENT)?
            .node_ref
            .clone(strong)
    }

    pub(crate) fn update_ref(&self, handle: u32, inc: bool, strong: bool) -> Result {
        if inc && handle == 0 {
            if let Ok(node_ref) = self.ctx.get_manager_node(strong) {
                if core::ptr::eq(self, &*node_ref.node.owner) {
                    return Err(EINVAL);
                }
                let _ = self.insert_or_update_handle(node_ref, true);
                return Ok(());
            }
        }

        // To preserve original binder behaviour, we only fail requests where the manager tries to
        // increment references on itself.
        let mut refs = self.node_refs.lock();
        if let Some(info) = refs.by_handle.get_mut(&handle) {
            if info.node_ref.update(inc, strong) {
                // Remove reference from process tables.
                let id = info.node_ref.node.global_id;
                refs.by_handle.remove(&handle);
                refs.by_global_id.remove(&id);
            }
        }
        Ok(())
    }

    pub(crate) fn inc_ref_done(&self, reader: &mut UserSlicePtrReader, strong: bool) -> Result {
        let ptr = reader.read::<usize>()?;
        let cookie = reader.read::<usize>()?;
        let mut inner = self.inner.lock();
        if let Ok(Some(node)) = inner.get_existing_node(ptr, cookie) {
            if node.inc_ref_done_locked(strong, &mut inner) {
                let _ = inner.push_work(node);
            }
        }
        Ok(())
    }

    fn version(&self, data: UserSlicePtr) -> Result {
        data.writer().write(&BinderVersion::current())
    }

    pub(crate) fn register_thread(&self) -> bool {
        self.inner.lock().register_thread()
    }

    fn remove_thread(&self, thread: Arc<Thread>) {
        self.inner.lock().threads.remove(&thread.id);
        thread.release();
    }

    fn set_max_threads(&self, max: u32) {
        self.inner.lock().max_threads = max;
    }

    fn get_node_debug_info(&self, data: UserSlicePtr) -> Result {
        let (mut reader, mut writer) = data.reader_writer();

        // Read the starting point.
        let ptr = reader.read::<BinderNodeDebugInfo>()?.ptr as usize;
        let mut out = BinderNodeDebugInfo::default();

        {
            let inner = self.inner.lock();
            for (node_ptr, node) in &inner.nodes {
                if *node_ptr > ptr {
                    node.populate_debug_info(&mut out, &inner);
                    break;
                }
            }
        }

        writer.write(&out)
    }

    fn get_node_info_from_ref(&self, data: UserSlicePtr) -> Result {
        let (mut reader, mut writer) = data.reader_writer();
        let mut out = reader.read::<BinderNodeInfoForRef>()?;

        if out.strong_count != 0
            || out.weak_count != 0
            || out.reserved1 != 0
            || out.reserved2 != 0
            || out.reserved3 != 0
        {
            return Err(EINVAL);
        }

        // Only the context manager is allowed to use this ioctl.
        if !self.inner.lock().is_manager {
            return Err(EPERM);
        }

        let node_ref = self
            .get_node_from_handle(out.handle, true)
            .or(Err(EINVAL))?;
        // Get the counts from the node.
        {
            let owner_inner = node_ref.node.owner.inner.lock();
            node_ref.node.populate_counts(&mut out, &owner_inner);
        }

        // Write the result back.
        writer.write(&out)
    }

    pub(crate) fn needs_thread(&self) -> bool {
        let mut inner = self.inner.lock();
        let ret =
            inner.requested_thread_count == 0 && inner.started_thread_count < inner.max_threads;
        if ret {
            inner.requested_thread_count += 1
        }
        ret
    }

    fn deferred_flush(&self) {
        let inner = self.inner.lock();
        for thread in inner.threads.values() {
            thread.exit_looper();
        }
    }

    fn deferred_release(self: Arc<Self>) {
        let is_manager = {
            let mut inner = self.inner.lock();
            inner.is_dead = true;
            inner.is_manager
        };

        if is_manager {
            self.ctx.unset_manager_node();
        }

        self.ctx.deregister_process(&self);

        // Move the threads out of `inner` so that we can iterate over them without holding the
        // lock.
        let mut inner = self.inner.lock();
        let threads = take(&mut inner.threads);
        drop(inner);

        // Release all threads.
        for thread in threads.values() {
            thread.release();
        }
    }

    pub(crate) fn flush(this: ArcBorrow<'_, Process>) -> Result {
        let should_schedule;
        {
            let mut inner = this.inner.lock();
            should_schedule = inner.defer_work == 0;
            inner.defer_work |= PROC_DEFER_FLUSH;
        }

        if should_schedule {
            // Ignore failures to schedule to the workqueue. Those just mean that we're already
            // scheduled for execution.
            let _ = workqueue::system().enqueue(Arc::from(this));
        }
        Ok(())
    }
}

impl IoctlHandler for Process {
    type Target<'a> = ArcBorrow<'a, Process>;

    fn write(
        this: ArcBorrow<'_, Process>,
        _file: &File,
        cmd: u32,
        reader: &mut UserSlicePtrReader,
    ) -> Result<i32> {
        let thread = this.get_thread(kernel::current!().pid())?;
        match cmd {
            bindings::BINDER_SET_MAX_THREADS => this.set_max_threads(reader.read()?),
            bindings::BINDER_THREAD_EXIT => this.remove_thread(thread),
            bindings::BINDER_SET_CONTEXT_MGR => this.set_as_manager(None, &thread)?,
            bindings::BINDER_SET_CONTEXT_MGR_EXT => {
                this.set_as_manager(Some(reader.read()?), &thread)?
            }
            _ => return Err(EINVAL),
        }
        Ok(0)
    }

    fn read_write(
        this: ArcBorrow<'_, Process>,
        file: &File,
        cmd: u32,
        data: UserSlicePtr,
    ) -> Result<i32> {
        let thread = this.get_thread(kernel::current!().pid())?;
        let blocking = (file.flags() & file::flags::O_NONBLOCK) == 0;
        match cmd {
            bindings::BINDER_WRITE_READ => thread.write_read(data, blocking)?,
            bindings::BINDER_GET_NODE_DEBUG_INFO => this.get_node_debug_info(data)?,
            bindings::BINDER_GET_NODE_INFO_FOR_REF => this.get_node_info_from_ref(data)?,
            bindings::BINDER_VERSION => this.version(data)?,
            bindings::BINDER_GET_EXTENDED_ERROR => thread.get_extended_error(data)?,
            _ => return Err(EINVAL),
        }
        Ok(0)
    }
}

#[vtable]
impl file::Operations for Process {
    type Data = Arc<Self>;
    type OpenData = Arc<Context>;

    fn open(ctx: &Arc<Context>, file: &File) -> Result<Self::Data> {
        Self::new(ctx.clone(), ARef::from(file.cred()))
    }

    fn release(this: Self::Data, _file: &File) {
        let should_schedule;
        {
            let mut inner = this.inner.lock();
            should_schedule = inner.defer_work == 0;
            inner.defer_work |= PROC_DEFER_RELEASE;
        }

        if should_schedule {
            // Ignore failures to schedule to the workqueue. Those just mean that we're already
            // scheduled for execution.
            let _ = workqueue::system().enqueue(this);
        }
    }

    fn ioctl(this: ArcBorrow<'_, Process>, file: &File, cmd: &mut IoctlCommand) -> Result<i32> {
        cmd.dispatch::<Self>(this, file)
    }

    fn compat_ioctl(
        this: ArcBorrow<'_, Process>,
        file: &File,
        cmd: &mut IoctlCommand,
    ) -> Result<i32> {
        cmd.dispatch::<Self>(this, file)
    }

    fn mmap(_this: ArcBorrow<'_, Process>, _file: &File, _vma: &mut mm::virt::Area) -> Result {
        Err(EINVAL)
    }

    fn poll(_this: ArcBorrow<'_, Process>, _file: &File, _table: &PollTable) -> Result<u32> {
        Err(EINVAL)
    }
}

/// Represents that a thread has registered with the `ready_threads` list of its process.
///
/// The destructor of this type will unregister the thread from the list of ready threads.
pub(crate) struct Registration<'a> {
    process: &'a Process,
    thread: &'a Arc<Thread>,
}

impl<'a> Registration<'a> {
    fn new(
        process: &'a Process,
        thread: &'a Arc<Thread>,
        guard: &mut Guard<'_, ProcessInner, kernel::sync::SpinLockBackend>,
    ) -> Self {
        assert!(core::ptr::eq(process, &*thread.process));
        // INVARIANT: We are pushing this thread to the right `ready_threads` list.
        assert!(guard.ready_threads.push_back(thread.clone()));
        Self { process, thread }
    }
}

impl Drop for Registration<'_> {
    fn drop(&mut self) {
        let mut inner = self.process.inner.lock();
        // SAFETY: The thread has the invariant that we never push it to any other linked list than
        // the `ready_threads` list of its parent process. Therefore, the thread is either in that
        // list, or in no list.
        unsafe { inner.ready_threads.remove(self.thread) };
    }
}
