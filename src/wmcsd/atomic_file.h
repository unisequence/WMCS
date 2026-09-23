// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_ATOMIC_FILE_H
#define WMCS_ATOMIC_FILE_H

#include <stddef.h>
#include <sys/types.h>

/*
 * Write a protected record through a unique temporary file, fsync both the
 * file and its parent directory, then publish it with rename(2).
 *
 * Temporary names include a per-process counter.  A stale file from an
 * interrupted process is never reused or removed; the next attempt simply
 * selects another name.  This keeps recovery independent from cleanup races.
 */
int wmcs_atomic_file_write(const char *directory, const char *path,
			   const unsigned char *data, size_t size, mode_t mode);
int wmcs_atomic_file_sync_directory(const char *directory);

#ifdef WMCS_ATOMIC_FILE_TEST
enum wmcs_atomic_file_test_stage {
	WMCS_ATOMIC_FILE_TEST_NONE = 0,
	WMCS_ATOMIC_FILE_TEST_WRITE,
	WMCS_ATOMIC_FILE_TEST_FILE_FSYNC,
	WMCS_ATOMIC_FILE_TEST_RENAME,
	WMCS_ATOMIC_FILE_TEST_DIRECTORY_FSYNC,
};

extern enum wmcs_atomic_file_test_stage wmcs_atomic_file_test_fail_stage;
#endif

#endif
