// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "discovery_wire.h"

static struct wmcs_discovery_message sample_message(uint8_t type, uint8_t role)
{
	struct wmcs_discovery_message message = {
		.type = type,
		.nonce = UINT64_C(0x0102030405060708),
		.role = role,
	};
	size_t i;

	for (i = 0; i < sizeof(message.instance_id); i++)
		message.instance_id[i] = (uint8_t)(i + 1U);

	return message;
}

int main(void)
{
	struct wmcs_discovery_message probe = sample_message(
		WMCS_DISCOVERY_PROBE, WMCS_DISCOVERY_ROLE_CONTROLLER);
	struct wmcs_discovery_message announcement = sample_message(
		WMCS_DISCOVERY_ANNOUNCEMENT, WMCS_DISCOVERY_ROLE_AGENT);
	struct wmcs_discovery_message decoded;
	uint8_t wire[WMCS_DISCOVERY_WIRE_SIZE];

	assert(wmcs_discovery_encode(wire, &probe));
	assert(wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(decoded.type == probe.type);
	assert(decoded.nonce == probe.nonce);
	assert(decoded.role == probe.role);
	assert(!memcmp(decoded.instance_id, probe.instance_id, sizeof(probe.instance_id)));

	assert(wmcs_discovery_encode(wire, &announcement));
	assert(wmcs_discovery_decode(&decoded, wire, sizeof(wire)));

	wire[0] ^= 1U;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_discovery_encode(wire, &announcement));
	wire[4] = 1;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_discovery_encode(wire, &announcement));
	wire[7]--;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_discovery_encode(wire, &announcement));
	wire[39] = 1;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire) - 1U));
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire) + 1U));

	assert(wmcs_discovery_encode(wire, &announcement));
	wire[5] = 3;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_discovery_encode(wire, &announcement));
	wire[32] = WMCS_DISCOVERY_ROLE_CONTROLLER;
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));
	assert(wmcs_discovery_encode(wire, &announcement));
	memset(&wire[16], 0, WMCS_DISCOVERY_INSTANCE_SIZE);
	assert(!wmcs_discovery_decode(&decoded, wire, sizeof(wire)));

	memset(probe.instance_id, 0, sizeof(probe.instance_id));
	assert(!wmcs_discovery_encode(wire, &probe));
	probe = sample_message(WMCS_DISCOVERY_PROBE, WMCS_DISCOVERY_ROLE_AGENT);
	assert(!wmcs_discovery_encode(wire, &probe));

	return 0;
}
