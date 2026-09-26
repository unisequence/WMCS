// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "neighbor_sync_wire.h"

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
	uint8_t payload[WMCS_CONTROL_PAYLOAD_SIZE];
	uint8_t challenge[WMCS_NR_CHALLENGE_SIZE] = {1};
	uint8_t decoded_challenge[WMCS_NR_CHALLENGE_SIZE] = {0};
	struct wmcs_nr_record first = record(0x55);
	struct wmcs_nr_record second = record(0x66);
	struct wmcs_nr_record decoded = {0};
	struct wmcs_nr_record duplicates[2] = {first, first};
	struct wmcs_nr_record distinct[2] = {first, second};

	assert(wmcs_nr_record_valid(&first));
	assert(!wmcs_nr_record_equal(&first, &second));
	assert(wmcs_nr_query_encode(payload, challenge));
	assert(wmcs_nr_query_decode(decoded_challenge, payload));
	assert(!memcmp(challenge, decoded_challenge, sizeof(challenge)));
	payload[31] = 1;
	assert(!wmcs_nr_query_decode(decoded_challenge, payload));
	assert(wmcs_nr_reply_encode(payload, challenge, &first));
	assert(wmcs_nr_reply_decode(decoded_challenge, &decoded, payload));
	assert(wmcs_nr_record_equal(&first, &decoded));
	payload[WMCS_CONTROL_PAYLOAD_SIZE - 1U] = 1;
	assert(!wmcs_nr_reply_decode(decoded_challenge, &decoded, payload));
	payload[WMCS_CONTROL_PAYLOAD_SIZE - 1U] = 0;
	payload[52] = 0x01;
	assert(!wmcs_nr_reply_decode(decoded_challenge, &decoded, payload));

	assert(!wmcs_nr_set_equal(duplicates, 2, distinct, 2));
	assert(wmcs_nr_set_subset(&first, 1, distinct, 2));
	assert(!wmcs_nr_set_subset(&second, 1, &first, 1));
	assert(wmcs_nr_set_equal(distinct, 2,
				 (struct wmcs_nr_record[]){second, first}, 2));
	assert(wmcs_nr_reconcile_decide(NULL, 0, NULL, 0, false,
					 &first, 1) == WMCS_NR_APPLY);
	assert(wmcs_nr_reconcile_decide(&first, 1, NULL, 0, false,
					 &first, 1) == WMCS_NR_UNCHANGED);
	assert(wmcs_nr_reconcile_decide(&first, 1, NULL, 0, false,
					 &second, 1) == WMCS_NR_CONFLICT);
	assert(wmcs_nr_reconcile_decide(&first, 1, &first, 1, true,
					 &second, 1) == WMCS_NR_APPLY);
	assert(wmcs_nr_reconcile_decide(&first, 1, &second, 1, true,
					 NULL, 0) == WMCS_NR_CONFLICT);
	assert(wmcs_nr_reconcile_decide(&first, 1, &first, 1, true,
					 NULL, 0) == WMCS_NR_APPLY);
	assert(wmcs_nr_reconcile_decide(NULL, 0, NULL, 0, false,
					 NULL, 0) == WMCS_NR_UNCHANGED);
	first.report[0] = 0x01;
	assert(!wmcs_nr_record_valid(&first));
	puts("Neighbor sync wire and ownership: ok");
	return 0;
}
