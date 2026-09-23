// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libubox/blobmsg.h>
#include <libubox/utils.h>

#include "discovery.h"
#include "roaming.h"

#define WMCS_ROAMING_POLL_MS 1000U
#define WMCS_ROAMING_CONFIRM_SAMPLES 5U
#define WMCS_ROAMING_MIN_DWELL_MS 20000U
#define WMCS_ROAMING_VALIDITY_PERIOD 30U
#define WMCS_ROAMING_MAX_CLIENTS 32U
#define WMCS_ROAMING_NEIGHBOR_MAX 512U

static void set_reason(struct wmcs_roaming *roaming, const char *reason)
{
	if (!reason)
		reason = "unknown";
	snprintf(roaming->last_reason, sizeof(roaming->last_reason), "%s", reason);
}

static bool mac_valid(const char *address)
{
	size_t i;

	if (!address || strlen(address) != 17U)
		return false;
	for (i = 0; i < 17U; i++) {
		if ((i + 1U) % 3U == 0) {
			if (address[i] != ':')
				return false;
			continue;
		}
		if (!((address[i] >= '0' && address[i] <= '9') ||
		      (address[i] >= 'a' && address[i] <= 'f') ||
		      (address[i] >= 'A' && address[i] <= 'F')))
			return false;
	}
	return true;
}

static bool hex_string_valid(const char *value)
{
	size_t i;
	size_t length;

	if (!value)
		return false;
	length = strlen(value);
	if (!length || length >= WMCS_ROAMING_NEIGHBOR_MAX || length % 2U)
		return false;
	for (i = 0; i < length; i++) {
		if (!((value[i] >= '0' && value[i] <= '9') ||
		      (value[i] >= 'a' && value[i] <= 'f') ||
		      (value[i] >= 'A' && value[i] <= 'F')))
			return false;
	}
	return true;
}

static struct wmcs_roaming_client *find_client(
	struct wmcs_roaming *roaming, const char *address)
{
	struct wmcs_roaming_client *free_slot = NULL;
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		if (!roaming->clients[i].used) {
			if (!free_slot)
				free_slot = &roaming->clients[i];
			continue;
		}
		if (!strcmp(roaming->clients[i].address, address))
			return &roaming->clients[i];
	}
	if (!free_slot)
		return NULL;
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->used = true;
	memcpy(free_slot->address, address, 17U);
	free_slot->address[17] = '\0';
	return free_slot;
}

static void retire_unseen_clients(struct wmcs_roaming *roaming)
{
	size_t i;

	roaming->client_count = 0;
	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		if (!roaming->clients[i].used)
			continue;
		if (!roaming->clients[i].observed) {
			wmcs_secure_zero(&roaming->clients[i],
					 sizeof(roaming->clients[i]));
			continue;
		}
		roaming->client_count++;
	}
}

static void parse_capability_array(struct blob_attr *array, bool *btm,
					   bool *neighbor_report)
{
	struct blob_attr *entry;
	size_t remaining;
	size_t index = 0;

	if (!array || blobmsg_type(array) != BLOBMSG_TYPE_ARRAY)
		return;
	blobmsg_for_each_attr(entry, array, remaining) {
		int64_t value;

		if (blobmsg_type(entry) != BLOBMSG_TYPE_INT32 &&
		    blobmsg_type(entry) != BLOBMSG_TYPE_INT8)
			continue;
		value = blobmsg_cast_s64(entry);
		if (value < 0 || value > 255) {
			index++;
			continue;
		}
		if (index == 0U && neighbor_report && ((uint8_t)value & 0x02U))
			*neighbor_report = true;
		if (index == 2U && btm && ((uint8_t)value & 0x08U))
			*btm = true;
		index++;
	}
}

static void parse_client_fields(struct wmcs_roaming_client *client,
					struct blob_attr *table)
{
	struct blob_attr *field;
	size_t remaining;

	blobmsg_for_each_attr(field, table, remaining) {
		const char *name = blobmsg_name(field);

		if (!strcmp(name, "assoc") &&
		    blobmsg_type(field) == BLOBMSG_TYPE_BOOL) {
			client->observed = client->observed && blobmsg_get_bool(field);
		} else if (!strcmp(name, "authorized") &&
			   blobmsg_type(field) == BLOBMSG_TYPE_BOOL) {
			client->observed = client->observed && blobmsg_get_bool(field);
		} else if (!strcmp(name, "signal") &&
			   blobmsg_type(field) == BLOBMSG_TYPE_INT32) {
			int64_t signal = blobmsg_cast_s64(field);

			if (signal >= -127 && signal <= 0) {
				client->signal_dbm = (int)signal;
				client->signal_seen = true;
			}
		} else if (!strcmp(name, "rrm")) {
			parse_capability_array(field, NULL, &client->neighbor_report);
		} else if (!strcmp(name, "extended_capabilities")) {
			parse_capability_array(field, &client->btm, NULL);
		}
	}
}

static void clients_callback(struct ubus_request *request, int type,
				     struct blob_attr *message)
{
	struct wmcs_roaming *roaming = request->priv;
	struct blob_attr *attribute;
	size_t remaining;

