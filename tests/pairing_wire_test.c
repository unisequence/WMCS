// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <string.h>

#include "pairing_wire.h"

static struct wmcs_pairing_message sample(uint8_t type)
{
	struct wmcs_pairing_message message = {.type = type};
	size_t i;

	for (i = 0; i < sizeof(message.nonce); i++)
		message.nonce[i] = (uint8_t)(i + 1U);
	message.identity_public[0] = 4;
	message.ephemeral_public[0] = 4;
	for (i = 1; i < sizeof(message.identity_public); i++) {
		message.identity_public[i] = (uint8_t)i;
		message.ephemeral_public[i] = (uint8_t)(i + 1U);
	}
	for (i = 0; i < sizeof(message.signature); i++)
		message.signature[i] = (uint8_t)(i + 1U);
	return message;
}

int main(void)
{
	struct wmcs_pairing_message request = sample(WMCS_PAIRING_REQUEST);
	struct wmcs_pairing_message decoded;
	uint8_t wire[WMCS_PAIRING_WIRE_SIZE];

	assert(wmcs_pairing_encode(wire, &request));
	assert(wmcs_pairing_decode(&decoded, wire, sizeof(wire)));
	assert(!memcmp(&decoded, &request, sizeof(request)));
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire) - 1U));
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire) + 1U));

	wire[0] ^= 1U;
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_pairing_encode(wire, &request));
	wire[4] = 1;
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_pairing_encode(wire, &request));
	wire[7]--;
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_pairing_encode(wire, &request));
	wire[223] = 1;
	assert(!wmcs_pairing_decode(&decoded, wire, sizeof(wire)));

	request.type = 3;
	assert(!wmcs_pairing_encode(wire, &request));
	request = sample(WMCS_PAIRING_REQUEST);
	memset(request.nonce, 0, sizeof(request.nonce));
	assert(!wmcs_pairing_encode(wire, &request));
	request = sample(WMCS_PAIRING_REQUEST);
	request.identity_public[0] = 3;
	assert(!wmcs_pairing_encode(wire, &request));
	request = sample(WMCS_PAIRING_REQUEST);
	memset(request.signature, 0, sizeof(request.signature));
	assert(!wmcs_pairing_encode(wire, &request));
	return 0;
}
