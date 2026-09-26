// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_ROAMING_EVENT_H
#define WMCS_ROAMING_EVENT_H

#include <stdbool.h>
#include <stdint.h>

struct blob_attr;

struct wmcs_btm_response {
	const char *address;
	uint8_t dialog_token;
	uint8_t status_code;
};

bool wmcs_roaming_parse_btm_response(struct blob_attr *message,
				     struct wmcs_btm_response *response);

#endif
