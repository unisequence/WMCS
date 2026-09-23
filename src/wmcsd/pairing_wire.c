// SPDX-License-Identifier: Apache-2.0

#include <string.h>

#include "pairing_wire.h"

static const uint8_t magic[] = {'W', 'M', 'C', 'P'};

static bool any_nonzero(const uint8_t *input, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (input[i])
			return true;
	}
	return false;
}

static bool message_valid(const struct wmcs_pairing_message *message)
{
	if (!message || (message->type != WMCS_PAIRING_REQUEST &&
			 message->type != WMCS_PAIRING_RESPONSE))
		return false;
	if (!any_nonzero(message->nonce, sizeof(message->nonce)) ||
	    message->identity_public[0] != 4 ||
	    message->ephemeral_public[0] != 4 ||
	    !any_nonzero(message->signature, sizeof(message->signature)))
		return false;
	return true;
}

bool wmcs_pairing_encode(uint8_t output[WMCS_PAIRING_WIRE_SIZE],
			 const struct wmcs_pairing_message *message)
{
	if (!output || !message_valid(message))
		return false;
	memset(output, 0, WMCS_PAIRING_WIRE_SIZE);
	memcpy(output, magic, sizeof(magic));
	output[4] = 0;
	output[5] = message->type;
	output[6] = (uint8_t)(WMCS_PAIRING_WIRE_SIZE >> 8U);
	output[7] = (uint8_t)WMCS_PAIRING_WIRE_SIZE;
	memcpy(&output[8], message->nonce, sizeof(message->nonce));
	memcpy(&output[24], message->identity_public,
	       sizeof(message->identity_public));
	memcpy(&output[89], message->ephemeral_public,
	       sizeof(message->ephemeral_public));
	memcpy(&output[154], message->signature, sizeof(message->signature));
	return true;
}

bool wmcs_pairing_decode(struct wmcs_pairing_message *message,
			 const uint8_t *input, size_t input_size)
{
	struct wmcs_pairing_message decoded = {0};
	size_t i;

	if (!message || !input || input_size != WMCS_PAIRING_WIRE_SIZE)
		return false;
	if (memcmp(input, magic, sizeof(magic)) || input[4] != 0 ||
	    input[6] != (uint8_t)(WMCS_PAIRING_WIRE_SIZE >> 8U) ||
	    input[7] != (uint8_t)WMCS_PAIRING_WIRE_SIZE)
		return false;
	for (i = 218; i < WMCS_PAIRING_WIRE_SIZE; i++) {
		if (input[i])
			return false;
	}
	decoded.type = input[5];
	memcpy(decoded.nonce, &input[8], sizeof(decoded.nonce));
	memcpy(decoded.identity_public, &input[24],
	       sizeof(decoded.identity_public));
	memcpy(decoded.ephemeral_public, &input[89],
	       sizeof(decoded.ephemeral_public));
	memcpy(decoded.signature, &input[154], sizeof(decoded.signature));
	if (!message_valid(&decoded))
		return false;
	*message = decoded;
	return true;
}
