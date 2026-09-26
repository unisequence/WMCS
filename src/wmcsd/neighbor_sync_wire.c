// SPDX-License-Identifier: Apache-2.0

#include <string.h>

#include "neighbor_sync_wire.h"

#define WMCS_NR_WIRE_VERSION 1U
#define WMCS_NR_SSID_OFFSET 20U
#define WMCS_NR_REPORT_OFFSET 52U

static bool all_zero(const uint8_t *data, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (data[i])
			return false;
	}
	return true;
}

static size_t bounded_string_size(const char *value, size_t maximum)
{
	size_t i;

	for (i = 0; i <= maximum; i++) {
		if (!value[i])
			return i;
	}
	return maximum + 1U;
}

bool wmcs_nr_record_valid(const struct wmcs_nr_record *record)
{
	size_t ssid_size;
	size_t i;
	bool nonzero_bssid = false;

	if (!record || record->report_size < 13U ||
	    record->report_size > WMCS_NR_REPORT_MAX)
		return false;
	ssid_size = bounded_string_size(record->ssid, WMCS_NR_SSID_MAX);
	if (!ssid_size || ssid_size > WMCS_NR_SSID_MAX ||
	    (record->report[0] & 0x01U) || !record->report[10] ||
	    !record->report[11])
		return false;
	for (i = 0; i < 6U; i++)
		nonzero_bssid |= record->report[i] != 0;
	return nonzero_bssid;
}

bool wmcs_nr_record_equal(const struct wmcs_nr_record *left,
			  const struct wmcs_nr_record *right)
{
	return wmcs_nr_record_valid(left) && wmcs_nr_record_valid(right) &&
	       !strcmp(left->ssid, right->ssid) &&
	       left->report_size == right->report_size &&
	       !memcmp(left->report, right->report, left->report_size);
}

bool wmcs_nr_set_subset(const struct wmcs_nr_record *subset,
			size_t subset_count,
			const struct wmcs_nr_record *superset,
			size_t superset_count)
{
	bool matched[WMCS_NR_SET_MAX] = {0};
	size_t i;
	size_t j;

	if (subset_count > superset_count ||
	    superset_count > WMCS_NR_SET_MAX ||
	    (subset_count && (!subset || !superset)))
		return false;
	for (i = 0; i < subset_count; i++) {
		for (j = 0; j < superset_count; j++) {
			if (!matched[j] &&
			    wmcs_nr_record_equal(&subset[i], &superset[j])) {
				matched[j] = true;
				break;
			}
		}
		if (j == superset_count)
			return false;
	}
	return true;
}

bool wmcs_nr_set_equal(const struct wmcs_nr_record *left, size_t left_count,
		       const struct wmcs_nr_record *right, size_t right_count)
{
	return left_count == right_count &&
	       wmcs_nr_set_subset(left, left_count, right, right_count);
}

enum wmcs_nr_reconcile_decision wmcs_nr_reconcile_decide(
	const struct wmcs_nr_record *current, size_t current_count,
	const struct wmcs_nr_record *applied, size_t applied_count,
	bool applied_valid, const struct wmcs_nr_record *desired,
	size_t desired_count)
{
	if (wmcs_nr_set_equal(current, current_count, desired, desired_count))
		return WMCS_NR_UNCHANGED;
	if (!current_count ||
	    (applied_valid && wmcs_nr_set_equal(current, current_count,
					   applied, applied_count)))
		return WMCS_NR_APPLY;
	return WMCS_NR_CONFLICT;
}

bool wmcs_nr_query_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			  const uint8_t challenge[WMCS_NR_CHALLENGE_SIZE])
{
	if (!output || !challenge || all_zero(challenge, WMCS_NR_CHALLENGE_SIZE))
		return false;
	memset(output, 0, WMCS_CONTROL_PAYLOAD_SIZE);
	output[1] = WMCS_NR_WIRE_VERSION;
	memcpy(&output[2], challenge, WMCS_NR_CHALLENGE_SIZE);
	return true;
}

bool wmcs_nr_query_decode(uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE])
{
	if (!challenge || !input || input[0] ||
	    input[1] != WMCS_NR_WIRE_VERSION ||
	    all_zero(&input[2], WMCS_NR_CHALLENGE_SIZE) ||
	    !all_zero(&input[18], WMCS_CONTROL_PAYLOAD_SIZE - 18U))
		return false;
	memcpy(challenge, &input[2], WMCS_NR_CHALLENGE_SIZE);
	return true;
}

bool wmcs_nr_reply_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			  const uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  const struct wmcs_nr_record *record)
{
	size_t ssid_size;

	if (!output || !challenge ||
	    all_zero(challenge, WMCS_NR_CHALLENGE_SIZE) ||
	    !wmcs_nr_record_valid(record))
		return false;
	ssid_size = strlen(record->ssid);
	memset(output, 0, WMCS_CONTROL_PAYLOAD_SIZE);
	output[1] = WMCS_NR_WIRE_VERSION;
	memcpy(&output[2], challenge, WMCS_NR_CHALLENGE_SIZE);
	output[18] = (uint8_t)ssid_size;
	output[19] = (uint8_t)record->report_size;
	memcpy(&output[WMCS_NR_SSID_OFFSET], record->ssid, ssid_size);
	memcpy(&output[WMCS_NR_REPORT_OFFSET], record->report,
	       record->report_size);
	return true;
}

bool wmcs_nr_reply_decode(uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
			  struct wmcs_nr_record *record,
			  const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE])
{
	struct wmcs_nr_record decoded = {0};
	size_t ssid_size;
	size_t report_size;

	if (!challenge || !record || !input || input[0] ||
	    input[1] != WMCS_NR_WIRE_VERSION ||
	    all_zero(&input[2], WMCS_NR_CHALLENGE_SIZE))
		return false;
	ssid_size = input[18];
	report_size = input[19];
	if (!ssid_size || ssid_size > WMCS_NR_SSID_MAX ||
	    report_size < 13U || report_size > WMCS_NR_REPORT_MAX ||
	    memchr(&input[WMCS_NR_SSID_OFFSET], 0, ssid_size) ||
	    !all_zero(&input[WMCS_NR_SSID_OFFSET + ssid_size],
		      WMCS_NR_SSID_MAX - ssid_size) ||
	    !all_zero(&input[WMCS_NR_REPORT_OFFSET + report_size],
		      WMCS_NR_REPORT_MAX - report_size) ||
	    !all_zero(&input[WMCS_NR_REPORT_OFFSET + WMCS_NR_REPORT_MAX],
		      WMCS_CONTROL_PAYLOAD_SIZE -
		      WMCS_NR_REPORT_OFFSET - WMCS_NR_REPORT_MAX))
		return false;
	memcpy(decoded.ssid, &input[WMCS_NR_SSID_OFFSET], ssid_size);
	memcpy(decoded.report, &input[WMCS_NR_REPORT_OFFSET], report_size);
	decoded.report_size = report_size;
	if (!wmcs_nr_record_valid(&decoded))
		return false;
	memcpy(challenge, &input[2], WMCS_NR_CHALLENGE_SIZE);
	*record = decoded;
	return true;
}
