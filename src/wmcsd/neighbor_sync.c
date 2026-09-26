// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libubox/blobmsg.h>
#include <libubox/utils.h>

#include "control_crypto.h"
#include "discovery.h"
#include "neighbor_sync.h"
#include "neighbor_sync_store.h"
#include "wlan.h"

#define WMCS_NR_GROUP "239.255.77.68"
#define WMCS_NR_PORT 45126U
#define WMCS_NR_INTERVAL_MS 10000U
#define WMCS_NR_REPLY_WINDOW_MS 15000U
#define WMCS_NR_PEER_TTL_MS 30000U
#define WMCS_NR_OWN_TTL_MS 15000U
#define WMCS_NR_PACKET_BUDGET 64U

struct nr_status_result {
	char ssid[WMCS_NR_SSID_MAX + 1U];
	char bssid[18];
	bool enabled;
	bool seen;
};

struct nr_record_result {
	struct wmcs_nr_record record;
	bool seen;
	bool valid;
};

struct nr_list_result {
	struct wmcs_nr_record records[WMCS_NR_SET_MAX];
	size_t count;
	bool seen;
	bool valid;
};

static void set_reason(struct wmcs_neighbor_sync *sync, const char *reason)
{
	snprintf(sync->reason, sizeof(sync->reason), "%s", reason);
}

static bool all_zero(const uint8_t *input, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (input[i])
			return false;
	}
	return true;
}

static int hex_nibble(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

static bool decode_hex(const char *input, uint8_t *output, size_t *size)
{
	size_t length;
	size_t i;

	if (!input || !output || !size)
		return false;
	length = strlen(input);
	if (length < 26U || length > WMCS_NR_REPORT_MAX * 2U ||
	    (length & 1U))
		return false;
	for (i = 0; i < length / 2U; i++) {
		int high = hex_nibble(input[i * 2U]);
		int low = hex_nibble(input[i * 2U + 1U]);

		if (high < 0 || low < 0)
			return false;
		output[i] = (uint8_t)((high << 4) | low);
	}
	*size = length / 2U;
	return true;
}

static void format_hex(const uint8_t *input, size_t size,
		       char output[WMCS_NR_REPORT_MAX * 2U + 1U])
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < size; i++) {
		output[i * 2U] = digits[input[i] >> 4U];
		output[i * 2U + 1U] = digits[input[i] & 15U];
	}
	output[size * 2U] = '\0';
}

static void format_bssid(const struct wmcs_nr_record *record,
			 char output[18])
{
	snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		 (unsigned int)record->report[0],
		 (unsigned int)record->report[1],
		 (unsigned int)record->report[2],
		 (unsigned int)record->report[3],
		 (unsigned int)record->report[4],
		 (unsigned int)record->report[5]);
}

static bool parse_nr_triplet(struct blob_attr *array,
			     struct wmcs_nr_record *record)
{
	struct blob_attr *field;
	struct wmcs_nr_record parsed = {0};
	const char *bssid = NULL;
	const char *ssid = NULL;
	const char *hex = NULL;
	char report_bssid[18];
	size_t remaining;
	size_t fields = 0;

	if (!array || blobmsg_type(array) != BLOBMSG_TYPE_ARRAY || !record)
		return false;
	blobmsg_for_each_attr(field, array, remaining) {
		if (blobmsg_type(field) != BLOBMSG_TYPE_STRING)
			return false;
		if (fields == 0U)
			bssid = blobmsg_get_string(field);
		else if (fields == 1U)
			ssid = blobmsg_get_string(field);
		else if (fields == 2U)
			hex = blobmsg_get_string(field);
		else
			return false;
		fields++;
	}
	if (fields != 3U || !bssid || strlen(bssid) != 17U || !ssid ||
	    !*ssid || strlen(ssid) > WMCS_NR_SSID_MAX ||
	    !decode_hex(hex, parsed.report, &parsed.report_size))
		return false;
	strcpy(parsed.ssid, ssid);
	if (!wmcs_nr_record_valid(&parsed))
		return false;
	format_bssid(&parsed, report_bssid);
	if (strcasecmp(bssid, report_bssid))
		return false;
	*record = parsed;
	return true;
}

