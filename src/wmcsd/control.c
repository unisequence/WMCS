// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libubox/utils.h>

#include "control.h"
#include "operation_store.h"
#include "result_store.h"
#include "wlan.h"

#define WMCS_CONTROL_PORT 45125U
#define WMCS_CONTROL_RETRY_MS 1000
#define WMCS_CONTROL_PACKET_BUDGET 64U

static bool role_is(const struct wmcs_control *control, const char *role)
{
	return control && !strcmp(control->role, role);
}

static bool request_type_valid(uint8_t type)
{
	return type == WMCS_CONTROL_WLAN_REQUEST ||
	       type == WMCS_CONTROL_RELEASE_REQUEST;
}

static uint8_t result_type_for_request(uint8_t type)
{
	if (type == WMCS_CONTROL_WLAN_REQUEST)
		return WMCS_CONTROL_WLAN_RESULT;
	if (type == WMCS_CONTROL_RELEASE_REQUEST)
		return WMCS_CONTROL_RELEASE_RESULT;
	return 0;
}

static uint8_t result_type_for_operation(enum wmcs_control_operation operation)
{
	if (operation == WMCS_CONTROL_OPERATION_WLAN_SYNC)
		return WMCS_CONTROL_WLAN_RESULT;
	if (operation == WMCS_CONTROL_OPERATION_RELEASE)
		return WMCS_CONTROL_RELEASE_RESULT;
	return 0;
}

static enum wmcs_control_operation operation_for_request(uint8_t type)
{
	if (type == WMCS_CONTROL_WLAN_REQUEST)
		return WMCS_CONTROL_OPERATION_WLAN_SYNC;
	if (type == WMCS_CONTROL_RELEASE_REQUEST)
		return WMCS_CONTROL_OPERATION_RELEASE;
	return WMCS_CONTROL_OPERATION_NONE;
}

static void clear_exchange(struct wmcs_control *control)
{
	wmcs_secure_zero(&control->request, sizeof(control->request));
	wmcs_secure_zero(&control->response, sizeof(control->response));
	wmcs_secure_zero(control->request_wire, sizeof(control->request_wire));
	wmcs_secure_zero(&control->result, sizeof(control->result));
	wmcs_secure_zero(control->peer_id, sizeof(control->peer_id));
	memset(&control->peer_address, 0, sizeof(control->peer_address));
	control->sequence = 0;
	control->operation = WMCS_CONTROL_OPERATION_NONE;
	control->journal_phase = 0;
	control->request_dry_run = false;
}

static void close_socket(struct wmcs_control *control)
{
	uloop_timeout_cancel(&control->retry_timer);
	if (control->fd >= 0) {
		uloop_fd_delete(&control->socket_event);
		close(control->fd);
		control->fd = -1;
		control->socket_event.fd = -1;
	}
}

static int open_socket(struct wmcs_control *control)
{
	struct sockaddr_in bind_address = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_CONTROL_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int enabled = 1;
	int fd;

	if (!if_nametoindex(control->interface))
		return -ENODEV;
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
	    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, control->interface,
		       strlen(control->interface) + 1U) ||
	    bind(fd, (struct sockaddr *)&bind_address, sizeof(bind_address))) {
		int error = -errno;

		close(fd);
		return error;
	}
	control->socket_event.fd = fd;
	if (uloop_fd_add(&control->socket_event, ULOOP_READ)) {
		int error = errno ? -errno : -EIO;

		control->socket_event.fd = -1;
		close(fd);
		return error;
	}
	control->fd = fd;
	return 0;
}

static int send_packet(struct wmcs_control *control,
		       const struct wmcs_control_packet *packet,
		       const struct sockaddr_in *destination)
{
	uint8_t wire[WMCS_CONTROL_WIRE_SIZE];
	ssize_t sent;

	if (!wmcs_control_encode(wire, packet))
		return -EINVAL;
	sent = sendto(control->fd, wire, sizeof(wire), 0,
		      (const struct sockaddr *)destination, sizeof(*destination));
	wmcs_secure_zero(wire, sizeof(wire));
	if (sent < 0)
		return -errno;
	return (size_t)sent == WMCS_CONTROL_WIRE_SIZE ? 0 : -EIO;
}

static enum wmcs_control_reason reason_from_check(int result)
{
	if (result == -EEXIST)
		return WMCS_CONTROL_REASON_OWNERSHIP_CONFLICT;
	if (result == -EINVAL || result == -ENODEV)
		return WMCS_CONTROL_REASON_INVALID_PROFILE;
	return WMCS_CONTROL_REASON_PLATFORM_FAILURE;
}

static int make_result(struct wmcs_control *control,
		       const struct wmcs_control_packet *request,
		       const struct wmcs_peer_record *peer,
		       const struct wmcs_wlan_result *result)
{
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	int status;

	memset(&control->response, 0, sizeof(control->response));
	control->response.type = result_type_for_request(request->type);
	if (!control->response.type)
		return -EINVAL;
	memcpy(control->response.sender_id, request->recipient_id,
	       sizeof(control->response.sender_id));
	memcpy(control->response.recipient_id, request->sender_id,
	       sizeof(control->response.recipient_id));
	control->response.sequence = request->sequence;
	if (!wmcs_wlan_result_encode(plaintext, result))
		return -EINVAL;
	status = wmcs_control_seal(&control->response, peer->relationship_key,
				   plaintext);
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return status;
}

static int advance_peer(struct wmcs_control *control, const char *peer_id,
			const struct wmcs_peer_record *peer, uint64_t sequence,
			enum wmcs_peer_state next_state)
{
	return wmcs_identity_advance_peer_state(control->identity, peer_id,
						peer->generation, sequence,
						next_state);
}

static enum wmcs_peer_state state_after_result(
	const struct wmcs_peer_record *peer,
	const struct wmcs_control_packet *response,
	const struct wmcs_wlan_result *result)
{
	if (response->type == WMCS_CONTROL_RELEASE_RESULT &&
	    result->outcome == WMCS_CONTROL_OUTCOME_COMMITTED)
		return WMCS_PEER_STATE_RELEASED;
	return peer->state;
}

