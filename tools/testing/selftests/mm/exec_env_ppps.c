// SPDX-License-Identifier: GPL-2.0
/* Verify that a P3S exec preserves argv and environment strings. */
#define _GNU_SOURCE

#include "../kselftest_ppps.h"

#define ENV_COUNT 3
#define ENV_SIZE 3072
#define MARKER "--check-exec-env"

static int check_child(int argc, char **argv)
{
	char name[32];
	unsigned int i;
	bool ok = true;

	ksft_print_header();
	ksft_set_plan(2);
	ppps_require_compat();
	ksft_test_result(argc == 2 && !strcmp(argv[1], MARKER),
			 "exec preserved argv\n");

	for (i = 0; i < ENV_COUNT; i++) {
		const char *value;

		snprintf(name, sizeof(name), "PPPS_EXEC_ENV_%u", i);
		value = getenv(name);
		if (!value || strlen(value) != ENV_SIZE ||
		    value[0] != 'A' + i || value[ENV_SIZE - 1] != 'A' + i) {
			ksft_print_msg("%s was not preserved\n", name);
			ok = false;
		}
	}
	ksft_test_result(ok, "exec preserved environment across native slices\n");
	ksft_finished();
	return 0;
}

int main(int argc, char **argv)
{
	char name[32];
	char *value;
	unsigned int i;

	if (argc == 2 && !strcmp(argv[1], MARKER))
		return check_child(argc, argv);
	if (argc != 1)
		return EXIT_FAILURE;

	value = malloc(ENV_SIZE + 1);
	if (!value)
		ksft_exit_fail_msg("malloc failed\n");
	for (i = 0; i < ENV_COUNT; i++) {
		memset(value, 'A' + i, ENV_SIZE);
		value[ENV_SIZE] = '\0';
		snprintf(name, sizeof(name), "PPPS_EXEC_ENV_%u", i);
		if (setenv(name, value, 1))
			ksft_exit_fail_msg("setenv %s failed: %s\n", name,
					   strerror(errno));
	}
	free(value);
	exec_compat(argv[0], MARKER, NULL);
}
