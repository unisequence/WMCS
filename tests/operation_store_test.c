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

#include "operation_store.h"

#define TEST_OPERATION_HEADER_SIZE 32U

static int failures;

static void expect(bool condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "operation store test failed: %s\n", name);
		failures++;
	}
}

static void make_packet(struct wmcs_control_packet *packet, uint8_t type,
			uint8_t sender, uint8_t recipient, uint64_t sequence)
{
	memset(packet, 0, sizeof(*packet));
	packet->type = type;
	packet->sequence = sequence;
	memset(packet->sender_id, sender, sizeof(packet->sender_id));
	memset(packet->recipient_id, recipient, sizeof(packet->recipient_id));
	memset(packet->nonce, 0x33, sizeof(packet->nonce));
	memset(packet->ciphertext, 0x44, sizeof(packet->ciphertext));
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
	struct wmcs_control_packet request;
	struct wmcs_control_packet response;
	struct wmcs_control_packet wrong_response;
	struct wmcs_control_packet wrong_sequence;
	struct wmcs_control_packet invalid;
	struct wmcs_operation_record loaded;
	char directory[] = "/tmp/wmcs-operation-store.XXXXXX";
	char path[sizeof(directory) + 32U];
	uint8_t damaged = 0;
	uint8_t nonzero = 1;
	int fd;

	make_packet(&request, WMCS_CONTROL_WLAN_REQUEST, 0x11, 0x22, 42);
	make_packet(&response, WMCS_CONTROL_WLAN_RESULT, 0x22, 0x11, 42);
	make_packet(&wrong_response, WMCS_CONTROL_RELEASE_RESULT, 0x22, 0x11,
		    42);
	make_packet(&wrong_sequence, WMCS_CONTROL_WLAN_RESULT, 0x22, 0x11, 43);
	make_packet(&invalid, WMCS_CONTROL_WLAN_RESULT, 0x11, 0x22, 42);

	expect(mkdtemp(directory) != NULL, "create private state directory");
	expect(chmod(directory, 0700) == 0, "set private directory mode");
	expect(wmcs_operation_store_load(directory, &loaded) == -ENOENT,
	       "missing operation is absent");
	expect(wmcs_operation_store_save_pending(directory, &invalid) == -EINVAL,
	       "result packet cannot be stored as pending request");
	expect(wmcs_operation_store_save_pending(directory, &request) == 0,
	       "save pending request atomically");
	expect(wmcs_operation_store_load(directory, &loaded) == 0,
	       "load pending request");
	expect(loaded.phase == WMCS_OPERATION_PENDING,
	       "pending phase round trip");
	expect(packets_equal(&request, &loaded.request),
	       "pending packet round trip");
	snprintf(path, sizeof(path), "%s/control.operation", directory);
	fd = open(path, O_RDWR | O_CLOEXEC);
	expect(fd >= 0, "open pending operation for closed-field test");
	if (fd >= 0) {
		expect(pwrite(fd, &nonzero, sizeof(nonzero),
			      TEST_OPERATION_HEADER_SIZE +
				      WMCS_CONTROL_WIRE_SIZE) ==
		       (ssize_t)sizeof(nonzero),
		       "set forbidden pending response byte");
		expect(close(fd) == 0, "close malformed pending operation");
	}
	expect(wmcs_operation_store_load(directory, &loaded) == -EKEYREJECTED,
	       "pending record requires an empty response slot");
	expect(wmcs_operation_store_save_pending(directory, &request) == 0,
	       "replace malformed pending operation");
	expect(wmcs_operation_store_save_complete(directory, &request,
					     &wrong_response) == -EINVAL,
	       "reject result from a different operation");
	expect(wmcs_operation_store_save_complete(directory, &request,
					     &wrong_sequence) == -EINVAL,
	       "reject result with a different sequence");
	expect(wmcs_operation_store_save_complete(directory, &request,
					     &response) == 0,
	       "save completed operation atomically");
	expect(wmcs_operation_store_load(directory, &loaded) == 0,
	       "load completed operation");
	expect(loaded.phase == WMCS_OPERATION_COMPLETE,
	       "complete phase round trip");
	expect(packets_equal(&request, &loaded.request) &&
	       packets_equal(&response, &loaded.response),
	       "request and result round trip");

	expect(chmod(path, 0644) == 0, "make operation permissions unsafe");
	expect(wmcs_operation_store_load(directory, &loaded) == -EPERM,
	       "reject operation with group or other permissions");
	expect(chmod(path, 0600) == 0, "restore private permissions");
	fd = open(path, O_WRONLY | O_CLOEXEC);
	expect(fd >= 0, "open operation for corruption test");
	if (fd >= 0) {
		expect(write(fd, &damaged, sizeof(damaged)) ==
		       (ssize_t)sizeof(damaged), "corrupt operation header");
		expect(close(fd) == 0, "close corrupted operation");
	}
	expect(wmcs_operation_store_load(directory, &loaded) == -EKEYREJECTED,
	       "reject corrupted operation");
	expect(wmcs_operation_store_save_pending(directory, &request) == 0,
	       "replace corrupted operation atomically");
	expect(wmcs_operation_store_remove(directory) == 0,
	       "remove operation");
	expect(wmcs_operation_store_load(directory, &loaded) == -ENOENT,
	       "removed operation stays absent");
	expect(rmdir(directory) == 0, "remove test directory");

	if (failures)
		return 1;
	puts("Durable controller operation store tests: ok");
	return 0;
}
