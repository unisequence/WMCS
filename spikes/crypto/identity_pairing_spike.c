// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <psa/crypto.h>

#include "control_crypto.h"
#include "control_wire.h"
#include "identity.h"
#include "pairing_crypto.h"
#include "pairing_wire.h"

static int fail(const char *step)
{
	fprintf(stderr, "identity pairing spike failed: %s\n", step);
	return 1;
}

int main(int argc, char **argv)
{
	struct wmcs_identity controller = {0};
	struct wmcs_identity agent = {0};
	struct wmcs_peer_record peer = {0};
	struct wmcs_pairing_message request = {.type = WMCS_PAIRING_REQUEST};
	struct wmcs_pairing_message response = {.type = WMCS_PAIRING_RESPONSE};
	struct wmcs_control_packet control_packet = {
		.type = WMCS_CONTROL_WLAN_REQUEST,
		.sequence = 2,
	};
	struct wmcs_wlan_request wlan_request = {
		.dry_run = true,
		.encryption = WMCS_CONTROL_ENCRYPTION_SAE_MIXED,
	};
	struct wmcs_wlan_request decoded_wlan;
	struct wmcs_wlan_result wlan_result = {
		.outcome = WMCS_CONTROL_OUTCOME_DRY_RUN_READY,
		.reason = WMCS_CONTROL_REASON_NONE,
	};
	struct wmcs_wlan_result decoded_result;
	uint8_t controller_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	uint8_t agent_key[WMCS_IDENTITY_RELATIONSHIP_KEY_SIZE];
	uint8_t controller_public[WMCS_IDENTITY_PUBLIC_SIZE];
	uint8_t agent_public[WMCS_IDENTITY_PUBLIC_SIZE];
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE];
	uint8_t decoded_hash[WMCS_IDENTITY_HASH_SIZE];
	uint8_t wire[WMCS_PAIRING_WIRE_SIZE];
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	uint8_t decoded_plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	psa_key_id_t controller_ephemeral = 0;
	psa_key_id_t agent_ephemeral = 0;
	char controller_directory[320];
	char agent_directory[320];
	char controller_peer[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	char agent_peer[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	char controller_sas[WMCS_PAIRING_SAS_SIZE];
	char agent_sas[WMCS_PAIRING_SAS_SIZE];
	int result = 1;

	if (argc != 2)
		return fail("usage");
	if (snprintf(controller_directory, sizeof(controller_directory),
		     "%s/controller", argv[1]) >= (int)sizeof(controller_directory) ||
	    snprintf(agent_directory, sizeof(agent_directory), "%s/agent", argv[1]) >=
		    (int)sizeof(agent_directory))
		return fail("path");
	if (mkdir(argv[1], 0700) && errno != EEXIST)
		return fail("base directory");
	if (wmcs_identity_init(&controller, controller_directory) ||
	    wmcs_identity_init(&agent, agent_directory))
		return fail("identity init");
	if (wmcs_identity_ensure(&controller) || wmcs_identity_ensure(&agent))
		goto out_fail_ensure;
	memcpy(controller_public, wmcs_identity_public_key(&controller),
	       sizeof(controller_public));
	memcpy(agent_public, wmcs_identity_public_key(&agent), sizeof(agent_public));

	if (psa_generate_random(request.nonce, sizeof(request.nonce)) != PSA_SUCCESS)
		goto out_fail_random;
	memcpy(request.identity_public, controller_public,
	       sizeof(request.identity_public));
	if (wmcs_identity_ephemeral_create(&controller_ephemeral,
					   request.ephemeral_public) ||
	    wmcs_pairing_sign_request(&controller, &request) ||
	    wmcs_pairing_verify_request(&request))
		goto out_fail_request;
	if (!wmcs_pairing_encode(wire, &request) ||
	    !wmcs_pairing_decode(&request, wire, sizeof(wire)))
		goto out_fail_request;

	response.type = WMCS_PAIRING_RESPONSE;
	memcpy(response.nonce, request.nonce, sizeof(response.nonce));
	memcpy(response.identity_public, agent_public,
	       sizeof(response.identity_public));
	if (wmcs_identity_ephemeral_create(&agent_ephemeral,
					   response.ephemeral_public) ||
	    wmcs_pairing_sign_response(&agent, &request, &response,
				       transcript_hash) ||
	    wmcs_pairing_verify_response(&request, &response, decoded_hash) ||
	    memcmp(transcript_hash, decoded_hash, sizeof(transcript_hash)))
		goto out_fail_response;
	if (wmcs_identity_relationship_key(controller_ephemeral,
					   response.ephemeral_public,
					   request.nonce, transcript_hash,
					   controller_key) ||
	    wmcs_identity_relationship_key(agent_ephemeral, request.ephemeral_public,
					   request.nonce, transcript_hash,
					   agent_key) ||
	    memcmp(controller_key, agent_key, sizeof(controller_key)))
		goto out_fail_relationship;
	if (wmcs_control_local_id(&controller, control_packet.sender_id) ||
	    wmcs_control_local_id(&agent, control_packet.recipient_id))
		goto out_fail_control;
	strcpy(wlan_request.ssid, "Synthetic Lab");
	strcpy(wlan_request.key, "synthetic-only-passphrase");
	if (!wmcs_wlan_request_encode(plaintext, &wlan_request) ||
	    wmcs_control_seal(&control_packet, controller_key, plaintext) ||
	    wmcs_control_open(&control_packet, agent_key, decoded_plaintext) ||
	    !wmcs_wlan_request_decode(&decoded_wlan, decoded_plaintext) ||
	    strcmp(decoded_wlan.ssid, wlan_request.ssid) ||
	    strcmp(decoded_wlan.key, wlan_request.key))
		goto out_fail_control;
	control_packet.ciphertext[0] ^= 1U;
	if (!wmcs_control_open(&control_packet, agent_key, decoded_plaintext))
		goto out_fail_control;
	control_packet.ciphertext[0] ^= 1U;
	control_packet.type = WMCS_CONTROL_WLAN_RESULT;
	memcpy(control_packet.sender_id,
	       control_packet.recipient_id, WMCS_CONTROL_NODE_ID_SIZE);
	if (wmcs_control_local_id(&controller, control_packet.recipient_id) ||
	    !wmcs_wlan_result_encode(plaintext, &wlan_result) ||
	    wmcs_control_seal(&control_packet, agent_key, plaintext) ||
	    wmcs_control_open(&control_packet, controller_key, decoded_plaintext) ||
	    !wmcs_wlan_result_decode(&decoded_result, decoded_plaintext) ||
	    decoded_result.outcome != WMCS_CONTROL_OUTCOME_DRY_RUN_READY)
		goto out_fail_control;
	if (wmcs_pairing_sas(transcript_hash, controller_sas) ||
	    wmcs_pairing_sas(decoded_hash, agent_sas) ||
	    strcmp(controller_sas, agent_sas))
		goto out_fail_sas;

	response.signature[0] ^= 1U;
	if (!wmcs_pairing_verify_response(&request, &response, decoded_hash))
		goto out_fail_tamper;
	response.signature[0] ^= 1U;
	if (wmcs_identity_store_peer(&controller, response.identity_public,
				     controller_key, WMCS_PEER_ROLE_AGENT,
				     controller_peer) ||
	    wmcs_identity_store_peer(&agent, request.identity_public, agent_key,
				     WMCS_PEER_ROLE_CONTROLLER, agent_peer))
		goto out_fail_store;
	if (wmcs_identity_load_peer(&controller, controller_peer, &peer) ||
	    memcmp(peer.relationship_key, controller_key, sizeof(controller_key)))
		goto out_fail_load;
	wmcs_secure_zero(&peer, sizeof(peer));

	wmcs_identity_close(&controller);
	wmcs_identity_close(&agent);
	if (wmcs_identity_init(&controller, controller_directory) ||
	    wmcs_identity_init(&agent, agent_directory) ||
	    wmcs_identity_peer_count(&controller) != 1 ||
	    wmcs_identity_peer_count(&agent) != 1 ||
	    wmcs_identity_generation(&controller) != 1 ||
	    wmcs_identity_generation(&agent) != 1 ||
	    memcmp(wmcs_identity_public_key(&controller), controller_public,
		   sizeof(controller_public)) ||
	    memcmp(wmcs_identity_public_key(&agent), agent_public,
		   sizeof(agent_public)))
		goto out_fail_reload;

	printf("Identity/control spike: ok (persistent identity, signed ECDH, SAS, AES-GCM)\n");
	result = 0;
	goto out;

out_fail_reload:
	fprintf(stderr, "identity pairing spike failed: reload\n");
	goto out;
out_fail_load:
	fprintf(stderr, "identity pairing spike failed: load peer\n");
	goto out;
out_fail_store:
	fprintf(stderr, "identity pairing spike failed: store peer\n");
	goto out;
out_fail_tamper:
	fprintf(stderr, "identity pairing spike failed: tamper acceptance\n");
	goto out;
out_fail_sas:
	fprintf(stderr, "identity pairing spike failed: SAS\n");
	goto out;
out_fail_relationship:
	fprintf(stderr, "identity pairing spike failed: relationship key\n");
	goto out;
out_fail_control:
	fprintf(stderr, "identity pairing spike failed: authenticated control\n");
	goto out;
out_fail_response:
	fprintf(stderr, "identity pairing spike failed: response\n");
	goto out;
out_fail_request:
	fprintf(stderr, "identity pairing spike failed: request\n");
	goto out;
out_fail_random:
	fprintf(stderr, "identity pairing spike failed: random\n");
	goto out;
out_fail_ensure:
	fprintf(stderr, "identity pairing spike failed: ensure\n");

out:
	if (agent_ephemeral)
		psa_destroy_key(agent_ephemeral);
	if (controller_ephemeral)
		psa_destroy_key(controller_ephemeral);
	wmcs_secure_zero(controller_key, sizeof(controller_key));
	wmcs_secure_zero(agent_key, sizeof(agent_key));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	wmcs_secure_zero(decoded_plaintext, sizeof(decoded_plaintext));
	wmcs_identity_close(&controller);
	wmcs_identity_close(&agent);
	return result;
}
