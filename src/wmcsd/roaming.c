// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <libubox/blobmsg.h>
#include <libubox/utils.h>

#include "discovery.h"
#include "roaming.h"
#include "roaming_event.h"
#include "roaming_policy.h"

#define WMCS_ROAMING_POLL_MS 1000U
#define WMCS_ROAMING_CONFIRM_SAMPLES 5U
#define WMCS_ROAMING_MIN_DWELL_MS 20000U
#define WMCS_ROAMING_VALIDITY_PERIOD 30U
#define WMCS_ROAMING_NEIGHBOR_MAX WMCS_ROAMING_NEIGHBOR_SIZE
/* 802.11 RRM Enabled Capabilities, first octet. */
#define WMCS_RRM_BEACON_PASSIVE 0x10U
#define WMCS_RRM_BEACON_ACTIVE 0x20U
#define WMCS_RRM_BEACON_DURATION_TU 100U

enum {
	BEACON_REPORT_ADDRESS,
	BEACON_REPORT_OP_CLASS,
	BEACON_REPORT_CHANNEL,
	BEACON_REPORT_RCPI,
	BEACON_REPORT_BSSID,
	BEACON_REPORT_MODE,
	__BEACON_REPORT_MAX,
};

static const struct blobmsg_policy beacon_report_policy[__BEACON_REPORT_MAX] = {
	[BEACON_REPORT_ADDRESS] = { "address", BLOBMSG_TYPE_STRING },
	[BEACON_REPORT_OP_CLASS] = { "op-class", BLOBMSG_TYPE_INT16 },
	[BEACON_REPORT_CHANNEL] = { "channel", BLOBMSG_TYPE_INT16 },
	[BEACON_REPORT_RCPI] = { "rcpi", BLOBMSG_TYPE_INT16 },
	[BEACON_REPORT_BSSID] = { "bssid", BLOBMSG_TYPE_STRING },
	[BEACON_REPORT_MODE] = { "rep-mode", BLOBMSG_TYPE_INT16 },
};

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

static uint8_t hex_byte(const char *value, size_t index)
{
	static const char digits[] = "0123456789abcdef";
	int high;
	int low;
	char high_char = value[index * 2U];
	char low_char = value[index * 2U + 1U];

	if (high_char >= 'A' && high_char <= 'F')
		high_char = (char)(high_char - 'A' + 'a');
	if (low_char >= 'A' && low_char <= 'F')
		low_char = (char)(low_char - 'A' + 'a');
	high = (int)(strchr(digits, high_char) - digits);
	low = (int)(strchr(digits, low_char) - digits);
	return (uint8_t)((high << 4) | low);
}

static bool parse_neighbor_report(const char *encoded,
				 struct wmcs_roaming_neighbor *neighbor)
{
	uint8_t bssid[6];
	size_t i;

	if (!hex_string_valid(encoded) || strlen(encoded) < 26U || !neighbor)
		return false;
	/* Neighbor Report body: BSSID[0..5], BSSID info[6..9], op-class[10],
	 * channel[11], PHY type[12]. */
	for (i = 0; i < sizeof(bssid); i++)
		bssid[i] = hex_byte(encoded, i);
	if ((bssid[0] & 0x01U) ||
	    !(bssid[0] | bssid[1] | bssid[2] | bssid[3] | bssid[4] | bssid[5]))
		return false;
	neighbor->op_class = hex_byte(encoded, 10U);
	neighbor->channel = hex_byte(encoded, 11U);
	if (!neighbor->op_class || !neighbor->channel)
		return false;
	snprintf(neighbor->bssid, sizeof(neighbor->bssid),
		 "%02x:%02x:%02x:%02x:%02x:%02x",
		 (unsigned int)bssid[0], (unsigned int)bssid[1],
		 (unsigned int)bssid[2], (unsigned int)bssid[3],
		 (unsigned int)bssid[4], (unsigned int)bssid[5]);
	snprintf(neighbor->report, sizeof(neighbor->report), "%s", encoded);
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
		if (!strcasecmp(roaming->clients[i].address, address))
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

static struct wmcs_roaming_client *find_existing_client(
	struct wmcs_roaming *roaming, const char *address)
{
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		if (roaming->clients[i].used &&
		    !strcasecmp(roaming->clients[i].address, address))
			return &roaming->clients[i];
	}
	return NULL;
}

static void clear_survey(struct wmcs_roaming *roaming)
{
	wmcs_secure_zero(&roaming->survey, sizeof(roaming->survey));
}

static void retire_unseen_clients(struct wmcs_roaming *roaming)
{
	size_t i;

	roaming->client_count = 0;
	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		if (!roaming->clients[i].used)
			continue;
		if (!roaming->clients[i].observed) {
			if (roaming->survey.active &&
			    !strcasecmp(roaming->survey.station,
					roaming->clients[i].address))
				clear_survey(roaming);
			wmcs_secure_zero(&roaming->clients[i],
					 sizeof(roaming->clients[i]));
			continue;
		}
		roaming->client_count++;
	}
}

