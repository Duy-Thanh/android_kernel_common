/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

extern "C" {
#define new extern_new
#include <lkl.h>
#include <lkl_host.h>
#include <lkl/linux/const.h>
#include <lkl/linux/ioctl.h>
#include <lkl/linux/android/binder.h>
#undef new
}

#include <queue>

#include <fuzzer/FuzzedDataProvider.h>

#define LOG(fmt, ...)                                                          \
	if (g_log_enabled) {                                                   \
		printf(fmt, ##__VA_ARGS__);                                    \
	}

static bool g_log_enabled = true;

static const char *g_binder_dev = "/dev/binder";

#define BINDER_VM_SIZE ((32 * 1024))
#define NUM_CLIENT 3
#define NUM_HANDLE 10

struct binder_context {
	int fd;
	int epoll_fd;
	void *mapped_ptr;
	size_t map_size;
	void *task;
	std::queue<uintptr_t> buffers;
};

struct binder_object {
	union {
		struct lkl_binder_object_header hdr;
		struct lkl_flat_binder_object fbo;
		struct lkl_binder_fd_object fdo;
		struct lkl_binder_buffer_object bbo;
		struct lkl_binder_fd_array_object fdao;
	};
};

static struct binder_context *open_binder(size_t size)
{
	struct binder_context *ctx;

	ctx = new struct binder_context;
	if (ctx == NULL) {
		LOG("malloc binder_context failed\n");
		return NULL;
	}

	ctx->fd =
		lkl_sys_open(g_binder_dev, O_RDWR | O_CLOEXEC | O_NONBLOCK, 0);
	if (ctx->fd == -1) {
		LOG("open binder device failed\n");
		goto fail_open;
	}

	ctx->epoll_fd = -1;
	ctx->map_size = size;
	ctx->mapped_ptr =
		lkl_sys_mmap(NULL, size, PROT_READ, MAP_PRIVATE, ctx->fd, 0);
	if (ctx->mapped_ptr == MAP_FAILED) {
		LOG("mmap binder device failed\n");
		goto fail_map;
	}

	ctx->task = lkl_get_task();

	return ctx;
fail_map:
	close(ctx->fd);
fail_open:
	delete ctx;
	return NULL;
}

static void close_binder(struct binder_context *ctx)
{
	lkl_sys_munmap((unsigned long)ctx->mapped_ptr, ctx->map_size);
	lkl_sys_close(ctx->fd);
	delete ctx;
}

static int check_binder_version()
{
	int ret;
	struct binder_context *ctx;
	struct lkl_binder_version vers = {};

	ctx = open_binder(1024 * 1024);
	if (ctx == NULL) {
		return -1;
	}

	ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_VERSION, (unsigned long)&vers);
	if (ret == -1) {
		LOG("ioctl BINDER_VERSION failed\n");
		return -1;
	}
	if (vers.protocol_version != LKL_BINDER_CURRENT_PROTOCOL_VERSION) {
		LOG("Binder version does not match\n");
		return -1;
	}

	close_binder(ctx);
	return 0;
}

static int write_binder(struct binder_context *ctx, uintptr_t buf, size_t size)
{
	int ret;
	struct lkl_binder_write_read bwr = {
		.write_size = size,
		.write_buffer = buf,
	};

	ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_WRITE_READ,
			    (unsigned long)&bwr);

	return ret;
}

static void process_transaction(struct binder_context *ctx, const void *data)
{
	struct lkl_binder_transaction_data *tr =
		(struct lkl_binder_transaction_data *)data;

	if (tr->data_size == 0)
		return;

	// Save buffer ptr for BC_FREE_BUFFER operations
	ctx->buffers.push(tr->data.ptr.buffer);
}