	(void)type;
	if (!message)
		return;
	blobmsg_for_each_attr(attribute, message, remaining) {
		struct blob_attr *entry;
		size_t entry_remaining;

		if (strcmp(blobmsg_name(attribute), "clients") ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_TABLE)
			continue;
		blobmsg_for_each_attr(entry, attribute, entry_remaining) {
			struct wmcs_roaming_client *client;

			if (blobmsg_type(entry) != BLOBMSG_TYPE_TABLE ||
			    !mac_valid(blobmsg_name(entry)))
				continue;
			client = find_client(roaming, blobmsg_name(entry));
			if (!client)
				continue;
			client->observed = true;
			client->signal_seen = false;
			client->btm = false;
			client->neighbor_report = false;
			parse_client_fields(client, entry);
		}
	}
}

static void neighbors_callback(struct ubus_request *request, int type,
				       struct blob_attr *message)
{
	struct wmcs_roaming *roaming = request->priv;
	struct blob_attr *attribute;
	size_t remaining;
	size_t count = 0;

	(void)type;
	roaming->neighbor[0] = '\0';
	roaming->neighbor_count = 0;
	if (!message)
		return;
	blobmsg_for_each_attr(attribute, message, remaining) {
		struct blob_attr *record;
		size_t record_remaining;

		if (strcmp(blobmsg_name(attribute), "list") ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_ARRAY)
			continue;
		blobmsg_for_each_attr(record, attribute, record_remaining) {
			struct blob_attr *field;
			const char *encoded = NULL;
			size_t field_remaining;
			unsigned int fields = 0;

			if (blobmsg_type(record) != BLOBMSG_TYPE_ARRAY)
				continue;
			blobmsg_for_each_attr(field, record, field_remaining) {
				if (blobmsg_type(field) == BLOBMSG_TYPE_STRING &&
				    fields == 2U)
					encoded = blobmsg_get_string(field);
				fields++;
			}
			if (fields != 3U || !hex_string_valid(encoded))
				continue;
			count++;
			if (count == 1U)
				snprintf(roaming->neighbor, sizeof(roaming->neighbor),
					 "%s", encoded);
		}
	}
	roaming->neighbor_count = count;
}

static int invoke_noarg(struct wmcs_roaming *roaming, const char *method,
				ubus_data_handler_t callback)
{
	struct blob_buf request = {0};
	uint32_t id;
	int result;

	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	result = ubus_invoke(roaming->ubus, id, method, request.head, callback,
				 roaming, 3000);
	blob_buf_free(&request);
	return result ? -EIO : 0;
}

static int send_advisory_btm(struct wmcs_roaming *roaming,
				     const struct wmcs_roaming_client *client)
{
	struct blob_buf request = {0};
	void *neighbors;
	uint32_t id;
	int result;

	if (!roaming->neighbor[0] || roaming->neighbor_count != 1U)
		return -ENOENT;
	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	blobmsg_add_string(&request, "addr", client->address);
	blobmsg_add_u8(&request, "disassociation_imminent", 0);
	blobmsg_add_u32(&request, "disassociation_timer", 0);
	blobmsg_add_u32(&request, "validity_period",
			WMCS_ROAMING_VALIDITY_PERIOD);
	neighbors = blobmsg_open_array(&request, "neighbors");
	blobmsg_add_string(&request, NULL, roaming->neighbor);
	blobmsg_close_array(&request, neighbors);
	blobmsg_add_u8(&request, "abridged", 1);
	result = ubus_invoke(roaming->ubus, id, "bss_transition_request",
				 request.head, NULL, NULL, 3000);
	blob_buf_free(&request);
	return result ? -EIO : 0;
}

static int refresh_hostapd(struct wmcs_roaming *roaming)
{
	const char *section = roaming->source_iface;
	int result;

	if (!strcmp(roaming->role, "agent"))
		section = WMCS_WLAN_SECTION;
	result = wmcs_wlan_resolve_hostapd(roaming->ubus, roaming->source_radio,
					   section, roaming->hostapd_object,
					   sizeof(roaming->hostapd_object));
	if (result) {
		roaming->hostapd_ready = false;
		set_reason(roaming, "hostapd_unavailable");
		return result;
	}
	roaming->hostapd_ready = true;
	return 0;
}

static void reset_observed(struct wmcs_roaming *roaming)
{
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++)
		roaming->clients[i].observed = false;
}