static bool result_consumes_sequence(const struct wmcs_wlan_result *result)
{
	return !(result->outcome == WMCS_CONTROL_OUTCOME_REJECTED &&
		 result->reason == WMCS_CONTROL_REASON_REPLAY);
}

static int reconcile_controller_peer(
	struct wmcs_control *control, const char *peer_id,
	struct wmcs_peer_record *peer,
	const struct wmcs_control_packet *response,
	const struct wmcs_wlan_result *result)
{
	enum wmcs_peer_state expected_state =
		state_after_result(peer, response, result);

	if (!result_consumes_sequence(result))
		return response->sequence == peer->generation + 1U ? 0 : -ESTALE;
	if (response->sequence == peer->generation + 1U) {
		int status = advance_peer(control, peer_id, peer,
					  response->sequence, expected_state);

		if (status)
			return status;
		peer->generation = response->sequence;
		peer->state = expected_state;
		return 0;
	}
	if (response->sequence != peer->generation)
		return -ESTALE;
	return peer->state == expected_state ? 0 : -EKEYREJECTED;
}

static int decode_request_payload(
	const struct wmcs_control_packet *request,
	const struct wmcs_peer_record *peer, bool *dry_run)
{
	struct wmcs_wlan_request wlan_request = {0};
	struct wmcs_release_request release_request = {0};
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE] = {0};
	int status;

	if (!request || !peer || !dry_run)
		return -EINVAL;
	status = wmcs_control_open(request, peer->relationship_key, plaintext);
	if (status)
		goto out;
	if (request->type == WMCS_CONTROL_WLAN_REQUEST) {
		if (!wmcs_wlan_request_decode(&wlan_request, plaintext)) {
			status = -EKEYREJECTED;
			goto out;
		}
		*dry_run = wlan_request.dry_run;
	} else if (request->type == WMCS_CONTROL_RELEASE_REQUEST) {
		if (!wmcs_release_request_decode(&release_request, plaintext)) {
			status = -EKEYREJECTED;
			goto out;
		}
		*dry_run = release_request.dry_run;
	} else {
		status = -EKEYREJECTED;
	}

out:
	wmcs_secure_zero(&wlan_request, sizeof(wlan_request));
	wmcs_secure_zero(&release_request, sizeof(release_request));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return status;
}

static int decode_result_payload(
	const struct wmcs_control_packet *response,
	const struct wmcs_peer_record *peer, struct wmcs_wlan_result *result)
{
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	int status;

	if (!response || !peer || !result)
		return -EINVAL;
	status = wmcs_control_open(response, peer->relationship_key, plaintext);
	if (!status && !wmcs_wlan_result_decode(result, plaintext))
		status = -EKEYREJECTED;
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return status;
}

static int load_cached_result(
	struct wmcs_control *control, const char *peer_id,
	const struct wmcs_peer_record *peer, uint64_t expected_sequence,
	uint8_t expected_type,
	struct wmcs_control_packet *response, struct wmcs_wlan_result *result)
{
	uint8_t local_id[WMCS_CONTROL_NODE_ID_SIZE];
	uint8_t peer_binary[WMCS_CONTROL_NODE_ID_SIZE];
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	int status;

	status = wmcs_result_store_load(control->state_dir, response);
	if (status)
		return status;
	if (wmcs_control_local_id(control->identity, local_id) ||
	    !wmcs_control_id_from_hex(peer_id, peer_binary)) {
		status = -EKEYREJECTED;
		goto out;
	}
	if (memcmp(response->recipient_id, peer_binary, sizeof(peer_binary))) {
		status = -ENOENT;
		goto out;
	}
	if (memcmp(response->sender_id, local_id, sizeof(local_id))) {
		status = -EKEYREJECTED;
		goto out;
	}
	if (response->sequence != expected_sequence) {
		status = -ENOENT;
		goto out;
	}
	if (response->type != expected_type) {
		status = -EALREADY;
		goto out;
	}
	status = wmcs_control_open(response, peer->relationship_key, plaintext);
	if (!status && !wmcs_wlan_result_decode(result, plaintext))
		status = -EKEYREJECTED;

out:
	wmcs_secure_zero(local_id, sizeof(local_id));
	wmcs_secure_zero(peer_binary, sizeof(peer_binary));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return status;
}

static int finalize_cached_result(
	struct wmcs_control *control, const char *peer_id,
	struct wmcs_peer_record *peer,
	const struct wmcs_control_packet *response,
	const struct wmcs_wlan_result *result, bool require_cleanup)
{
	int cleanup = 0;
	int status;

	if (response->sequence == peer->generation + 1U) {
		enum wmcs_peer_state next_state = state_after_result(peer, response,
								     result);

		status = advance_peer(control, peer_id, peer, response->sequence,
				      next_state);
		if (status)
			return status;
		peer->generation = response->sequence;
		peer->state = next_state;
	} else if (response->sequence != peer->generation) {
		return -ESTALE;
	}
	if (result->outcome == WMCS_CONTROL_OUTCOME_COMMITTED)
		cleanup = wmcs_wlan_finish(control->state_dir);
	else if (result->outcome == WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED &&
		 require_cleanup) {
		cleanup = wmcs_wlan_abort(control->ubus, control->state_dir,
					  control->target_radio);
		if (cleanup == -ENOENT)
			cleanup = 0;
	}
	if (require_cleanup && cleanup)
		return cleanup;
	return 0;
}

static int send_cached_result(struct wmcs_control *control,
			      const struct wmcs_control_packet *request,
			      const char *peer_id,
			      struct wmcs_peer_record *peer,
			      const struct sockaddr_in *source)
{
	struct wmcs_control_packet response;
	struct wmcs_wlan_result result;
	int status;

	status = load_cached_result(control, peer_id, peer, request->sequence,
				    result_type_for_request(request->type),
				    &response, &result);
	if (status)
		goto out;
	status = finalize_cached_result(control, peer_id, peer, &response,
					&result, false);
	if (status)
		goto out;
	control->response = response;
	control->peer_address = *source;
	strcpy(control->peer_id, peer_id);
	control->sequence = response.sequence;
	control->result = result;
	control->operation = operation_for_request(request->type);
	control->state = WMCS_CONTROL_COMPLETE;
	status = send_packet(control, &control->response, source);

out:
	wmcs_secure_zero(&response, sizeof(response));
	wmcs_secure_zero(&result, sizeof(result));
	return status;
}

