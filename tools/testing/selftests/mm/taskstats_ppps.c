// SPDX-License-Identifier: GPL-2.0
/*
 * taskstats of a 4K compat process reports the RSS and VM high-watermarks in
 * process-page KB matching /proc/self/status, and integrates resident and
 * virtual memory (Mbyte-usecs) consistently with VmRSS and VmSize.
 */
#define _GNU_SOURCE

#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/taskstats.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>

#include "kselftest_ppps.h"

#define WORKLOAD_SIZE	(32UL * 1024 * 1024)
#define MESSAGE_SIZE	4096

#define ATTR_DATA(attr) ((void *)((char *)(attr) + NLA_HDRLEN))
#define ATTR_PAYLOAD(attr) ((int)((attr)->nla_len - NLA_HDRLEN))

struct genl_message {
	struct nlmsghdr nlh;
	struct genlmsghdr genl;
	char data[MESSAGE_SIZE];
};

struct memory_status {
	unsigned long hwm_kb;
	unsigned long peak_kb;
	unsigned long rss_kb;
	unsigned long size_kb;
};

static int open_netlink(void)
{
	struct sockaddr_nl local = {
		.nl_family = AF_NETLINK,
	};
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
	if (fd < 0)
		return -1;
	if (bind(fd, (struct sockaddr *)&local, sizeof(local))) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool send_request(int fd, uint16_t family, uint8_t command,
			 uint16_t attribute, const void *data, size_t length)
{
	struct sockaddr_nl kernel = {
		.nl_family = AF_NETLINK,
	};
	struct genl_message message = { };
	struct nlattr *attr = (struct nlattr *)message.data;
	size_t attr_length = NLA_HDRLEN + length;

	message.nlh.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN) +
		NLA_ALIGN(attr_length);
	message.nlh.nlmsg_type = family;
	message.nlh.nlmsg_flags = NLM_F_REQUEST;
	message.nlh.nlmsg_pid = getpid();
	message.genl.cmd = command;
	message.genl.version = 1;
	attr->nla_type = attribute;
	attr->nla_len = attr_length;
	memcpy(ATTR_DATA(attr), data, length);

	return sendto(fd, &message, message.nlh.nlmsg_len, 0,
		      (struct sockaddr *)&kernel, sizeof(kernel)) ==
		(ssize_t)message.nlh.nlmsg_len;
}

static int receive_message(int fd, struct genl_message *message)
{
	ssize_t length;

	length = recv(fd, message, sizeof(*message), 0);
	if (length < 0 || !NLMSG_OK(&message->nlh, length))
		return -1;
	if (message->nlh.nlmsg_type == NLMSG_ERROR) {
		struct nlmsgerr *error = NLMSG_DATA(&message->nlh);

		errno = error->error ? -error->error : EPROTO;
		return -1;
	}
	return length;
}

static uint16_t taskstats_family(int fd)
{
	struct genl_message message;
	const char family_name[] = TASKSTATS_GENL_NAME;
	struct nlattr *attr;
	int remaining;
	int length;

	if (!send_request(fd, GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
			  CTRL_ATTR_FAMILY_NAME, family_name,
			  sizeof(family_name)))
		return 0;
	length = receive_message(fd, &message);
	if (length < 0)
		return 0;
	remaining = message.nlh.nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	attr = (struct nlattr *)message.data;
	while (remaining >= NLA_HDRLEN && attr->nla_len >= NLA_HDRLEN &&
	       attr->nla_len <= remaining) {
		if (attr->nla_type == CTRL_ATTR_FAMILY_ID &&
		    ATTR_PAYLOAD(attr) >= (int)sizeof(uint16_t))
			return *(uint16_t *)ATTR_DATA(attr);
		remaining -= NLA_ALIGN(attr->nla_len);
		attr = (struct nlattr *)((char *)attr +
					NLA_ALIGN(attr->nla_len));
	}
	return 0;
}

static bool find_stats(struct nlattr *attr, struct taskstats *stats)
{
	int remaining = ATTR_PAYLOAD(attr);

	attr = ATTR_DATA(attr);
	while (remaining >= NLA_HDRLEN && attr->nla_len >= NLA_HDRLEN &&
	       attr->nla_len <= remaining) {
		if (attr->nla_type == TASKSTATS_TYPE_STATS &&
		    ATTR_PAYLOAD(attr) >= (int)sizeof(*stats)) {
			memcpy(stats, ATTR_DATA(attr), sizeof(*stats));
			return true;
		}
		remaining -= NLA_ALIGN(attr->nla_len);
		attr = (struct nlattr *)((char *)attr +
					NLA_ALIGN(attr->nla_len));
	}
	return false;
}

static bool query_taskstats(int fd, uint16_t family, struct taskstats *stats)
{
	struct genl_message message;
	uint32_t pid = getpid();
	struct nlattr *attr;
	int remaining;
	int length;

	if (!send_request(fd, family, TASKSTATS_CMD_GET,
			  TASKSTATS_CMD_ATTR_PID, &pid, sizeof(pid)))
		return false;
	length = receive_message(fd, &message);
	if (length < 0)
		return false;
	remaining = message.nlh.nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	attr = (struct nlattr *)message.data;
	while (remaining >= NLA_HDRLEN && attr->nla_len >= NLA_HDRLEN &&
	       attr->nla_len <= remaining) {
		if (attr->nla_type == TASKSTATS_TYPE_AGGR_PID &&
		    find_stats(attr, stats))
			return true;
		remaining -= NLA_ALIGN(attr->nla_len);
		attr = (struct nlattr *)((char *)attr +
					NLA_ALIGN(attr->nla_len));
	}
	return false;
}

