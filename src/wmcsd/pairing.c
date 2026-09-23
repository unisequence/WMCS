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

#include "pairing.h"

#define WMCS_PAIRING_PORT 45124U
#define WMCS_PAIRING_RETRY_MS 1000
#define WMCS_PAIRING_PACKET_BUDGET 64U

static bool role_is(const struct wmcs_pairing *pairing, const char *role)
{
	return pairing && !strcmp(pairing->role, role);
}

static void clear_exchange(struct wmcs_pairing *pairing)
{
	if (pairing->ephemeral_key)
		psa_destroy_key(pairing->ephemeral_key);
	pairing->ephemeral_key = 0;
	wmcs_secure_zero(&pairing->request, sizeof(pairing->request));
	wmcs_secure_zero(&pairing->response, sizeof(pairing->response));
	wmcs_secure_zero(pairing->relationship_key,
			 sizeof(pairing->relationship_key));
	wmcs_secure_zero(pairing->sas, sizeof(pairing->sas));
	wmcs_secure_zero(pairing->peer_id, sizeof(pairing->peer_id));
	memset(&pairing->peer_address, 0, sizeof(pairing->peer_address));
}

static int open_socket(struct wmcs_pairing *pairing)
{
	struct sockaddr_in bind_address = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_PAIRING_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int enabled = 1;
	int fd;

	if (!if_nametoindex(pairing->interface))
		return -ENODEV;
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
	    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, pairing->interface,
		       strlen(pairing->interface) + 1U) ||
	    bind(fd, (struct sockaddr *)&bind_address, sizeof(bind_address))) {
		int error = -errno;

		close(fd);
		return error;
	}
	pairing->socket_event.fd = fd;
	if (uloop_fd_add(&pairing->socket_event, ULOOP_READ)) {
		int error = errno ? -errno : -EIO;

		pairing->socket_event.fd = -1;
		close(fd);
		return error;
	}
	pairing->fd = fd;
	return 0;
}

static int send_pairing_message(struct wmcs_pairing *pairing,
				const struct wmcs_pairing_message *message,
				const struct sockaddr_in *destination)
{
	uint8_t wire[WMCS_PAIRING_WIRE_SIZE];
	ssize_t sent;

	if (!wmcs_pairing_encode(wire, message))
		return -EINVAL;
	sent = sendto(pairing->fd, wire, sizeof(wire), 0,
		      (const struct sockaddr *)destination, sizeof(*destination));
	if (sent < 0)
		return -errno;
	return (size_t)sent == sizeof(wire) ? 0 : -EIO;
}

static int finalize_pending(struct wmcs_pairing *pairing,
			    const uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE],
			    const uint8_t peer_identity[WMCS_IDENTITY_PUBLIC_SIZE],
			    const uint8_t peer_ephemeral[WMCS_IDENTITY_PUBLIC_SIZE])
{
	const uint8_t *local_ephemeral = role_is(pairing, "controller") ?
		pairing->request.ephemeral_public :
		pairing->response.ephemeral_public;
	int result;

	if (!memcmp(peer_identity,
		    wmcs_identity_public_key(pairing->identity),
		    WMCS_IDENTITY_PUBLIC_SIZE) ||
	    !memcmp(peer_ephemeral, local_ephemeral,
		    WMCS_IDENTITY_PUBLIC_SIZE))
		return -EKEYREJECTED;
	result = wmcs_identity_relationship_key(pairing->ephemeral_key,
						 peer_ephemeral,
						 pairing->request.nonce,
						 transcript_hash,
						 pairing->relationship_key);
	if (result)
		return result;
	result = wmcs_pairing_sas(transcript_hash, pairing->sas);
	if (result)
		return result;
	result = wmcs_identity_fingerprint(peer_identity, pairing->peer_id);
	if (result)
		return result;
	pairing->state = WMCS_PAIRING_AWAITING_CONFIRMATION;
	uloop_timeout_cancel(&pairing->retry_timer);
	return 0;
}

