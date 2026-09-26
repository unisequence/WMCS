// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdio.h>

#include "roaming_policy.h"

static enum wmcs_roaming_target_decision decide(bool report_seen,
	uint64_t report_age_ms, int margin_db, size_t survey_count,
	size_t current_count, bool overflow)
{
	return wmcs_roaming_target_decide(report_seen, true, report_age_ms,
		margin_db, 8, survey_count, current_count, 0, overflow, 10000);
}

static void test_target_selection(void)
{
	assert(decide(false, 0, 0, 1, 1, false) ==
	       WMCS_ROAMING_TARGET_SINGLE_UNMEASURED);
	assert(decide(false, 0, 0, 2, 2, false) ==
	       WMCS_ROAMING_TARGET_NO_REPORT);
	assert(decide(false, 0, 0, 1, 2, false) ==
	       WMCS_ROAMING_TARGET_NO_REPORT);
	assert(decide(false, 0, 0, 1, 1, true) ==
	       WMCS_ROAMING_TARGET_OVERFLOW);
	assert(decide(true, 0, 7, 1, 1, false) ==
	       WMCS_ROAMING_TARGET_WEAK);
	assert(decide(true, 10001, 20, 1, 1, false) ==
	       WMCS_ROAMING_TARGET_STALE);
	assert(decide(true, 10000, 8, 1, 1, false) ==
	       WMCS_ROAMING_TARGET_MEASURED);
	assert(wmcs_roaming_target_decide(true, false, 0, 20, 8, 1, 1,
		0, false, 10000) == WMCS_ROAMING_TARGET_INVALID);
	assert(wmcs_roaming_target_decide(true, true, 0, 20, 8, 1, 1,
		1, false, 10000) == WMCS_ROAMING_TARGET_INVALID);
}

static void test_force_gate(void)
{
	assert(wmcs_roaming_force_gate(true, true, true, true, false,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(false, true, true, true, false,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, false, true, true, false,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, false, true, false,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, true, false, false,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, true, true, true,
		1, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, true, true, false,
		2, -80, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, true, true, false,
		1, -77, -78, 5, 5));
	assert(!wmcs_roaming_force_gate(true, true, true, true, false,
		1, -80, -78, 4, 5));
}

int main(void)
{
	test_target_selection();
	test_force_gate();
	puts("Roaming policy tests: ok");
	return 0;
}
