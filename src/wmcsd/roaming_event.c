// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include <stdint.h>

#include <libubox/blobmsg.h>

#include "roaming_event.h"

enum {
	BTM_RESPONSE_ADDRESS,
	BTM_RESPONSE_DIALOG_TOKEN,
	BTM_RESPONSE_STATUS_CODE,
	__BTM_RESPONSE_MAX,
};

/* OpenWrt hostapd emits these two octets as blobmsg INT8 in 25.12 and
 * INT32 in newer snapshots. Validate their actual type and range below. */
static const struct blobmsg_policy btm_response_policy[__BTM_RESPONSE_MAX] = {
	[BTM_RESPONSE_ADDRESS] = { "address", BLOBMSG_TYPE_STRING },
	[BTM_RESPONSE_DIALOG_TOKEN] = { "dialog-token", BLOBMSG_TYPE_UNSPEC },
	[BTM_RESPONSE_STATUS_CODE] = { "status-code", BLOBMSG_TYPE_UNSPEC },
};

static bool parse_octet(struct blob_attr *attribute, uint8_t *value)
{
	uint64_t decoded;
	int type;

	if (!attribute || !value)
		return false;
	type = blobmsg_type(attribute);
	if (type != BLOBMSG_TYPE_INT8 && type != BLOBMSG_TYPE_INT32)
		return false;
	decoded = blobmsg_cast_u64(attribute);
	if (decoded > UINT8_MAX)
		return false;
	*value = (uint8_t)decoded;
	return true;
}

bool wmcs_roaming_parse_btm_response(struct blob_attr *message,
				     struct wmcs_btm_response *response)
{
	struct blob_attr *attributes[__BTM_RESPONSE_MAX] = {0};
	struct wmcs_btm_response parsed;

	if (!message || !response ||
	    blobmsg_parse(btm_response_policy, __BTM_RESPONSE_MAX, attributes,
			  blob_data(message), blob_len(message)) < 0 ||
	    !attributes[BTM_RESPONSE_ADDRESS] ||
	    !parse_octet(attributes[BTM_RESPONSE_DIALOG_TOKEN],
			 &parsed.dialog_token) ||
	    !parse_octet(attributes[BTM_RESPONSE_STATUS_CODE],
			 &parsed.status_code) ||
	    !parsed.dialog_token)
		return false;
	parsed.address = blobmsg_get_string(attributes[BTM_RESPONSE_ADDRESS]);
	if (!parsed.address || !*parsed.address)
		return false;
	*response = parsed;
	return true;
}