static int accept_request(struct wmcs_pairing *pairing,
			  const struct wmcs_pairing_message *request,
			  const struct sockaddr_in *source)
{
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE];
	int result;

	if (pairing->state == WMCS_PAIRING_AWAITING_CONFIRMATION) {
		if (!memcmp(request->nonce, pairing->request.nonce,
			    sizeof(request->nonce)) &&
		    !memcmp(request->identity_public,
			    pairing->request.identity_public,
			    sizeof(request->identity_public)) &&
		    !memcmp(request->ephemeral_public,
			    pairing->request.ephemeral_public,
			    sizeof(request->ephemeral_public)))
			return send_pairing_message(pairing, &pairing->response, source);
		return -EALREADY;
	}
	result = wmcs_pairing_verify_request(request);
	if (result)
		return result;
	pairing->request = *request;
	pairing->response.type = WMCS_PAIRING_RESPONSE;
	memcpy(pairing->response.nonce, request->nonce,
	       sizeof(pairing->response.nonce));
	memcpy(pairing->response.identity_public,
	       wmcs_identity_public_key(pairing->identity),
	       sizeof(pairing->response.identity_public));
	result = wmcs_identity_ephemeral_create(
		&pairing->ephemeral_key, pairing->response.ephemeral_public);
	if (result)
		goto fail;
	result = wmcs_pairing_sign_response(pairing->identity, &pairing->request,
					    &pairing->response, transcript_hash);
	if (result)
		goto fail;
	result = finalize_pending(pairing, transcript_hash,
				  request->identity_public,
				  request->ephemeral_public);
	if (result)
		goto fail;
	pairing->peer_address = *source;
	wmcs_secure_zero(transcript_hash, sizeof(transcript_hash));
	return send_pairing_message(pairing, &pairing->response, source);

fail:
	wmcs_secure_zero(transcript_hash, sizeof(transcript_hash));
	clear_exchange(pairing);
	pairing->state = WMCS_PAIRING_EXCHANGING;
	return result;
}

static int accept_response(struct wmcs_pairing *pairing,
			   const struct wmcs_pairing_message *response,
			   const struct sockaddr_in *source)
{
	uint8_t transcript_hash[WMCS_IDENTITY_HASH_SIZE];
	int result;

	if (pairing->state != WMCS_PAIRING_EXCHANGING ||
	    source->sin_addr.s_addr != pairing->peer_address.sin_addr.s_addr ||
	    memcmp(response->nonce, pairing->request.nonce,
		   sizeof(response->nonce)))
		return -EINVAL;
	result = wmcs_pairing_verify_response(&pairing->request, response,
					      transcript_hash);
	if (!result) {
		pairing->response = *response;
		result = finalize_pending(pairing, transcript_hash,
					  response->identity_public,
					  response->ephemeral_public);
	}
	wmcs_secure_zero(transcript_hash, sizeof(transcript_hash));
	return result;
}

static void receive_messages(struct uloop_fd *event, unsigned int events)
{
	struct wmcs_pairing *pairing = container_of(
		event, struct wmcs_pairing, socket_event);
	uint8_t wire[WMCS_PAIRING_WIRE_SIZE + 1U];
	unsigned int processed = 0;

	(void)events;
	for (;;) {
		struct wmcs_pairing_message message;
		struct sockaddr_in source;
		socklen_t source_size = sizeof(source);
		ssize_t received;

		if (processed++ >= WMCS_PAIRING_PACKET_BUDGET)
			break;

		received = recvfrom(pairing->fd, wire, sizeof(wire), 0,
				    (struct sockaddr *)&source, &source_size);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno == EINTR)
				continue;
			return;
		}
		if (source_size != sizeof(source) || source.sin_family != AF_INET ||
		    source.sin_port != htons(WMCS_PAIRING_PORT) ||
		    !wmcs_pairing_decode(&message, wire, (size_t)received))
			continue;
		if (role_is(pairing, "agent") &&
		    message.type == WMCS_PAIRING_REQUEST)
			(void)accept_request(pairing, &message, &source);
		else if (role_is(pairing, "controller") &&
			 message.type == WMCS_PAIRING_RESPONSE)
			(void)accept_response(pairing, &message, &source);
	}
}