static bool status_value(const char *status, const char *name,
			 unsigned long *value)
{
	const char *line = strstr(status, name);

	return line && sscanf(line + strlen(name), ": %lu kB", value) == 1;
}

static bool read_memory_status(struct memory_status *memory)
{
	char status[8192];
	ssize_t length;
	int fd;

	fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	length = read(fd, status, sizeof(status) - 1);
	close(fd);
	if (length < 0)
		return false;
	status[length] = '\0';
	return status_value(status, "VmHWM", &memory->hwm_kb) &&
		status_value(status, "VmPeak", &memory->peak_kb) &&
		status_value(status, "VmRSS", &memory->rss_kb) &&
		status_value(status, "VmSize", &memory->size_kb);
}

static uint64_t process_cpu_ns(void)
{
	struct timespec time;

	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time))
		return 0;
	return (uint64_t)time.tv_sec * 1000000000ULL + time.tv_nsec;
}

static unsigned int burn_cpu(unsigned char *mapping, uint64_t duration_ns)
{
	uint64_t start = process_cpu_ns();
	unsigned int checksum = 0;
	unsigned long offset;

	do {
		for (offset = 0; offset < WORKLOAD_SIZE;
		     offset += PROCESS_PAGE_SIZE) {
			mapping[offset]++;
			checksum += mapping[offset];
		}
	} while (process_cpu_ns() - start < duration_ns);
	return checksum;
}

static bool close_enough(uint64_t actual, unsigned long expected)
{
	uint64_t difference = actual > expected ? actual - expected :
		expected - actual;

	return difference <= 512;
}

static bool density_close(uint64_t integral_delta, uint64_t cpu_delta,
			  unsigned long expected_kb)
{
	uint64_t expected_mib_x100;
	uint64_t actual_mib_x100;

	if (!cpu_delta)
		return false;
	actual_mib_x100 = integral_delta * 100 / cpu_delta;
	expected_mib_x100 = expected_kb * 100 / 1024;
	return actual_mib_x100 * 4 >= expected_mib_x100 * 3 &&
		actual_mib_x100 * 4 <= expected_mib_x100 * 5;
}

static int run_test(void)
{
	struct taskstats before = { };
	struct taskstats after = { };
	struct taskstats warm = { };
	struct memory_status memory = { };
	unsigned char *mapping;
	uint64_t cpu_delta;
	uint64_t core_delta;
	uint64_t virt_delta;
	unsigned int checksum;
	uint16_t family;
	int netlink;

	ksft_print_header();
	ksft_set_plan(8);

	netlink = open_netlink();
	family = netlink >= 0 ? taskstats_family(netlink) : 0;
	ksft_test_result(netlink >= 0 && family,
			 "resolve the taskstats generic-netlink family\n");
	if (netlink < 0 || !family)
		ksft_exit_skip("taskstats is unavailable: %s\n",
			       strerror(errno));
	ksft_test_result(query_taskstats(netlink, family, &warm),
			 "query taskstats before the workload\n");
	if (!read_memory_status(&memory))
		ksft_exit_fail_msg("cannot read /proc/self/status\n");

	mapping = mmap(NULL, WORKLOAD_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("workload mmap failed: %s\n", strerror(errno));
	checksum = burn_cpu(mapping, 50000000ULL);
	ksft_test_result(query_taskstats(netlink, family, &before),
			 "establish accounting after faulting the workload\n");
	checksum += burn_cpu(mapping, 300000000ULL);
	ksft_test_result(query_taskstats(netlink, family, &after) &&
			 read_memory_status(&memory),
			 "collect taskstats and proc memory accounting\n");

	ksft_print_msg("VmHWM=%lu taskstats_hwm=%llu VmPeak=%lu taskstats_peak=%llu\n",
		       memory.hwm_kb, (unsigned long long)after.hiwater_rss,
		       memory.peak_kb, (unsigned long long)after.hiwater_vm);
	ksft_test_result(close_enough(after.hiwater_rss, memory.hwm_kb),
			 "report RSS high-watermark in process-page KB\n");
	ksft_test_result(close_enough(after.hiwater_vm, memory.peak_kb),
			 "report VM high-watermark in process-page KB\n");

	cpu_delta = after.ac_utime + after.ac_stime -
		(before.ac_utime + before.ac_stime);
	core_delta = after.coremem - before.coremem;
	virt_delta = after.virtmem - before.virtmem;
	ksft_print_msg("cpu_us=%llu coremem_delta=%llu virtmem_delta=%llu\n",
		       (unsigned long long)cpu_delta,
		       (unsigned long long)core_delta,
		       (unsigned long long)virt_delta);
	ksft_print_msg("VmRSS=%lu VmSize=%lu checksum=%u\n", memory.rss_kb,
		       memory.size_kb, checksum);
	ksft_test_result(density_close(core_delta, cpu_delta, memory.rss_kb),
			 "integrate resident memory in Mbyte-usecs\n");
	ksft_test_result(density_close(virt_delta, cpu_delta, memory.size_kb),
			 "integrate virtual memory in Mbyte-usecs\n");

	munmap(mapping, WORKLOAD_SIZE);
	close(netlink);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
