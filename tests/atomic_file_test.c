// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atomic_file.h"

static int write_file(const char *path, const unsigned char *data, size_t size)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	ssize_t written;

	if (fd < 0)
		return -1;
	written = write(fd, data, size);
	if (close(fd))
		return -1;
	return written == (ssize_t)size ? 0 : -1;
}

static int read_matches(const char *path, const unsigned char *data,
			 size_t size)
{
	unsigned char buffer[64];
	int fd;
	ssize_t amount;

	if (size > sizeof(buffer))
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	amount = read(fd, buffer, size);
	if (close(fd) || amount != (ssize_t)size || memcmp(buffer, data, size))
		return -1;
	return 0;
}

int main(void)
{
	const unsigned char stale[] = "stale";
	const unsigned char first[] = "first-record";
	const unsigned char second[] = "second-record";
	char directory[] = "/tmp/wmcs-atomic.XXXXXX";
	char path[256];
	char stale_path[256];
	unsigned char buffer[sizeof(second)];
	struct stat state;
	int fd;

	if (!mkdtemp(directory) ||
	    snprintf(path, sizeof(path), "%s/record", directory) < 0 ||
	    snprintf(stale_path, sizeof(stale_path), "%s.tmp.%ld.0", path,
		     (long)getpid()) < 0 ||
	    write_file(stale_path, stale, sizeof(stale) - 1U) ||
	    wmcs_atomic_file_write(directory, path, first, sizeof(first) - 1U,
				   0600) ||
	    access(stale_path, F_OK) ||
	    stat(path, &state) || (state.st_mode & 0777U) != 0600) {
		(void)unlink(stale_path);
		(void)unlink(path);
		(void)rmdir(directory);
		return 1;
	}

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || read(fd, buffer, sizeof(first) - 1U) !=
		(ssize_t)(sizeof(first) - 1U) ||
	    memcmp(buffer, first, sizeof(first) - 1U) || close(fd)) {
		if (fd >= 0)
			close(fd);
		(void)unlink(stale_path);
		(void)unlink(path);
		(void)rmdir(directory);
		return 1;
	}

	if (wmcs_atomic_file_write(directory, path, second,
				   sizeof(second) - 1U, 0600)) {
		(void)unlink(stale_path);
		(void)unlink(path);
		(void)rmdir(directory);
		return 1;
	}

	{
		const enum wmcs_atomic_file_test_stage stages[] = {
			WMCS_ATOMIC_FILE_TEST_WRITE,
			WMCS_ATOMIC_FILE_TEST_FILE_FSYNC,
			WMCS_ATOMIC_FILE_TEST_RENAME,
			WMCS_ATOMIC_FILE_TEST_DIRECTORY_FSYNC,
		};
		size_t i;

		for (i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
			const unsigned char *expected =
				stages[i] == WMCS_ATOMIC_FILE_TEST_DIRECTORY_FSYNC ?
				first : second;
			char temporary[256];

			wmcs_atomic_file_test_fail_stage = stages[i];
			if (wmcs_atomic_file_write(directory, path, first,
						   sizeof(first) - 1U, 0600) != -ENOSPC ||
			    read_matches(path, expected, strlen((const char *)expected)) ||
			    snprintf(temporary, sizeof(temporary), "%s.tmp.%ld.1", path,
				     (long)getpid()) < 0 || access(temporary, F_OK) == 0) {
				(void)unlink(stale_path);
				(void)unlink(path);
				(void)rmdir(directory);
				return 1;
			}
		}
	}
	(void)unlink(stale_path);
	(void)unlink(path);
	return rmdir(directory) ? 1 : 0;
}