static void process_read_buffer(struct binder_context *ctx, uintptr_t buf,
				size_t size)
{
	uintptr_t buf_offset = (uintptr_t)buf;
	uintptr_t buf_end_offset = (uintptr_t)buf + size;

	while (buf_offset < buf_end_offset) {
		uint32_t cmd = *((uint32_t *)buf_offset);
		buf_offset += sizeof(uint32_t);
		switch (cmd) {
		case LKL_BR_ERROR: {
			int error = *((int *)buf_offset);
			buf_offset += sizeof(int);
			LOG("\tBR_ERROR %d\n", error);
		} break;
		case LKL_BR_OK:
			LOG("\tBR_OK\n");
			break;
		case LKL_BR_TRANSACTION_SEC_CTX:
			buf_offset += sizeof(
				struct lkl_binder_transaction_data_secctx);
			LOG("\tBR_TRANSACTION_SEC_CTX\n");
			break;
		case LKL_BR_TRANSACTION:
		case LKL_BR_REPLY:
			if (cmd == LKL_BR_TRANSACTION) {
				LOG("\tBR_TRANSACTION\n");
			} else {
				LOG("\tBR_REPLY\n");
			}
			process_transaction(ctx, (void *)buf_offset);
			buf_offset +=
				sizeof(struct lkl_binder_transaction_data);
			break;
		case LKL_BR_DEAD_REPLY:
			LOG("\tBR_DEAD_REPLY\n");
			break;
		case LKL_BR_TRANSACTION_COMPLETE:
			LOG("\tBR_TRANSACTION_COMPLETE\n");
			break;
		case LKL_BR_INCREFS: {
			void *ptr = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_INCREFS %p %p\n", ptr, cookie);
		} break;
		case LKL_BR_ACQUIRE: {
			void *ptr = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_ACQUIRE %p %p\n", ptr, cookie);
		} break;
		case LKL_BR_RELEASE: {
			void *ptr = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_RELEASE %p %p\n", ptr, cookie);
		} break;
		case LKL_BR_DECREFS: {
			void *ptr = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_DECREFS %p %p\n", ptr, cookie);
		} break;
		case LKL_BR_NOOP:
			LOG("\tBR_NOOP\n");
			break;
		case LKL_BR_SPAWN_LOOPER:
			LOG("\tBR_SPAWN_LOOPER\n");
			break;
		case LKL_BR_DEAD_BINDER: {
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_DEAD_BINDER %p\n", cookie);
		} break;
		case LKL_BR_CLEAR_DEATH_NOTIFICATION_DONE: {
			void *cookie = *((void **)buf_offset);
			buf_offset += sizeof(void *);
			LOG("\tBR_CLEAR_DEATH_NOTIFICATION_DONE %p\n", cookie);
		} break;
		case LKL_BR_FAILED_REPLY:
			LOG("\tBR_FAILED_REPLY\n");
			break;
		default:
			LOG("WARN: Unknown returned command\n");
			break;
		}
	}
}

static int read_binder(struct binder_context *ctx, size_t size)
{
	int ret;
	unsigned char buf[size];
	struct lkl_binder_write_read bwr = {
		.read_size = size,
		.read_buffer = (uintptr_t)buf,
	};

	ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_WRITE_READ,
			    (unsigned long)&bwr);

	process_read_buffer(ctx, (uintptr_t)buf, bwr.read_consumed);

	return ret;
}

#define COOKIE_A 0x987654321u
#define COOKIE_B 0x987654322u

static void binder_object_init(FuzzedDataProvider *fdp,
			       struct binder_object *bo, bool sg)
{
	unsigned char type;

	if (sg) {
		type = fdp->ConsumeIntegralInRange<uint8_t>(0, 5);
	} else {
		type = fdp->ConsumeIntegralInRange<uint8_t>(0, 4);
	}