static void parse_capability_array(struct blob_attr *array, bool *btm,
					   bool *neighbor_report,
					   uint8_t *beacon_modes)
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
		if (index == 0U) {
			if (neighbor_report && ((uint8_t)value & 0x02U))
				*neighbor_report = true;
			if (beacon_modes)
				*beacon_modes = (uint8_t)value &
					(WMCS_RRM_BEACON_PASSIVE |
					 WMCS_RRM_BEACON_ACTIVE);
		}
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
			client->assoc_seen = true;
			client->observed = client->observed && blobmsg_get_bool(field);
		} else if (!strcmp(name, "authorized") &&
			   blobmsg_type(field) == BLOBMSG_TYPE_BOOL) {
			client->authorized_seen = true;
			client->observed = client->observed && blobmsg_get_bool(field);
		} else if (!strcmp(name, "signal") &&
			   blobmsg_type(field) == BLOBMSG_TYPE_INT32) {
			int64_t signal = blobmsg_cast_s64(field);

			if (signal >= -127 && signal <= 0) {
				client->signal_dbm = (int)signal;
				client->signal_seen = true;
			}
		} else if (!strcmp(name, "rrm")) {
			parse_capability_array(field, NULL, &client->neighbor_report,
					       &client->beacon_measurement_modes);
		} else if (!strcmp(name, "extended_capabilities")) {
			parse_capability_array(field, &client->btm, NULL, NULL);
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
			client->assoc_seen = false;
			client->authorized_seen = false;
			client->signal_seen = false;
			client->btm = false;
			client->neighbor_report = false;
			client->beacon_measurement_modes = 0;
			parse_client_fields(client, entry);
			client->observed = client->observed &&
				client->assoc_seen && client->authorized_seen;
		}
	}
}

static void source_status_callback(struct ubus_request *request, int type,
				   struct blob_attr *message)
{
	struct wmcs_roaming *roaming = request->priv;
	struct blob_attr *attribute;
	const char *ssid = NULL;
	const char *bssid = NULL;
	size_t remaining;

	(void)type;
	if (!message)
		return;
	blobmsg_for_each_attr(attribute, message, remaining) {
		if (blobmsg_type(attribute) != BLOBMSG_TYPE_STRING)
			continue;
		if (!strcmp(blobmsg_name(attribute), "ssid"))
			ssid = blobmsg_get_string(attribute);
		else if (!strcmp(blobmsg_name(attribute), "bssid"))
			bssid = blobmsg_get_string(attribute);
	}
	if (!ssid || !*ssid || strlen(ssid) > 32U || !mac_valid(bssid))
		return;
	snprintf(roaming->source_ssid, sizeof(roaming->source_ssid), "%s", ssid);
	snprintf(roaming->source_bssid, sizeof(roaming->source_bssid), "%s", bssid);
	roaming->source_identity_ready = true;
}

static void neighbors_callback(struct ubus_request *request, int type,
				       struct blob_attr *message)
{
	struct wmcs_roaming *roaming = request->priv;
	struct blob_attr *attribute;
	size_t remaining;
	size_t count = 0;
	size_t encoded_bytes = 0;

	(void)type;
	memset(roaming->neighbors, 0, sizeof(roaming->neighbors));
	roaming->neighbor_count = 0;
	roaming->observed_neighbor_records = 0;
	roaming->neighbor_overflow = false;
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
			struct wmcs_roaming_neighbor parsed;
			const char *record_bssid = NULL;
			const char *record_ssid = NULL;
			const char *encoded = NULL;
			size_t field_remaining;
			size_t report_bytes;
			size_t candidate;
			unsigned int fields = 0;

			roaming->observed_neighbor_records++;

			if (blobmsg_type(record) != BLOBMSG_TYPE_ARRAY)
				continue;
			blobmsg_for_each_attr(field, record, field_remaining) {
				if (blobmsg_type(field) == BLOBMSG_TYPE_STRING) {
					if (fields == 0U)
						record_bssid = blobmsg_get_string(field);
					else if (fields == 1U)
						record_ssid = blobmsg_get_string(field);
					else if (fields == 2U)
						encoded = blobmsg_get_string(field);
				}
				fields++;
			}
			if (fields != 3U || !mac_valid(record_bssid) ||
			    !record_ssid || !roaming->source_identity_ready ||
			    strcmp(record_ssid, roaming->source_ssid) ||
			    !hex_string_valid(encoded))
				continue;
			if (roaming->neighbor_overflow)
				continue;
			if (!parse_neighbor_report(encoded, &parsed))
				continue;
			if (strcasecmp(parsed.bssid, record_bssid) ||
			    !strcasecmp(parsed.bssid, roaming->source_bssid))
				continue;
			for (candidate = 0; candidate < count; candidate++) {
				if (!strcasecmp(roaming->neighbors[candidate].bssid,
						parsed.bssid))
					break;
			}
			if (candidate < count)
				continue;
			report_bytes = strlen(encoded) / 2U + 2U;
			if (count == WMCS_ROAMING_MAX_NEIGHBORS ||
			    report_bytes > WMCS_ROAMING_NEIGHBOR_BYTES_MAX -
					   encoded_bytes) {
				roaming->neighbor_overflow = true;
				continue;
			}
			roaming->neighbors[count] = parsed;
			encoded_bytes += report_bytes;
			count++;
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

static void handle_beacon_report(struct wmcs_roaming *roaming,
				 struct blob_attr *message)
{
	struct blob_attr *attributes[__BEACON_REPORT_MAX] = {0};
	struct wmcs_roaming_survey *survey = &roaming->survey;
	struct wmcs_roaming_neighbor *candidate;
	const char *address;
	const char *bssid;
	uint32_t op_class;
	uint32_t channel;
	uint32_t rcpi;
	uint32_t report_mode;
	int signal_dbm;
	uint64_t now = wmcs_monotonic_ms();

