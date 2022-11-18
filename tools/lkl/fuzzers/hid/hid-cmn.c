/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/uhid.h>
#include <linux/major.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>

#ifdef LKL_FUZZER

#include <lkl.h>
#include <lkl_host.h>

#define LKL_CALL(op)	lkl_sys_##op
#else
#define LKL_CALL(op)	op
#endif // LKL_FUZZER

#define LOG(fmt, ...) \
    if (g_log_enabled) {\
        printf(fmt, ##__VA_ARGS__); \
    }

#define LOG_BYTES(title, data, size) \
    if (g_log_enabled) {\
        dump_bytes(title, data, size); \
    }

bool g_log_enabled = true;

void dump_bytes(const char* title, const void* data, size_t size) {
    const int kBytesPerLine = 16;   // 16 bytes per line
    const int kCharPerByte = 3;     // format string: " %02X"
    const int kAddrWidth = 9;       // format string:  "%08X:"

    const uint8_t* p = (const uint8_t*)data;
    char line[kAddrWidth + kCharPerByte * kBytesPerLine + kBytesPerLine + 2];

    printf("%s:size=%zu\n", title, size);
    for (size_t i = 0; i < size; i++) {
        size_t col = i % kBytesPerLine;
        if (col == 0) {
            memset(line, ' ', sizeof(line));
            snprintf(line, sizeof(line), "%08zX: ", i);
        }

        // Hex code display
        snprintf(&line[kAddrWidth + col * kCharPerByte],
            sizeof(line) - (kAddrWidth + col * kCharPerByte), " %02X", *p);

        // Printable display
        line[kAddrWidth + kCharPerByte * kBytesPerLine + col + 1] = \
            isprint(*p) ? *p : '.';

        // Line ending
        if (col == (kBytesPerLine - 1) || i == (size - 1)) {
            // This erases the '\0' added by snprintf right after hex code
            line[kAddrWidth + (col + 1) * kCharPerByte] = ' ';

            // This adds the '\0' at the end of entire line
            line[kAddrWidth + \
                kCharPerByte * kBytesPerLine + kBytesPerLine + 1] = '\0';
            printf("%s\n", line);
        }

        p++;
    }
    printf("\n");
}

static
int uhid_write(int fd, const struct uhid_event *ev)
{
    ssize_t ret;

    ret = LKL_CALL(write)(fd, (const char*)ev, sizeof(*ev));
    if (ret < 0) {
        LOG("Cannot write to uhid: %d\n", errno);
        return ret;
    } else if (ret != sizeof(*ev)) {
        LOG("Wrong size written to uhid: %ld != %lu\n", ret, sizeof(ev));
        return -EFAULT;
    } else {
        return 0;
    }
}

static
int uhid_create(int fd, uint16_t vid, uint16_t pid,
    const void* data, size_t size)
{
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE;
    strcpy((char*)ev.u.create.name, "test-uhid-device");
    ev.u.create.rd_data = (void*)data;
    ev.u.create.rd_size = size;
    ev.u.create.bus = BUS_USB;
    ev.u.create.vendor = vid;
    ev.u.create.product = pid;
    ev.u.create.version = 0;
    ev.u.create.country = 0;

    return uhid_write(fd, &ev);
}


static int uhid_input(int fd, const void* data, size_t size)
{
#if 0
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT;
    ev.u.input.size = size;
    memcpy(ev.u.input.data, data, size);

    return uhid_write(fd, &ev);
#endif
    return 0;
}


static void uhid_destroy(int fd)
{
    struct uhid_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_DESTROY;

    uhid_write(fd, &ev);
}

typedef struct {
    uint8_t  rdesc_sz;
    uint8_t  input_sz;
    uint8_t  reserved[2];
    uint16_t vid;
    uint16_t pid;
    uint8_t  payload[0];
} fuzz_data_t;

static
int fuzz_data_fixup(fuzz_data_t* data, size_t data_sz) {
    if (data_sz < sizeof(fuzz_data_t)) {
        return 0;
    }

    if (data->rdesc_sz > 255) {
        data->rdesc_sz = 255;
    }

    if (data->input_sz > 255) {
        data->input_sz = 255;
    }

    size_t sz = data_sz - sizeof(fuzz_data_t);
    if (data->rdesc_sz > sz) {
        data->rdesc_sz = sz;
    }
    sz -= data->rdesc_sz;

    if (data->input_sz > sz) {
        data->input_sz = sz;
    }

    return data_sz - sz;
}

static
int uhid_fuzz(fuzz_data_t* data) {
    if (data->rdesc_sz == 0) {
        LOG("Empty fuzz dat\n");
        return 0;
    }

    LOG("VID=%04X, PID=%04X, RDESC: %u bytes, INPUT: %u byetes\n",
        data->vid, data->pid, data->rdesc_sz, data->input_sz);

    LOG_BYTES("RDESC:", data->payload, data->rdesc_sz);
    LOG_BYTES("INPUT:", data->payload + data->rdesc_sz, data->input_sz);

    int fd =LKL_CALL(open)("/dev/uhid", O_RDWR | O_CLOEXEC, 0);
    if (fd < 0) {
        LOG("Cannot open /dev/uhid\n");
        return -1;
    }

    int ret = uhid_create(fd, data->vid, data->pid,
        data->payload, data->rdesc_sz);
    if (ret) {
        close(fd);
        LOG("Creating uhid device failed, %d\n", ret);
        return -1;
    }

    if (data->input_sz > 0) {
        ret = uhid_input(fd, data->payload + data->rdesc_sz, data->input_sz);
        if (ret) {
            LOG("Sending uhid input failed, %d\n", ret);
        }
    }

    uhid_destroy(fd);
    LKL_CALL(close)(fd);
    return 0;
}

#ifdef LKL_FUZZER
static int lkl_init() {
    if (!g_log_enabled) {
        lkl_host_ops.print = NULL;
    }

    int ret = lkl_start_kernel(&lkl_host_ops, "mem=50M");
    if (ret) {
        LOG("lkl_start_kernel failed\n");
        return -1;
    }

    lkl_mount_fs("sysfs");
    lkl_mount_fs("proc");
    lkl_mount_fs("dev");

    // This is defined in miscdevice.h which is, however, not visible to
    // userspace code.
    #define UHID_MINOR  239

    dev_t dev = makedev(MISC_MAJOR, UHID_MINOR);
    int mknod_result = LKL_CALL(mknodat)(AT_FDCWD, "/dev/uhid",
        S_IFCHR | S_IRUSR | S_IWUSR, dev);
    if (mknod_result != 0) {
        LOG("Create device file failed\n");
        return -1;
    }

    return 0;
}

void __llvm_profile_initialize_file(void);
int __llvm_profile_write_file(void);

void flush_coverage() {
    LOG("Flushing coverage data...\n");
    __llvm_profile_write_file();
    LOG("Done...\n");
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    for (int i = 0; i < *argc; i ++) {
        if (strcmp((*argv)[i], "-quiet=1") == 0) {
            g_log_enabled = false;
            break;
        }
    }

    lkl_init();

    __llvm_profile_initialize_file();
    atexit(flush_coverage);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    static int iter = 0;

    uint8_t data[sizeof(fuzz_data_t) + 128] = {0};
    if (Size > sizeof(data)) {
        Size = sizeof(data);
    }

    memcpy(data, Data, Size);
    
    fuzz_data_t* fuzz_data = (fuzz_data_t*)data;
    Size = fuzz_data_fixup(fuzz_data, Size);

    if (Size > 0) {
        uhid_fuzz(fuzz_data);

        iter ++;
        if (iter > 1000) {
            flush_coverage();
            iter = 0;
        }
    }

    return 0;
}
#else // LKL_FUZZER
int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: uhid-test <path_to_test_data>\n");
        return -1;
    }

    printf("Testing payload from '%s'\n", argv[1]);

    FILE* fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        printf("Failed to open input file %s\n", argv[1]);
        return -1;
    }

    uint8_t data[sizeof(fuzz_data_t) + 512] = {0};

    size_t size = fread(data, 1, sizeof(data), fp);
    fclose(fp);

    fuzz_data_t* fuzz_data = (fuzz_data_t*)data;
    size = fuzz_data_fixup(fuzz_data, size);

    if (size > 0) {
        uhid_fuzz(fuzz_data);
    }
    return 0;
}
#endif // LKL_FUZZER