static int recover_cached_result(struct wmcs_control *control)
{
	struct wmcs_control_packet response;
	struct wmcs_control_packet verified;
	struct wmcs_wlan_result result;
	struct wmcs_peer_record peer;
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	int status;

	status = wmcs_result_store_load(control->state_dir, &response);
	if (status == -ENOENT)
		return 0;
	if (status)
		return status;
	wmcs_control_id_to_hex(response.recipient_id, peer_id);
	status = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (status || peer.role != WMCS_PEER_ROLE_CONTROLLER) {
		status = status ? status : -EKEYREJECTED;
		goto out;
	}
	status = load_cached_result(control, peer_id, &peer, response.sequence,
				    response.type,
				    &verified, &result);
	if (status)
		goto out;
	if (response.sequence < peer.generation) {
		status = wmcs_result_store_remove(control->state_dir);
		goto out;
	}
	status = finalize_cached_result(control, peer_id, &peer, &verified,
					&result, true);

out:
	wmcs_secure_zero(&response, sizeof(response));
	wmcs_secure_zero(&verified, sizeof(verified));
	wmcs_secure_zero(&result, sizeof(result));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(peer_id, sizeof(peer_id));
	return status;
}

static int recover_controller_operation(struct wmcs_control *control)
{
	struct wmcs_operation_record operation = {0};
	struct wmcs_wlan_result result = {0};
	struct wmcs_peer_record peer = {0};
	uint8_t local_id[WMCS_CONTROL_NODE_ID_SIZE] = {0};
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U] = {0};
	enum wmcs_control_operation operation_type;
	bool dry_run = false;
	int status;

	status = wmcs_operation_store_load(control->state_dir, &operation);
	if (status == -ENOENT)
		return 0;
	if (status)
		return status;
	operation_type = operation_for_request(operation.request.type);
	if (operation_type == WMCS_CONTROL_OPERATION_NONE ||
	    wmcs_control_local_id(control->identity, local_id) ||
	    memcmp(operation.request.sender_id, local_id, sizeof(local_id))) {
		status = -EKEYREJECTED;
		goto out;
	}
	wmcs_control_id_to_hex(operation.request.recipient_id, peer_id);
	status = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (status || peer.role != WMCS_PEER_ROLE_AGENT) {
		status = status ? status : -EKEYREJECTED;
		goto out;
	}
	status = decode_request_payload(&operation.request, &peer, &dry_run);
	if (status)
		goto out;
	if (operation.phase == WMCS_OPERATION_PENDING) {
		if (peer.state != WMCS_PEER_STATE_ACTIVE ||
		    operation.request.sequence != peer.generation + 1U) {
			status = -ESTALE;
			goto out;
		}
	} else {
		status = decode_result_payload(&operation.response, &peer, &result);
		if (status)
			goto out;
		status = reconcile_controller_peer(control, peer_id, &peer,
						   &operation.response,
						   &result);
		if (status)
			goto out;
	}
	control->request = operation.request;
	control->response = operation.response;
	control->result = result;
	strcpy(control->peer_id, peer_id);
	control->sequence = operation.request.sequence;
	control->operation = operation_type;
	control->journal_phase = operation.phase;
	control->request_dry_run = dry_run;
	control->state = operation.phase == WMCS_OPERATION_PENDING ?
				 WMCS_CONTROL_RECOVERY_PENDING :
				 WMCS_CONTROL_COMPLETE;
	status = 0;

out:
	wmcs_secure_zero(&operation, sizeof(operation));
	wmcs_secure_zero(&result, sizeof(result));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(local_id, sizeof(local_id));
	wmcs_secure_zero(peer_id, sizeof(peer_id));
	return status;
}

static int remove_operation_for_peer(struct wmcs_control *control,
				      const char *peer_id)
{
	struct wmcs_operation_record operation = {0};
	uint8_t peer_binary[WMCS_CONTROL_NODE_ID_SIZE] = {0};
	int result;

	result = wmcs_operation_store_load(control->state_dir, &operation);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	if (!wmcs_control_id_from_hex(peer_id, peer_binary)) {
		result = -EKEYREJECTED;
		goto out;
	}
	if (memcmp(operation.request.recipient_id, peer_binary,
		   sizeof(peer_binary))) {
		result = 0;
		goto out;
	}
	result = wmcs_operation_store_remove(control->state_dir);

out:
	wmcs_secure_zero(&operation, sizeof(operation));
	wmcs_secure_zero(peer_binary, sizeof(peer_binary));
	return result;
}

static int remove_result_for_peer(struct wmcs_control *control,
				  const char *peer_id)
{
	struct wmcs_control_packet response = {0};
	uint8_t peer_binary[WMCS_CONTROL_NODE_ID_SIZE] = {0};
	int result;

	result = wmcs_result_store_load(control->state_dir, &response);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	if (!wmcs_control_id_from_hex(peer_id, peer_binary)) {
		result = -EKEYREJECTED;
		goto out;
	}
	if (memcmp(response.recipient_id, peer_binary, sizeof(peer_binary))) {
		result = 0;
		goto out;
	}
	result = wmcs_result_store_remove(control->state_dir);

out:
	wmcs_secure_zero(&response, sizeof(response));
	wmcs_secure_zero(peer_binary, sizeof(peer_binary));
	return result;
}