static void status_callback(struct ubus_request *request, int type,
			    struct blob_attr *message)
{
	struct nr_status_result *status = request->priv;
	struct blob_attr *field;
	const char *ssid = NULL;
	const char *bssid = NULL;
	const char *state = NULL;
	size_t remaining;

	(void)type;
	if (!message)
		return;
	status->seen = true;
	blobmsg_for_each_attr(field, message, remaining) {
		if (blobmsg_type(field) != BLOBMSG_TYPE_STRING)
			continue;
		if (!strcmp(blobmsg_name(field), "ssid"))
			ssid = blobmsg_get_string(field);
		else if (!strcmp(blobmsg_name(field), "bssid"))
			bssid = blobmsg_get_string(field);
		else if (!strcmp(blobmsg_name(field), "status"))
			state = blobmsg_get_string(field);
	}
	if (!ssid || !*ssid || strlen(ssid) > WMCS_NR_SSID_MAX ||
	    !bssid || strlen(bssid) != 17U || !state ||
	    strcmp(state, "ENABLED"))
		return;
	strcpy(status->ssid, ssid);
	strcpy(status->bssid, bssid);
	status->enabled = true;
}

static void own_callback(struct ubus_request *request, int type,
			 struct blob_attr *message)
{
	struct nr_record_result *result = request->priv;
	struct blob_attr *field;
	size_t remaining;

	(void)type;
	if (!message)
		return;
	result->seen = true;
	blobmsg_for_each_attr(field, message, remaining) {
		if (!strcmp(blobmsg_name(field), "value")) {
			result->valid = parse_nr_triplet(field, &result->record);
			return;
		}
	}
}

static void list_callback(struct ubus_request *request, int type,
			  struct blob_attr *message)
{
	struct nr_list_result *result = request->priv;
	struct blob_attr *field;
	size_t remaining;

	(void)type;
	if (!message)
		return;
	result->seen = true;
	blobmsg_for_each_attr(field, message, remaining) {
		struct blob_attr *entry;
		size_t entry_remaining;

		if (strcmp(blobmsg_name(field), "list") ||
		    blobmsg_type(field) != BLOBMSG_TYPE_ARRAY)
			continue;
		result->valid = true;
		blobmsg_for_each_attr(entry, field, entry_remaining) {
			if (result->count >= WMCS_NR_SET_MAX ||
			    !parse_nr_triplet(entry,
					      &result->records[result->count])) {
				result->valid = false;
				return;
			}
			result->count++;
		}
		return;
	}
}

static int call_hostapd(struct wmcs_neighbor_sync *sync, const char *method,
			struct blob_attr *payload, ubus_data_handler_t callback,
			void *private)
{
	uint32_t id;
	int result;

	if (ubus_lookup_id(sync->ubus, sync->object, &id))
		return -ENOENT;
	result = ubus_invoke(sync->ubus, id, method, payload, callback,
			     private, 3000);
	return result ? -EIO : 0;
}

static int call_noarg(struct wmcs_neighbor_sync *sync, const char *method,
		      ubus_data_handler_t callback, void *private)
{
	struct blob_buf request = {0};
	int result;

	blob_buf_init(&request, 0);
	result = call_hostapd(sync, method, request.head, callback, private);
	blob_buf_free(&request);
	return result;
}

static int read_list(struct wmcs_neighbor_sync *sync,
		     struct nr_list_result *list)
{
	memset(list, 0, sizeof(*list));
	if (call_noarg(sync, "rrm_nr_list", list_callback, list) ||
	    !list->seen || !list->valid)
		return -EIO;
	return 0;
}