	switch (type) {
	case 0: // BINDER_TYPE_BINDER;
		bo->fbo.hdr.type = LKL_BINDER_TYPE_BINDER;

		// Allow other clients to send BINDER_TYPE_FD object
		bo->fbo.flags = LKL_FLAT_BINDER_FLAG_ACCEPTS_FDS;

		// Context manager owns the default handle 0, so we can have
		// NUM_HANDLE binders and NUM_HANDLE + 1 handles.
		bo->fbo.binder =
			fdp->ConsumeIntegralInRange<uint8_t>(1, NUM_HANDLE);

		bo->fbo.cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
		break;
	case 1: // BINDER_TYPE_WEAK_BINDER
		bo->fbo.hdr.type = LKL_BINDER_TYPE_WEAK_BINDER;
		bo->fbo.flags = LKL_FLAT_BINDER_FLAG_ACCEPTS_FDS;
		bo->fbo.binder =
			fdp->ConsumeIntegralInRange<uint8_t>(1, NUM_HANDLE);
		bo->fbo.cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
		break;
	case 2: // BINDER_TYPE_HANDLE
		bo->fbo.hdr.type = LKL_BINDER_TYPE_HANDLE;
		bo->fbo.handle =
			fdp->ConsumeIntegralInRange<uint8_t>(0, NUM_HANDLE);
		break;
	case 3: // BINDER_TYPE_WEAK_HANDLE
		bo->fbo.hdr.type = LKL_BINDER_TYPE_WEAK_HANDLE;
		bo->fbo.handle =
			fdp->ConsumeIntegralInRange<uint8_t>(0, NUM_HANDLE);
		break;
	case 4: // BINDER_TYPE_FD
	{
		int fd = lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
		assert(fd != -1);

		bo->fdo.hdr.type = LKL_BINDER_TYPE_FD;
		bo->fdo.fd = fd;
		bo->fdo.cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
	} break;
	case 5: // BINDER_TYPE_PTR
		bo->fdo.hdr.type = LKL_BINDER_TYPE_PTR;
		bo->bbo.length = fdp->ConsumeIntegral<uint8_t>();
		if (bo->bbo.length > 0) {
			bo->bbo.buffer = (uintptr_t)lkl_malloc(bo->bbo.length);
			std::vector<char> buffer =
				fdp->ConsumeBytes<char>(bo->bbo.length);
			memcpy((void *)bo->bbo.buffer, buffer.data(),
			       buffer.size());
		}
		if (fdp->ConsumeBool()) {
			bo->bbo.flags = LKL_BINDER_BUFFER_FLAG_HAS_PARENT;
			bo->bbo.parent = fdp->ConsumeIntegral<uint8_t>();
			bo->bbo.parent_offset = fdp->ConsumeIntegral<uint8_t>();
		}
		break;
	default:
		break;
	}
}

#define ALIGN(x, a) __LKL__ALIGN_KERNEL((x), (a))

static void transaction_init(FuzzedDataProvider *fdp,
			     struct lkl_binder_transaction_data *tr, bool sg,
			     lkl_binder_size_t *retsize)
{
	size_t i, num_object;
	size_t num_offset = 0;
	size_t data_size;
	size_t buffers_size = 0;
	struct binder_object *data_buf;
	lkl_binder_size_t *offsets;

	tr->flags = fdp->ConsumeBool() ? LKL_TF_ONE_WAY : 0;
	tr->target.handle = fdp->ConsumeIntegralInRange<uint8_t>(0, NUM_HANDLE);

	/*
         * Create Binder objects
         */
	num_object = fdp->ConsumeIntegralInRange<uint8_t>(0, 5);
	if (num_object == 0) {
		return;
	}

	data_size = num_object * sizeof(struct binder_object);
	data_buf = (struct binder_object *)calloc(1, data_size);
	assert(data_buf != NULL);

	offsets = (lkl_binder_size_t *)calloc(num_object,
					      sizeof(lkl_binder_size_t));
	assert(offsets != NULL);

	for (i = 0; i < num_object; i++) {
		binder_object_init(fdp, &data_buf[i], sg);

		if (data_buf[i].hdr.type == LKL_BINDER_TYPE_PTR)
			buffers_size += data_buf[i].bbo.length;
		if (fdp->ConsumeBool())
			// Insert object offset in the offsets buffer
			offsets[num_offset++] =
				num_offset * sizeof(struct binder_object);
	}

	/*
         * Copy Binder objects to transaction data
         */
	tr->data_size = data_size;
	tr->data.ptr.buffer = (uintptr_t)lkl_malloc(tr->data_size);
	memcpy((void *)tr->data.ptr.buffer, (void *)data_buf, tr->data_size);

	tr->offsets_size = num_offset * sizeof(lkl_binder_size_t);
	tr->data.ptr.offsets = (uintptr_t)lkl_malloc(tr->offsets_size);
	memcpy((void *)tr->data.ptr.offsets, (void *)offsets, tr->offsets_size);

	if (sg && retsize) {
		buffers_size +=
			sizeof(uint64_t) * fdp->ConsumeIntegral<uint8_t>();
		*retsize = ALIGN(buffers_size, sizeof(uint64_t));
	}

	free(data_buf);
	free(offsets);
}

