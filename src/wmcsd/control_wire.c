// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "control_wire.h"

static const uint8_t magic[] = {'W', 'M', 'C', 'X'};

static bool any_nonzero(const uint8_t *input, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (input[i])
			return true;
	}
	return false;
}

static bool all_zero(const uint8_t *input, size_t size)
{
	return !any_nonzero(input, size);
}

static void put_u64(uint8_t *output, uint64_t value)
{
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		output[i] = (uint8_t)(value >> (56U - i * 8U));
}

static uint64_t get_u64(const uint8_t *input)
{
	uint64_t value = 0;
	size_t i;

	for (i = 0; i < sizeof(value); i++)
		value = (value << 8U) | input[i];
	return value;
}

static bool packet_valid(const struct wmcs_control_packet *packet)
{
	return packet &&
	       (packet->type == WMCS_CONTROL_WLAN_REQUEST ||
		packet->type == WMCS_CONTROL_WLAN_RESULT ||
		packet->type == WMCS_CONTROL_RELEASE_REQUEST ||
		packet->type == WMCS_CONTROL_RELEASE_RESULT ||
		packet->type == WMCS_CONTROL_NR_QUERY ||
		packet->type == WMCS_CONTROL_NR_REPLY) &&
	       packet->sequence &&
	       any_nonzero(packet->sender_id, sizeof(packet->sender_id)) &&
	       any_nonzero(packet->recipient_id, sizeof(packet->recipient_id)) &&
	       memcmp(packet->sender_id, packet->recipient_id,
		      sizeof(packet->sender_id)) &&
	       any_nonzero(packet->nonce, sizeof(packet->nonce));
}

bool wmcs_control_header_encode(
	uint8_t output[WMCS_CONTROL_HEADER_SIZE],
	const struct wmcs_control_packet *packet)
{
	if (!output || !packet_valid(packet))
		return false;
	memset(output, 0, WMCS_CONTROL_HEADER_SIZE);
	memcpy(output, magic, sizeof(magic));
	output[4] = 0;
	output[5] = packet->type;
	output[6] = (uint8_t)(WMCS_CONTROL_WIRE_SIZE >> 8U);
	output[7] = (uint8_t)WMCS_CONTROL_WIRE_SIZE;
	memcpy(&output[8], packet->sender_id, sizeof(packet->sender_id));
	memcpy(&output[24], packet->recipient_id, sizeof(packet->recipient_id));
	put_u64(&output[40], packet->sequence);
	memcpy(&output[48], packet->nonce, sizeof(packet->nonce));
	return true;
}

bool wmcs_control_encode(uint8_t output[WMCS_CONTROL_WIRE_SIZE],
			 const struct wmcs_control_packet *packet)
{
	if (!output || !wmcs_control_header_encode(output, packet))
		return false;
	memcpy(&output[WMCS_CONTROL_HEADER_SIZE], packet->ciphertext,
	       sizeof(packet->ciphertext));
	return true;
}

bool wmcs_control_decode(struct wmcs_control_packet *packet,
			 const uint8_t *input, size_t input_size)
{
	struct wmcs_control_packet decoded = {0};

	if (!packet || !input || input_size != WMCS_CONTROL_WIRE_SIZE ||
	    memcmp(input, magic, sizeof(magic)) || input[4] != 0 ||
	    input[6] != (uint8_t)(WMCS_CONTROL_WIRE_SIZE >> 8U) ||
	    input[7] != (uint8_t)WMCS_CONTROL_WIRE_SIZE)
		return false;
	decoded.type = input[5];
	memcpy(decoded.sender_id, &input[8], sizeof(decoded.sender_id));
	memcpy(decoded.recipient_id, &input[24], sizeof(decoded.recipient_id));
	decoded.sequence = get_u64(&input[40]);
	memcpy(decoded.nonce, &input[48], sizeof(decoded.nonce));
	memcpy(decoded.ciphertext, &input[WMCS_CONTROL_HEADER_SIZE],
	       sizeof(decoded.ciphertext));
	if (!packet_valid(&decoded))
		return false;
	*packet = decoded;
	return true;
}

static bool encryption_valid(enum wmcs_control_encryption encryption)
{
	return encryption == WMCS_CONTROL_ENCRYPTION_SAE_MIXED;
}

