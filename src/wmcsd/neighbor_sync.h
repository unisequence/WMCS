// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_NEIGHBOR_SYNC_H
#define WMCS_NEIGHBOR_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <net/if.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "identity.h"
#include "neighbor_sync_wire.h"

#define WMCS_NR_OBJECT_SIZE (IF_NAMESIZE + 16U)

struct wmcs_neighbor_sync_peer {
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	struct wmcs_nr_record record;
	uint8_t last_challenge[WMCS_NR_CHALLENGE_SIZE];
	uint64_t seen_ms;
};

struct wmcs_neighbor_sync {
	const char *role;
	const char *interface;
	const char *source_iface;
	const char *source_radio;
	struct wmcs_identity *identity;
	struct ubus_context *ubus;
	struct uloop_fd socket_event;
	struct uloop_timeout timer;
	int fd;
	bool enabled;
	bool initialized;
	bool ready;
	bool hostapd_ready;
	bool own_valid;
	bool applied_valid;
	bool store_loaded;
	char reason[48];
	char object[WMCS_NR_OBJECT_SIZE];
	uint32_t object_id;
	uint8_t local_id[16];
	uint8_t challenge[WMCS_NR_CHALLENGE_SIZE];
	uint64_t challenge_ms;
	uint64_t next_query_ms;
	uint64_t own_seen_ms;
	struct wmcs_nr_record own;
	struct wmcs_neighbor_sync_peer peers[WMCS_IDENTITY_MAX_PEERS];
	struct wmcs_nr_record applied[WMCS_IDENTITY_MAX_PEERS];
	size_t applied_count;
	struct wmcs_nr_record trusted[WMCS_IDENTITY_MAX_PEERS];
	size_t trusted_count;
	size_t fresh_peer_count;
	uint64_t queries_sent;
	uint64_t replies_accepted;
	uint64_t apply_failures;
};

int wmcs_neighbor_sync_init(struct wmcs_neighbor_sync *sync,
			    const char *role, const char *interface,
			    const char *source_iface, const char *source_radio,
			    struct wmcs_identity *identity,
			    struct ubus_context *ubus, bool enabled);
void wmcs_neighbor_sync_start(struct wmcs_neighbor_sync *sync);
void wmcs_neighbor_sync_ubus_disconnected(struct wmcs_neighbor_sync *sync);
void wmcs_neighbor_sync_close(struct wmcs_neighbor_sync *sync);
bool wmcs_neighbor_sync_contains(const struct wmcs_neighbor_sync *sync,
				 const char *report_hex);
void wmcs_neighbor_sync_invalidate(struct wmcs_neighbor_sync *sync);

#endif