static void transaction_sg_init(FuzzedDataProvider *fdp,
				struct lkl_binder_transaction_data_sg *tr_sg)
{
	struct lkl_binder_transaction_data *tr = &tr_sg->transaction_data;

	transaction_init(fdp, tr, true, &tr_sg->buffers_size);
}

static void binder_fd_object_fini(struct lkl_binder_fd_object *fdo)
{
	assert(fdo->hdr.type == LKL_BINDER_TYPE_FD);
	lkl_sys_close(fdo->fd);
}

static void binder_buffer_object_fini(struct lkl_binder_buffer_object *bbo)
{
	assert(bbo->hdr.type == LKL_BINDER_TYPE_PTR);
	if (bbo->length > 0) {
		lkl_free((void *)bbo->buffer);
	}
}

static void transaction_fini(struct lkl_binder_transaction_data *tr)
{
	size_t i;
	size_t num_object;
	struct binder_object *data_buf;

	num_object = tr->data_size / sizeof(struct binder_object);
	data_buf = (struct binder_object *)tr->data.ptr.buffer;
	for (i = 0; i < num_object; i++) {
		switch (data_buf[i].hdr.type) {
		case LKL_BINDER_TYPE_FD:
			binder_fd_object_fini(
				(struct lkl_binder_fd_object *)&data_buf[i]);
			break;
		case LKL_BINDER_TYPE_PTR:
			binder_buffer_object_fini((
				struct lkl_binder_buffer_object *)&data_buf[i]);
			break;
		default:
			break;
		}
	}

	lkl_free((void *)tr->data.ptr.buffer);
	lkl_free((void *)tr->data.ptr.offsets);
}

static int send_handle_request(struct binder_context *ctx,
			       FuzzedDataProvider *fdp, uint32_t cmd)
{
	int ret;
        uint32_t handle = fdp->ConsumeIntegralInRange<uint8_t>(0, NUM_HANDLE);
	struct {
		uint32_t cmd;
		uint32_t descriptor;
	} __attribute__((packed)) bc = {
		.cmd = cmd,
		.descriptor = handle
	};

	ret = write_binder(ctx, (uintptr_t)&bc, sizeof(bc));

	return ret;
}

static int send_handle_done(struct binder_context *ctx, FuzzedDataProvider *fdp,
			    uint32_t cmd)
{
	int ret;
        uintptr_t ptr = fdp->ConsumeIntegralInRange<uint8_t>(1, NUM_HANDLE);
        uintptr_t cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
	struct {
		uint32_t cmd;
		uintptr_t ptr;
		uintptr_t cookie;
	} __attribute__((packed)) bc = {
		.cmd = cmd,
		.ptr = ptr,
		.cookie = cookie
	};

	ret = write_binder(ctx, (uintptr_t)&bc, sizeof(bc));

	return ret;
}