	if (!survey->active || !survey->pending || !message ||
	    now >= survey->deadline_ms)
		return;
	blobmsg_parse(beacon_report_policy, __BEACON_REPORT_MAX, attributes,
		      blob_data(message), blob_len(message));
	if (!attributes[BEACON_REPORT_ADDRESS] ||
	    !attributes[BEACON_REPORT_OP_CLASS] ||
	    !attributes[BEACON_REPORT_CHANNEL] ||
	    !attributes[BEACON_REPORT_RCPI] ||
	    !attributes[BEACON_REPORT_BSSID] ||
	    !attributes[BEACON_REPORT_MODE])
		return;
	address = blobmsg_get_string(attributes[BEACON_REPORT_ADDRESS]);
	bssid = blobmsg_get_string(attributes[BEACON_REPORT_BSSID]);
	if (!mac_valid(address) || !mac_valid(bssid) ||
	    strcasecmp(address, survey->station) ||
	    survey->pending_candidate >= survey->candidate_count)
		return;
	candidate = &survey->candidates[survey->pending_candidate];
	op_class = blobmsg_get_u16(attributes[BEACON_REPORT_OP_CLASS]);
	channel = blobmsg_get_u16(attributes[BEACON_REPORT_CHANNEL]);
	rcpi = blobmsg_get_u16(attributes[BEACON_REPORT_RCPI]);
	report_mode = blobmsg_get_u16(attributes[BEACON_REPORT_MODE]);
	if (strcasecmp(bssid, candidate->bssid) ||
	    op_class != candidate->op_class || channel != candidate->channel ||
	    report_mode || rcpi > 220U)
		return;

	/* IEEE RCPI maps 0..220 to -110..0 dBm in half-dB steps. */
	signal_dbm = (int)(rcpi / 2U) - 110;
	if (!survey->best_signal_seen || signal_dbm > survey->best_signal_dbm) {
		survey->best_signal_seen = true;
		survey->best_signal_dbm = signal_dbm;
		survey->best_candidate = survey->pending_candidate;
		survey->best_observed_ms = now;
	}
	roaming->beacon_reports_received++;
	survey->next_candidate = survey->pending_candidate + 1U;
	survey->pending = false;
}

static int transition_response_event(struct ubus_context *ctx,
				     struct ubus_object *object,
				     struct ubus_request_data *request,
				     const char *method,
				     struct blob_attr *message)
{
	struct ubus_subscriber *subscriber = container_of(
		object, struct ubus_subscriber, obj);
	struct wmcs_roaming *roaming = container_of(
		subscriber, struct wmcs_roaming, subscriber);
	struct wmcs_btm_response response;
	const char *address;
	uint8_t token;
	uint8_t status_code;
	uint64_t now = wmcs_monotonic_ms();
	size_t i;

	(void)ctx;
	(void)request;
	if (!method || !message)
		return 0;
	if (!strcmp(method, "beacon-report")) {
		handle_beacon_report(roaming, message);
		return 0;
	}
	if (strcmp(method, "bss-transition-response"))
		return 0;
	if (!wmcs_roaming_parse_btm_response(message, &response))
		return 0;
	address = response.address;
	token = response.dialog_token;
	status_code = response.status_code;
	if (!mac_valid(address))
		return 0;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		struct wmcs_roaming_client *client = &roaming->clients[i];

		if (!client->used ||
		    (!client->btm_pending && !client->force_pending) ||
		    client->btm_dialog_token != token ||
		    strcasecmp(client->address, address))
			continue;
		if (client->btm_pending && now >= client->btm_deadline_ms)
			roaming->btm_response_timeouts++;
		client->btm_pending = false;
		/* Even a late explicit response cancels the optional disconnect. */
		client->force_pending = false;
		roaming->btm_responses++;
		roaming->last_btm_status_seen = true;
		roaming->last_btm_status_code = (uint8_t)status_code;
		if (!status_code) {
			roaming->btm_accepted++;
			set_reason(roaming, "btm_response_accepted");
		} else {
			roaming->btm_rejected++;
			set_reason(roaming, "btm_response_rejected");
		}
		return 0;
	}
	return 0;
}

