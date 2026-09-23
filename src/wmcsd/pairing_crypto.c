// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "pairing_crypto.h"

static const uint8_t request_domain[] = "WMCS-PAIR-REQUEST-V0";
static const uint8_t response_domain[] = "WMCS-PAIR-RESPONSE-V0";
static const uint8_t sas_domain[] = "WMCS-PAIR-SAS-V0";

_Static_assert(WMCS_PAIRING_PUBLIC_KEY_SIZE == WMCS_IDENTITY_PUBLIC_SIZE,
	       "pairing and identity public key sizes must match");
_Static_assert(WMCS_PAIRING_SIGNATURE_SIZE == WMCS_IDENTITY_SIGNATURE_SIZE,
	       "pairing and identity signature sizes must match");

static size_t request_transcript(const struct wmcs_pairing_message *request,
				 uint8_t *output)
{
	size_t offset = 0;

	memcpy(&output[offset], request_domain, sizeof(request_domain) - 1U);
	offset += sizeof(request_domain) - 1U;
	memcpy(&output[offset], request->nonce, sizeof(request->nonce));
	offset += sizeof(request->nonce);
	memcpy(&output[offset], request->identity_public,
	       sizeof(request->identity_public));
	offset += sizeof(request->identity_public);
	memcpy(&output[offset], request->ephemeral_public,
	       sizeof(request->ephemeral_public));
	offset += sizeof(request->ephemeral_public);
	return offset;
}

static size_t response_transcript(const struct wmcs_pairing_message *request,
				  const struct wmcs_pairing_message *response,
				  uint8_t *output)
{
	size_t offset = 0;

	memcpy(&output[offset], response_domain, sizeof(response_domain) - 1U);
	offset += sizeof(response_domain) - 1U;
	memcpy(&output[offset], request->nonce, sizeof(request->nonce));
	offset += sizeof(request->nonce);
	memcpy(&output[offset], request->identity_public,
	       sizeof(request->identity_public));
	offset += sizeof(request->identity_public);
	memcpy(&output[offset], request->ephemeral_public,
	       sizeof(request->ephemeral_public));
	offset += sizeof(request->ephemeral_public);
	memcpy(&output[offset], response->identity_public,
	       sizeof(response->identity_public));
	offset += sizeof(response->identity_public);
	memcpy(&output[offset], response->ephemeral_public,
	       sizeof(response->ephemeral_public));
	offset += sizeof(response->ephemeral_public);
	return offset;
}

int wmcs_pairing_sign_request(
	const struct wmcs_identity *identity,
	struct wmcs_pairing_message *request)
{
	uint8_t transcript[sizeof(request_domain) - 1U +
			   WMCS_PAIRING_NONCE_SIZE +
			   WMCS_PAIRING_PUBLIC_KEY_SIZE * 2U];
	size_t transcript_size;
	int result;

	if (!identity || !request || request->type != WMCS_PAIRING_REQUEST)
		return -EINVAL;
	transcript_size = request_transcript(request, transcript);
	result = wmcs_identity_sign(identity, transcript, transcript_size,
				    request->signature);
	wmcs_secure_zero(transcript, sizeof(transcript));
	return result;
}

int wmcs_pairing_verify_request(const struct wmcs_pairing_message *request)
{
	uint8_t transcript[sizeof(request_domain) - 1U +
			   WMCS_PAIRING_NONCE_SIZE +
			   WMCS_PAIRING_PUBLIC_KEY_SIZE * 2U];
	size_t transcript_size;
	int result;

	if (!request || request->type != WMCS_PAIRING_REQUEST)
		return -EINVAL;
	transcript_size = request_transcript(request, transcript);
	result = wmcs_identity_verify(request->identity_public, transcript,
				      transcript_size, request->signature);
	wmcs_secure_zero(transcript, sizeof(transcript));
	return result;
}

int wmcs_pairing_sign_response(
	const struct wmcs_identity *identity,
	const struct wmcs_pairing_message *request,
	struct wmcs_pairing_message *response,
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE])
{
	uint8_t transcript[sizeof(response_domain) - 1U +
			   WMCS_PAIRING_NONCE_SIZE +
			   WMCS_PAIRING_PUBLIC_KEY_SIZE * 4U];
	size_t transcript_size;
	int result;

	if (!identity || !request || !response || !transcript_hash ||
	    request->type != WMCS_PAIRING_REQUEST ||
	    response->type != WMCS_PAIRING_RESPONSE ||
	    memcmp(request->nonce, response->nonce, sizeof(request->nonce)))
		return -EINVAL;
	transcript_size = response_transcript(request, response, transcript);
	result = wmcs_identity_hash(transcript, transcript_size, transcript_hash);
	if (!result)
		result = wmcs_identity_sign(identity, transcript, transcript_size,
					    response->signature);
	wmcs_secure_zero(transcript, sizeof(transcript));
	return result;
}

int wmcs_pairing_verify_response(
	const struct wmcs_pairing_message *request,
	const struct wmcs_pairing_message *response,
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE])
{
	uint8_t transcript[sizeof(response_domain) - 1U +
			   WMCS_PAIRING_NONCE_SIZE +
			   WMCS_PAIRING_PUBLIC_KEY_SIZE * 4U];
	size_t transcript_size;
	int result;

	if (!request || !response || !transcript_hash ||
	    request->type != WMCS_PAIRING_REQUEST ||
	    response->type != WMCS_PAIRING_RESPONSE ||
	    memcmp(request->nonce, response->nonce, sizeof(request->nonce)))
		return -EINVAL;
	transcript_size = response_transcript(request, response, transcript);
	result = wmcs_identity_hash(transcript, transcript_size, transcript_hash);
	if (!result)
		result = wmcs_identity_verify(response->identity_public, transcript,
					      transcript_size, response->signature);
	wmcs_secure_zero(transcript, sizeof(transcript));
	return result;
}

int wmcs_pairing_sas(
	const uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE],
	char output[WMCS_PAIRING_SAS_SIZE])
{
	uint8_t input[sizeof(sas_domain) - 1U + WMCS_IDENTITY_HASH_SIZE];
	uint8_t digest[WMCS_IDENTITY_HASH_SIZE];
	int result;

	if (!transcript_hash || !output)
		return -EINVAL;
	memcpy(input, sas_domain, sizeof(sas_domain) - 1U);
	memcpy(&input[sizeof(sas_domain) - 1U], transcript_hash,
	       WMCS_IDENTITY_HASH_SIZE);
	result = wmcs_identity_hash(input, sizeof(input), digest);
	wmcs_secure_zero(input, sizeof(input));
	if (result)
		return result;
	if (snprintf(output, WMCS_PAIRING_SAS_SIZE,
		     "%02x%02x-%02x%02x-%02x%02x", digest[0], digest[1],
		     digest[2], digest[3], digest[4], digest[5]) !=
	    (int)(WMCS_PAIRING_SAS_SIZE - 1U))
		result = -EIO;
	wmcs_secure_zero(digest, sizeof(digest));
	return result;
}