static int send_transaction(struct binder_context *ctx, FuzzedDataProvider *fdp,
			    bool reply)
{
	int ret;
	uint32_t cmd = reply ? LKL_BC_REPLY : LKL_BC_TRANSACTION;
	struct lkl_binder_transaction_data tr = {};
	uint8_t *write_buf = (uint8_t *)lkl_malloc(sizeof(cmd) + sizeof(tr));

	transaction_init(fdp, &tr, false, NULL);

	memcpy(write_buf, &cmd, sizeof(cmd));
	memcpy(write_buf + sizeof(cmd), &tr, sizeof(tr));
	ret = write_binder(ctx, (uintptr_t)write_buf, sizeof(write_buf));

	transaction_fini(&tr);
	lkl_free(write_buf);
	return ret;
}

static int send_transaction_sg(struct binder_context *ctx,
			       FuzzedDataProvider *fdp, bool reply)
{
	int ret;
	uint32_t cmd = reply ? LKL_BC_REPLY_SG : LKL_BC_TRANSACTION_SG;
	struct lkl_binder_transaction_data_sg tr_sg = {};
	uint8_t *write_buf = (uint8_t *)lkl_malloc(sizeof(cmd) + sizeof(tr_sg));

	transaction_sg_init(fdp, &tr_sg);

	memcpy(write_buf, &cmd, sizeof(cmd));
	memcpy(write_buf + sizeof(cmd), &tr_sg, sizeof(tr_sg));
	ret = write_binder(ctx, (uintptr_t)write_buf, sizeof(write_buf));

	transaction_fini(&tr_sg.transaction_data);
	lkl_free(write_buf);
	return ret;
}

static int send_free_buffer(struct binder_context *ctx, FuzzedDataProvider *fdp)
{
	int ret = 0;
	uintptr_t buffer_ptr;

	if (ctx->buffers.empty())
		return ret;

	buffer_ptr = ctx->buffers.front();
	ctx->buffers.pop();

	struct {
		uint32_t cmd;
		uintptr_t buffer_ptr;
	} __attribute__((packed)) bc = {
		.cmd = LKL_BC_FREE_BUFFER,
		.buffer_ptr = buffer_ptr,
	};

	ret = write_binder(ctx, (uintptr_t)&bc, sizeof(bc));

	return ret;
}

static int send_death_notif(struct binder_context *ctx, FuzzedDataProvider *fdp,
			    uint32_t cmd)
{
	int ret;
        uint32_t handle = fdp->ConsumeIntegralInRange<uint8_t>(0, NUM_HANDLE);
        uintptr_t cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
	struct {
		uint32_t cmd;
		uint32_t handle;
		uintptr_t cookie;
	} __attribute__((packed))
	bc = {
		.cmd = cmd,
		.handle = handle,
		.cookie = cookie
	};

	ret = write_binder(ctx, (uintptr_t)&bc, sizeof(bc));

	return ret;
}

static int send_death_done(struct binder_context *ctx, FuzzedDataProvider *fdp)
{
	int ret;
        uintptr_t cookie = fdp->ConsumeBool() ? COOKIE_A : COOKIE_B;
	struct {
		uint32_t cmd;
		uintptr_t cookie;
	} __attribute__((packed)) bc = {
		.cmd = LKL_BC_DEAD_BINDER_DONE,
		.cookie = cookie
	};

	ret = write_binder(ctx, (uintptr_t)&bc, sizeof(bc));

	return ret;
}

static int send_looper_cmd(struct binder_context *ctx, uint32_t cmd)
{
	return write_binder(ctx, (uintptr_t)&cmd, sizeof(cmd));
}

static int send_ioctl_req(struct binder_context *ctx, unsigned long request)
{
	return lkl_sys_ioctl(ctx->fd, request, 0);
}

static int ep_add(struct binder_context *ctx)
{
	int ret;
	struct lkl_epoll_event ev = { .events = LKL_POLLIN };

	if (ctx->epoll_fd != -1)
		return -1;

	ctx->epoll_fd = lkl_sys_epoll_create(1);
	assert(ctx->epoll_fd != -1);

	ret = lkl_sys_epoll_ctl(ctx->epoll_fd, LKL_EPOLL_CTL_ADD, ctx->fd, &ev);
	assert(ret != -1);

	return 0;
}