static void deadline_expired(struct uloop_timeout *timeout)
{
	struct wmcs_pairing *pairing = container_of(
		timeout, struct wmcs_pairing, deadline);

	wmcs_pairing_stop(pairing);
}

static void retry_expired(struct uloop_timeout *timeout)
{
	struct wmcs_pairing *pairing = container_of(
		timeout, struct wmcs_pairing, retry_timer);

	if (!pairing->active || !role_is(pairing, "controller") ||
	    pairing->state != WMCS_PAIRING_EXCHANGING)
		return;
	(void)send_pairing_message(pairing, &pairing->request,
				   &pairing->peer_address);
	uloop_timeout_set(&pairing->retry_timer, WMCS_PAIRING_RETRY_MS);
}

int wmcs_pairing_init(struct wmcs_pairing *pairing, const char *role,
		      const char *interface, struct wmcs_identity *identity)
{
	if (!pairing || !role || !interface || !identity ||
	    strlen(interface) >= IF_NAMESIZE)
		return -EINVAL;
	memset(pairing, 0, sizeof(*pairing));
	pairing->role = role;
	pairing->identity = identity;
	pairing->fd = -1;
	pairing->socket_event.fd = -1;
	pairing->socket_event.cb = receive_messages;
	pairing->deadline.cb = deadline_expired;
	pairing->retry_timer.cb = retry_expired;
	strcpy(pairing->interface, interface);
	return 0;
}

void wmcs_pairing_stop(struct wmcs_pairing *pairing)
{
	if (!pairing)
		return;
	uloop_timeout_cancel(&pairing->deadline);
	uloop_timeout_cancel(&pairing->retry_timer);
	if (pairing->fd >= 0) {
		uloop_fd_delete(&pairing->socket_event);
		close(pairing->fd);
		pairing->fd = -1;
		pairing->socket_event.fd = -1;
	}
	pairing->active = false;
	pairing->state = WMCS_PAIRING_IDLE;
	clear_exchange(pairing);
}

void wmcs_pairing_close(struct wmcs_pairing *pairing)
{
	wmcs_pairing_stop(pairing);
}

static int start_common(struct wmcs_pairing *pairing, uint32_t duration_seconds)
{
	int result;

	if (!pairing || duration_seconds < WMCS_PAIRING_MIN_SECONDS ||
	    duration_seconds > WMCS_PAIRING_MAX_SECONDS)
		return -EINVAL;
	if (pairing->active)
		return -EALREADY;
	if (!role_is(pairing, "controller") && !role_is(pairing, "agent"))
		return -EOPNOTSUPP;
	result = wmcs_identity_ensure(pairing->identity);
	if (result)
		return result;
	result = open_socket(pairing);
	if (result)
		return result;
	pairing->active = true;
	pairing->state = WMCS_PAIRING_EXCHANGING;
	uloop_timeout_set(&pairing->deadline, (int)(duration_seconds * 1000U));
	return 0;
}

int wmcs_pairing_start_agent(struct wmcs_pairing *pairing,
			     uint32_t duration_seconds)
{
	if (!role_is(pairing, "agent"))
		return -EOPNOTSUPP;
	return start_common(pairing, duration_seconds);
}