bool wmcs_wlan_request_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			      const struct wmcs_wlan_request *request)
{
	size_t ssid_size;
	size_t key_size;

	if (!output || !request || !encryption_valid(request->encryption))
		return false;
	ssid_size = strnlen(request->ssid, sizeof(request->ssid));
	key_size = strnlen(request->key, sizeof(request->key));
	if (!ssid_size || ssid_size > WMCS_WLAN_SSID_MAX || key_size < 8U ||
	    key_size > WMCS_WLAN_KEY_MAX)
		return false;
	memset(output, 0, WMCS_CONTROL_PAYLOAD_SIZE);
	output[0] = 0;
	output[1] = 1;
	output[2] = request->dry_run ? 1U : 0U;
	output[3] = (uint8_t)request->encryption;
	output[4] = (uint8_t)ssid_size;
	output[5] = (uint8_t)key_size;
	output[6] = 5;
	memcpy(&output[8], request->ssid, ssid_size);
	memcpy(&output[40], request->key, key_size);
	return true;
}

bool wmcs_wlan_request_decode(struct wmcs_wlan_request *request,
			      const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE])
{
	struct wmcs_wlan_request decoded = {0};
	size_t ssid_size;
	size_t key_size;

	if (!request || !input || input[0] != 0 || input[1] != 1 ||
	    (input[2] & ~1U) || input[6] != 5 || input[7] ||
	    !all_zero(&input[104], WMCS_CONTROL_PAYLOAD_SIZE - 104U))
		return false;
	decoded.dry_run = input[2] != 0;
	decoded.encryption = (enum wmcs_control_encryption)input[3];
	ssid_size = input[4];
	key_size = input[5];
	if (!encryption_valid(decoded.encryption) || !ssid_size ||
	    ssid_size > WMCS_WLAN_SSID_MAX || key_size < 8U ||
	    key_size > WMCS_WLAN_KEY_MAX ||
	    memchr(&input[8], 0, ssid_size) ||
	    memchr(&input[40], 0, key_size) ||
	    !all_zero(&input[8 + ssid_size], 32U - ssid_size) ||
	    !all_zero(&input[40 + key_size], 64U - key_size))
		return false;
	memcpy(decoded.ssid, &input[8], ssid_size);
	memcpy(decoded.key, &input[40], key_size);
	*request = decoded;
	return true;
}

static bool outcome_valid(enum wmcs_control_outcome outcome)
{
	return outcome >= WMCS_CONTROL_OUTCOME_DRY_RUN_READY &&
	       outcome <= WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED;
}

static bool reason_valid(enum wmcs_control_reason reason)
{
	return reason >= WMCS_CONTROL_REASON_NONE &&
	       reason <= WMCS_CONTROL_REASON_INTERNAL;
}

bool wmcs_wlan_result_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			     const struct wmcs_wlan_result *result)
{
	if (!output || !result || !outcome_valid(result->outcome) ||
	    !reason_valid(result->reason) ||
	    ((result->outcome == WMCS_CONTROL_OUTCOME_DRY_RUN_READY ||
	      result->outcome == WMCS_CONTROL_OUTCOME_COMMITTED) !=
	     (result->reason == WMCS_CONTROL_REASON_NONE)))
		return false;
	memset(output, 0, WMCS_CONTROL_PAYLOAD_SIZE);
	output[0] = 0;
	output[1] = 1;
	output[2] = (uint8_t)result->outcome;
	output[3] = (uint8_t)result->reason;
	return true;
}

bool wmcs_wlan_result_decode(struct wmcs_wlan_result *result,
			     const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE])
{
	struct wmcs_wlan_result decoded;

	if (!result || !input || input[0] != 0 || input[1] != 1 ||
	    !all_zero(&input[4], WMCS_CONTROL_PAYLOAD_SIZE - 4U))
		return false;
	decoded.outcome = (enum wmcs_control_outcome)input[2];
	decoded.reason = (enum wmcs_control_reason)input[3];
	if (!outcome_valid(decoded.outcome) || !reason_valid(decoded.reason) ||
	    ((decoded.outcome == WMCS_CONTROL_OUTCOME_DRY_RUN_READY ||
	      decoded.outcome == WMCS_CONTROL_OUTCOME_COMMITTED) !=
	     (decoded.reason == WMCS_CONTROL_REASON_NONE)))
		return false;
	*result = decoded;
	return true;
}

bool wmcs_release_request_encode(
	uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
	const struct wmcs_release_request *request)
{
	if (!output || !request)
		return false;
	memset(output, 0, WMCS_CONTROL_PAYLOAD_SIZE);
	output[0] = 0;
	output[1] = 1;
	output[2] = request->dry_run ? 1U : 0U;
	return true;
}

bool wmcs_release_request_decode(
	struct wmcs_release_request *request,
	const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE])
{
	if (!request || !input || input[0] != 0 || input[1] != 1 ||
	    (input[2] & ~1U) ||
	    !all_zero(&input[3], WMCS_CONTROL_PAYLOAD_SIZE - 3U))
		return false;
	request->dry_run = input[2] != 0;
	return true;
}