static int set_list(struct wmcs_neighbor_sync *sync,
		    const struct wmcs_nr_record *records, size_t count)
{
	struct blob_buf request = {0};
	void *list;
	size_t i;
	int result;

	if (count > WMCS_NR_SET_MAX || (count && !records))
		return -EINVAL;
	blob_buf_init(&request, 0);
	list = blobmsg_open_array(&request, "list");
	for (i = 0; i < count; i++) {
		char hex[WMCS_NR_REPORT_MAX * 2U + 1U];
		char bssid[18];
		void *entry;

		if (!wmcs_nr_record_valid(&records[i])) {
			blob_buf_free(&request);
			return -EINVAL;
		}
		format_hex(records[i].report, records[i].report_size, hex);
		format_bssid(&records[i], bssid);
		entry = blobmsg_open_array(&request, NULL);
		blobmsg_add_string(&request, NULL, bssid);
		blobmsg_add_string(&request, NULL, records[i].ssid);
		blobmsg_add_string(&request, NULL, hex);
		blobmsg_close_array(&request, entry);
	}
	blobmsg_close_array(&request, list);
	result = call_hostapd(sync, "rrm_nr_set", request.head, NULL, NULL);
	blob_buf_free(&request);
	return result;
}

static int enable_capabilities(struct wmcs_neighbor_sync *sync)
{
	struct blob_buf request = {0};
	int result;

	blob_buf_init(&request, 0);
	blobmsg_add_u8(&request, "neighbor_report", true);
	blobmsg_add_u8(&request, "beacon_report", true);
	blobmsg_add_u8(&request, "link_measurement", true);
	blobmsg_add_u8(&request, "bss_transition", true);
	result = call_hostapd(sync, "bss_mgmt_enable", request.head, NULL,
			      NULL);
	blob_buf_free(&request);
	return result;
}

static bool expected_peer_role(const struct wmcs_neighbor_sync *sync,
			       enum wmcs_peer_role role)
{
	return (!strcmp(sync->role, "controller") &&
		role == WMCS_PEER_ROLE_AGENT) ||
	       (!strcmp(sync->role, "agent") &&
		role == WMCS_PEER_ROLE_CONTROLLER);
}

static struct wmcs_neighbor_sync_peer *peer_slot(
	struct wmcs_neighbor_sync *sync, const char *peer_id)
{
	struct wmcs_neighbor_sync_peer *free_slot = NULL;
	size_t i;

	for (i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++) {
		if (!strcmp(sync->peers[i].peer_id, peer_id))
			return &sync->peers[i];
		if (!sync->peers[i].peer_id[0] && !free_slot)
			free_slot = &sync->peers[i];
	}
	if (!free_slot)
		return NULL;
	snprintf(free_slot->peer_id, sizeof(free_slot->peer_id), "%s", peer_id);
	return free_slot;
}

static bool peer_in_active_list(const struct wmcs_peer_info *peers,
				size_t count, const char *peer_id,
				const struct wmcs_neighbor_sync *sync)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (!strcmp(peers[i].peer_id, peer_id) &&
		    peers[i].state == WMCS_PEER_STATE_ACTIVE &&
		    expected_peer_role(sync, peers[i].role))
			return true;
	}
	return false;
}

static void prune_peer_slots(struct wmcs_neighbor_sync *sync,
			     const struct wmcs_peer_info *peers, size_t count,
			     uint64_t now)
{
	size_t i;

	for (i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++) {
		struct wmcs_neighbor_sync_peer *slot = &sync->peers[i];

		if (!slot->peer_id[0])
			continue;
		if (!peer_in_active_list(peers, count, slot->peer_id, sync)) {
			memset(slot, 0, sizeof(*slot));
			continue;
		}
		if (slot->seen_ms &&
		    (now < slot->seen_ms ||
		     now - slot->seen_ms > WMCS_NR_PEER_TTL_MS)) {
			memset(&slot->record, 0, sizeof(slot->record));
			slot->seen_ms = 0;
		}
	}
}

static bool local_ownership_ok(struct wmcs_neighbor_sync *sync,
			       const struct wmcs_peer_info *peers,
			       size_t count)
{
	size_t i;

	if (strcmp(sync->role, "agent"))
		return true;
	for (i = 0; i < count; i++) {
		bool managed_present = false;

		if (peers[i].state != WMCS_PEER_STATE_ACTIVE ||
		    peers[i].role != WMCS_PEER_ROLE_CONTROLLER)
			continue;
		if (!wmcs_wlan_check_release(sync->source_radio,
					     peers[i].peer_id,
					     &managed_present) && managed_present)
			return true;
	}
	return false;
}

