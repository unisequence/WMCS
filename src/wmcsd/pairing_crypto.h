// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_PAIRING_CRYPTO_H
#define WMCS_PAIRING_CRYPTO_H

#include <stdint.h>

#include "identity.h"
#include "pairing_wire.h"

#define WMCS_PAIRING_SAS_SIZE 15U

int wmcs_pairing_sign_request(
	const struct wmcs_identity *identity,
	struct wmcs_pairing_message *request);
int wmcs_pairing_verify_request(const struct wmcs_pairing_message *request);
int wmcs_pairing_sign_response(
	const struct wmcs_identity *identity,
	const struct wmcs_pairing_message *request,
	struct wmcs_pairing_message *response,
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE]);
int wmcs_pairing_verify_response(
	const struct wmcs_pairing_message *request,
	const struct wmcs_pairing_message *response,
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE]);
int wmcs_pairing_sas(
	const uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE],
	char output[WMCS_PAIRING_SAS_SIZE]);

#endif