static int subscribe_to_hostapd(struct wmcs_roaming *roaming)
{
	uint32_t object_id;
	int result;

	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &object_id)) {
		if (roaming->hostapd_subscribed) {
			(void)ubus_unsubscribe(roaming->ubus, &roaming->subscriber,
					       roaming->subscribed_hostapd_id);
			roaming->hostapd_subscribed = false;
		}
		roaming->response_monitor_active = false;
		return -ENOENT;
	}
	if (!roaming->subscriber_registered) {
		roaming->subscriber.cb = transition_response_event;
		result = ubus_register_subscriber(roaming->ubus,
						 &roaming->subscriber);
		if (result) {
			roaming->response_monitor_active = false;
			return -EIO;
		}
		roaming->subscriber_registered = true;
	}
	if (roaming->hostapd_subscribed &&
	    roaming->subscribed_hostapd_id != object_id) {
		(void)ubus_unsubscribe(roaming->ubus, &roaming->subscriber,
				       roaming->subscribed_hostapd_id);
		roaming->hostapd_subscribed = false;
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
	}
	if (!roaming->hostapd_subscribed) {
		result = ubus_subscribe(roaming->ubus, &roaming->subscriber,
					object_id);
		if (result) {
			roaming->response_monitor_active = false;
			return -EIO;
		}
		roaming->subscribed_hostapd_id = object_id;
		roaming->hostapd_subscribed = true;
	}
	roaming->response_monitor_active = true;
	return 0;
}

static int request_beacon_measurement(struct wmcs_roaming *roaming,
				      struct wmcs_roaming_client *client)
{
	struct wmcs_roaming_survey *survey = &roaming->survey;
	struct wmcs_roaming_neighbor *candidate;
	struct blob_buf request = {0};
	uint32_t id;
	uint32_t mode;
	int result;

	if (!survey->active || survey->pending ||
	    survey->next_candidate >= survey->candidate_count)
		return -EINVAL;
	if (client->beacon_measurement_modes & WMCS_RRM_BEACON_PASSIVE)
		mode = 0;
	else if (client->beacon_measurement_modes & WMCS_RRM_BEACON_ACTIVE)
		mode = 1;
	else
		return -EOPNOTSUPP;
	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &id))
		return -ENOENT;

	candidate = &survey->candidates[survey->next_candidate];
	blob_buf_init(&request, 0);
	blobmsg_add_string(&request, "addr", client->address);
	blobmsg_add_u32(&request, "op_class", candidate->op_class);
	blobmsg_add_u32(&request, "channel", candidate->channel);
	blobmsg_add_u32(&request, "duration", WMCS_RRM_BEACON_DURATION_TU);
	blobmsg_add_u32(&request, "mode", mode);
	blobmsg_add_string(&request, "bssid", candidate->bssid);
	survey->pending = true;
	survey->pending_candidate = survey->next_candidate;
	survey->deadline_ms = wmcs_monotonic_ms() +
			      WMCS_ROAMING_BEACON_TIMEOUT_MS;
	result = ubus_invoke(roaming->ubus, id, "rrm_beacon_req", request.head,
			     NULL, NULL, 1000);
	blob_buf_free(&request);
	if (result) {
		survey->pending = false;
		survey->next_candidate++;
		roaming->beacon_request_failures++;
		return -EIO;
	}
	roaming->beacon_requests_sent++;
	return 0;
}

static int send_advisory_btm(struct wmcs_roaming *roaming,
			     struct wmcs_roaming_client *client,
			     const struct wmcs_roaming_neighbor *target)
{
	struct blob_buf request = {0};
	void *neighbors;
	uint8_t dialog_token;
	uint32_t id;
	int result;

	if (!target || !target->report[0] || roaming->neighbor_overflow)
		return -ENOENT;
	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	blobmsg_add_string(&request, "addr", client->address);
	blobmsg_add_u8(&request, "disassociation_imminent", 0);
	blobmsg_add_u32(&request, "disassociation_timer", 0);
	blobmsg_add_u32(&request, "validity_period",
			WMCS_ROAMING_VALIDITY_PERIOD);
	dialog_token = (uint8_t)(roaming->next_dialog_token + 1U);
	if (!dialog_token)
		dialog_token = 1U;
	roaming->next_dialog_token = dialog_token;
	blobmsg_add_u32(&request, "dialog_token", dialog_token);
	neighbors = blobmsg_open_array(&request, "neighbors");
	blobmsg_add_string(&request, NULL, target->report);
	blobmsg_close_array(&request, neighbors);
	blobmsg_add_u8(&request, "abridged", 1);
	snprintf(client->btm_target_bssid, sizeof(client->btm_target_bssid),
		 "%s", target->bssid);
	snprintf(client->btm_source_bssid, sizeof(client->btm_source_bssid),
		 "%s", roaming->source_bssid);
	client->btm_target_op_class = target->op_class;
	client->btm_target_channel = target->channel;
	client->btm_pending = roaming->response_monitor_active;
	client->btm_dialog_token = dialog_token;
	if (client->btm_pending)
		client->btm_deadline_ms = wmcs_monotonic_ms() +
					  WMCS_ROAMING_BTM_RESPONSE_TIMEOUT_MS;
	result = ubus_invoke(roaming->ubus, id, "bss_transition_request",
				 request.head, NULL, NULL, 3000);
	blob_buf_free(&request);
	if (result)
		client->btm_pending = false;
	return result ? -EIO : 0;
}

static struct wmcs_roaming_force_cooldown *force_cooldown_slot(
	struct wmcs_roaming *roaming, const char *address, uint64_t now)
{
	struct wmcs_roaming_force_cooldown *free_slot = NULL;
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		struct wmcs_roaming_force_cooldown *slot =
			&roaming->force_cooldowns[i];

