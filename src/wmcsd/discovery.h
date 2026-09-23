// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_DISCOVERY_H
#define WMCS_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <net/if.h>

#include <libubox/uloop.h>

#include "discovery_wire.h"

#define WMCS_DISCOVERY_MAX_NODES 16U
#define WMCS_DISCOVERY_MIN_SECONDS 5U
#define WMCS_DISCOVERY_MAX_SECONDS 300U
#define WMCS_DISCOVERY_NODE_ID_SIZE 43U

struct wmcs_discovery_node {
	uint8_t instance_id[WMCS_DISCOVERY_INSTANCE_SIZE];
	char address[16];
	uint64_t last_seen_ms;
};

struct wmcs_discovery {
	const char *role;
	char interface[IF_NAMESIZE];
	uint8_t instance_id[WMCS_DISCOVERY_INSTANCE_SIZE];
	uint64_t nonce;
	int fd;
	bool active;
	struct uloop_fd socket_event;
	struct uloop_timeout deadline;
	struct uloop_timeout probe_timer;
	struct wmcs_discovery_node nodes[WMCS_DISCOVERY_MAX_NODES];
	size_t node_count;
};

int wmcs_discovery_init(struct wmcs_discovery *discovery, const char *role,
			const char *interface);
void wmcs_discovery_close(struct wmcs_discovery *discovery);
int wmcs_discovery_start(struct wmcs_discovery *discovery, uint32_t duration_seconds);
void wmcs_discovery_stop(struct wmcs_discovery *discovery);
bool wmcs_discovery_active(const struct wmcs_discovery *discovery);
const char *wmcs_discovery_interface(const struct wmcs_discovery *discovery);
const struct wmcs_discovery_node *wmcs_discovery_nodes(
	struct wmcs_discovery *discovery, size_t *count);
bool wmcs_discovery_node_id(const struct wmcs_discovery_node *node,
			    char output[WMCS_DISCOVERY_NODE_ID_SIZE]);
bool wmcs_discovery_find_node(struct wmcs_discovery *discovery,
			      const char *candidate_id, char address[16]);
uint64_t wmcs_monotonic_ms(void);

#endif
