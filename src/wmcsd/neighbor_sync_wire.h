// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_NEIGHBOR_SYNC_WIRE_H
#define WMCS_NEIGHBOR_SYNC_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "control_wire.h"

#define WMCS_NR_CHALLENGE_SIZE 16U
#define WMCS_NR_REPORT_MAX 100U
#define WMCS_NR_SSID_MAX 32U
#define WMCS_NR_SET_MAX 16U

struct wmcs_nr_record {
	char ssid[WMCS_NR_SSID_MAX + 1U];
	uint8_t report[WMCS_NR_REPORT_MAX];
	size_t report_size;
};

enum wmcs_nr_reconcile_decision {
	WMCS_NR_UNCHANGED = 0,
	WMCS_NR_APPLY,
	WMCS_NR_CONFLICT,
};

bool wmcs_nr_record_valid(const struct wmcs_nr_record *record);
bool wmcs_nr_record_equal(const struct wmcs_nr_record *left,
			  const struct wmcs_nr_record *right);
bool wmcs_nr_set_equal(const struct wmcs_nr_record *left, size_t left_count,
		       const struct wmcs_nr_record *right, size_t right_count);
bool wmcs_nr_set_subset(const struct wmcs_nr_record *subset,
			size_t subset_count,
			const struct wmcs_nr_record *superset,
			size_t superset_count);
enum wmcs_nr_reconcile_decision wmcs_nr_reconcile_decide(
	const struct wmcs_nr_record *current, size_t current_count,
	const struct wmcs_nr_record *applied, size_t applied_count,
	bool applied_valid, const struct wmcs_nr_record *desired,
	size_t desired_count);
bool wmcs_nr_query_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			  const uint8_t challenge[WMCS_NR_CHALLENGE_SIZE]);
bool wmcs_nr_query_decode(uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE]);
bool wmcs_nr_reply_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			  const uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  const struct wmcs_nr_record *record);
bool wmcs_nr_reply_decode(uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  struct wmcs_nr_record *record,
			  const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE]);

#endif
