// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <libubox/utils.h>

#include "discovery.h"

#define WMCS_DISCOVERY_GROUP "239.255.77.67"
#define WMCS_DISCOVERY_PORT 45123U
#define WMCS_DISCOVERY_PROBE_INTERVAL_MS 2000
#define WMCS_DISCOVERY_NODE_TTL_MS 300000U
#define WMCS_DISCOVERY_PACKET_BUDGET 64U

uint64_t wmcs_monotonic_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return 0;

	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static bool fill_random(void *buffer, size_t size)
{
	uint8_t *bytes = buffer;
	size_t offset = 0;
	int fd;

	while (offset < size) {
		ssize_t result = getrandom(bytes + offset, size - offset, GRND_NONBLOCK);

		if (result > 0) {
			offset += (size_t)result;
			continue;
		}
		if (result < 0 && errno == EINTR)
			continue;
		break;
	}

	if (offset == size)
		return true;

	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	while (offset < size) {
		ssize_t result = read(fd, bytes + offset, size - offset);

		if (result > 0) {
			offset += (size_t)result;
			continue;
		}
		if (result < 0 && errno == EINTR)
			continue;
		close(fd);
		return false;
	}

	close(fd);
	return true;
}

static uint8_t wire_role(const struct wmcs_discovery *discovery)
{
	if (!strcmp(discovery->role, "controller"))
		return WMCS_DISCOVERY_ROLE_CONTROLLER;
	if (!strcmp(discovery->role, "agent"))
		return WMCS_DISCOVERY_ROLE_AGENT;
	return 0;
}

static int send_message(struct wmcs_discovery *discovery, uint8_t type,
			uint64_t nonce, const struct sockaddr_in *destination)
{
	struct wmcs_discovery_message message = {
		.type = type,
		.nonce = nonce,
		.role = wire_role(discovery),
	};
	uint8_t wire[WMCS_DISCOVERY_WIRE_SIZE];
	ssize_t sent;

	memcpy(message.instance_id, discovery->instance_id, sizeof(message.instance_id));
	if (!wmcs_discovery_encode(wire, &message))
		return -EINVAL;

	sent = sendto(discovery->fd, wire, sizeof(wire), 0,
		      (const struct sockaddr *)destination, sizeof(*destination));
	if (sent < 0)
		return -errno;
	if ((size_t)sent != sizeof(wire))
		return -EIO;

	return 0;
}

static int send_probe(struct wmcs_discovery *discovery)
{
	struct sockaddr_in destination = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_DISCOVERY_PORT),
	};

	if (inet_pton(AF_INET, WMCS_DISCOVERY_GROUP, &destination.sin_addr) != 1)
		return -EINVAL;

	return send_message(discovery, WMCS_DISCOVERY_PROBE, discovery->nonce,
			    &destination);
}

static void prune_nodes(struct wmcs_discovery *discovery, uint64_t now)
{
	size_t input;
	size_t output = 0;

	for (input = 0; input < discovery->node_count; input++) {
		if (now >= discovery->nodes[input].last_seen_ms &&
		    now - discovery->nodes[input].last_seen_ms >=
			    WMCS_DISCOVERY_NODE_TTL_MS)
			continue;
		if (output != input)
			discovery->nodes[output] = discovery->nodes[input];
		output++;
	}

	discovery->node_count = output;
}

static void record_node(struct wmcs_discovery *discovery,
			const struct wmcs_discovery_message *message,
			const struct sockaddr_in *source)
{
	struct wmcs_discovery_node *node = NULL;
	uint64_t now = wmcs_monotonic_ms();
	size_t i;

	prune_nodes(discovery, now);

	for (i = 0; i < discovery->node_count; i++) {
		if (!memcmp(discovery->nodes[i].instance_id, message->instance_id,
			    WMCS_DISCOVERY_INSTANCE_SIZE)) {
			node = &discovery->nodes[i];
			break;
		}
	}

	if (!node) {
		if (discovery->node_count >= WMCS_DISCOVERY_MAX_NODES)
			return;
		node = &discovery->nodes[discovery->node_count++];
		memset(node, 0, sizeof(*node));
		memcpy(node->instance_id, message->instance_id,
		       WMCS_DISCOVERY_INSTANCE_SIZE);
	}

	if (!inet_ntop(AF_INET, &source->sin_addr, node->address,
		       sizeof(node->address)))
		strcpy(node->address, "0.0.0.0");
	node->last_seen_ms = now;
}