		if (slot->address[0] && !strcasecmp(slot->address, address))
			return slot;
		if (!free_slot && (!slot->address[0] || now >= slot->until_ms))
			free_slot = slot;
	}
	if (free_slot) {
		memset(free_slot, 0, sizeof(*free_slot));
		snprintf(free_slot->address, sizeof(free_slot->address), "%s",
			 address);
	}
	return free_slot;
}

static int force_disassociate_client(struct wmcs_roaming *roaming,
				     struct wmcs_roaming_client *client)
{
	struct blob_buf request = {0};
	uint32_t id;
	int result;

	if (ubus_lookup_id(roaming->ubus, roaming->hostapd_object, &id))
		return -ENOENT;
	blob_buf_init(&request, 0);
	blobmsg_add_string(&request, "addr", client->address);
	/* IEEE 802.11 reason 12: disassociated due to BSS transition. */
	blobmsg_add_u32(&request, "reason", 12U);
	blobmsg_add_u8(&request, "deauth", 0);
	blobmsg_add_u32(&request, "ban_time", WMCS_ROAMING_FORCE_BAN_MS);
	result = ubus_invoke(roaming->ubus, id, "del_client", request.head,
			     NULL, NULL, 3000);
	blob_buf_free(&request);
	return result ? -EIO : 0;
}

static void maybe_force_disassociate(struct wmcs_roaming *roaming,
				     struct wmcs_roaming_client *client,
				     uint64_t now)
{
	struct wmcs_roaming_force_cooldown *cooldown;
	int result;

	if (!client->force_pending || now < client->force_due_ms)
		return;
	client->force_pending = false;
	if (!wmcs_roaming_force_gate(roaming->force_after_timeout,
		client->observed && client->signal_seen,
		roaming->response_monitor_active,
		roaming->neighbor_count == 1U &&
		!strcasecmp(roaming->neighbors[0].bssid,
			    client->btm_target_bssid) &&
		!strcasecmp(roaming->source_bssid, client->btm_source_bssid) &&
		roaming->neighbors[0].op_class == client->btm_target_op_class &&
		roaming->neighbors[0].channel == client->btm_target_channel,
		roaming->neighbor_overflow, roaming->neighbor_count,
		client->signal_dbm, roaming->force_trigger_dbm,
		client->weak_samples, WMCS_ROAMING_CONFIRM_SAMPLES)) {
		set_reason(roaming, "force_safety_gate_closed");
		return;
	}
	cooldown = force_cooldown_slot(roaming, client->address, now);
	if (!cooldown) {
		set_reason(roaming, "force_cooldown_table_full");
		return;
	}
	if (now < cooldown->until_ms) {
		set_reason(roaming, "force_cooldown_active");
		return;
	}
	/* Reserve the cooldown before invoking hostapd; an error must not turn
	 * into a disconnect storm after the client associates again. */
	cooldown->until_ms = now + WMCS_ROAMING_FORCE_COOLDOWN_MS;
	result = force_disassociate_client(roaming, client);
	roaming->last_action_ms = now;
	if (result) {
		roaming->force_failures++;
		set_reason(roaming, "force_disconnect_failed");
	} else {
		roaming->force_disconnects++;
		set_reason(roaming, "force_disconnect_sent");
	}
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
		roaming->source_identity_ready = false;
		if (roaming->hostapd_subscribed) {
			(void)ubus_unsubscribe(roaming->ubus, &roaming->subscriber,
					       roaming->subscribed_hostapd_id);
			roaming->hostapd_subscribed = false;
		}
		roaming->response_monitor_active = false;
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
		set_reason(roaming, "hostapd_unavailable");
		return result;
	}
	roaming->hostapd_ready = true;
	if (subscribe_to_hostapd(roaming)) {
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
		set_reason(roaming, "btm_response_monitor_unavailable");
	}
	return 0;
}

static void reset_observed(struct wmcs_roaming *roaming)
{
	size_t i;

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++)
		roaming->clients[i].observed = false;
}

static void start_survey(struct wmcs_roaming *roaming,
			 struct wmcs_roaming_client *client)
{
	struct wmcs_roaming_survey *survey = &roaming->survey;

	clear_survey(roaming);
	survey->active = true;
	snprintf(survey->station, sizeof(survey->station), "%s",
		 client->address);
	survey->candidate_count = roaming->neighbor_count;
	memcpy(survey->candidates, roaming->neighbors,
	       survey->candidate_count * sizeof(survey->candidates[0]));
	roaming->gate_passes++;
}

static const struct wmcs_roaming_neighbor *find_current_neighbor(
	const struct wmcs_roaming *roaming,
	const struct wmcs_roaming_neighbor *candidate)
{
	size_t i;

	for (i = 0; i < roaming->neighbor_count; i++) {
		if (!strcasecmp(roaming->neighbors[i].bssid, candidate->bssid) &&
		    !strcasecmp(roaming->neighbors[i].report, candidate->report))
			return &roaming->neighbors[i];
	}
	return NULL;
}