static int ep_wait(struct binder_context *ctx)
{
	int ret;
	struct lkl_epoll_event ev;

	ret = lkl_sys_epoll_wait(ctx->epoll_fd, &ev, 1, 0);

	return ret;
}

static int write(struct binder_context *ctx, FuzzedDataProvider *fdp)
{
	int ret = 0;

	switch (fdp->ConsumeIntegralInRange<uint8_t>(0, 20)) {
	case 1: // BC_ACQUIRE
		LOG("BC_ACQUIRE\n");
		ret = send_handle_request(ctx, fdp, LKL_BC_ACQUIRE);
		break;
	case 2: // BC_INCREFS
		LOG("BC_INCREFS\n");
		ret = send_handle_request(ctx, fdp, LKL_BC_INCREFS);
		break;
	case 3: // BC_RELEASE
		LOG("BC_RELEASE\n");
		ret = send_handle_request(ctx, fdp, LKL_BC_RELEASE);
		break;
	case 4: // BC_DECREFS
		LOG("BC_DECREFS\n");
		ret = send_handle_request(ctx, fdp, LKL_BC_DECREFS);
		break;
	case 5: // BC_INCREFS_DONE
		LOG("BC_INCREFS_DONE\n");
		ret = send_handle_done(ctx, fdp, LKL_BC_INCREFS_DONE);
		break;
	case 6: // BC_ACQUIRE_DONE
		LOG("BC_ACQUIRE_DONE\n");
		ret = send_handle_done(ctx, fdp, LKL_BC_ACQUIRE_DONE);
		break;
	case 7: // BC_TRANSACTION
		LOG("BC_TRANSACTION\n");
		ret = send_transaction(ctx, fdp, false);
		break;
	case 8: // BC_REPLY
		LOG("BC_REPLY\n");
		ret = send_transaction(ctx, fdp, true);
		break;
	case 9: // BC_TRANSACTION_SG
		LOG("BC_TRANSACTION_SG\n");
		ret = send_transaction_sg(ctx, fdp, false);
		break;
	case 10: // BC_REPLY_SG
		LOG("BC_REPLY_SG\n");
		ret = send_transaction_sg(ctx, fdp, true);
		break;
	case 11: // BC_FREE_BUFFER
		LOG("BC_FREE_BUFFER\n");
		ret = send_free_buffer(ctx, fdp);
		break;
	case 12: // BC_REQUEST_DEATH_NOTIFICATION
		LOG("BC_REQUEST_DEATH_NOTIFICATION\n");
		ret = send_death_notif(ctx, fdp,
				       LKL_BC_REQUEST_DEATH_NOTIFICATION);
		break;
	case 13: // BC_CLEAR_DEATH_NOTIFICATION
		LOG("BC_CLEAR_DEATH_NOTIFICATION\n");
		ret = send_death_notif(ctx, fdp,
				       LKL_BC_CLEAR_DEATH_NOTIFICATION);
		break;
	case 14: // BC_DEAD_BINDER_DONE
		LOG("BC_DEAD_BINDER_DONE\n");
		ret = send_death_done(ctx, fdp);
		break;
	case 15: // BC_REGISTER_LOOPER
		LOG("BC_REGISTER_LOOPER\n");
		ret = send_looper_cmd(ctx, LKL_BC_REGISTER_LOOPER);
		break;
	case 16: // BC_ENTER_LOOPER
		LOG("BC_ENTER_LOOPER\n");
		ret = send_looper_cmd(ctx, LKL_BC_ENTER_LOOPER);
		break;
	case 17: // BC_EXIT_LOOPER
		LOG("BC_EXIT_LOOPER\n");
		ret = send_looper_cmd(ctx, LKL_BC_EXIT_LOOPER);
		break;
	case 18: // BINDER_THREAD_EXIT
		LOG("BINDER_THREAD_EXIT\n");
		ret = send_ioctl_req(ctx, LKL_BINDER_THREAD_EXIT);
		break;
	case 19: // EPOLL_ADD
		LOG("EPOLL_ADD\n");
		ret = ep_add(ctx);
		break;
	case 20: // EPOLL_WAIT
		LOG("EPOLL_WAIT\n");
		ret = ep_wait(ctx);
		break;
	default: // Read Binder
		LOG("READ\n");
		ret = read_binder(ctx, 1024);
		break;
	}

	return ret;
}

