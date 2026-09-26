// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "neighbor_sync_store.h"

/* The storage format test uses a deterministic digest stub. Production
 * links the tested identity_hash implementation instead. */
int wmcs_identity_hash(const uint8_t *input, size_t input_size,
		       uint8_t output[WMCS_IDENTITY_HASH_SIZE])
{
	uint32_t value = 2166136261U;
	size_t i;

	for (i = 0; i < input_size; i++)
		value = (value ^ input[i]) * 16777619U;
	for (i = 0; i < WMCS_IDENTITY_HASH_SIZE; i++) {
		value = (value ^ (uint32_t)i) * 16777619U;
		output[i] = (uint8_t)(value >> 24U);
	}
	return 0;
}

void wmcs_secure_zero(void *buffer, size_t size)
{
	volatile uint8_t *data = buffer;

	while (size--)
		*data++ = 0;
}

static struct wmcs_nr_record record(uint8_t suffix)
{
	struct wmcs_nr_record result = {
		.ssid = "OpenWrt_5G",
		.report = {0x02, 0x11, 0x22, 0x33, 0x44, suffix,
			   0x00, 0x00, 0x00, 0x00, 0x80, 0x24, 0x09},
		.report_size = 13,
	};

	return result;
}

int main(void)
{
	char directory[] = "/tmp/wmcs-nr-store-XXXXXX";
	char path[sizeof(directory) + 32U];
	struct wmcs_identity identity = {0};
	struct wmcs_nr_record own = record(0x55);
	struct wmcs_nr_record neighbor = record(0x66);
	struct wmcs_nr_record loaded[WMCS_NR_SET_MAX] = {0};
	struct wmcs_nr_record duplicate[2] = {neighbor, neighbor};
	size_t count = 0;
	int fd;
	uint8_t byte;

	assert(mkdtemp(directory));
	strcpy(identity.state_dir, directory);
	assert(snprintf(path, sizeof(path), "%s/neighbor-sync.state",
			directory) > 0);
	assert(wmcs_nr_store_load(&identity, &own, loaded, &count) == -ENOENT);
	assert(!wmcs_nr_store_save(&identity, &own, &neighbor, 1));
	assert(!wmcs_nr_store_load(&identity, &own, loaded, &count));
	assert(count == 1 && wmcs_nr_record_equal(&loaded[0], &neighbor));
	assert(wmcs_nr_store_save(&identity, &own, duplicate, 2) == -EINVAL);
	own.report[5] = 0x77;
	assert(wmcs_nr_store_load(&identity, &own, loaded, &count) == -ESTALE);
	own.report[5] = 0x55;
	fd = open(path, O_RDWR);
	assert(fd >= 0);
	assert(pread(fd, &byte, 1, 48) == 1);
	byte ^= 0x01U;
	assert(pwrite(fd, &byte, 1, 48) == 1);
	assert(!close(fd));
	assert(wmcs_nr_store_load(&identity, &own, loaded, &count) ==
	       -EKEYREJECTED);
	assert(!wmcs_nr_store_save(&identity, &own, &neighbor, 1));
	assert(!chmod(path, 0644));
	assert(wmcs_nr_store_load(&identity, &own, loaded, &count) ==
	       -EKEYREJECTED);
	assert(!chmod(path, 0600));
	assert(!wmcs_nr_store_save(&identity, &own, NULL, 0));
	assert(!wmcs_nr_store_load(&identity, &own, loaded, &count));
	assert(count == 0);
	assert(!unlink(path));
	assert(!rmdir(directory));
	puts("Neighbor sync protected store: ok");
	return 0;
}