static void finish_survey(struct wmcs_roaming *roaming,
			  struct wmcs_roaming_client *client, uint64_t now)
{
	struct wmcs_roaming_survey *survey = &roaming->survey;
	const struct wmcs_roaming_neighbor *target = NULL;
	enum wmcs_roaming_target_decision decision;
	bool report_time_valid = !survey->best_signal_seen ||
				 now >= survey->best_observed_ms;
	uint64_t age = 0;
	int margin = 0;
	int result;

	client->measurement_retry_after_ms = now +
					     WMCS_ROAMING_SURVEY_RETRY_MS;
	if (survey->best_signal_seen && report_time_valid) {
		age = now - survey->best_observed_ms;
		roaming->last_target_signal_dbm = survey->best_signal_dbm;
		roaming->last_target_seen = true;
		roaming->last_target_observed_ms = survey->best_observed_ms;
		margin = survey->best_signal_dbm - client->signal_dbm;
		roaming->last_target_margin_db = margin;
	} else {
		roaming->last_target_seen = false;
	}
	decision = wmcs_roaming_target_decide(survey->best_signal_seen,
		report_time_valid, age, margin, roaming->improvement_margin_db,
		survey->candidate_count, roaming->neighbor_count,
		survey->best_candidate, roaming->neighbor_overflow,
		WMCS_ROAMING_TARGET_FRESH_MS);
	switch (decision) {
	case WMCS_ROAMING_TARGET_OVERFLOW:
		set_reason(roaming, "neighbor_list_too_large");
		break;
	case WMCS_ROAMING_TARGET_NO_REPORT:
		set_reason(roaming, "beacon_report_unavailable");
		break;
	case WMCS_ROAMING_TARGET_STALE:
			set_reason(roaming, "target_measurement_stale");
		break;
	case WMCS_ROAMING_TARGET_WEAK:
			set_reason(roaming, "target_improvement_too_small");
		break;
	case WMCS_ROAMING_TARGET_INVALID:
		set_reason(roaming, "target_measurement_invalid");
		break;
	case WMCS_ROAMING_TARGET_SINGLE_UNMEASURED:
		/* A locally published sole neighbor allows only an
		 * advisory BTM; no target signal is claimed. */
		target = find_current_neighbor(roaming, &survey->candidates[0]);
		break;
	case WMCS_ROAMING_TARGET_MEASURED:
		target = find_current_neighbor(roaming,
			&survey->candidates[survey->best_candidate]);
		break;
	}
	if (decision != WMCS_ROAMING_TARGET_SINGLE_UNMEASURED &&
	    decision != WMCS_ROAMING_TARGET_MEASURED) {
		clear_survey(roaming);
		return;
	}
	if (!target) {
		set_reason(roaming, "target_neighbor_changed");
		clear_survey(roaming);
		return;
	}
	result = send_advisory_btm(roaming, client, target);
	client->attempted = true;
	roaming->last_action_ms = now;
	if (result) {
		roaming->request_failures++;
		set_reason(roaming, "btm_request_failed");
	} else {
		roaming->requests_sent++;
		if (decision == WMCS_ROAMING_TARGET_SINGLE_UNMEASURED) {
			roaming->fallback_btm_sent++;
			set_reason(roaming, "btm_request_sent_without_measurement");
		} else {
			set_reason(roaming, "btm_request_sent");
		}
	}
	clear_survey(roaming);
}