static int refresh_local_ap(struct wmcs_neighbor_sync *sync)
{
	struct nr_status_result status = {0};
	struct nr_record_result own = {0};
	const char *section = strcmp(sync->role, "agent") ?
			      sync->source_iface : WMCS_WLAN_SECTION;
	char object[WMCS_NR_OBJECT_SIZE];
	char own_bssid[18];
	uint32_t id;
	bool changed;

	if (wmcs_wlan_resolve_hostapd(sync->ubus, sync->source_radio, section,
				      object, sizeof(object)) ||
	    ubus_lookup_id(sync->ubus, object, &id))
		return -ENOENT;
	changed = !sync->object_id || sync->object_id != id ||
		  strcmp(sync->object, object);
	if (changed) {
		sync->object_id = id;
		strcpy(sync->object, object);
		sync->applied_valid = false;
		sync->store_loaded = false;
		sync->applied_count = 0;
		memset(sync->applied, 0, sizeof(sync->applied));
		for (size_t i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++)
			sync->peers[i].seen_ms = 0;
	}
	if (call_noarg(sync, "get_status", status_callback, &status) ||
	    !status.seen || !status.enabled ||
	    enable_capabilities(sync) ||
	    call_noarg(sync, "rrm_nr_get_own", own_callback, &own) ||
	    !own.seen || !own.valid)
		return -EIO;
	format_bssid(&own.record, own_bssid);
	if (strcmp(own.record.ssid, status.ssid) ||
	    strcasecmp(own_bssid, status.bssid))
		return -EKEYREJECTED;
	if (!sync->store_loaded) {
		size_t stored_count = 0;
		int stored = wmcs_nr_store_load(sync->identity, &own.record,
						 sync->applied,
						 &stored_count);

		if (!stored) {
			sync->applied_count = stored_count;
			sync->applied_valid = true;
		} else if (stored != -ENOENT && stored != -ESTALE) {
			return stored;
		}
		sync->store_loaded = true;
	}
	if (sync->own_valid &&
	    !wmcs_nr_record_equal(&sync->own, &own.record)) {
		/* A local AP identity/channel change invalidates old peer reports. */
		for (size_t i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++)
			sync->peers[i].seen_ms = 0;
	}
	sync->own = own.record;
	sync->own_seen_ms = wmcs_monotonic_ms();
	sync->own_valid = true;
	sync->hostapd_ready = true;
	return 0;
}

static size_t desired_records(struct wmcs_neighbor_sync *sync,
			      struct wmcs_nr_record output[WMCS_NR_SET_MAX],
			      uint64_t now)
{
	size_t count = 0;
	size_t i;

	for (i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++) {
		struct wmcs_neighbor_sync_peer *peer = &sync->peers[i];

		if (!peer->seen_ms || now < peer->seen_ms ||
		    now - peer->seen_ms > WMCS_NR_PEER_TTL_MS ||
		    strcmp(peer->record.ssid, sync->own.ssid) ||
		    !memcmp(peer->record.report, sync->own.report, 6U) ||
		    !wmcs_nr_record_valid(&peer->record))
			continue;
		output[count++] = peer->record;
	}
	return count;
}