static int complete_forget(struct wmcs_control *control,
				   const struct wmcs_forget_record *record)
{
	struct wmcs_peer_record peer = {0};
	bool has_state = false;
	bool peer_present = false;
	int result;

	result = wmcs_wlan_peer_has_state(control->state_dir, record->peer_id,
					 &has_state);
	if (result || has_state) {
		result = result ? result : -EBUSY;
		goto out;
	}
	result = wmcs_identity_load_peer(control->identity, record->peer_id,
					 &peer);
	if (!result) {
		peer_present = true;
		if (peer.generation != record->generation) {
			result = -ESTALE;
			goto out;
		}
	} else if (result != -ENOENT) {
		goto out;
	}
	result = remove_operation_for_peer(control, record->peer_id);
	if (result)
		goto out;
	result = remove_result_for_peer(control, record->peer_id);
	if (result)
		goto out;
	if (peer_present) {
		result = wmcs_identity_delete_peer(control->identity,
						   record->peer_id,
						   record->generation);
		if (result)
			goto out;
	}
	result = wmcs_identity_forget_finish(control->identity);
	if (!result && !strcmp(control->peer_id, record->peer_id)) {
		control->state = WMCS_CONTROL_IDLE;
		clear_exchange(control);
	}

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	return result;
}

static int recover_forget(struct wmcs_control *control)
{
	struct wmcs_forget_record record = {0};
	int result;

	result = wmcs_identity_forget_load(control->identity, &record);
	if (result == -ENOENT)
		return 0;
	if (result)
		return result;
	result = complete_forget(control, &record);
	wmcs_secure_zero(&record, sizeof(record));
	return result;
}

static void complete_agent_wlan_operation(void *private, int status,
					  enum wmcs_control_reason reason,
					  bool backup_pending)
{
	struct wmcs_control *control = private;
	struct wmcs_peer_record peer = {0};
	struct wmcs_wlan_result result = {
		.outcome = status ?
			(status == -EREMOTEIO ? WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED :
			 WMCS_CONTROL_OUTCOME_ROLLED_BACK) :
			WMCS_CONTROL_OUTCOME_COMMITTED,
		.reason = reason,
	};
	int operation_status;

	if (!control || control->state != WMCS_CONTROL_PROCESSING)
		return;
	operation_status = wmcs_identity_load_peer(control->identity,
						  control->peer_id, &peer);
	if (operation_status || peer.role != WMCS_PEER_ROLE_CONTROLLER) {
		control->state = WMCS_CONTROL_FAILED;
		goto out;
	}
	operation_status = make_result(control, &control->request, &peer,
					       &result);
	if (operation_status)
		goto failed;
	/* The result is the commit record: persist it before consuming sequence. */
	operation_status = wmcs_result_store_save(control->state_dir,
						  &control->response);
	if (operation_status)
		goto failed;
	operation_status = advance_peer(control, control->peer_id, &peer,
						control->sequence,
						state_after_result(&peer,
								   &control->response,
								   &result));
	if (operation_status)
		goto failed;
	if (backup_pending && wmcs_wlan_finish(control->state_dir))
		goto failed;
	control->result = result;
	control->state = WMCS_CONTROL_COMPLETE;
	if (send_packet(control, &control->response, &control->peer_address))
		control->state = WMCS_CONTROL_FAILED;
	goto out;

failed:
	/* Keep a pending backup for startup recovery instead of blocking the loop. */
	control->state = WMCS_CONTROL_FAILED;

out:
	wmcs_secure_zero(&peer, sizeof(peer));
}