static void receive_messages(struct uloop_fd *event, unsigned int events)
{
	struct wmcs_discovery *discovery = container_of(
		event, struct wmcs_discovery, socket_event);
	uint8_t wire[WMCS_DISCOVERY_WIRE_SIZE + 1U];
	unsigned int processed = 0;

	(void)events;

	for (;;) {
		struct wmcs_discovery_message message;
		struct sockaddr_in source;
		socklen_t source_size = sizeof(source);
		ssize_t received;

		if (processed++ >= WMCS_DISCOVERY_PACKET_BUDGET)
			break;

		received = recvfrom(discovery->fd, wire, sizeof(wire), 0,
				    (struct sockaddr *)&source, &source_size);
		if (received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno == EINTR)
				continue;
			return;
		}

		if (source_size != sizeof(source) || source.sin_family != AF_INET ||
		    !wmcs_discovery_decode(&message, wire, (size_t)received))
			continue;

		if (!strcmp(discovery->role, "agent") &&
		    message.type == WMCS_DISCOVERY_PROBE) {
			(void)send_message(discovery, WMCS_DISCOVERY_ANNOUNCEMENT,
					   message.nonce, &source);
		} else if (!strcmp(discovery->role, "controller") &&
			   message.type == WMCS_DISCOVERY_ANNOUNCEMENT &&
			   message.nonce == discovery->nonce) {
			record_node(discovery, &message, &source);
		}
	}
}

static void deadline_expired(struct uloop_timeout *timeout)
{
	struct wmcs_discovery *discovery = container_of(
		timeout, struct wmcs_discovery, deadline);

	wmcs_discovery_stop(discovery);
}

static void probe_timer_expired(struct uloop_timeout *timeout)
{
	struct wmcs_discovery *discovery = container_of(
		timeout, struct wmcs_discovery, probe_timer);

	if (!discovery->active || strcmp(discovery->role, "controller"))
		return;

	(void)send_probe(discovery);
	uloop_timeout_set(&discovery->probe_timer, WMCS_DISCOVERY_PROBE_INTERVAL_MS);
}

static int open_socket(struct wmcs_discovery *discovery)
{
	struct sockaddr_in bind_address = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_DISCOVERY_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	struct ip_mreqn membership = {0};
	unsigned int interface_index;
	unsigned char ttl = 1;
	unsigned char loop = 1;
	int enabled = 1;
	int fd;

	interface_index = if_nametoindex(discovery->interface);
	if (!interface_index)
		return -ENODEV;

	fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) ||
	    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, discovery->interface,
		       strlen(discovery->interface) + 1U))
		goto socket_error;

	if (bind(fd, (struct sockaddr *)&bind_address, sizeof(bind_address)))
		goto socket_error;

	if (inet_pton(AF_INET, WMCS_DISCOVERY_GROUP, &membership.imr_multiaddr) != 1) {
		errno = EINVAL;
		goto socket_error;
	}
	membership.imr_ifindex = (int)interface_index;

	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
		       sizeof(membership)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &membership,
		       sizeof(membership)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)))
		goto socket_error;

	discovery->socket_event.fd = fd;
	discovery->socket_event.cb = receive_messages;
	if (uloop_fd_add(&discovery->socket_event, ULOOP_READ)) {
		int error = errno ? -errno : -EIO;

		discovery->socket_event.fd = -1;
		close(fd);
		return error;
	}
	discovery->fd = fd;

	return 0;

socket_error:
	{
		int error = -errno;
		close(fd);
		return error;
	}
}

int wmcs_discovery_init(struct wmcs_discovery *discovery, const char *role,
			const char *interface)
{
	if (!discovery || !role || !interface || strlen(interface) >= IF_NAMESIZE)
		return -EINVAL;

	memset(discovery, 0, sizeof(*discovery));
	discovery->fd = -1;
	discovery->socket_event.fd = -1;
	discovery->role = role;
	strcpy(discovery->interface, interface);
	discovery->deadline.cb = deadline_expired;
	discovery->probe_timer.cb = probe_timer_expired;

	if (!fill_random(discovery->instance_id, sizeof(discovery->instance_id)))
		return -EIO;

	return 0;
}