static int reconcile_list(struct wmcs_neighbor_sync *sync, uint64_t now)
{
	struct wmcs_nr_record desired[WMCS_NR_SET_MAX] = {0};
	struct nr_list_result before;
	struct nr_list_result after;
	enum wmcs_nr_reconcile_decision decision;
	size_t desired_count = desired_records(sync, desired, now);
	size_t i;
	size_t j;
	int result;

	sync->fresh_peer_count = desired_count;
	sync->ready = false;
	sync->trusted_count = 0;
	for (i = 0; i < desired_count; i++) {
		for (j = 0; j < i; j++) {
			if (!memcmp(desired[i].report, desired[j].report, 6U)) {
				set_reason(sync, "duplicate_neighbor_bssid");
				return -EKEYREJECTED;
			}
		}
	}
	if (read_list(sync, &before)) {
		set_reason(sync, "neighbor_observation_failed");
		return -EIO;
	}
	decision = wmcs_nr_reconcile_decide(before.records, before.count,
					    sync->applied, sync->applied_count,
					    sync->applied_valid, desired,
					    desired_count);
	if (decision == WMCS_NR_CONFLICT) {
		set_reason(sync, "foreign_neighbor_conflict");
		return -EEXIST;
	}
	if (decision == WMCS_NR_APPLY) {
		result = set_list(sync, desired, desired_count);
		if (!result)
			result = read_list(sync, &after);
		if (!result && !wmcs_nr_set_equal(after.records, after.count,
						 desired, desired_count))
			result = -EIO;
		if (!result)
			result = wmcs_nr_store_save(sync->identity, &sync->own,
						    desired, desired_count);
		if (result) {
			/* rrm_nr_set clears first and can fail after a partial write.
			 * Restore only if the observed list is a subset of our own
			 * attempted write; a concurrent foreign change is untouchable. */
			if (read_list(sync, &after) ||
			    !wmcs_nr_set_subset(after.records, after.count,
					   desired, desired_count)) {
				set_reason(sync, "neighbor_rollback_conflict");
			} else if (set_list(sync, before.records, before.count) ||
				   read_list(sync, &after) ||
				   !wmcs_nr_set_equal(after.records, after.count,
						      before.records,
						      before.count)) {
				set_reason(sync, "neighbor_rollback_failed");
			} else {
				set_reason(sync, "neighbor_apply_failed");
			}
			sync->apply_failures++;
			return result;
		}
		memcpy(sync->applied, desired,
		       desired_count * sizeof(desired[0]));
		sync->applied_count = desired_count;
		sync->applied_valid = true;
	}
	sync->ready = desired_count > 0U;
	if (sync->ready) {
		memcpy(sync->trusted, desired,
		       desired_count * sizeof(desired[0]));
		sync->trusted_count = desired_count;
	}
	set_reason(sync, sync->ready ? "ready" : "no_fresh_peer");
	return 0;
}

static int open_socket(struct wmcs_neighbor_sync *sync)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_NR_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	struct ip_mreqn membership = {0};
	unsigned char ttl = 1;
	unsigned char loop = 0;
	int enabled = 1;
	int index;
	int fd;

	index = (int)if_nametoindex(sync->interface);
	if (!index)
		return -ENODEV;
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
		       sizeof(enabled)) ||
	    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, sync->interface,
		       strlen(sync->interface) + 1U) ||
	    bind(fd, (struct sockaddr *)&address, sizeof(address)) ||
	    inet_pton(AF_INET, WMCS_NR_GROUP,
		      &membership.imr_multiaddr) != 1)
		goto error;
	membership.imr_ifindex = index;
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       &membership, sizeof(membership)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
		       &membership, sizeof(membership)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)))
		goto error;
	sync->socket_event.fd = fd;
	if (uloop_fd_add(&sync->socket_event, ULOOP_READ))
		goto error;
	sync->fd = fd;
	return 0;

error:
	{
		int saved = errno ? -errno : -EIO;

		close(fd);
		sync->socket_event.fd = -1;
		return saved;
	}
}

static void close_socket(struct wmcs_neighbor_sync *sync)
{
	if (sync->fd < 0)
		return;
	uloop_fd_delete(&sync->socket_event);
	close(sync->fd);
	sync->fd = -1;
	sync->socket_event.fd = -1;
}

static int send_packet(struct wmcs_neighbor_sync *sync,
		       const struct wmcs_control_packet *packet,
		       const struct sockaddr_in *destination)
{
	uint8_t wire[WMCS_CONTROL_WIRE_SIZE];
	ssize_t sent;

	if (!wmcs_control_encode(wire, packet))
		return -EINVAL;
	sent = sendto(sync->fd, wire, sizeof(wire), 0,
		      (const struct sockaddr *)destination,
		      sizeof(*destination));
	wmcs_secure_zero(wire, sizeof(wire));
	if (sent < 0)
		return -errno;
	return (size_t)sent == WMCS_CONTROL_WIRE_SIZE ? 0 : -EIO;
}