static int process_agent_request(struct wmcs_control *control,
				 const struct wmcs_control_packet *packet,
				 const uint8_t wire[WMCS_CONTROL_WIRE_SIZE],
				 const struct sockaddr_in *source)
{
	struct wmcs_peer_record peer;
	struct wmcs_wlan_request request;
	struct wmcs_release_request release_request;
	struct wmcs_wlan_result result = {
		.outcome = WMCS_CONTROL_OUTCOME_REJECTED,
		.reason = WMCS_CONTROL_REASON_INTERNAL,
	};
	uint8_t local_id[WMCS_CONTROL_NODE_ID_SIZE];
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	bool dry_run;
	int status;

	if (control->state == WMCS_CONTROL_COMPLETE &&
	    !memcmp(wire, control->request_wire, WMCS_CONTROL_WIRE_SIZE) &&
	    source->sin_addr.s_addr == control->peer_address.sin_addr.s_addr)
		return send_packet(control, &control->response, source);
	if ((control->state != WMCS_CONTROL_LISTENING &&
	     control->state != WMCS_CONTROL_COMPLETE &&
	     control->state != WMCS_CONTROL_FAILED) ||
	    !request_type_valid(packet->type) ||
	    wmcs_control_local_id(control->identity, local_id) ||
	    memcmp(packet->recipient_id, local_id, sizeof(local_id)))
		return -EINVAL;
	wmcs_control_id_to_hex(packet->sender_id, peer_id);
	status = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (status || peer.role != WMCS_PEER_ROLE_CONTROLLER)
		goto out;
	status = wmcs_control_open(packet, peer.relationship_key, plaintext);
	if (status)
		goto out;
	if (packet->type == WMCS_CONTROL_WLAN_REQUEST) {
		if (!wmcs_wlan_request_decode(&request, plaintext))
			goto invalid_payload;
		dry_run = request.dry_run;
	} else {
		if (!wmcs_release_request_decode(&release_request, plaintext))
			goto invalid_payload;
		dry_run = release_request.dry_run;
	}
	control->operation = operation_for_request(packet->type);
	if (control->operation == WMCS_CONTROL_OPERATION_NONE)
		goto out;
	status = send_cached_result(control, packet, peer_id, &peer, source);
	if (!status)
		goto out;
	if (status != -ENOENT)
		goto out;
	if (peer.state == WMCS_PEER_STATE_RELEASED) {
		status = -EACCES;
		goto out;
	}
	if (control->state != WMCS_CONTROL_LISTENING) {
		status = -EALREADY;
		goto out;
	}
	if (packet->sequence != peer.generation + 1U || !packet->sequence) {
		result.reason = WMCS_CONTROL_REASON_REPLAY;
		status = make_result(control, packet, &peer, &result);
		if (!status)
			status = send_packet(control, &control->response, source);
		goto out;
	}
	control->state = WMCS_CONTROL_PROCESSING;
	control->request = *packet;
	memcpy(control->request_wire, wire, WMCS_CONTROL_WIRE_SIZE);
	control->peer_address = *source;
	strcpy(control->peer_id, peer_id);
	control->sequence = packet->sequence;
	if (!dry_run && !control->mutation_enabled) {
		result.reason = WMCS_CONTROL_REASON_MUTATION_DISABLED;
	} else if (packet->type == WMCS_CONTROL_WLAN_REQUEST) {
		status = wmcs_wlan_check_target(control->target_radio, peer_id,
						&request);
		if (status) {
			result.reason = reason_from_check(status);
		} else if (dry_run) {
			result.outcome = WMCS_CONTROL_OUTCOME_DRY_RUN_READY;
			result.reason = WMCS_CONTROL_REASON_NONE;
		} else {
			status = wmcs_wlan_async_start_apply(
				&control->wlan, control->ubus, control->state_dir,
				control->target_radio, peer_id, packet->sequence,
				&request, complete_agent_wlan_operation, control);
			if (!status)
				goto out;
			result.reason = reason_from_check(status);
			result.outcome = WMCS_CONTROL_OUTCOME_ROLLED_BACK;
		}
	} else {
		bool managed_present;

		status = wmcs_wlan_check_release(control->target_radio, peer_id,
						 &managed_present);
		if (status) {
			result.reason = reason_from_check(status);
		} else if (dry_run) {
			result.outcome = WMCS_CONTROL_OUTCOME_DRY_RUN_READY;
			result.reason = WMCS_CONTROL_REASON_NONE;
		} else {
			status = wmcs_wlan_async_start_release(
				&control->wlan, control->ubus, control->state_dir,
				control->target_radio, peer_id, packet->sequence,
				complete_agent_wlan_operation, control);
			if (!status)
				goto out;
			result.reason = reason_from_check(status);
			result.outcome = WMCS_CONTROL_OUTCOME_ROLLED_BACK;
		}
	}
	status = make_result(control, packet, &peer, &result);
	if (status)
		goto cache_failed;
	/* The result is the commit record: persist it before consuming sequence. */
	status = wmcs_result_store_save(control->state_dir, &control->response);
	if (status)
		goto cache_failed;
	status = advance_peer(control, peer_id, &peer, packet->sequence,
			      state_after_result(&peer, &control->response, &result));
	if (status) {
		control->state = WMCS_CONTROL_FAILED;
		goto out;
	}
	control->result = result;
	control->operation = operation_for_request(packet->type);
	control->state = WMCS_CONTROL_COMPLETE;
	status = send_packet(control, &control->response, source);
	goto out;

cache_failed:
	if (result.outcome == WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED) {
		control->state = WMCS_CONTROL_FAILED;
	} else {
		control->state = WMCS_CONTROL_LISTENING;
	}
	goto out;

invalid_payload:
	status = -EKEYREJECTED;

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(&request, sizeof(request));
	wmcs_secure_zero(&release_request, sizeof(release_request));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	wmcs_secure_zero(local_id, sizeof(local_id));
	return status;
}

static int process_controller_result(struct wmcs_control *control,
				     const struct wmcs_control_packet *packet,
				     const struct sockaddr_in *source)
{
	struct wmcs_peer_record peer = {0};
	struct wmcs_wlan_result result = {0};
	uint8_t local_id[WMCS_CONTROL_NODE_ID_SIZE];
	uint8_t peer_binary[WMCS_CONTROL_NODE_ID_SIZE];
	int status;

	if (control->state != WMCS_CONTROL_WAITING_RESULT ||
	    control->journal_phase != WMCS_OPERATION_PENDING ||
	    packet->type != result_type_for_operation(control->operation) ||
	    packet->sequence != control->sequence ||
	    source->sin_addr.s_addr != control->peer_address.sin_addr.s_addr ||
	    wmcs_control_local_id(control->identity, local_id) ||
	    !wmcs_control_id_from_hex(control->peer_id, peer_binary) ||
	    memcmp(packet->recipient_id, local_id, sizeof(local_id)) ||
	    memcmp(packet->sender_id, peer_binary, sizeof(peer_binary)))
		return -EINVAL;
	status = wmcs_identity_load_peer(control->identity, control->peer_id, &peer);
	if (status || peer.role != WMCS_PEER_ROLE_AGENT)
		goto out;
	status = decode_result_payload(packet, &peer, &result);
	if (status)
		goto out;
	status = wmcs_operation_store_save_complete(control->state_dir,
						    &control->request,
						    packet);
	if (status)
		goto out;
	status = reconcile_controller_peer(control, control->peer_id, &peer,
					   packet, &result);
	if (status)
		goto out;
	control->journal_phase = WMCS_OPERATION_COMPLETE;
	control->response = *packet;
	control->result = result;
	control->state = WMCS_CONTROL_COMPLETE;
	uloop_timeout_cancel(&control->retry_timer);
	uloop_timeout_cancel(&control->deadline);
	close_socket(control);
	control->active = false;

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(&result, sizeof(result));
	wmcs_secure_zero(local_id, sizeof(local_id));
	wmcs_secure_zero(peer_binary, sizeof(peer_binary));
	return status;
}

static void receive_packets(struct uloop_fd *event, unsigned int events)
{
	struct wmcs_control *control = container_of(
		event, struct wmcs_control, socket_event);
	uint8_t wire[WMCS_CONTROL_WIRE_SIZE + 1U];
	unsigned int processed = 0;

	(void)events;
	for (;;) {
		struct wmcs_control_packet packet;
		struct sockaddr_in source;
		socklen_t source_size = sizeof(source);
		ssize_t received;

		if (processed++ >= WMCS_CONTROL_PACKET_BUDGET)
			break;

		received = recvfrom(control->fd, wire, sizeof(wire), 0,
				    (struct sockaddr *)&source, &source_size);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			if (errno == EINTR)
				continue;
			break;
		}
		if (source_size != sizeof(source) || source.sin_family != AF_INET ||
		    source.sin_port != htons(WMCS_CONTROL_PORT) ||
		    !wmcs_control_decode(&packet, wire, (size_t)received))
			continue;
		if (role_is(control, "agent"))
			(void)process_agent_request(control, &packet, wire, &source);
		else if (role_is(control, "controller"))
			(void)process_controller_result(control, &packet, &source);
	}
	wmcs_secure_zero(wire, sizeof(wire));
}

