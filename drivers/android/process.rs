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
    sync::{Arc, ArcBorrow, Guard, SpinLock},
    task::Task,
    types::ARef,
    types::Either,
    user_ptr::{UserSlicePtr, UserSlicePtrReader},
    workqueue::{self, Work},
};

use crate::{
    context::Context, defs::*, error::BinderError, thread::Thread, DeliverToRead,
    DeliverToReadListAdapter,
};

use core::mem::take;

const PROC_DEFER_FLUSH: u8 = 1;
const PROC_DEFER_RELEASE: u8 = 2;

/// The fields of `Process` protected by the spinlock.
pub(crate) struct ProcessInner {
    pub(crate) is_dead: bool,
    threads: RBTree<i32, Arc<Thread>>,
    ready_threads: List<Arc<Thread>>,
    work: List<DeliverToReadListAdapter>,

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
            is_dead: false,
            threads: RBTree::new(),
            ready_threads: List::new(),
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
    #[allow(dead_code)]
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

    fn register_thread(&mut self) -> bool {
        if self.requested_thread_count == 0 {
            return false;
        }

        self.requested_thread_count -= 1;
        self.started_thread_count += 1;
        true
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

    // Work node for deferred work item.
    #[pin]
    defer_work: Work<Arc<Process>>,

    // Links for process list in Context.
    links: Links<Process>,
}

kernel::impl_has_work! {
    impl HasWork<Arc<Process>> for Process { self.defer_work }
}

impl GetLinks for Process {
    type EntryType = Process;
    fn get_links(data: &Process) -> &Links<Process> {
        &data.links
    }
}

impl workqueue::ArcWorkItem for Process {
    fn run(self: Arc<Self>) {
        let defer;
        {
            let mut inner = self.inner.lock();
            defer = inner.defer_work;
            inner.defer_work = 0;
        }

        if defer & PROC_DEFER_FLUSH != 0 {
            self.deferred_flush();
        }
        if defer & PROC_DEFER_RELEASE != 0 {
            self.deferred_release();
        }
    }
}

impl Process {
    fn new(ctx: Arc<Context>, cred: ARef<Credential>) -> Result<Arc<Self>> {
        let process = Arc::pin_init(pin_init!(Process {
            ctx,
            cred,
            inner <- kernel::new_spinlock!(ProcessInner::new(), "Process::inner"),
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
        {
            let mut inner = self.inner.lock();
            inner.is_dead = true;
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
