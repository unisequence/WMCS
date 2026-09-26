// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libubox/blobmsg.h>

#include "roaming_event.h"

static void add_octet(struct blob_buf *buffer, const char *name,
		      uint32_t value, unsigned int width)
{
	switch (width) {
	case 8:
		blobmsg_add_u8(buffer, name, (uint8_t)value);
		break;
	case 16:
		blobmsg_add_u16(buffer, name, (uint16_t)value);
		break;
	case 32:
		blobmsg_add_u32(buffer, name, value);
		break;
	default:
		assert(0);
	}
}

static void check_response(unsigned int token_width, uint32_t token,
			   unsigned int status_width, uint32_t status,
			   bool expected)
{
	struct blob_buf buffer = {0};
	struct wmcs_btm_response response = {0};

	blob_buf_init(&buffer, 0);
	blobmsg_add_string(&buffer, "address", "02:00:00:00:00:01");
	add_octet(&buffer, "dialog-token", token, token_width);
	add_octet(&buffer, "status-code", status, status_width);
	assert(wmcs_roaming_parse_btm_response(buffer.head, &response) ==
	       expected);
	if (expected) {
		assert(!strcmp(response.address, "02:00:00:00:00:01"));
		assert(response.dialog_token == token);
		assert(response.status_code == status);
	}
	blob_buf_free(&buffer);
}

int main(void)
{
	struct blob_buf buffer = {0};
	struct wmcs_btm_response response = {0};

	/* 25.12 emits INT8; newer snapshots emit INT32. */
	check_response(8, 1, 8, 0, true);
	check_response(32, 1, 32, 0, true);
	check_response(8, 7, 8, 1, true);
	check_response(32, 255, 32, 255, true);
	check_response(32, 0, 32, 0, false);
	check_response(32, 256, 32, 0, false);
	check_response(32, 1, 32, 256, false);
	check_response(16, 1, 8, 0, false);
	check_response(8, 1, 16, 0, false);

	blob_buf_init(&buffer, 0);
	add_octet(&buffer, "dialog-token", 1, 8);
	add_octet(&buffer, "status-code", 0, 8);
	assert(!wmcs_roaming_parse_btm_response(buffer.head, &response));
	blob_buf_free(&buffer);

	puts("Roaming BTM event tests: ok");
	return 0;
}