static int make_packet(struct wmcs_neighbor_sync *sync,
		       const struct wmcs_peer_record *peer,
		       const char *peer_id, uint8_t type,
		       const uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE],
		       struct wmcs_control_packet *packet)
{
	memset(packet, 0, sizeof(*packet));
	packet->type = type;
	packet->sequence = wmcs_identity_generation(sync->identity);
	if (!packet->sequence)
		return -EINVAL;
	memcpy(packet->sender_id, sync->local_id,
	       sizeof(packet->sender_id));
	if (!wmcs_control_id_from_hex(peer_id, packet->recipient_id))
		return -EINVAL;
	return wmcs_control_seal(packet, peer->relationship_key, plaintext);
}

static int send_reply(struct wmcs_neighbor_sync *sync,
		      const struct wmcs_peer_record *peer,
		      const char *peer_id,
		      const uint8_t challenge[WMCS_NR_CHALLENGE_SIZE],
		      const struct sockaddr_in *destination)
{
	struct wmcs_control_packet packet;
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	uint64_t now = wmcs_monotonic_ms();
	int result;

	if (!sync->own_valid || !sync->hostapd_ready ||
	    now < sync->own_seen_ms ||
	    now - sync->own_seen_ms > WMCS_NR_OWN_TTL_MS ||
	    !wmcs_nr_reply_encode(plaintext, challenge, &sync->own))
		return -ENOENT;
	result = make_packet(sync, peer, peer_id, WMCS_CONTROL_NR_REPLY,
			     plaintext, &packet);
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	if (!result)
		result = send_packet(sync, &packet, destination);
	wmcs_secure_zero(&packet, sizeof(packet));
	return result;
}

static int send_queries(struct wmcs_neighbor_sync *sync,
			const struct wmcs_peer_info *peers, size_t count,
			uint64_t now)
{
	struct sockaddr_in destination = {
		.sin_family = AF_INET,
		.sin_port = htons(WMCS_NR_PORT),
	};
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE];
	size_t i;
	int result = 0;

	if (inet_pton(AF_INET, WMCS_NR_GROUP, &destination.sin_addr) != 1 ||
	    psa_generate_random(sync->challenge,
				sizeof(sync->challenge)) != PSA_SUCCESS ||
	    all_zero(sync->challenge, sizeof(sync->challenge)) ||
	    !wmcs_nr_query_encode(plaintext, sync->challenge))
		return -EIO;
	sync->challenge_ms = now;
	for (i = 0; i < count; i++) {
		struct wmcs_peer_record peer = {0};
		struct wmcs_control_packet packet;
		int status;

		if (peers[i].state != WMCS_PEER_STATE_ACTIVE ||
		    !expected_peer_role(sync, peers[i].role))
			continue;
		status = wmcs_identity_load_peer(sync->identity,
					 peers[i].peer_id, &peer);
		if (!status && peer.state == WMCS_PEER_STATE_ACTIVE &&
		    expected_peer_role(sync, peer.role)) {
			status = make_packet(sync, &peer, peers[i].peer_id,
					     WMCS_CONTROL_NR_QUERY,
					     plaintext, &packet);
			if (!status)
				status = send_packet(sync, &packet,
						     &destination);
			wmcs_secure_zero(&packet, sizeof(packet));
			if (!status)
				sync->queries_sent++;
		}
		wmcs_secure_zero(&peer, sizeof(peer));
		if (status)
			result = status;
	}
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	return result;
}

