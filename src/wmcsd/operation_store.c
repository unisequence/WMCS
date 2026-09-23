// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atomic_file.h"
#include "operation_store.h"

#define STATE_DIR_SIZE 256U
#define OPERATION_HEADER_SIZE 32U
#define OPERATION_RECORD_SIZE \
	(OPERATION_HEADER_SIZE + 2U * WMCS_CONTROL_WIRE_SIZE)

static const uint8_t operation_magic[] = {'W', 'M', 'C', 'O'};

static void clear_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
}

static bool all_zero(const uint8_t *input, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (input[i])
			return false;
	}
	return true;
}

static void put_u16(uint8_t *output, uint16_t value)
{
	output[0] = (uint8_t)(value >> 8U);
	output[1] = (uint8_t)value;
}

static uint16_t get_u16(const uint8_t *input)
{
	return (uint16_t)input[0] << 8U | input[1];
}

static void put_u64(uint8_t *output, uint64_t value)
{
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		output[i] = (uint8_t)(value >> (56U - i * 8U));
}

static uint64_t get_u64(const uint8_t *input)
{
	uint64_t value = 0;
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		value = (value << 8U) | input[i];
	return value;
}

static bool request_type_valid(uint8_t type)
{
	return type == WMCS_CONTROL_WLAN_REQUEST ||
	       type == WMCS_CONTROL_RELEASE_REQUEST;
}

static uint8_t result_type_for_request(uint8_t type)
{
	if (type == WMCS_CONTROL_WLAN_REQUEST)
		return WMCS_CONTROL_WLAN_RESULT;
	if (type == WMCS_CONTROL_RELEASE_REQUEST)
		return WMCS_CONTROL_RELEASE_RESULT;
	return 0;
}

static bool response_matches(const struct wmcs_control_packet *request,
			     const struct wmcs_control_packet *response)
{
	return response->type == result_type_for_request(request->type) &&
	       response->sequence == request->sequence &&
	       !memcmp(response->sender_id, request->recipient_id,
		       WMCS_CONTROL_NODE_ID_SIZE) &&
	       !memcmp(response->recipient_id, request->sender_id,
		       WMCS_CONTROL_NODE_ID_SIZE);
}

static int operation_path(char output[STATE_DIR_SIZE + 32U],
			  const char *state_dir)
{
	int length;

	if (!state_dir || !state_dir[0])
		return -EINVAL;
	length = snprintf(output, STATE_DIR_SIZE + 32U,
			  "%s/control.operation", state_dir);
	if (length < 0 || length >= (int)(STATE_DIR_SIZE + 32U))
		return -ENAMETOOLONG;
	return 0;
}

static int read_all(int fd, uint8_t *output, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		ssize_t amount = read(fd, output + offset, size - offset);

		if (amount > 0) {
			offset += (size_t)amount;
			continue;
		}
		if (amount < 0 && errno == EINTR)
			continue;
		return amount < 0 ? -errno : -EIO;
	}
	return 0;
}

static int read_record(const char *path,
		       uint8_t record[OPERATION_RECORD_SIZE])
{
	struct stat state;
	int result;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &state) || !S_ISREG(state.st_mode) ||
	    state.st_uid != geteuid() || (state.st_mode & 0077U) ||
	    state.st_size != (off_t)OPERATION_RECORD_SIZE) {
		close(fd);
		return -EPERM;
	}
	result = read_all(fd, record, OPERATION_RECORD_SIZE);
	if (close(fd) && !result)
		result = -errno;
	return result;
}