int wmcs_pairing_start_controller(struct wmcs_pairing *pairing,
				  const char *address,
				  uint32_t duration_seconds)
{
	static const uint8_t zero_nonce[WMCS_PAIRING_NONCE_SIZE] = {0};
	int result;
	unsigned int attempt;

	if (!role_is(pairing, "controller") || !address)
		return -EOPNOTSUPP;
	result = start_common(pairing, duration_seconds);
	if (result)
		return result;
	pairing->peer_address.sin_family = AF_INET;
	pairing->peer_address.sin_port = htons(WMCS_PAIRING_PORT);
	if (inet_pton(AF_INET, address, &pairing->peer_address.sin_addr) != 1) {
		wmcs_pairing_stop(pairing);
		return -EINVAL;
	}
	pairing->request.type = WMCS_PAIRING_REQUEST;
	for (attempt = 0; attempt < 2U; attempt++) {
		if (psa_generate_random(pairing->request.nonce,
					 sizeof(pairing->request.nonce)) == PSA_SUCCESS &&
		    memcmp(pairing->request.nonce, zero_nonce, sizeof(zero_nonce)))
			break;
	}
	if (attempt == 2U) {
		wmcs_pairing_stop(pairing);
		return -EIO;
	}
	memcpy(pairing->request.identity_public,
	       wmcs_identity_public_key(pairing->identity),
	       sizeof(pairing->request.identity_public));
	result = wmcs_identity_ephemeral_create(
		&pairing->ephemeral_key, pairing->request.ephemeral_public);
	if (!result)
		result = wmcs_pairing_sign_request(pairing->identity,
						   &pairing->request);
	if (!result)
		result = send_pairing_message(pairing, &pairing->request,
					      &pairing->peer_address);
	if (result) {
		wmcs_pairing_stop(pairing);
		return result;
	}
	uloop_timeout_set(&pairing->retry_timer, WMCS_PAIRING_RETRY_MS);
	return 0;
}

static bool sas_equal(const char *first, const char *second)
{
	unsigned int difference = 0;
	size_t i;

	if (!first || !second || strlen(first) != WMCS_PAIRING_SAS_SIZE - 1U ||
	    strlen(second) != WMCS_PAIRING_SAS_SIZE - 1U)
		return false;
	for (i = 0; i < WMCS_PAIRING_SAS_SIZE - 1U; i++)
		difference |= (unsigned char)first[i] ^ (unsigned char)second[i];
	return difference == 0;
}

int wmcs_pairing_confirm(struct wmcs_pairing *pairing, const char *sas,
			 char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U])
{
	enum wmcs_peer_role role;
	int result;

	if (!pairing || !sas || !peer_id || !pairing->active ||
	    pairing->state != WMCS_PAIRING_AWAITING_CONFIRMATION)
		return -EINVAL;
	if (!sas_equal(sas, pairing->sas))
		return -EKEYREJECTED;
	role = role_is(pairing, "controller") ? WMCS_PEER_ROLE_AGENT :
					       WMCS_PEER_ROLE_CONTROLLER;
	result = wmcs_identity_store_peer(pairing->identity,
					  role_is(pairing, "controller") ?
						  pairing->response.identity_public :
						  pairing->request.identity_public,
					  pairing->relationship_key, role, peer_id);
	if (!result)
		wmcs_pairing_stop(pairing);
	return result;
}

bool wmcs_pairing_active(const struct wmcs_pairing *pairing)
{
	return pairing && pairing->active;
}

enum wmcs_pairing_state wmcs_pairing_state(const struct wmcs_pairing *pairing)
{
	return pairing ? pairing->state : WMCS_PAIRING_IDLE;
}

const char *wmcs_pairing_state_name(enum wmcs_pairing_state state)
{
	switch (state) {
	case WMCS_PAIRING_EXCHANGING:
		return "exchanging";
	case WMCS_PAIRING_AWAITING_CONFIRMATION:
		return "awaiting_confirmation";
	case WMCS_PAIRING_IDLE:
	default:
		return "idle";
	}
}

const char *wmcs_pairing_sas_value(const struct wmcs_pairing *pairing)
{
	return pairing && pairing->state == WMCS_PAIRING_AWAITING_CONFIRMATION ?
		       pairing->sas : "";
}

const char *wmcs_pairing_peer_id(const struct wmcs_pairing *pairing)
{
	return pairing && pairing->state == WMCS_PAIRING_AWAITING_CONFIRMATION ?
		       pairing->peer_id : "";
}
