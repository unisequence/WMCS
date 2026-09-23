// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "control_wire.h"

static int failures;

static void expect(bool condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "control wire test failed: %s\n", name);
		failures++;
	}
}

int main(void)
{
	struct wmcs_control_packet packet = {
		.type = WMCS_CONTROL_WLAN_REQUEST,
		.sequence = 2,
	};
	struct wmcs_control_packet decoded;
	struct wmcs_wlan_request request = {
		.dry_run = true,
		.encryption = WMCS_CONTROL_ENCRYPTION_SAE_MIXED,
	};
	struct wmcs_wlan_request decoded_request;
	struct wmcs_wlan_result result = {
		.outcome = WMCS_CONTROL_OUTCOME_COMMITTED,
		.reason = WMCS_CONTROL_REASON_NONE,
	};
	struct wmcs_wlan_result decoded_result;
	struct wmcs_release_request release = {
		.dry_run = true,
	};
	struct wmcs_release_request decoded_release;
	uint8_t wire[WMCS_CONTROL_WIRE_SIZE];
	uint8_t payload[WMCS_CONTROL_PAYLOAD_SIZE];

	memset(packet.sender_id, 0x11, sizeof(packet.sender_id));
	memset(packet.recipient_id, 0x22, sizeof(packet.recipient_id));
	memset(packet.nonce, 0x33, sizeof(packet.nonce));
	memset(packet.ciphertext, 0x44, sizeof(packet.ciphertext));
	expect(wmcs_control_encode(wire, &packet), "packet encode");
	expect(wmcs_control_decode(&decoded, wire, sizeof(wire)), "packet decode");
	expect(!memcmp(&packet, &decoded, sizeof(packet)), "packet round trip");
	expect(!wmcs_control_decode(&decoded, wire, sizeof(wire) - 1U),
	       "reject short packet");
	wire[4] = 1;
	expect(!wmcs_control_decode(&decoded, wire, sizeof(wire)),
	       "reject unknown version");
	wire[4] = 0;
	wire[5] = 99;
	expect(!wmcs_control_decode(&decoded, wire, sizeof(wire)),
	       "reject unknown type");
	packet.type = WMCS_CONTROL_WLAN_REQUEST;
	memcpy(packet.recipient_id, packet.sender_id, sizeof(packet.recipient_id));
	expect(!wmcs_control_encode(wire, &packet), "reject reflected identity");
	memset(packet.recipient_id, 0x22, sizeof(packet.recipient_id));

	strcpy(request.ssid, "Synthetic Lab");
	strcpy(request.key, "synthetic-only-passphrase");
	expect(wmcs_wlan_request_encode(payload, &request), "request encode");
	expect(wmcs_wlan_request_decode(&decoded_request, payload),
	       "request decode");
	expect(decoded_request.dry_run &&
	       decoded_request.encryption == request.encryption &&
	       !strcmp(decoded_request.ssid, request.ssid) &&
	       !strcmp(decoded_request.key, request.key), "request round trip");
	payload[104] = 1;
	expect(!wmcs_wlan_request_decode(&decoded_request, payload),
	       "reject request reserved bytes");

	expect(wmcs_wlan_result_encode(payload, &result), "result encode");
	expect(wmcs_wlan_result_decode(&decoded_result, payload), "result decode");
	expect(decoded_result.outcome == result.outcome &&
	       decoded_result.reason == result.reason, "result round trip");
	payload[3] = WMCS_CONTROL_REASON_VERIFY_FAILURE;
	expect(!wmcs_wlan_result_decode(&decoded_result, payload),
	       "reject contradictory result");

	packet.type = WMCS_CONTROL_RELEASE_REQUEST;
	expect(wmcs_control_encode(wire, &packet), "release packet encode");
	expect(wmcs_control_decode(&decoded, wire, sizeof(wire)),
	       "release packet decode");
	expect(decoded.type == WMCS_CONTROL_RELEASE_REQUEST,
	       "release packet type");
	expect(wmcs_release_request_encode(payload, &release),
	       "release request encode");
	expect(wmcs_release_request_decode(&decoded_release, payload),
	       "release request decode");
	expect(decoded_release.dry_run, "release request round trip");
	payload[3] = 1;
	expect(!wmcs_release_request_decode(&decoded_release, payload),
	       "reject release reserved bytes");

	packet.type = WMCS_CONTROL_RELEASE_RESULT;
	expect(wmcs_control_encode(wire, &packet), "release result packet encode");
	expect(wmcs_control_decode(&decoded, wire, sizeof(wire)),
	       "release result packet decode");
	expect(decoded.type == WMCS_CONTROL_RELEASE_RESULT,
	       "release result packet type");

	if (failures)
		return 1;
	puts("Control wire codec tests: ok");
	return 0;
}
