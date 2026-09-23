// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "atomic_file.h"

#define WMCS_ATOMIC_PATH_SIZE 512U
#define WMCS_ATOMIC_TEMP_ATTEMPTS 64U

#ifdef WMCS_ATOMIC_FILE_TEST
enum wmcs_atomic_file_test_stage wmcs_atomic_file_test_fail_stage;

static bool test_failure(enum wmcs_atomic_file_test_stage stage)
{
	if (wmcs_atomic_file_test_fail_stage != stage)
		return false;
	wmcs_atomic_file_test_fail_stage = WMCS_ATOMIC_FILE_TEST_NONE;
	errno = ENOSPC;
	return true;
}
#else
enum {
	WMCS_ATOMIC_FILE_TEST_WRITE = 0,
	WMCS_ATOMIC_FILE_TEST_FILE_FSYNC = 0,
	WMCS_ATOMIC_FILE_TEST_RENAME = 0,
	WMCS_ATOMIC_FILE_TEST_DIRECTORY_FSYNC = 0,
};

static bool test_failure(int stage)
{
	(void)stage;
	return false;
}
#endif

static int write_all(int fd, const unsigned char *data, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		ssize_t amount = write(fd, data + offset, size - offset);

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

int wmcs_atomic_file_sync_directory(const char *directory)
{
	int fd;
	int result = 0;

	if (!directory || !*directory)
		return -EINVAL;
	fd = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return -errno;
	if (fsync(fd))
		result = -errno;
	if (close(fd) && !result)
		result = -errno;
	return result;
}

int wmcs_atomic_file_write(const char *directory, const char *path,
			   const unsigned char *data, size_t size, mode_t mode)
{
	char temporary[WMCS_ATOMIC_PATH_SIZE];
	unsigned int attempt;
	int fd = -1;
	int length;
	int result;
	bool renamed = false;

	if (!directory || !*directory || !path || !*path || (!data && size))
		return -EINVAL;
	for (attempt = 0; attempt < WMCS_ATOMIC_TEMP_ATTEMPTS; attempt++) {
		length = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld.%u",
				  path, (long)getpid(), attempt);
		if (length < 0 || (size_t)length >= sizeof(temporary))
			return -ENAMETOOLONG;
		fd = open(temporary,
			  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
			  mode);
		if (fd >= 0)
			break;
		if (errno != EEXIST)
			return -errno;
	}
	if (fd < 0)
		return -EEXIST;
	if (test_failure(WMCS_ATOMIC_FILE_TEST_WRITE))
		result = -ENOSPC;
	else if (fchmod(fd, mode))
		result = -errno;
	else
		result = write_all(fd, data, size);
	if (!result && test_failure(WMCS_ATOMIC_FILE_TEST_FILE_FSYNC))
		result = -ENOSPC;
	else if (!result && fsync(fd))
		result = -errno;
	if (close(fd) && !result)
		result = -errno;
	if (!result && test_failure(WMCS_ATOMIC_FILE_TEST_RENAME))
		result = -ENOSPC;
	else if (!result && rename(temporary, path))
		result = -errno;
	if (!result) {
		renamed = true;
		if (test_failure(WMCS_ATOMIC_FILE_TEST_DIRECTORY_FSYNC))
			result = -ENOSPC;
		else
			result = wmcs_atomic_file_sync_directory(directory);
	}
	if (result && !renamed)
		(void)unlink(temporary);
	return result;
}
