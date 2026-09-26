// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <string.h>

#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <psa/crypto.h>

#include "control_crypto.h"

static const uint8_t request_domain[] = "WMCS-CONTROL-C2A-V0";
static const uint8_t result_domain[] = "WMCS-CONTROL-A2C-V0";
static const uint8_t release_request_domain[] = "WMCS-RELEASE-C2A-V0";
static const uint8_t release_result_domain[] = "WMCS-RELEASE-A2C-V0";
static const uint8_t nr_query_domain[] = "WMCS-NR-QUERY-TO-V0";
static const uint8_t nr_reply_domain[] = "WMCS-NR-REPLY-TO-V0";

_Static_assert(sizeof(request_domain) == sizeof(result_domain),
	       "control domains must have one fixed size");
_Static_assert(sizeof(request_domain) == sizeof(release_request_domain),
	       "release request domain must fit the control KDF input");
_Static_assert(sizeof(request_domain) == sizeof(release_result_domain),
	       "release result domain must fit the control KDF input");
_Static_assert(sizeof(request_domain) == sizeof(nr_query_domain),
	       "neighbor query domain must fit the control KDF input");
_Static_assert(sizeof(request_domain) == sizeof(nr_reply_domain),
	       "neighbor reply domain must fit the control KDF input");

static int hex_value(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

bool wmcs_control_id_from_hex(
	const char hex[WMCS_IDENTITY_PEER_ID_SIZE + 1U],
	uint8_t output[WMCS_CONTROL_NODE_ID_SIZE])
{
	size_t i;

	if (!hex || !output || strlen(hex) != WMCS_IDENTITY_PEER_ID_SIZE)
		return false;
	for (i = 0; i < WMCS_CONTROL_NODE_ID_SIZE; i++) {
		int high = hex_value(hex[i * 2U]);
		int low = hex_value(hex[i * 2U + 1U]);

		if (high < 0 || low < 0)
			return false;
		output[i] = (uint8_t)((unsigned int)high << 4U | (unsigned int)low);
	}
	return true;
}

void wmcs_control_id_to_hex(
	const uint8_t input[WMCS_CONTROL_NODE_ID_SIZE],
	char output[WMCS_IDENTITY_PEER_ID_SIZE + 1U])
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < WMCS_CONTROL_NODE_ID_SIZE; i++) {
		output[i * 2U] = hex[input[i] >> 4U];
		output[i * 2U + 1U] = hex[input[i] & 15U];
	}
	output[WMCS_IDENTITY_PEER_ID_SIZE] = '\0';
}

int wmcs_control_local_id(const struct wmcs_identity *identity,
			  uint8_t output[WMCS_CONTROL_NODE_ID_SIZE])
{
	char fingerprint[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	int result;

	if (!identity || !output || !wmcs_identity_ready(identity))
		return -EINVAL;
	result = wmcs_identity_fingerprint(wmcs_identity_public_key(identity),
					   fingerprint);
	if (result)
		return result;
	return wmcs_control_id_from_hex(fingerprint, output) ? 0 : -EIO;
}

static int derive_key(
	const struct wmcs_control_packet *packet,
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	uint8_t output[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE])
{
	uint8_t info[sizeof(request_domain) - 1U + WMCS_CONTROL_NODE_ID_SIZE * 2U];
	const uint8_t *domain;
	size_t domain_size;

	if (packet->type == WMCS_CONTROL_WLAN_REQUEST) {
		domain = request_domain;
		domain_size = sizeof(request_domain) - 1U;
	} else if (packet->type == WMCS_CONTROL_WLAN_RESULT) {
		domain = result_domain;
		domain_size = sizeof(result_domain) - 1U;
	} else if (packet->type == WMCS_CONTROL_RELEASE_REQUEST) {
		domain = release_request_domain;
		domain_size = sizeof(release_request_domain) - 1U;
	} else if (packet->type == WMCS_CONTROL_RELEASE_RESULT) {
		domain = release_result_domain;
		domain_size = sizeof(release_result_domain) - 1U;
	} else if (packet->type == WMCS_CONTROL_NR_QUERY) {
		domain = nr_query_domain;
		domain_size = sizeof(nr_query_domain) - 1U;
	} else if (packet->type == WMCS_CONTROL_NR_REPLY) {
		domain = nr_reply_domain;
		domain_size = sizeof(nr_reply_domain) - 1U;
	} else {
		return -EINVAL;
	}
	memcpy(info, domain, domain_size);
	memcpy(&info[domain_size], packet->sender_id,
	       WMCS_CONTROL_NODE_ID_SIZE);
	memcpy(&info[domain_size + WMCS_CONTROL_NODE_ID_SIZE],
	       packet->recipient_id, WMCS_CONTROL_NODE_ID_SIZE);
	if (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), NULL, 0,
			 relationship_key, WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE,
			 info, domain_size + WMCS_CONTROL_NODE_ID_SIZE * 2U,
			 output, WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE)) {
		wmcs_secure_zero(info, sizeof(info));
		return -EIO;
	}
	wmcs_secure_zero(info, sizeof(info));
	return 0;
}