static void evaluate_clients(struct wmcs_roaming *roaming, uint64_t now)
{
	size_t i;
	struct wmcs_roaming_client *survey_owner = NULL;

	if (roaming->survey.active) {
		survey_owner = find_existing_client(roaming,
						roaming->survey.station);
		if (!survey_owner || !survey_owner->observed ||
		    !survey_owner->signal_seen ||
		    survey_owner->signal_dbm > roaming->source_trigger_dbm ||
		    !survey_owner->btm || !survey_owner->neighbor_report ||
		    (!survey_owner->beacon_measurement_modes &&
		     roaming->survey.candidate_count != 1U) ||
		    survey_owner->attempted || !roaming->neighbor_count ||
		    roaming->neighbor_overflow ||
		    !roaming->response_monitor_active) {
			clear_survey(roaming);
			survey_owner = NULL;
		}
	}
	if (roaming->survey.active && roaming->survey.pending &&
	    now >= roaming->survey.deadline_ms) {
		roaming->survey.pending = false;
		roaming->survey.next_candidate =
			roaming->survey.pending_candidate + 1U;
		roaming->beacon_report_timeouts++;
		set_reason(roaming, "beacon_report_timeout");
	}

	for (i = 0; i < WMCS_ROAMING_MAX_CLIENTS; i++) {
		struct wmcs_roaming_client *client = &roaming->clients[i];
		struct wmcs_roaming_survey *survey = &roaming->survey;
		int result;

		if (!client->used || !client->observed)
			continue;
		if (client->btm_pending) {
			if (now >= client->btm_deadline_ms) {
				client->btm_pending = false;
				roaming->btm_response_timeouts++;
				if (roaming->force_after_timeout) {
					client->force_pending = true;
					client->force_due_ms = now +
						WMCS_ROAMING_FORCE_DELAY_MS;
				}
				set_reason(roaming, "btm_response_timeout");
			} else {
				set_reason(roaming, "btm_response_pending");
			}
			continue;
		}
		if (!client->signal_seen) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "signal_unavailable");
			continue;
		}
		roaming->last_source_signal_dbm = client->signal_dbm;
		roaming->last_signal_seen = true;
		if (client->signal_dbm > roaming->source_trigger_dbm) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "source_above_trigger");
			continue;
		}
		if (!client->btm) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "client_without_btm");
			continue;
		}
		if (!client->neighbor_report) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "client_without_neighbor_report");
			continue;
		}
		if (!client->beacon_measurement_modes &&
		    roaming->neighbor_count != 1U) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "client_without_beacon_measurement");
			continue;
		}
		if (roaming->neighbor_overflow) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "neighbor_list_too_large");
			continue;
		}
		if (!roaming->neighbor_count) {
			client->force_pending = false;
			client->weak = false;
			client->weak_since_ms = 0;
			client->weak_samples = 0;
			set_reason(roaming, "neighbor_unavailable");
			continue;
		}
		if (!client->first_seen_ms)
			client->first_seen_ms = now;
		if (now < client->first_seen_ms ||
		    now - client->first_seen_ms < WMCS_ROAMING_MIN_DWELL_MS) {
			set_reason(roaming, "minimum_dwell");
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
		if (client->force_pending &&
		    (roaming->neighbor_count != 1U ||
		     strcasecmp(roaming->neighbors[0].bssid,
				client->btm_target_bssid) ||
		     strcasecmp(roaming->source_bssid,
				client->btm_source_bssid) ||
		     roaming->neighbors[0].op_class !=
				client->btm_target_op_class ||
		     roaming->neighbors[0].channel !=
				client->btm_target_channel)) {
			client->force_pending = false;
			set_reason(roaming, "force_safety_gate_closed");
			continue;
		}
		if (client->force_pending && now >= client->force_due_ms) {
			maybe_force_disassociate(roaming, client, now);
			continue;
		}
		if (client->force_pending) {
			set_reason(roaming, "force_waiting_after_btm_timeout");
			continue;
		}
		if (client->attempted) {
			set_reason(roaming, "per_association_attempt_budget");
			continue;
		}
		if (!roaming->response_monitor_active) {
			set_reason(roaming, "btm_response_monitor_unavailable");
			continue;
		}
		if (client->measurement_retry_after_ms &&
		    now < client->measurement_retry_after_ms) {
			set_reason(roaming, "target_measurement_cooldown");
			continue;
		}
		if (survey->active && strcasecmp(survey->station,
						 client->address)) {
			set_reason(roaming, "beacon_measurement_busy");
			continue;
		}
		if (!survey->active) {
			start_survey(roaming, client);
			survey = &roaming->survey;
			if (!client->beacon_measurement_modes)
				survey->next_candidate = survey->candidate_count;
		}
		if (survey->pending) {
			set_reason(roaming, "beacon_measurement_pending");
			continue;
		}
		if (!client->beacon_measurement_modes)
			survey->next_candidate = survey->candidate_count;
		if (survey->next_candidate < survey->candidate_count) {
			result = request_beacon_measurement(roaming, client);
			set_reason(roaming, result ? "beacon_request_failed" :
				   "beacon_measurement_pending");
			continue;
		}
		finish_survey(roaming, client, now);
	}
}