static void handle_packet(struct wmcs_neighbor_sync *sync,
			  const uint8_t *wire, size_t size,
			  const struct sockaddr_in *source)
{
	struct wmcs_control_packet packet;
	struct wmcs_peer_record peer = {0};
	struct wmcs_nr_record record = {0};
	struct wmcs_neighbor_sync_peer *slot;
	uint8_t plaintext[WMCS_CONTROL_PAYLOAD_SIZE] = {0};
	uint8_t challenge[WMCS_NR_CHALLENGE_SIZE] = {0};
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	uint64_t now = wmcs_monotonic_ms();

	if (!wmcs_control_decode(&packet, wire, size) ||
	    (packet.type != WMCS_CONTROL_NR_QUERY &&
	     packet.type != WMCS_CONTROL_NR_REPLY) ||
	    memcmp(packet.recipient_id, sync->local_id,
		   sizeof(packet.recipient_id)))
		return;
	wmcs_control_id_to_hex(packet.sender_id, peer_id);
	if (wmcs_identity_load_peer(sync->identity, peer_id, &peer) ||
	    peer.state != WMCS_PEER_STATE_ACTIVE ||
	    !expected_peer_role(sync, peer.role) ||
	    wmcs_control_open(&packet, peer.relationship_key, plaintext))
		goto out;
	if (packet.type == WMCS_CONTROL_NR_QUERY) {
		if (wmcs_nr_query_decode(challenge, plaintext))
			(void)send_reply(sync, &peer, peer_id, challenge, source);
		goto out;
	}
	if (!wmcs_nr_reply_decode(challenge, &record, plaintext) ||
	    !sync->challenge_ms || now < sync->challenge_ms ||
	    now - sync->challenge_ms > WMCS_NR_REPLY_WINDOW_MS ||
	    memcmp(challenge, sync->challenge, sizeof(challenge)) ||
	    !sync->own_valid || strcmp(record.ssid, sync->own.ssid) ||
	    !memcmp(record.report, sync->own.report, 6U))
		goto out;
	slot = peer_slot(sync, peer_id);
	if (!slot || !memcmp(slot->last_challenge, challenge,
			     sizeof(challenge)))
		goto out;
	slot->record = record;
	memcpy(slot->last_challenge, challenge, sizeof(challenge));
	slot->seen_ms = now;
	sync->replies_accepted++;
	uloop_timeout_set(&sync->timer, 0);

out:
	wmcs_secure_zero(&peer, sizeof(peer));
	wmcs_secure_zero(plaintext, sizeof(plaintext));
	wmcs_secure_zero(&packet, sizeof(packet));
}

