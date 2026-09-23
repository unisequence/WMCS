// SPDX-License-Identifier: Apache-2.0

#include <string.h>

#include "discovery_wire.h"

static const uint8_t magic[] = {'W', 'M', 'C', 'S'};

static bool instance_id_valid(const uint8_t instance_id[WMCS_DISCOVERY_INSTANCE_SIZE])
{
	size_t i;

	for (i = 0; i < WMCS_DISCOVERY_INSTANCE_SIZE; i++) {
		if (instance_id[i])
			return true;
	}

	return false;
}

static bool message_valid(const struct wmcs_discovery_message *message)
{
	if (!message || !instance_id_valid(message->instance_id))
		return false;

	if (message->type == WMCS_DISCOVERY_PROBE)
		return message->role == WMCS_DISCOVERY_ROLE_CONTROLLER;

	if (message->type == WMCS_DISCOVERY_ANNOUNCEMENT)
		return message->role == WMCS_DISCOVERY_ROLE_AGENT;

	return false;
}

bool wmcs_discovery_encode(uint8_t output[WMCS_DISCOVERY_WIRE_SIZE],
			   const struct wmcs_discovery_message *message)
{
	size_t i;

	if (!output || !message_valid(message))
		return false;

	memset(output, 0, WMCS_DISCOVERY_WIRE_SIZE);
	memcpy(output, magic, sizeof(magic));
	output[4] = 0;
	output[5] = message->type;
	output[6] = 0;
	output[7] = WMCS_DISCOVERY_WIRE_SIZE;

	for (i = 0; i < sizeof(message->nonce); i++)
		output[8 + i] = (uint8_t)(message->nonce >> (56U - i * 8U));

	memcpy(&output[16], message->instance_id, WMCS_DISCOVERY_INSTANCE_SIZE);
	output[32] = message->role;

	return true;
}

bool wmcs_discovery_decode(struct wmcs_discovery_message *message,
			   const uint8_t *input, size_t input_size)
{
	struct wmcs_discovery_message decoded = {0};
	size_t i;

	if (!message || !input || input_size != WMCS_DISCOVERY_WIRE_SIZE)
		return false;

	if (memcmp(input, magic, sizeof(magic)) || input[4] != 0 ||
	    input[6] != 0 || input[7] != WMCS_DISCOVERY_WIRE_SIZE)
		return false;

	for (i = 33; i < WMCS_DISCOVERY_WIRE_SIZE; i++) {
		if (input[i] != 0)
			return false;
	}

	decoded.type = input[5];
	for (i = 0; i < sizeof(decoded.nonce); i++)
		decoded.nonce = (decoded.nonce << 8U) | input[8 + i];
	memcpy(decoded.instance_id, &input[16], WMCS_DISCOVERY_INSTANCE_SIZE);
	decoded.role = input[32];

	if (!message_valid(&decoded))
		return false;

	*message = decoded;
	return true;
}
