// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "result_store.h"

static int failures;

static void expect(bool condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "result store test failed: %s\n", name);
		failures++;
	}
}

static bool packets_equal(const struct wmcs_control_packet *left,
			  const struct wmcs_control_packet *right)
{
	uint8_t left_wire[WMCS_CONTROL_WIRE_SIZE];
	uint8_t right_wire[WMCS_CONTROL_WIRE_SIZE];

	return wmcs_control_encode(left_wire, left) &&
	       wmcs_control_encode(right_wire, right) &&
	       !memcmp(left_wire, right_wire, sizeof(left_wire));
}

int main(void)
{
	struct wmcs_control_packet response = {
		.type = WMCS_CONTROL_WLAN_RESULT,
		.sequence = 42,
	};
	struct wmcs_control_packet loaded;
	struct wmcs_control_packet invalid = {
		.type = WMCS_CONTROL_WLAN_REQUEST,
		.sequence = 42,
	};
	char directory[] = "/tmp/wmcs-result-store.XXXXXX";
	char path[sizeof(directory) + 32U];
	uint8_t damaged = 0;
	int fd;

	expect(mkdtemp(directory) != NULL, "create private state directory");
	expect(chmod(directory, 0700) == 0, "set private directory mode");
	memset(response.sender_id, 0x11, sizeof(response.sender_id));
	memset(response.recipient_id, 0x22, sizeof(response.recipient_id));
	memset(response.nonce, 0x33, sizeof(response.nonce));
	memset(response.ciphertext, 0x44, sizeof(response.ciphertext));

	expect(wmcs_result_store_load(directory, &loaded) == -ENOENT,
	       "missing result is absent");
	expect(wmcs_result_store_save(directory, &invalid) == -EINVAL,
	       "request packet cannot be cached as a result");
	expect(wmcs_result_store_save(directory, &response) == 0,
	       "save result atomically");
	expect(wmcs_result_store_load(directory, &loaded) == 0,
	       "load saved result");
	expect(packets_equal(&response, &loaded), "saved result round trip");

	snprintf(path, sizeof(path), "%s/control.result", directory);
	response.sequence++;
	response.ciphertext[0] ^= 0x5aU;
	expect(wmcs_result_store_save(directory, &response) == 0,
	       "replace previous result atomically");
	expect(wmcs_result_store_load(directory, &loaded) == 0 &&
	       packets_equal(&response, &loaded), "replacement is authoritative");
	response.type = WMCS_CONTROL_RELEASE_RESULT;
	response.sequence++;
	expect(wmcs_result_store_save(directory, &response) == 0,
	       "save release result");
	expect(wmcs_result_store_load(directory, &loaded) == 0 &&
	       packets_equal(&response, &loaded), "release result round trip");
	expect(chmod(path, 0644) == 0, "make result permissions unsafe");
	expect(wmcs_result_store_load(directory, &loaded) == -EPERM,
	       "reject result with group or other permissions");
	expect(chmod(path, 0600) == 0, "restore private result permissions");
	fd = open(path, O_WRONLY | O_CLOEXEC);
	expect(fd >= 0, "open result for corruption test");
	if (fd >= 0) {
		expect(write(fd, &damaged, sizeof(damaged)) == (ssize_t)sizeof(damaged),
		       "corrupt result header");
		expect(close(fd) == 0, "close corrupted result");
	}
	expect(wmcs_result_store_load(directory, &loaded) == -EKEYREJECTED,
	       "reject corrupted result");
	expect(wmcs_result_store_save(directory, &response) == 0,
	       "replace corrupted result atomically");
	expect(wmcs_result_store_remove(directory) == 0, "remove result");
	expect(wmcs_result_store_load(directory, &loaded) == -ENOENT,
	       "removed result stays absent");
	expect(rmdir(directory) == 0, "remove test directory");

	if (failures)
		return 1;
	puts("Durable result store tests: ok");
	return 0;
}
