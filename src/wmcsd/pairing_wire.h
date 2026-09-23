// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_PAIRING_WIRE_H
#define WMCS_PAIRING_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WMCS_PAIRING_WIRE_SIZE 224U
#define WMCS_PAIRING_NONCE_SIZE 16U
#define WMCS_PAIRING_PUBLIC_KEY_SIZE 65U
#define WMCS_PAIRING_SIGNATURE_SIZE 64U

enum wmcs_pairing_message_type {
	WMCS_PAIRING_REQUEST = 1,
	WMCS_PAIRING_RESPONSE = 2,
};

struct wmcs_pairing_message {
	uint8_t type;
	uint8_t nonce[WMCS_PAIRING_NONCE_SIZE];
	uint8_t identity_public[WMCS_PAIRING_PUBLIC_KEY_SIZE];
	uint8_t ephemeral_public[WMCS_PAIRING_PUBLIC_KEY_SIZE];
	uint8_t signature[WMCS_PAIRING_SIGNATURE_SIZE];
};

bool wmcs_pairing_encode(uint8_t output[WMCS_PAIRING_WIRE_SIZE],
			 const struct wmcs_pairing_message *message);
bool wmcs_pairing_decode(struct wmcs_pairing_message *message,
			 const uint8_t *input, size_t input_size);

#endif
