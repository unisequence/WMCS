// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_CONTROL_WIRE_H
#define WMCS_CONTROL_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WMCS_CONTROL_WIRE_SIZE 256U
#define WMCS_CONTROL_HEADER_SIZE 60U
#define WMCS_CONTROL_NODE_ID_SIZE 16U
#define WMCS_CONTROL_NONCE_SIZE 12U
#define WMCS_CONTROL_PAYLOAD_SIZE 180U
#define WMCS_CONTROL_TAG_SIZE 16U
#define WMCS_CONTROL_CIPHERTEXT_SIZE \
	(WMCS_CONTROL_PAYLOAD_SIZE + WMCS_CONTROL_TAG_SIZE)
#define WMCS_WLAN_SSID_MAX 32U
#define WMCS_WLAN_KEY_MAX 63U

enum wmcs_control_message_type {
	WMCS_CONTROL_WLAN_REQUEST = 1,
	WMCS_CONTROL_WLAN_RESULT = 2,
	WMCS_CONTROL_RELEASE_REQUEST = 3,
	WMCS_CONTROL_RELEASE_RESULT = 4,
};

enum wmcs_control_encryption {
	WMCS_CONTROL_ENCRYPTION_SAE_MIXED = 1,
};

enum wmcs_control_outcome {
	WMCS_CONTROL_OUTCOME_NONE = 0,
	WMCS_CONTROL_OUTCOME_DRY_RUN_READY = 1,
	WMCS_CONTROL_OUTCOME_COMMITTED = 2,
	WMCS_CONTROL_OUTCOME_ROLLED_BACK = 3,
	WMCS_CONTROL_OUTCOME_REJECTED = 4,
	WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED = 5,
};

enum wmcs_control_reason {
	WMCS_CONTROL_REASON_NONE = 0,
	WMCS_CONTROL_REASON_INVALID_PROFILE = 1,
	WMCS_CONTROL_REASON_OWNERSHIP_CONFLICT = 2,
	WMCS_CONTROL_REASON_PLATFORM_FAILURE = 3,
	WMCS_CONTROL_REASON_VERIFY_FAILURE = 4,
	WMCS_CONTROL_REASON_REPLAY = 5,
	WMCS_CONTROL_REASON_MUTATION_DISABLED = 6,
	WMCS_CONTROL_REASON_INTERNAL = 7,
};

struct wmcs_control_packet {
	uint8_t type;
	uint8_t sender_id[WMCS_CONTROL_NODE_ID_SIZE];
	uint8_t recipient_id[WMCS_CONTROL_NODE_ID_SIZE];
	uint64_t sequence;
	uint8_t nonce[WMCS_CONTROL_NONCE_SIZE];
	uint8_t ciphertext[WMCS_CONTROL_CIPHERTEXT_SIZE];
};

struct wmcs_wlan_request {
	bool dry_run;
	enum wmcs_control_encryption encryption;
	char ssid[WMCS_WLAN_SSID_MAX + 1U];
	char key[WMCS_WLAN_KEY_MAX + 1U];
};

struct wmcs_wlan_result {
	enum wmcs_control_outcome outcome;
	enum wmcs_control_reason reason;
};

struct wmcs_release_request {
	bool dry_run;
};

bool wmcs_control_header_encode(
	uint8_t output[WMCS_CONTROL_HEADER_SIZE],
	const struct wmcs_control_packet *packet);
bool wmcs_control_encode(uint8_t output[WMCS_CONTROL_WIRE_SIZE],
			 const struct wmcs_control_packet *packet);
bool wmcs_control_decode(struct wmcs_control_packet *packet,
			 const uint8_t *input, size_t input_size);

bool wmcs_wlan_request_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			      const struct wmcs_wlan_request *request);
bool wmcs_wlan_request_decode(struct wmcs_wlan_request *request,
			      const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE]);
bool wmcs_wlan_result_encode(uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
			     const struct wmcs_wlan_result *result);
bool wmcs_wlan_result_decode(struct wmcs_wlan_result *result,
			     const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE]);
bool wmcs_release_request_encode(
	uint8_t output[WMCS_CONTROL_PAYLOAD_SIZE],
	const struct wmcs_release_request *request);
bool wmcs_release_request_decode(
	struct wmcs_release_request *request,
	const uint8_t input[WMCS_CONTROL_PAYLOAD_SIZE]);

#endif