void wmcs_discovery_stop(struct wmcs_discovery *discovery)
{
	if (!discovery)
		return;

	uloop_timeout_cancel(&discovery->deadline);
	uloop_timeout_cancel(&discovery->probe_timer);

	if (discovery->fd >= 0) {
		uloop_fd_delete(&discovery->socket_event);
		close(discovery->fd);
		discovery->fd = -1;
		discovery->socket_event.fd = -1;
	}

	discovery->active = false;
}

void wmcs_discovery_close(struct wmcs_discovery *discovery)
{
	wmcs_discovery_stop(discovery);
	if (discovery) {
		memset(discovery->instance_id, 0, sizeof(discovery->instance_id));
		discovery->node_count = 0;
	}
}

int wmcs_discovery_start(struct wmcs_discovery *discovery, uint32_t duration_seconds)
{
	int result;

	if (!discovery || duration_seconds < WMCS_DISCOVERY_MIN_SECONDS ||
	    duration_seconds > WMCS_DISCOVERY_MAX_SECONDS)
		return -EINVAL;
	if (wire_role(discovery) == 0)
		return -EOPNOTSUPP;
	if (discovery->active)
		return -EALREADY;

	result = open_socket(discovery);
	if (result)
		return result;

	if (!fill_random(&discovery->nonce, sizeof(discovery->nonce))) {
		wmcs_discovery_stop(discovery);
		return -EIO;
	}

	/* A new window must never inherit candidates from an earlier window. */
	memset(discovery->nodes, 0, sizeof(discovery->nodes));
	discovery->node_count = 0;
	discovery->active = true;
	uloop_timeout_set(&discovery->deadline, (int)(duration_seconds * 1000U));

	if (!strcmp(discovery->role, "controller")) {
		(void)send_probe(discovery);
		uloop_timeout_set(&discovery->probe_timer,
				  WMCS_DISCOVERY_PROBE_INTERVAL_MS);
	}

	return 0;
}

bool wmcs_discovery_active(const struct wmcs_discovery *discovery)
{
	return discovery && discovery->active;
}

const char *wmcs_discovery_interface(const struct wmcs_discovery *discovery)
{
	return discovery ? discovery->interface : "";
}

const struct wmcs_discovery_node *wmcs_discovery_nodes(
	struct wmcs_discovery *discovery, size_t *count)
{
	uint64_t now = wmcs_monotonic_ms();

	if (!discovery || !count)
		return NULL;

	prune_nodes(discovery, now);
	*count = discovery->node_count;
	return discovery->nodes;
}

bool wmcs_discovery_node_id(const struct wmcs_discovery_node *node,
			    char output[WMCS_DISCOVERY_NODE_ID_SIZE])
{
	size_t byte;

	if (!node || !output)
		return false;
	memcpy(output, "ephemeral-", sizeof("ephemeral-") - 1U);
	for (byte = 0; byte < WMCS_DISCOVERY_INSTANCE_SIZE; byte++)
		snprintf(&output[sizeof("ephemeral-") - 1U + byte * 2U], 3,
			 "%02x", node->instance_id[byte]);
	return true;
}

bool wmcs_discovery_find_node(struct wmcs_discovery *discovery,
			      const char *candidate_id, char address[16])
{
	const struct wmcs_discovery_node *nodes;
	size_t count = 0;
	size_t i;

	if (!discovery || !candidate_id || !address ||
	    strlen(candidate_id) != WMCS_DISCOVERY_NODE_ID_SIZE - 1U)
		return false;
	nodes = wmcs_discovery_nodes(discovery, &count);
	for (i = 0; nodes && i < count; i++) {
		char identifier[WMCS_DISCOVERY_NODE_ID_SIZE];

		if (!wmcs_discovery_node_id(&nodes[i], identifier))
			continue;
		if (!strcmp(candidate_id, identifier)) {
			strcpy(address, nodes[i].address);
			return true;
		}
	}
	return false;
}