static int save_operation(const char *state_dir,
			  enum wmcs_operation_phase phase,
			  const struct wmcs_control_packet *request,
			  const struct wmcs_control_packet *response)
{
	uint8_t record[OPERATION_RECORD_SIZE] = {0};
	char path[STATE_DIR_SIZE + 32U];
	int result;

	if (!request || !request_type_valid(request->type) ||
	    !request->sequence ||
	    (phase == WMCS_OPERATION_COMPLETE &&
	     (!response || !response_matches(request, response))) ||
	    (phase != WMCS_OPERATION_PENDING &&
	     phase != WMCS_OPERATION_COMPLETE))
		return -EINVAL;
	result = operation_path(path, state_dir);
	if (result)
		return result;
	memcpy(record, operation_magic, sizeof(operation_magic));
	record[4] = 0;
	record[5] = 1;
	put_u16(&record[6], OPERATION_RECORD_SIZE);
	record[8] = (uint8_t)phase;
	record[9] = request->type;
	put_u64(&record[16], request->sequence);
	if (!wmcs_control_encode(&record[OPERATION_HEADER_SIZE], request) ||
	    (phase == WMCS_OPERATION_COMPLETE &&
	     !wmcs_control_encode(
		     &record[OPERATION_HEADER_SIZE + WMCS_CONTROL_WIRE_SIZE],
		     response))) {
		clear_bytes(record, sizeof(record));
		return -EINVAL;
	}
	result = wmcs_atomic_file_write(state_dir, path, record, sizeof(record),
					 0600);
	clear_bytes(record, sizeof(record));
	return result;
}

int wmcs_operation_store_save_pending(
	const char *state_dir,
	const struct wmcs_control_packet *request)
{
	return save_operation(state_dir, WMCS_OPERATION_PENDING, request, NULL);
}

int wmcs_operation_store_save_complete(
	const char *state_dir,
	const struct wmcs_control_packet *request,
	const struct wmcs_control_packet *response)
{
	return save_operation(state_dir, WMCS_OPERATION_COMPLETE, request,
			      response);
}

int wmcs_operation_store_load(const char *state_dir,
			      struct wmcs_operation_record *operation)
{
	uint8_t record[OPERATION_RECORD_SIZE];
	char path[STATE_DIR_SIZE + 32U];
	int result;

	if (!operation)
		return -EINVAL;
	memset(operation, 0, sizeof(*operation));
	result = operation_path(path, state_dir);
	if (result)
		return result;
	result = read_record(path, record);
	if (result)
		return result;
	operation->phase = (enum wmcs_operation_phase)record[8];
	if (memcmp(record, operation_magic, sizeof(operation_magic)) ||
	    record[4] != 0 || record[5] != 1 ||
	    get_u16(&record[6]) != OPERATION_RECORD_SIZE ||
	    (operation->phase != WMCS_OPERATION_PENDING &&
	     operation->phase != WMCS_OPERATION_COMPLETE) ||
	    !request_type_valid(record[9]) || !all_zero(&record[10], 6U) ||
	    !all_zero(&record[24], 8U) ||
	    !wmcs_control_decode(&operation->request,
				 &record[OPERATION_HEADER_SIZE],
				 WMCS_CONTROL_WIRE_SIZE) ||
	    operation->request.type != record[9] ||
	    operation->request.sequence != get_u64(&record[16]) ||
	    (operation->phase == WMCS_OPERATION_PENDING &&
	     !all_zero(&record[OPERATION_HEADER_SIZE + WMCS_CONTROL_WIRE_SIZE],
		       WMCS_CONTROL_WIRE_SIZE)) ||
	    (operation->phase == WMCS_OPERATION_COMPLETE &&
	     (!wmcs_control_decode(
		      &operation->response,
		      &record[OPERATION_HEADER_SIZE + WMCS_CONTROL_WIRE_SIZE],
		      WMCS_CONTROL_WIRE_SIZE) ||
	      !response_matches(&operation->request, &operation->response)))) {
		memset(operation, 0, sizeof(*operation));
		result = -EKEYREJECTED;
	}
	clear_bytes(record, sizeof(record));
	return result;
}

int wmcs_operation_store_remove(const char *state_dir)
{
	char path[STATE_DIR_SIZE + 32U];
	int result;

	result = operation_path(path, state_dir);
	if (result)
		return result;
	if (unlink(path) && errno != ENOENT)
		return -errno;
	return wmcs_atomic_file_sync_directory(state_dir);
}