static void poll_once(struct wmcs_roaming *roaming)
{
	char previous_ssid[sizeof(roaming->source_ssid)];
	char previous_bssid[sizeof(roaming->source_bssid)];
	bool previous_identity_ready;
	uint64_t now;

	if (roaming->neighbor_sync.enabled && !roaming->neighbor_sync.ready) {
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
		roaming->neighbor_count = 0;
		roaming->hostapd_ready = false;
		set_reason(roaming, "neighbor_sync_unavailable");
		return;
	}
	if (refresh_hostapd(roaming))
		return;
	previous_identity_ready = roaming->source_identity_ready;
	memcpy(previous_ssid, roaming->source_ssid, sizeof(previous_ssid));
	memcpy(previous_bssid, roaming->source_bssid, sizeof(previous_bssid));
	roaming->source_identity_ready = false;
	if (invoke_noarg(roaming, "get_status", source_status_callback) ||
	    !roaming->source_identity_ready) {
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
		set_reason(roaming, "source_identity_unavailable");
		return;
	}
	if (previous_identity_ready &&
	    (strcmp(previous_ssid, roaming->source_ssid) ||
	     strcasecmp(previous_bssid, roaming->source_bssid))) {
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
	}
	reset_observed(roaming);
	if (invoke_noarg(roaming, "get_clients", clients_callback)) {
		retire_unseen_clients(roaming);
		set_reason(roaming, "client_observation_failed");
		return;
	}
	roaming->neighbor_count = 0;
	roaming->observed_neighbor_records = 0;
	roaming->neighbor_overflow = false;
	if (invoke_noarg(roaming, "rrm_nr_list", neighbors_callback)) {
		clear_survey(roaming);
		wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
		roaming->client_count = 0;
		roaming->neighbor_count = 0;
		set_reason(roaming, "neighbor_observation_failed");
		return;
	}
	if (roaming->neighbor_sync.enabled) {
		size_t candidate;

		if (!roaming->neighbor_sync.ready ||
		    roaming->neighbor_overflow ||
		    roaming->observed_neighbor_records !=
			roaming->neighbor_sync.trusted_count ||
		    roaming->neighbor_count != roaming->neighbor_sync.trusted_count)
			goto untrusted_neighbors;
		for (candidate = 0; candidate < roaming->neighbor_count;
		     candidate++) {
			if (!wmcs_neighbor_sync_contains(&roaming->neighbor_sync,
					roaming->neighbors[candidate].report))
				goto untrusted_neighbors;
		}
	}
	retire_unseen_clients(roaming);
	now = wmcs_monotonic_ms();
	roaming->samples++;
	evaluate_clients(roaming, now);
	return;

untrusted_neighbors:
	wmcs_neighbor_sync_invalidate(&roaming->neighbor_sync);
	clear_survey(roaming);
	wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
	roaming->client_count = 0;
	roaming->neighbor_count = 0;
	set_reason(roaming, "neighbor_sync_changed");
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
			      const char *interface, const char *source_iface,
			      const char *source_radio,
			      struct wmcs_identity *identity,
			      struct ubus_context *ubus, bool enabled,
			      bool neighbor_sync_enabled,
			      int source_trigger_dbm, int improvement_margin_db,
			      bool force_after_timeout, int force_trigger_dbm)
{
	if (!roaming || !role || !interface || !source_iface || !source_radio ||
	    !identity || !ubus ||
	    source_trigger_dbm < -95 || source_trigger_dbm > -50 ||
	    improvement_margin_db < 1 || improvement_margin_db > 20 ||
	    force_trigger_dbm < -95 || force_trigger_dbm > -75)
		return -EINVAL;
	memset(roaming, 0, sizeof(*roaming));
	roaming->role = role;
	roaming->source_iface = source_iface;
	roaming->source_radio = source_radio;
	roaming->ubus = ubus;
	roaming->enabled = enabled;
	roaming->source_trigger_dbm = source_trigger_dbm;
	roaming->improvement_margin_db = improvement_margin_db;
	roaming->force_after_timeout = force_after_timeout;
	roaming->force_trigger_dbm = force_trigger_dbm;
	roaming->poll_timer.cb = poll_expired;
	if (wmcs_neighbor_sync_init(&roaming->neighbor_sync, role, interface,
				    source_iface, source_radio, identity,
				    ubus, neighbor_sync_enabled))
		return -EINVAL;
	roaming->initialized = true;
	set_reason(roaming, enabled ? "starting" : "disabled_by_policy");
	return 0;
}

void wmcs_roaming_start(struct wmcs_roaming *roaming)
{
	if (!roaming || !roaming->initialized)
		return;
	wmcs_neighbor_sync_start(&roaming->neighbor_sync);
	if (roaming->enabled)
		uloop_timeout_set(&roaming->poll_timer, 0);
}

void wmcs_roaming_ubus_disconnected(struct wmcs_roaming *roaming)
{
	if (!roaming || !roaming->initialized)
		return;
	roaming->hostapd_ready = false;
	wmcs_neighbor_sync_ubus_disconnected(&roaming->neighbor_sync);
	roaming->source_identity_ready = false;
	/* ubus_reconnect() re-adds registered subscriber objects; only the
	 * remote hostapd subscription itself must be established again. */
	roaming->hostapd_subscribed = false;
	roaming->response_monitor_active = false;
	roaming->subscribed_hostapd_id = 0;
	clear_survey(roaming);
	wmcs_secure_zero(roaming->clients, sizeof(roaming->clients));
	roaming->client_count = 0;
}

void wmcs_roaming_close(struct wmcs_roaming *roaming)
{
	if (!roaming || !roaming->initialized)
		return;
	uloop_timeout_cancel(&roaming->poll_timer);
	wmcs_neighbor_sync_close(&roaming->neighbor_sync);
	if (roaming->hostapd_subscribed)
		(void)ubus_unsubscribe(roaming->ubus, &roaming->subscriber,
				       roaming->subscribed_hostapd_id);
	if (roaming->subscriber_registered)
		(void)ubus_unregister_subscriber(roaming->ubus,
						 &roaming->subscriber);
	wmcs_secure_zero(roaming, sizeof(*roaming));
}

bool wmcs_roaming_enabled(const struct wmcs_roaming *roaming)
{
	return roaming && roaming->initialized && roaming->enabled;
}

bool wmcs_roaming_active(const struct wmcs_roaming *roaming)
{
	return wmcs_roaming_enabled(roaming) && roaming->hostapd_ready &&
	       (!roaming->neighbor_sync.enabled || roaming->neighbor_sync.ready);
}

enum wmcs_roaming_state wmcs_roaming_state(
	const struct wmcs_roaming *roaming)
{
	if (!wmcs_roaming_enabled(roaming))
		return WMCS_ROAMING_DISABLED;
	return wmcs_roaming_active(roaming) ? WMCS_ROAMING_MONITORING :
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