static void evaluate_clients(struct wmcs_roaming *roaming, uint64_t now)
{
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		struct wmcs_roaming_client *client = &roaming->clients[i];
		int result;

		if (!client->used || !client->observed)
			continue;
		if (!client->signal_seen || !client->signal_dbm) {
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "signal_unavailable");
			continue;
		}
		roaming->last_source_signal_dbm = client->signal_dbm;
		roaming->last_signal_seen = true;
		if (client->signal_dbm > roaming->source_trigger_dbm) {
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "source_above_trigger");
			continue;
		}
		if (!client->btm) {
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "client_without_btm");
			continue;
		}
		if (!client->neighbor_report) {
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "client_without_neighbor_report");
			continue;
		}
		if (roaming->neighbor_count != 1U) {
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, roaming->neighbor_count ?
				   "ambiguous_neighbor_list" : "neighbor_unavailable");
			continue;
		}
		if (!client->first_seen_ms)
			client->first_seen_ms = now;
		if (now < client->first_seen_ms ||
		    now - client->first_seen_ms < WMCS_ROAMING_MIN_DWELL_MS) {
			set_reason(roaming, "minimum_dwell");
			continue;
		}
		if (client->attempted) {
			set_reason(roaming, "per_association_attempt_budget");
			continue;
		}
		if (!client->weak) {
			client->weak = true;
			client->weak_since_ms = now;
			client->weak_samples = 1;
		} else if (client->weak_samples < WMCS_ROAMING_CONFIRM_SAMPLES) {
			client->weak_samples++;
		}
		if (client->weak_samples < WMCS_ROAMING_CONFIRM_SAMPLES) {
			set_reason(roaming, "source_trigger_confirming");
			continue;
		}
		roaming->gate_passes++;
		result = send_advisory_btm(roaming, client);
		client->attempted = true;
		roaming->last_action_ms = now;
		if (result) {
			roaming->request_failures++;
			set_reason(roaming, "btm_request_failed");
		} else {
			roaming->requests_sent++;
			set_reason(roaming, "btm_request_sent");
		}
	}
}

static void poll_once(struct wmcs_roaming *roaming)
{
	uint64_t now = wmcs_monotonic_ms();

	if (refresh_hostapd(roaming))
		return;
	reset_observed(roaming);
	if (invoke_noarg(roaming, "get_clients", clients_callback)) {
		retire_unseen_clients(roaming);
		set_reason(roaming, "client_observation_failed");
		return;
	}
	if (invoke_noarg(roaming, "rrm_nr_list", neighbors_callback)) {
		retire_unseen_clients(roaming);
		set_reason(roaming, "neighbor_observation_failed");
		return;
	}
	retire_unseen_clients(roaming);
	roaming->samples++;
	evaluate_clients(roaming, now);
}

static void poll_expired(struct uloop_timeout *timeout)
{
	struct wmcs_roaming *roaming = container_of(timeout, struct wmcs_roaming,
							poll_timer);

	if (!roaming->enabled)
		return;
	poll_once(roaming);
	uloop_timeout_set(&roaming->poll_timer, WMCS_ROAMING_POLL_MS);
}

int wmcs_roaming_init(struct wmcs_roaming *roaming, const char *role,
			      const char *source_iface, const char *source_radio,
			      struct ubus_context *ubus, bool enabled,
			      int source_trigger_dbm)
{
	if (!roaming || !role || !source_iface || !source_radio || !ubus ||
	    source_trigger_dbm < -95 || source_trigger_dbm > -50)
		return -EINVAL;
	memset(roaming, 0, sizeof(*roaming));
	roaming->role = role;
	roaming->source_iface = source_iface;
	roaming->source_radio = source_radio;
	roaming->ubus = ubus;
	roaming->enabled = enabled;
	roaming->source_trigger_dbm = source_trigger_dbm;
	roaming->poll_timer.cb = poll_expired;
	roaming->initialized = true;
	set_reason(roaming, enabled ? "starting" : "disabled_by_policy");
	return 0;
}

void wmcs_roaming_start(struct wmcs_roaming *roaming)
{
	if (!roaming || !roaming->initialized || !roaming->enabled)
		return;
	uloop_timeout_set(&roaming->poll_timer, 0);
}

void wmcs_roaming_close(struct wmcs_roaming *roaming)
{
	if (!roaming || !roaming->initialized)
		return;
	uloop_timeout_cancel(&roaming->poll_timer);
	wmcs_secure_zero(roaming, sizeof(*roaming));
}

bool wmcs_roaming_enabled(const struct wmcs_roaming *roaming)
{
	return roaming && roaming->initialized && roaming->enabled;
}

bool wmcs_roaming_active(const struct wmcs_roaming *roaming)
{
	return wmcs_roaming_enabled(roaming) && roaming->hostapd_ready;
}

enum wmcs_roaming_state wmcs_roaming_state(
	const struct wmcs_roaming *roaming)
{
	if (!wmcs_roaming_enabled(roaming))
		return WMCS_ROAMING_DISABLED;
	return roaming->hostapd_ready ? WMCS_ROAMING_MONITORING :
		WMCS_ROAMING_WAITING_HOSTAPD;
}

const char *wmcs_roaming_state_name(enum wmcs_roaming_state state)
{
	switch (state) {
	case WMCS_ROAMING_DISABLED:
		return "disabled";
	case WMCS_ROAMING_WAITING_HOSTAPD:
		return "waiting_hostapd";
	case WMCS_ROAMING_MONITORING:
		return "monitoring";
	default:
		return "unknown";
	}
}

const char *wmcs_roaming_last_reason(const struct wmcs_roaming *roaming)
{
	return roaming && roaming->last_reason[0] ? roaming->last_reason :
		"unknown";
}
