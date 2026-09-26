// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atomic_file.h"
#include "neighbor_sync_store.h"

#define WMCS_NR_STORE_HEADER 48U
#define WMCS_NR_STORE_ENTRY (1U + WMCS_NR_REPORT_MAX)
#define WMCS_NR_STORE_MAX (WMCS_NR_STORE_HEADER + \
	WMCS_NR_SET_MAX * WMCS_NR_STORE_ENTRY + WMCS_IDENTITY_HASH_SIZE)

static const uint8_t store_magic[4] = {'W', 'M', 'N', 'R'};

static int store_path(const struct wmcs_identity *identity,
		      char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U])
{
	int length;

	if (!identity || !identity->state_dir[0])
		return -EINVAL;
	length = snprintf(path, WMCS_IDENTITY_STATE_DIR_SIZE + 32U,
			  "%s/neighbor-sync.state", identity->state_dir);
	return length < 0 ||
	       length >= (int)(WMCS_IDENTITY_STATE_DIR_SIZE + 32U) ?
		-ENAMETOOLONG : 0;
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

static bool records_valid(const struct wmcs_nr_record *own,
			  const struct wmcs_nr_record *records,
			  size_t count)
{
	size_t i;
	size_t j;

	if (!wmcs_nr_record_valid(own) || count > WMCS_NR_SET_MAX ||
	    (count && !records))
		return false;
	for (i = 0; i < count; i++) {
		if (!wmcs_nr_record_valid(&records[i]) ||
		    strcmp(records[i].ssid, own->ssid) ||
		    !memcmp(records[i].report, own->report, 6U))
			return false;
		for (j = 0; j < i; j++) {
			if (!memcmp(records[i].report, records[j].report, 6U))
				return false;
		}
	}
	return true;
}

int wmcs_nr_store_save(const struct wmcs_identity *identity,
		       const struct wmcs_nr_record *own,
		       const struct wmcs_nr_record *records,
		       size_t count)
{
	uint8_t data[WMCS_NR_STORE_MAX] = {0};
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	size_t size;
	size_t i;
	int result;

	if (!records_valid(own, records, count))
		return -EINVAL;
	result = store_path(identity, path);
	if (result)
		return result;
	memcpy(data, store_magic, sizeof(store_magic));
	data[4] = 1;
	data[5] = (uint8_t)count;
	data[6] = (uint8_t)strlen(own->ssid);
	memcpy(&data[8], own->report, 6U);
	memcpy(&data[14], own->ssid, data[6]);
	for (i = 0; i < count; i++) {
		size_t offset = WMCS_NR_STORE_HEADER + i * WMCS_NR_STORE_ENTRY;

		data[offset] = (uint8_t)records[i].report_size;
		memcpy(&data[offset + 1U], records[i].report,
		       records[i].report_size);
	}
	size = WMCS_NR_STORE_HEADER + count * WMCS_NR_STORE_ENTRY;
	result = wmcs_identity_hash(data, size, &data[size]);
	if (!result)
		result = wmcs_atomic_file_write(identity->state_dir, path, data,
						 size + WMCS_IDENTITY_HASH_SIZE,
						 0600);
	wmcs_secure_zero(data, sizeof(data));
	return result;
}

int wmcs_nr_store_load(const struct wmcs_identity *identity,
		       const struct wmcs_nr_record *own,
		       struct wmcs_nr_record records[WMCS_NR_SET_MAX],
		       size_t *count)
{
	uint8_t data[WMCS_NR_STORE_MAX] = {0};
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE] = {0};
	struct wmcs_nr_record parsed[WMCS_NR_SET_MAX] = {0};
	char path[WMCS_IDENTITY_STATE_DIR_SIZE + 32U];
	struct stat state;
	size_t size = 0;
	size_t parsed_count;
	size_t content_size;
	size_t i;
	int result;
	int fd;

	if (!count || !records || !wmcs_nr_record_valid(own))
		return -EINVAL;
	*count = 0;
	result = store_path(identity, path);
	if (result)
		return result;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &state) || !S_ISREG(state.st_mode) ||
	    state.st_uid != geteuid() || (state.st_mode & 0077U) ||
	    state.st_size < (off_t)(WMCS_NR_STORE_HEADER +
				    WMCS_IDENTITY_HASH_SIZE) ||
	    state.st_size > (off_t)sizeof(data)) {
		result = -EKEYREJECTED;
		goto out;
	}
	size = (size_t)state.st_size;
	for (i = 0; i < size;) {
		ssize_t amount = read(fd, &data[i], size - i);

		if (amount > 0) {
			i += (size_t)amount;
			continue;
		}
		if (amount < 0 && errno == EINTR)
			continue;
		result = -EIO;
		goto out;
	}
	content_size = size - WMCS_IDENTITY_HASH_SIZE;
	if (memcmp(data, store_magic, sizeof(store_magic)) ||
	    data[4] != 1 || data[5] > WMCS_NR_SET_MAX ||
	    !data[6] || data[6] > WMCS_NR_SSID_MAX || data[7] ||
	    !all_zero(&data[14 + data[6]], 32U - data[6]) ||
	    !all_zero(&data[46], 2U) ||
	    content_size != WMCS_NR_STORE_HEADER +
			    (size_t)data[5] * WMCS_NR_STORE_ENTRY) {
		result = -EKEYREJECTED;
		goto out;
	}
	result = wmcs_identity_hash(data, content_size, digest);
	if (result || memcmp(digest, &data[content_size], sizeof(digest))) {
		result = result ? result : -EKEYREJECTED;
		goto out;
	}
	if (memcmp(&data[8], own->report, 6U) ||
	    strlen(own->ssid) != data[6] ||
	    memcmp(&data[14], own->ssid, data[6])) {
		result = -ESTALE;
		goto out;
	}
	parsed_count = data[5];
	for (i = 0; i < parsed_count; i++) {
		size_t offset = WMCS_NR_STORE_HEADER + i * WMCS_NR_STORE_ENTRY;
		size_t report_size = data[offset];

		if (report_size < 13U || report_size > WMCS_NR_REPORT_MAX ||
		    !all_zero(&data[offset + 1U + report_size],
			      WMCS_NR_REPORT_MAX - report_size)) {
			result = -EKEYREJECTED;
			goto out;
		}
		strcpy(parsed[i].ssid, own->ssid);
		memcpy(parsed[i].report, &data[offset + 1U], report_size);
		parsed[i].report_size = report_size;
	}
	if (!records_valid(own, parsed, parsed_count)) {
		result = -EKEYREJECTED;
		goto out;
	}
	memcpy(records, parsed, parsed_count * sizeof(parsed[0]));
	*count = parsed_count;
	result = 0;

out:
	if (close(fd) && !result)
		result = -errno;
	wmcs_secure_zero(parsed, sizeof(parsed));
	wmcs_secure_zero(data, sizeof(data));
	wmcs_secure_zero(digest, sizeof(digest));
	return result;
}