static void deadline_expired(struct uloop_timeout *timeout)
{
	struct wmcs_control *control = container_of(
		timeout, struct wmcs_control, deadline);

	close_socket(control);
	control->active = false;
	if (control->state != WMCS_CONTROL_COMPLETE)
		control->state = WMCS_CONTROL_TIMED_OUT;
}

static void retry_expired(struct uloop_timeout *timeout)
{
	struct wmcs_control *control = container_of(
		timeout, struct wmcs_control, retry_timer);

	if (!control->active || !role_is(control, "controller") ||
	    control->state != WMCS_CONTROL_WAITING_RESULT)
		return;
	(void)send_packet(control, &control->request, &control->peer_address);
	uloop_timeout_set(&control->retry_timer, WMCS_CONTROL_RETRY_MS);
}

int wmcs_control_init(struct wmcs_control *control, const char *role,
		      const char *interface, const char *state_dir,
		      const char *source_iface, const char *source_radio,
		      const char *target_radio, bool mutation_enabled,
		      struct wmcs_identity *identity,
		      struct wmcs_discovery *discovery,
		      struct ubus_context *ubus)
{
	if (!control || !role || !interface || !state_dir || !source_iface ||
	    !source_radio || !target_radio || !identity || !discovery || !ubus ||
	    strlen(interface) >= sizeof(control->interface) ||
	    strlen(state_dir) >= sizeof(control->state_dir) ||
	    strlen(source_iface) >= sizeof(control->source_iface) ||
	    strlen(source_radio) >= sizeof(control->source_radio) ||
	    strlen(target_radio) >= sizeof(control->target_radio))
		return -EINVAL;
	memset(control, 0, sizeof(*control));
	control->role = role;
	control->identity = identity;
	control->discovery = discovery;
	control->ubus = ubus;
	control->mutation_enabled = mutation_enabled;
	control->fd = -1;
	control->socket_event.fd = -1;
	control->socket_event.cb = receive_packets;
	control->deadline.cb = deadline_expired;
	control->retry_timer.cb = retry_expired;
	if (wmcs_wlan_async_init(&control->wlan))
		return -EIO;
	strcpy(control->interface, interface);
	strcpy(control->state_dir, state_dir);
	strcpy(control->source_iface, source_iface);
	strcpy(control->source_radio, source_radio);
	strcpy(control->target_radio, target_radio);
	return 0;
}

int wmcs_control_stop(struct wmcs_control *control)
{
	int result;

	if (!control)
		return -EINVAL;
	if (wmcs_wlan_async_active(&control->wlan))
		return -EBUSY;
	uloop_timeout_cancel(&control->deadline);
	close_socket(control);
	control->active = false;
	memset(&control->peer_address, 0, sizeof(control->peer_address));
	if (role_is(control, "controller") &&
	    control->journal_phase == WMCS_OPERATION_PENDING) {
		control->state = WMCS_CONTROL_RECOVERY_PENDING;
		return 0;
	}
	if (role_is(control, "controller") &&
	    control->journal_phase == WMCS_OPERATION_COMPLETE) {
		if (!result_consumes_sequence(&control->result))
			return 0;
		result = wmcs_operation_store_remove(control->state_dir);
		if (result) {
			control->state = WMCS_CONTROL_FAILED;
			return result;
		}
	}
	control->state = WMCS_CONTROL_IDLE;
	clear_exchange(control);
	return 0;
}

void wmcs_control_close(struct wmcs_control *control)
{
	if (!control)
		return;
	uloop_timeout_cancel(&control->deadline);
	wmcs_wlan_async_close(&control->wlan);
	close_socket(control);
	control->active = false;
	clear_exchange(control);
}

int wmcs_control_forget_peer(struct wmcs_control *control,
			     const char *peer_id)
{
	struct wmcs_peer_record peer = {0};
	struct wmcs_forget_record existing = {0};
	struct wmcs_forget_record record = {0};
	bool has_state = false;
	int result;

	if (!control || !peer_id)
		return -EINVAL;
	if (control->active)
		return -EBUSY;
	result = wmcs_identity_forget_load(control->identity, &existing);
	if (!result) {
		if (strcmp(existing.peer_id, peer_id)) {
			result = -EBUSY;
			goto out;
		}
		result = complete_forget(control, &existing);
		goto out;
	}
	if (result != -ENOENT)
		goto out;
	result = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (result)
		goto out;
	result = wmcs_wlan_peer_has_state(control->state_dir, peer_id,
					 &has_state);
	if (result || has_state) {
		result = result ? result : -EBUSY;
		goto out;
	}
	record.generation = peer.generation;
	strcpy(record.peer_id, peer_id);
	result = wmcs_identity_forget_begin(control->identity, peer_id,
					   record.generation);
	if (!result)
		result = complete_forget(control, &record);

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(&existing, sizeof(existing));
	wmcs_secure_zero(&record, sizeof(record));
	return result;
}

int wmcs_control_recover(struct wmcs_control *control)
{
	int result;

	if (!control)
		return -EINVAL;
	result = recover_forget(control);
	if (result)
		return result;
	if (role_is(control, "agent")) {
		result = recover_cached_result(control);
		if (result)
			return result;
	} else if (role_is(control, "controller")) {
		result = recover_controller_operation(control);
		if (result)
			return result;
	}
	return wmcs_wlan_recover(control->ubus, control->identity,
				 control->state_dir, control->target_radio);
}

static int start_common(struct wmcs_control *control, uint32_t duration_seconds)
{
	int result;

	if (!control || duration_seconds < WMCS_CONTROL_MIN_SECONDS ||
	    duration_seconds > WMCS_CONTROL_MAX_SECONDS)
		return -EINVAL;
	if (control->active)
		return -EALREADY;
	if (!wmcs_identity_ready(control->identity))
		return -ENOKEY;
	result = open_socket(control);
	if (result)
		return result;
	control->active = true;
	uloop_timeout_set(&control->deadline, (int)(duration_seconds * 1000U));
	return 0;
}

