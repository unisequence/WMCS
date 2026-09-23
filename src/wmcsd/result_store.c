// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atomic_file.h"
#include "result_store.h"

#define STATE_DIR_SIZE 256U
#define RESULT_HEADER_SIZE 16U
#define RESULT_RECORD_SIZE (RESULT_HEADER_SIZE + WMCS_CONTROL_WIRE_SIZE)

static const uint8_t result_magic[] = {'W', 'M', 'C', 'R'};

static void clear_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
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

static int result_path(char output[STATE_DIR_SIZE + 32U],
		       const char *state_dir)
{
	int length;

	if (!state_dir || !state_dir[0])
		return -EINVAL;
	length = snprintf(output, STATE_DIR_SIZE + 32U,
			  "%s/control.result", state_dir);
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

static int read_record(const char *path, uint8_t record[RESULT_RECORD_SIZE])
{
	struct stat state;
	int result;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &state) || !S_ISREG(state.st_mode) ||
	    state.st_uid != geteuid() || (state.st_mode & 0077U) ||
	    state.st_size != (off_t)RESULT_RECORD_SIZE) {
		close(fd);
		return -EPERM;
	}
	result = read_all(fd, record, RESULT_RECORD_SIZE);
	if (close(fd) && !result)
		result = -errno;
	return result;
}

int wmcs_result_store_save(const char *state_dir,
			   const struct wmcs_control_packet *response)
{
	uint8_t record[RESULT_RECORD_SIZE] = {0};
	char path[STATE_DIR_SIZE + 32U];
	int result;

	if (!response ||
	    (response->type != WMCS_CONTROL_WLAN_RESULT &&
	     response->type != WMCS_CONTROL_RELEASE_RESULT) ||
	    !response->sequence)
		return -EINVAL;
	result = result_path(path, state_dir);
	if (result)
		return result;
	memcpy(record, result_magic, sizeof(result_magic));
	record[4] = 0;
	record[5] = 1;
	put_u16(&record[6], RESULT_RECORD_SIZE);
	put_u64(&record[8], response->sequence);
	if (!wmcs_control_encode(&record[RESULT_HEADER_SIZE], response)) {
		clear_bytes(record, sizeof(record));
		return -EINVAL;
	}
	result = wmcs_atomic_file_write(state_dir, path, record, sizeof(record),
					 0600);
	clear_bytes(record, sizeof(record));
	return result;
}

int wmcs_result_store_load(const char *state_dir,
			   struct wmcs_control_packet *response)
{
	uint8_t record[RESULT_RECORD_SIZE];
	char path[STATE_DIR_SIZE + 32U];
	int result;

	if (!response)
		return -EINVAL;
	memset(response, 0, sizeof(*response));
	result = result_path(path, state_dir);
	if (result)
		return result;
	result = read_record(path, record);
	if (result)
		return result;
	if (memcmp(record, result_magic, sizeof(result_magic)) || record[4] != 0 ||
	    record[5] != 1 || get_u16(&record[6]) != RESULT_RECORD_SIZE ||
	    !wmcs_control_decode(response, &record[RESULT_HEADER_SIZE],
				 WMCS_CONTROL_WIRE_SIZE) ||
	    (response->type != WMCS_CONTROL_WLAN_RESULT &&
	     response->type != WMCS_CONTROL_RELEASE_RESULT) ||
	    response->sequence != get_u64(&record[8])) {
		memset(response, 0, sizeof(*response));
		result = -EKEYREJECTED;
	}
	clear_bytes(record, sizeof(record));
	return result;
}

int wmcs_result_store_remove(const char *state_dir)
{
	char path[STATE_DIR_SIZE + 32U];
	int result;

	result = result_path(path, state_dir);
	if (result)
		return result;
	if (unlink(path) && errno != ENOENT)
		return -errno;
	return wmcs_atomic_file_sync_directory(state_dir);
}