static void receive_packets(struct uloop_fd *event, unsigned int events)
{
	struct wmcs_neighbor_sync *sync = container_of(
		event, struct wmcs_neighbor_sync, socket_event);
	uint8_t wire[WMCS_CONTROL_WIRE_SIZE + 1U];
	unsigned int processed = 0;

	(void)events;
	for (;;) {
		struct sockaddr_in source;
		socklen_t source_size = sizeof(source);
		ssize_t received;

		if (processed++ >= WMCS_NR_PACKET_BUDGET)
			return;
		received = recvfrom(sync->fd, wire, sizeof(wire), 0,
				    (struct sockaddr *)&source, &source_size);
		if (received < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		if (source_size != sizeof(source) ||
		    source.sin_family != AF_INET ||
		    source.sin_port != htons(WMCS_NR_PORT) ||
		    !wmcs_identity_ready(sync->identity))
			continue;
		handle_packet(sync, wire, (size_t)received, &source);
	}
}

static void timer_expired(struct uloop_timeout *timeout)
{
	struct wmcs_neighbor_sync *sync = container_of(
		timeout, struct wmcs_neighbor_sync, timer);
	struct wmcs_peer_info peers[WMCS_IDENTITY_MAX_PEERS] = {0};
	size_t count = 0;
	uint64_t now;
	uint64_t delay;
	int result;

	sync->ready = false;
	sync->hostapd_ready = false;
	sync->fresh_peer_count = 0;
	sync->trusted_count = 0;
	if (!wmcs_identity_ready(sync->identity) ||
	    wmcs_control_local_id(sync->identity, sync->local_id) ||
	    wmcs_identity_list_peers(sync->identity, peers,
				 WMCS_IDENTITY_MAX_PEERS, &count)) {
		set_reason(sync, "identity_unavailable");
		goto again;
	}
	if (sync->fd < 0 && open_socket(sync)) {
		set_reason(sync, "socket_unavailable");
		goto again;
	}
	now = wmcs_monotonic_ms();
	prune_peer_slots(sync, peers, count, now);
	if (!local_ownership_ok(sync, peers, count)) {
		sync->own_valid = false;
		set_reason(sync, "owned_ap_unavailable");
		goto again;
	}
	result = refresh_local_ap(sync);
	if (result) {
		sync->own_valid = false;
		set_reason(sync, result == -EKEYREJECTED ?
			   "neighbor_state_invalid" : "hostapd_unavailable");
		goto again;
	}
	if (reconcile_list(sync, now))
		sync->ready = false;
	if (!sync->next_query_ms || now >= sync->next_query_ms) {
		if (send_queries(sync, peers, count, now) && sync->ready) {
			sync->ready = false;
			set_reason(sync, "peer_query_failed");
		}
		sync->next_query_ms = now + WMCS_NR_INTERVAL_MS;
	}

again:
	now = wmcs_monotonic_ms();
	delay = sync->next_query_ms > now ? sync->next_query_ms - now :
		WMCS_NR_INTERVAL_MS;
	if (delay > WMCS_NR_INTERVAL_MS)
		delay = WMCS_NR_INTERVAL_MS;
	uloop_timeout_set(&sync->timer, (int)delay);
}

int wmcs_neighbor_sync_init(struct wmcs_neighbor_sync *sync,
			    const char *role, const char *interface,
			    const char *source_iface, const char *source_radio,
			    struct wmcs_identity *identity,
			    struct ubus_context *ubus, bool enabled)
{
	if (!sync || !role || !interface || !source_iface || !source_radio ||
	    !identity || !ubus || (strcmp(role, "controller") &&
				  strcmp(role, "agent") &&
				  strcmp(role, "standalone")))
		return -EINVAL;
	memset(sync, 0, sizeof(*sync));
	sync->role = role;
	sync->interface = interface;
	sync->source_iface = source_iface;
	sync->source_radio = source_radio;
	sync->identity = identity;
	sync->ubus = ubus;
	sync->enabled = enabled;
	sync->fd = -1;
	sync->socket_event.fd = -1;
	sync->socket_event.cb = receive_packets;
	sync->timer.cb = timer_expired;
	sync->initialized = true;
	set_reason(sync, enabled ? "starting" : "disabled_by_policy");
	return 0;
}

void wmcs_neighbor_sync_start(struct wmcs_neighbor_sync *sync)
{
	if (!sync || !sync->initialized || !sync->enabled ||
	    !strcmp(sync->role, "standalone"))
		return;
	uloop_timeout_set(&sync->timer, 0);
}

void wmcs_neighbor_sync_ubus_disconnected(struct wmcs_neighbor_sync *sync)
{
	if (!sync || !sync->initialized)
		return;
	sync->ready = false;
	sync->hostapd_ready = false;
	sync->own_valid = false;
	sync->trusted_count = 0;
	sync->applied_valid = false;
	sync->store_loaded = false;
	sync->object_id = 0;
	sync->next_query_ms = 0;
	sync->fresh_peer_count = 0;
	for (size_t i = 0; i < WMCS_IDENTITY_MAX_PEERS; i++)
		sync->peers[i].seen_ms = 0;
	set_reason(sync, "ubus_unavailable");
}

void wmcs_neighbor_sync_close(struct wmcs_neighbor_sync *sync)
{
	if (!sync || !sync->initialized)
		return;
	uloop_timeout_cancel(&sync->timer);
	close_socket(sync);
	wmcs_secure_zero(sync, sizeof(*sync));
}

bool wmcs_neighbor_sync_contains(const struct wmcs_neighbor_sync *sync,
				 const char *report_hex)
{
	uint8_t report[WMCS_NR_REPORT_MAX] = {0};
	size_t report_size = 0;
	size_t i;

	if (!sync || !sync->ready ||
	    !decode_hex(report_hex, report, &report_size))
		return false;
	for (i = 0; i < sync->trusted_count; i++) {
		if (sync->trusted[i].report_size == report_size &&
		    !memcmp(sync->trusted[i].report, report, report_size))
			return true;
	}
	return false;
}

void wmcs_neighbor_sync_invalidate(struct wmcs_neighbor_sync *sync)
{
	if (!sync || !sync->initialized || !sync->enabled)
		return;
	sync->ready = false;
	sync->trusted_count = 0;
	set_reason(sync, "neighbor_list_changed");
	uloop_timeout_set(&sync->timer, 0);
}
