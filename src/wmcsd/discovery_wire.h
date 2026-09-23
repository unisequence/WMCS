// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_DISCOVERY_WIRE_H
#define WMCS_DISCOVERY_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WMCS_DISCOVERY_WIRE_SIZE 40U
#define WMCS_DISCOVERY_INSTANCE_SIZE 16U

enum wmcs_discovery_message_type {
	WMCS_DISCOVERY_PROBE = 1,
	WMCS_DISCOVERY_ANNOUNCEMENT = 2,
};

enum wmcs_discovery_wire_role {
	WMCS_DISCOVERY_ROLE_CONTROLLER = 1,
	WMCS_DISCOVERY_ROLE_AGENT = 2,
};

struct wmcs_discovery_message {
	uint8_t type;
	uint64_t nonce;
	uint8_t instance_id[WMCS_DISCOVERY_INSTANCE_SIZE];
	uint8_t role;
};

bool wmcs_discovery_encode(uint8_t output[WMCS_DISCOVERY_WIRE_SIZE],
			   const struct wmcs_discovery_message *message);
bool wmcs_discovery_decode(struct wmcs_discovery_message *message,
			   const uint8_t *input, size_t input_size);

#endif