static int stop_context_mgr(struct binder_context *ctx)
{
	uint32_t cmd = LKL_BC_EXIT_LOOPER;

	if (write_binder(ctx, (uintptr_t)&cmd, sizeof(cmd))) {
		return -1;
	}

	return 0;
}

static int client_done = 0;
pthread_cond_t client_cond;
pthread_mutex_t client_lock;
pthread_mutex_t exit_lock[NUM_CLIENT];

struct binder_context *ctx[NUM_CLIENT];

extern "C" void __llvm_profile_initialize_file(void);
extern "C" int __llvm_profile_write_file(void);

void flush_coverage()
{
	__llvm_profile_write_file();
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	int ret;

	assert(lkl_start_kernel(&lkl_host_ops, "mem=50M loglevel=8") == 0);

	assert(lkl_mount_fs("sysfs") == 0);
	assert(lkl_mount_fs("proc") == 0);

	ret = lkl_sys_mkdir("/dev", 0770);
	assert(ret == 0 || ret == -LKL_EEXIST);

	assert(lkl_sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) == 0);

	assert(check_binder_version() == 0);

	pthread_cond_init(&client_cond, NULL);
	pthread_mutex_init(&client_lock, NULL);
	for (size_t i = 0; i < NUM_CLIENT; i++) {
		pthread_mutex_init(&exit_lock[i], NULL);
	}

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return -1;
}

void *initialize_binder_client(void *arg)
{
	int idx = (uintptr_t)arg;

	lkl_set_task_flag(LKL_TASK_NEW_TGID);

	pthread_mutex_lock(&client_lock);

	ctx[idx] = open_binder(BINDER_VM_SIZE);
	assert(ctx[idx] != NULL);
	client_done++;
	pthread_cond_signal(&client_cond);

	pthread_mutex_unlock(&client_lock);

	pthread_mutex_lock(&exit_lock[idx]);
	close_binder(ctx[idx]);
	ctx[idx] = NULL;
	pthread_mutex_unlock(&exit_lock[idx]);

	return NULL;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	FuzzedDataProvider fdp(data, size);
	static int iter = 0;

	int ret;
	size_t i, client;
	void *orig_task = lkl_get_task();
	pthread_t tid[NUM_CLIENT];

	client_done = 0;
	for (i = 0; i < NUM_CLIENT; i++) {
		pthread_mutex_lock(&exit_lock[i]);
		ret = pthread_create(&tid[i], NULL, initialize_binder_client,
				     (void *)i);
		assert(ret == 0);
	}

	// Wait for binder to initialize
	pthread_mutex_lock(&client_lock);
	while (client_done < NUM_CLIENT) {
		pthread_cond_wait(&client_cond, &client_lock);
	}
	pthread_mutex_unlock(&client_lock);

	ret = send_ioctl_req(ctx[1], LKL_BINDER_SET_CONTEXT_MGR);
	assert(ret == 0);

	while (fdp.remaining_bytes()) {
		client = fdp.ConsumeIntegralInRange<uint8_t>(0, NUM_CLIENT - 1);
		LOG("%lu: ", client);
		lkl_set_task(ctx[client]->task);
		write(ctx[client], &fdp);
	}

	stop_context_mgr(ctx[1]);

	// Restore to original task before exiting other threads
	lkl_set_task(orig_task);
	for (i = 0; i < NUM_CLIENT; i++) {
		pthread_mutex_unlock(&exit_lock[i]);
		pthread_join(tid[i], NULL);
	}

	iter++;
	if (iter > 1000) {
		flush_coverage();
		iter = 0;
	}
	return 0;
}