int wmcs_control_listen(struct wmcs_control *control, uint32_t duration_seconds)
{
	int result;

	if (!role_is(control, "agent"))
		return -EOPNOTSUPP;
	result = start_common(control, duration_seconds);
	if (!result) {
		clear_exchange(control);
		control->state = WMCS_CONTROL_LISTENING;
	}
	return result;
}

static int parse_peer_address(const char *address,
			      struct sockaddr_in *peer_address)
{
	if (!address || !peer_address)
		return -EINVAL;
	memset(peer_address, 0, sizeof(*peer_address));
	peer_address->sin_family = AF_INET;
	peer_address->sin_port = htons(WMCS_CONTROL_PORT);
	return inet_pton(AF_INET, address, &peer_address->sin_addr) == 1 ?
		       0 : -EINVAL;
}

static void suspend_controller_operation(struct wmcs_control *control)
{
	uloop_timeout_cancel(&control->deadline);
	close_socket(control);
	control->active = false;
	memset(&control->peer_address, 0, sizeof(control->peer_address));
	control->state = WMCS_CONTROL_RECOVERY_PENDING;
}

static int resume_controller_operation(
	struct wmcs_control *control, const char *address, const char *peer_id,
	bool dry_run, enum wmcs_control_operation operation,
	uint32_t duration_seconds)
{
	struct wmcs_peer_record peer = {0};
	struct sockaddr_in peer_address;
	int result;

	if (control->journal_phase != WMCS_OPERATION_PENDING)
		return -ENOENT;
	if (control->operation != operation ||
	    strcmp(control->peer_id, peer_id) ||
	    control->request_dry_run != dry_run)
		return -EALREADY;
	result = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (result)
		goto out;
	if (peer.role != WMCS_PEER_ROLE_AGENT ||
	    peer.state != WMCS_PEER_STATE_ACTIVE ||
	    control->sequence != peer.generation + 1U) {
		result = -EKEYREJECTED;
		goto out;
	}
	result = parse_peer_address(address, &peer_address);
	if (result)
		goto out;
	result = start_common(control, duration_seconds);
	if (result)
		goto out;
	control->peer_address = peer_address;
	control->state = WMCS_CONTROL_WAITING_RESULT;
	result = send_packet(control, &control->request, &control->peer_address);
	if (result) {
		suspend_controller_operation(control);
		goto out;
	}
	uloop_timeout_set(&control->retry_timer, WMCS_CONTROL_RETRY_MS);

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	memset(&peer_address, 0, sizeof(peer_address));
	return result;
}

static int start_controller_operation(
	struct wmcs_control *control, const char *address, const char *peer_id,
	bool dry_run, enum wmcs_control_operation operation,
	const struct wmcs_control_packet *request, uint32_t duration_seconds)
{
	struct sockaddr_in peer_address;
	int result;

	if (wmcs_control_reconciliation_pending(control))
		return -ESTALE;
	result = parse_peer_address(address, &peer_address);
	if (result)
		return result;
	result = start_common(control, duration_seconds);
	if (result)
		return result;
	result = wmcs_operation_store_save_pending(control->state_dir, request);
	if (result) {
		uloop_timeout_cancel(&control->deadline);
		close_socket(control);
		control->active = false;
		return result;
	}
	clear_exchange(control);
	control->request = *request;
	control->peer_address = peer_address;
	strcpy(control->peer_id, peer_id);
	control->sequence = request->sequence;
	control->operation = operation;
	control->journal_phase = WMCS_OPERATION_PENDING;
	control->request_dry_run = dry_run;
	control->state = WMCS_CONTROL_WAITING_RESULT;
	result = send_packet(control, &control->request, &control->peer_address);
	if (result) {
		suspend_controller_operation(control);
		return result;
	}
	uloop_timeout_set(&control->retry_timer, WMCS_CONTROL_RETRY_MS);
	return 0;
}

int wmcs_control_sync_start(struct wmcs_control *control, const char *address,
			    const char *peer_id, bool dry_run,
			    uint32_t duration_seconds)
{
	struct wmcs_control_packet packet = {0};
	struct wmcs_peer_record peer = {0};
	struct wmcs_wlan_request request = {0};
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	int result;

	if (!role_is(control, "controller") || !address || !peer_id)
		return -EOPNOTSUPP;
	if (!dry_run && !control->mutation_enabled)
		return -EACCES;
	result = resume_controller_operation(
		control, address, peer_id, dry_run,
		WMCS_CONTROL_OPERATION_WLAN_SYNC, duration_seconds);
	if (result != -ENOENT)
		return result;
	result = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (result)
		return result;
	if (peer.role != WMCS_PEER_ROLE_AGENT ||
	    peer.state != WMCS_PEER_STATE_ACTIVE ||
	    peer.generation == UINT64_MAX) {
		result = -EKEYREJECTED;
		goto out;
	}
	result = wmcs_wlan_read_source(control->source_iface,
				       control->source_radio, &request);
	if (result)
		goto out;
	request.dry_run = dry_run;
	if (!wmcs_control_id_from_hex(peer_id, packet.recipient_id) ||
	    wmcs_control_local_id(control->identity, packet.sender_id)) {
		result = -EINVAL;
		goto out;
	}
	packet.type = WMCS_CONTROL_WLAN_REQUEST;
	packet.sequence = peer.generation + 1U;
	if (!wmcs_wlan_request_encode(plaintext, &request)) {
		result = -EINVAL;
		goto out;
	}
	result = wmcs_control_seal(&packet, peer.relationship_key, plaintext);
	if (result)
		goto out;
	result = start_controller_operation(
		control, address, peer_id, dry_run,
		WMCS_CONTROL_OPERATION_WLAN_SYNC, &packet, duration_seconds);
out:
	wmcs_secure_zero(&packet, sizeof(packet));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(&request, sizeof(request));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return result;
}