static int import_aead_key(const uint8_t key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
			   psa_key_usage_t usage, psa_key_id_t *key_id)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_status_t status;

	psa_set_key_usage_flags(&attributes, usage);
	psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attributes, 256);
	status = psa_import_key(&attributes, key,
				WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE, key_id);
	psa_reset_key_attributes(&attributes);
	return status == PSA_SUCCESS ? 0 : -EIO;
}

int wmcs_control_seal(
	struct wmcs_control_packet *packet,
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	const uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE])
{
	uint8_t header[WMCS_CONTROL_HEADER_SIZE];
	uint8_t derived[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	static const uint8_t zero_nonce[WMCS_CONTROL_NONCE_SIZE] = {0};
	psa_key_id_t key_id = 0;
	size_t output_size = 0;
	psa_status_t status;
	unsigned int attempt;
	int result;

	if (!packet || !relationship_key || !plaintext)
		return -EINVAL;
	for (attempt = 0; attempt < 2U; attempt++) {
		if (psa_generate_random(packet->nonce, sizeof(packet->nonce)) ==
			    PSA_SUCCESS &&
		    memcmp(packet->nonce, zero_nonce, sizeof(zero_nonce)))
			break;
	}
	if (attempt == 2U)
		return -EIO;
	if (!wmcs_control_header_encode(header, packet))
		return -EINVAL;
	result = derive_key(packet, relationship_key, derived);
	if (result)
		goto out;
	result = import_aead_key(derived, PSA_KEY_USAGE_ENCRYPT, &key_id);
	if (result)
		goto out;
	status = psa_aead_encrypt(key_id, PSA_ALG_GCM, packet->nonce,
				  sizeof(packet->nonce), header, sizeof(header),
				  plaintext, WMCS_CONTROL_PAYLOAD_SIZE,
				  packet->ciphertext, sizeof(packet->ciphertext),
				  &output_size);
	result = status == PSA_SUCCESS && output_size == sizeof(packet->ciphertext) ?
			 0 : -EIO;

out:
	if (key_id)
		psa_destroy_key(key_id);
	wmcs_secure_zero(derived, sizeof(derived));
	wmcs_secure_zero(header, sizeof(header));
	return result;
}

int wmcs_control_open(
	const struct wmcs_control_packet *packet,
	const uint8_t relationship_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE],
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE])
{
	uint8_t header[WMCS_CONTROL_HEADER_SIZE];
	uint8_t derived[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	psa_key_id_t key_id = 0;
	size_t output_size = 0;
	psa_status_t status;
	int result;

	if (!packet || !relationship_key || !plaintext ||
	    !wmcs_control_header_encode(header, packet))
		return -EINVAL;
	result = derive_key(packet, relationship_key, derived);
	if (result)
		goto out;
	result = import_aead_key(derived, PSA_KEY_USAGE_DECRYPT, &key_id);
	if (result)
		goto out;
	status = psa_aead_decrypt(key_id, PSA_ALG_GCM, packet->nonce,
				  sizeof(packet->nonce), header, sizeof(header),
				  packet->ciphertext, sizeof(packet->ciphertext),
				  plaintext, WMCS_CONTROL_PAYLOAD_SIZE,
				  &output_size);
	result = status == PSA_SUCCESS && output_size == WMCS_CONTROL_PAYLOAD_SIZE ?
			 0 : -EKEYREJECTED;

out:
	if (key_id)
		psa_destroy_key(key_id);
	wmcs_secure_zero(derived, sizeof(derived));
	wmcs_secure_zero(header, sizeof(header));
	if (result)
		wmcs_secure_zero(plaintext, WMCS_CONTROL_PAYLOAD_SIZE);
	return result;
}
