// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_ROAMING_POLICY_H
#define WMCS_ROAMING_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wmcs_roaming_target_decision {
	WMCS_ROAMING_TARGET_MEASURED,
	WMCS_ROAMING_TARGET_SINGLE_UNMEASURED,
	WMCS_ROAMING_TARGET_NO_REPORT,
	WMCS_ROAMING_TARGET_STALE,
	WMCS_ROAMING_TARGET_WEAK,
	WMCS_ROAMING_TARGET_INVALID,
	WMCS_ROAMING_TARGET_OVERFLOW,
};

static inline enum wmcs_roaming_target_decision wmcs_roaming_target_decide(
	bool report_seen, bool report_time_valid, uint64_t report_age_ms,
	int measured_margin_db, int required_margin_db, size_t candidate_count,
	size_t current_neighbor_count, size_t measured_index, bool overflow,
	uint64_t freshness_ms)
{
	if (overflow)
		return WMCS_ROAMING_TARGET_OVERFLOW;
	if (!report_seen)
		return candidate_count == 1U && current_neighbor_count == 1U ?
			WMCS_ROAMING_TARGET_SINGLE_UNMEASURED :
			WMCS_ROAMING_TARGET_NO_REPORT;
	if (!report_time_valid || measured_index >= candidate_count)
		return WMCS_ROAMING_TARGET_INVALID;
	if (report_age_ms > freshness_ms)
		return WMCS_ROAMING_TARGET_STALE;
	if (measured_margin_db < required_margin_db)
		return WMCS_ROAMING_TARGET_WEAK;
	return WMCS_ROAMING_TARGET_MEASURED;
}

static inline bool wmcs_roaming_force_gate(bool enabled, bool observed,
	bool monitor_active, bool neighbor_unchanged, bool overflow,
	size_t neighbor_count, int source_dbm, int force_trigger_dbm,
	unsigned int weak_samples, unsigned int required_samples)
{
	return enabled && observed && monitor_active && neighbor_unchanged &&
		!overflow && neighbor_count == 1U &&
		source_dbm <= force_trigger_dbm &&
		weak_samples >= required_samples;
}

#endif