int wmcs_control_release_start(struct wmcs_control *control, const char *address,
			       const char *peer_id, bool dry_run,
			       uint32_t duration_seconds)
{
	struct wmcs_control_packet packet = {0};
	struct wmcs_peer_record peer = {0};
	struct wmcs_release_request request = {
		.dry_run = dry_run,
	};
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	int result;

	if (!role_is(control, "controller") || !address || !peer_id)
		return -EOPNOTSUPP;
	if (!dry_run && !control->mutation_enabled)
		return -EACCES;
	result = resume_controller_operation(
		control, address, peer_id, dry_run,
		WMCS_CONTROL_OPERATION_RELEASE, duration_seconds);
	if (result != -ENOENT)
		return result;
	result = wmcs_identity_load_peer(control->identity, peer_id, &peer);
	if (result)
		return result;
	if (peer.role != WMCS_PEER_ROLE_AGENT ||
	    peer.state != WMCS_PEER_STATE_ACTIVE ||
	    peer.generation == UINT64_MAX) {
		result = -EKEYREJECTED;
		goto out;
	}
	if (!wmcs_control_id_from_hex(peer_id, packet.recipient_id) ||
	    wmcs_control_local_id(control->identity, packet.sender_id)) {
		result = -EINVAL;
		goto out;
	}
	packet.type = WMCS_CONTROL_RELEASE_REQUEST;
	packet.sequence = peer.generation + 1U;
	if (!wmcs_release_request_encode(plaintext, &request)) {
		result = -EINVAL;
		goto out;
	}
	result = wmcs_control_seal(&packet, peer.relationship_key, plaintext);
	if (result)
		goto out;
	result = start_controller_operation(
		control, address, peer_id, dry_run,
		WMCS_CONTROL_OPERATION_RELEASE, &packet, duration_seconds);
out:
	wmcs_secure_zero(&packet, sizeof(packet));
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(&request, sizeof(request));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return result;
}

bool wmcs_control_active(const struct wmcs_control *control)
{
	return control && control->active;
}

bool wmcs_control_reconciliation_pending(const struct wmcs_control *control)
{
	if (!control || !role_is(control, "controller"))
		return false;
	if (control->journal_phase == WMCS_OPERATION_PENDING)
		return true;
	return control->journal_phase == WMCS_OPERATION_COMPLETE &&
	       !result_consumes_sequence(&control->result);
}

enum wmcs_control_state wmcs_control_state(const struct wmcs_control *control)
{
	return control ? control->state : WMCS_CONTROL_IDLE;
}

enum wmcs_control_operation wmcs_control_operation(
	const struct wmcs_control *control)
{
	return control ? control->operation : WMCS_CONTROL_OPERATION_NONE;
}

const char *wmcs_control_operation_name(enum wmcs_control_operation operation)
{
	switch (operation) {
	case WMCS_CONTROL_OPERATION_WLAN_SYNC:
		return "wlan_sync";
	case WMCS_CONTROL_OPERATION_RELEASE:
		return "release";
	case WMCS_CONTROL_OPERATION_NONE:
	default:
		return "none";
	}
}

const char *wmcs_control_state_name(enum wmcs_control_state state)
{
	switch (state) {
	case WMCS_CONTROL_LISTENING:
		return "listening";
	case WMCS_CONTROL_WAITING_RESULT:
		return "waiting_result";
	case WMCS_CONTROL_PROCESSING:
		return "processing";
	case WMCS_CONTROL_COMPLETE:
		return "complete";
	case WMCS_CONTROL_RECOVERY_PENDING:
		return "recovery_pending";
	case WMCS_CONTROL_FAILED:
		return "failed";
	case WMCS_CONTROL_TIMED_OUT:
		return "timed_out";
	case WMCS_CONTROL_IDLE:
	default:
		return "idle";
	}
}

const char *wmcs_control_outcome_name(enum wmcs_control_outcome outcome)
{
	switch (outcome) {
	case WMCS_CONTROL_OUTCOME_DRY_RUN_READY:
		return "dry_run_ready";
	case WMCS_CONTROL_OUTCOME_COMMITTED:
		return "committed";
	case WMCS_CONTROL_OUTCOME_ROLLED_BACK:
		return "rolled_back";
	case WMCS_CONTROL_OUTCOME_REJECTED:
		return "rejected";
	case WMCS_CONTROL_OUTCOME_ROLLBACK_FAILED:
		return "rollback_failed";
	case WMCS_CONTROL_OUTCOME_NONE:
	default:
		return "none";
	}
}

const char *wmcs_control_reason_name(enum wmcs_control_reason reason)
{
	switch (reason) {
	case WMCS_CONTROL_REASON_NONE:
		return "none";
	case WMCS_CONTROL_REASON_INVALID_PROFILE:
		return "invalid_profile";
	case WMCS_CONTROL_REASON_OWNERSHIP_CONFLICT:
		return "ownership_conflict";
	case WMCS_CONTROL_REASON_PLATFORM_FAILURE:
		return "platform_failure";
	case WMCS_CONTROL_REASON_VERIFY_FAILURE:
		return "verify_failure";
	case WMCS_CONTROL_REASON_REPLAY:
		return "replay_rejected";
	case WMCS_CONTROL_REASON_MUTATION_DISABLED:
		return "mutation_disabled";
	case WMCS_CONTROL_REASON_INTERNAL:
	default:
		return "internal_error";
	}
}

const char *wmcs_control_peer_id(const struct wmcs_control *control)
{
	return control ? control->peer_id : "";
}

uint64_t wmcs_control_sequence(const struct wmcs_control *control)
{
	return control ? control->sequence : 0;
}

enum wmcs_control_outcome wmcs_control_outcome(
	const struct wmcs_control *control)
{
	return control ? control->result.outcome : WMCS_CONTROL_OUTCOME_NONE;
}

enum wmcs_control_reason wmcs_control_reason(const struct wmcs_control *control)
{
	return control ? control->result.reason : WMCS_CONTROL_REASON_INTERNAL;
}
