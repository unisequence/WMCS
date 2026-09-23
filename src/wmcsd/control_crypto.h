// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_CONTROL_CRYPTO_H
#define WMCS_CONTROL_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

#include "control_wire.h"
#include "identity.h"

bool wmcs_control_id_from_hex(
	const char hex[WMCS_IDENTITY_PEER_ID_SIZE + 1U],
	uint8_t output[WMCS_CONTROL_NODE_ID_SIZE]);
void wmcs_control_id_to_hex(
	const uint8_t input[WMCS_CONTROL_NODE_ID_SIZE],
	char output[WMCS_IDENTITY_PEER_ID_SIZE + 1U]);
int wmcs_control_local_id(const struct wmcs_identity *identity,
			  uint8_t output[WMCS_CONTROL_NODE_ID_SIZE]);

int wmcs_control_seal(
	struct wmcs_control_packet *packet,
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	const uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE]);
int wmcs_control_open(
	const struct wmcs_control_packet *packet,
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE]);

#endif
