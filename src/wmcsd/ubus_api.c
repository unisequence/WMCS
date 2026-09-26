// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libubox/blobmsg.h>
#include <libubox/utils.h>
#include <libubus.h>

#include "wmcsd.h"
#include "result_store.h"
#include "wlan.h"

static struct blob_buf reply;
static const struct wmcs_runtime *active_runtime;
static bool object_registered;

static bool executable(const char *first, const char *second)
{
	return !access(first, X_OK) || (second && !access(second, X_OK));
}

static uint64_t current_generation(void)
{
	return active_runtime && active_runtime->identity ?
		       wmcs_identity_generation(active_runtime->identity) : 0;
}

static bool runtime_mutation_allowed(void)
{
	return active_runtime && !active_runtime->degraded;
}

static bool request_empty(struct blob_attr *message)
{
	struct blob_attr *attribute;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining)
		return false;

	return remaining == 0;
}

static int roaming_status(struct ubus_context *ctx,
			  struct ubus_object *object,
			  struct ubus_request_data *request, const char *method,
			  struct blob_attr *message)
{
	const struct wmcs_roaming *roaming;
	char threshold[8];
	char signal[8];
	char margin[8];
	char force_threshold[8];
	char target_signal[8];
	char target_margin[8];
	char btm_status[4];
	uint64_t target_age_ms;
	uint64_t now;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->roaming)
		return UBUS_STATUS_UNKNOWN_ERROR;
	roaming = active_runtime->roaming;
	now = wmcs_monotonic_ms();
	snprintf(threshold, sizeof(threshold), "%d",
		 roaming->source_trigger_dbm);
	snprintf(margin, sizeof(margin), "%d",
		 roaming->improvement_margin_db);
	snprintf(force_threshold, sizeof(force_threshold), "%d",
		 roaming->force_trigger_dbm);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_string(&reply, "mode", roaming->force_after_timeout ?
			   "source_gate_advisory_with_opt_in_force" :
			   "source_gate_advisory");
	blobmsg_add_u8(&reply, "enabled", wmcs_roaming_enabled(roaming));
	blobmsg_add_u8(&reply, "neighbor_sync_enabled",
			roaming->neighbor_sync.enabled);
	blobmsg_add_u8(&reply, "neighbor_sync_ready",
			roaming->neighbor_sync.ready);
	blobmsg_add_string(&reply, "neighbor_sync_state",
			   roaming->neighbor_sync.reason);
	blobmsg_add_u32(&reply, "authenticated_neighbor_count",
			(uint32_t)roaming->neighbor_sync.fresh_peer_count);
	blobmsg_add_u64(&reply, "neighbor_queries_sent",
			roaming->neighbor_sync.queries_sent);
	blobmsg_add_u64(&reply, "neighbor_replies_accepted",
			roaming->neighbor_sync.replies_accepted);
	blobmsg_add_u64(&reply, "neighbor_apply_failures",
			roaming->neighbor_sync.apply_failures);
	blobmsg_add_u8(&reply, "force_after_timeout",
		roaming->force_after_timeout);
	blobmsg_add_string(&reply, "force_trigger_dbm", force_threshold);
	blobmsg_add_u8(&reply, "active", wmcs_roaming_active(roaming));
	blobmsg_add_u8(&reply, "btm_response_monitor_active",
			roaming->response_monitor_active);
	blobmsg_add_u8(&reply, "beacon_measurement_pending",
			roaming->survey.active && roaming->survey.pending);
	blobmsg_add_string(&reply, "state",
			   wmcs_roaming_state_name(wmcs_roaming_state(roaming)));
	blobmsg_add_string(&reply, "source_trigger_dbm", threshold);
	blobmsg_add_string(&reply, "improvement_margin_db", margin);
	blobmsg_add_u32(&reply, "client_count", (uint32_t)roaming->client_count);
	blobmsg_add_u64(&reply, "samples", roaming->samples);
	blobmsg_add_u64(&reply, "gate_passes", roaming->gate_passes);
	blobmsg_add_u64(&reply, "requests_sent", roaming->requests_sent);
	blobmsg_add_u64(&reply, "request_failures", roaming->request_failures);
	blobmsg_add_u64(&reply, "fallback_btm_sent",
		roaming->fallback_btm_sent);
	blobmsg_add_u64(&reply, "force_disconnects",
		roaming->force_disconnects);
	blobmsg_add_u64(&reply, "force_failures", roaming->force_failures);
	blobmsg_add_u64(&reply, "beacon_requests_sent",
			roaming->beacon_requests_sent);
	blobmsg_add_u64(&reply, "beacon_request_failures",
			roaming->beacon_request_failures);
	blobmsg_add_u64(&reply, "beacon_reports_received",
			roaming->beacon_reports_received);
	blobmsg_add_u64(&reply, "beacon_report_timeouts",
			roaming->beacon_report_timeouts);
	blobmsg_add_u64(&reply, "btm_responses", roaming->btm_responses);
	blobmsg_add_u64(&reply, "btm_accepted", roaming->btm_accepted);
	blobmsg_add_u64(&reply, "btm_rejected", roaming->btm_rejected);
	blobmsg_add_u64(&reply, "btm_response_timeouts",
			roaming->btm_response_timeouts);
	blobmsg_add_u32(&reply, "neighbor_count",
			(uint32_t)roaming->neighbor_count);
	blobmsg_add_u8(&reply, "neighbor_list_truncated",
			roaming->neighbor_overflow);
	if (roaming->last_signal_seen) {
		snprintf(signal, sizeof(signal), "%d",
			 roaming->last_source_signal_dbm);
		blobmsg_add_string(&reply, "last_source_signal_dbm", signal);
	}
	if (roaming->last_target_seen) {
		snprintf(target_signal, sizeof(target_signal), "%d",
			 roaming->last_target_signal_dbm);
		snprintf(target_margin, sizeof(target_margin), "%d",
			 roaming->last_target_margin_db);
		blobmsg_add_string(&reply, "last_target_signal_dbm", target_signal);
		blobmsg_add_string(&reply, "last_target_margin_db", target_margin);
		target_age_ms = now >= roaming->last_target_observed_ms ?
				 now - roaming->last_target_observed_ms : 0;
		blobmsg_add_u64(&reply, "last_target_age_ms", target_age_ms);
	}
	if (roaming->last_btm_status_seen) {
		snprintf(btm_status, sizeof(btm_status), "%u",
			 (unsigned int)roaming->last_btm_status_code);
		blobmsg_add_string(&reply, "last_btm_status_code", btm_status);
	}
	blobmsg_add_string(&reply, "last_reason",
			   wmcs_roaming_last_reason(roaming));
	return ubus_send_reply(ctx, request, reply.head);
}

static bool discovery_request_valid(struct blob_attr *message)
{
	struct blob_attr *attribute;
	bool duration_seen = false;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining) {
		if (!blobmsg_check_attr_len(attribute, true, blob_pad_len(attribute)) ||
		    strcmp(blobmsg_name(attribute), "duration_seconds") ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_INT32 || duration_seen)
			return false;
		duration_seen = true;
	}

	return remaining == 0;
}

static bool pairing_start_request_valid(struct blob_attr *message)
{
	struct blob_attr *attribute;
	bool duration_seen = false;
	bool candidate_seen = false;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining) {
		const char *name;

		if (!blobmsg_check_attr_len(attribute, true, blob_pad_len(attribute)))
			return false;
		name = blobmsg_name(attribute);
		if (!strcmp(name, "duration_seconds")) {
			if (duration_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_INT32)
				return false;
			duration_seen = true;
		} else if (!strcmp(name, "candidate_id")) {
			if (candidate_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
			    strlen(blobmsg_get_string(attribute)) !=
				    WMCS_DISCOVERY_NODE_ID_SIZE - 1U)
				return false;
			candidate_seen = true;
		} else {
			return false;
		}
	}
	return remaining == 0;
}

static bool pairing_confirm_request_valid(struct blob_attr *message)
{
	struct blob_attr *attribute;
	bool sas_seen = false;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining) {
		if (!blobmsg_check_attr_len(attribute, true, blob_pad_len(attribute)) ||
		    strcmp(blobmsg_name(attribute), "sas") || sas_seen ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
		    strlen(blobmsg_get_string(attribute)) != WMCS_PAIRING_SAS_SIZE - 1U)
			return false;
		sas_seen = true;
	}
	return remaining == 0 && sas_seen;
}

static bool peer_id_valid(const char *peer_id)
{
	size_t i;

	if (!peer_id || strlen(peer_id) != WMCS_IDENTITY_PEER_ID_SIZE)
		return false;
	for (i = 0; i < WMCS_IDENTITY_PEER_ID_SIZE; i++) {
		if (!((peer_id[i] >= '0' && peer_id[i] <= '9') ||
		      (peer_id[i] >= 'a' && peer_id[i] <= 'f')))
			return false;
	}
	return true;
}

static bool wlan_sync_request_valid(struct blob_attr *message)
{
	struct blob_attr *attribute;
	bool duration_seen = false;
	bool candidate_seen = false;
	bool peer_seen = false;
	bool dry_run_seen = false;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining) {
		const char *name;

		if (!blobmsg_check_attr_len(attribute, true,
					    blob_pad_len(attribute)))
			return false;
		name = blobmsg_name(attribute);
		if (!strcmp(name, "duration_seconds")) {
			if (duration_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_INT32)
				return false;
			duration_seen = true;
		} else if (!strcmp(name, "candidate_id")) {
			if (candidate_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
			    strlen(blobmsg_get_string(attribute)) !=
				    WMCS_DISCOVERY_NODE_ID_SIZE - 1U)
				return false;
			candidate_seen = true;
		} else if (!strcmp(name, "peer_id")) {
			if (peer_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
			    !peer_id_valid(blobmsg_get_string(attribute)))
				return false;
			peer_seen = true;
		} else if (!strcmp(name, "dry_run")) {
			if (dry_run_seen ||
			    blobmsg_type(attribute) != BLOBMSG_TYPE_BOOL)
				return false;
			dry_run_seen = true;
		} else {
			return false;
		}
	}
	return remaining == 0 && candidate_seen && peer_seen;
}

static bool forget_request_valid(struct blob_attr *message)
{
	struct blob_attr *attribute;
	bool peer_seen = false;
	size_t remaining;

	blobmsg_for_each_attr(attribute, message, remaining) {
		if (!blobmsg_check_attr_len(attribute, true,
					    blob_pad_len(attribute)) ||
		    strcmp(blobmsg_name(attribute), "peer_id") || peer_seen ||
		    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
		    !peer_id_valid(blobmsg_get_string(attribute)))
			return false;
		peer_seen = true;
	}
	return remaining == 0 && peer_seen;
}

static int status(struct ubus_context *ctx, struct ubus_object *object,
		  struct ubus_request_data *request, const char *method,
		  struct blob_attr *message)
{
	uint64_t now = wmcs_monotonic_ms();
	uint64_t uptime = 0;
	char roaming_source_trigger_dbm[8];

	(void)object;
	(void)method;

	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;

	if (active_runtime && now >= active_runtime->started_ms)
		uptime = now - active_runtime->started_ms;
	snprintf(roaming_source_trigger_dbm,
		 sizeof(roaming_source_trigger_dbm), "%d",
		 active_runtime ? active_runtime->roaming_source_trigger_dbm : -68);

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_string(&reply, "daemon_version", WMCS_VERSION);
	blobmsg_add_string(&reply, "role", active_runtime ? active_runtime->role : "standalone");
	blobmsg_add_string(&reply, "state",
			   active_runtime && active_runtime->degraded ?
				   "degraded" :
			   active_runtime && active_runtime->mutation_enabled ?
				   "transactional" : "observe_and_pair");
	blobmsg_add_u8(&reply, "mutation_enabled",
			 active_runtime && active_runtime->mutation_enabled);
	blobmsg_add_u8(&reply, "neighbor_sync_enabled",
			 active_runtime && active_runtime->neighbor_sync_enabled);
	blobmsg_add_u8(&reply, "mutation_available",
			 active_runtime && active_runtime->mutation_enabled &&
			 !active_runtime->degraded);
	blobmsg_add_u8(&reply, "ubus_connected",
			 active_runtime && active_runtime->ubus_connected);
	blobmsg_add_u8(&reply, "degraded",
			 active_runtime && active_runtime->degraded);
	if (active_runtime && active_runtime->degraded_reason[0])
		blobmsg_add_string(&reply, "degraded_reason",
				   active_runtime->degraded_reason);
	blobmsg_add_string(&reply, "roaming_source_trigger_dbm",
			   roaming_source_trigger_dbm);
	blobmsg_add_u8(&reply, "discovery_active",
			active_runtime &&
			wmcs_discovery_active(active_runtime->discovery));
	blobmsg_add_u8(&reply, "pairing_active",
			active_runtime && active_runtime->pairing &&
			wmcs_pairing_active(active_runtime->pairing));
	blobmsg_add_string(&reply, "pairing_state",
			   active_runtime && active_runtime->pairing ?
				   wmcs_pairing_state_name(wmcs_pairing_state(
					   active_runtime->pairing)) : "idle");
	blobmsg_add_u8(&reply, "control_active",
			active_runtime && active_runtime->control &&
			wmcs_control_active(active_runtime->control));
	blobmsg_add_string(&reply, "control_state",
			   active_runtime && active_runtime->control ?
				   wmcs_control_state_name(wmcs_control_state(
					   active_runtime->control)) : "idle");
	blobmsg_add_string(&reply, "control_operation",
			   active_runtime && active_runtime->control ?
				   wmcs_control_operation_name(wmcs_control_operation(
					   active_runtime->control)) : "none");
	blobmsg_add_u8(&reply, "control_reconciliation_pending",
			 active_runtime && active_runtime->control &&
			 wmcs_control_reconciliation_pending(
				 active_runtime->control));
	blobmsg_add_u8(&reply, "identity_ready",
			active_runtime && active_runtime->identity &&
			wmcs_identity_ready(active_runtime->identity));
	blobmsg_add_u32(&reply, "paired_peer_count",
			active_runtime && active_runtime->identity ?
				(uint32_t)wmcs_identity_peer_count(
					active_runtime->identity) : 0);
	blobmsg_add_u32(&reply, "active_controller_count",
			active_runtime && active_runtime->identity ?
				(uint32_t)wmcs_identity_active_controller_count(
					active_runtime->identity) : 0);
	blobmsg_add_u64(&reply, "uptime_ms", uptime);
	blobmsg_add_u64(&reply, "generation", current_generation());

	return ubus_send_reply(ctx, request, reply.head);
}

static int capabilities(struct ubus_context *ctx, struct ubus_object *object,
			struct ubus_request_data *request, const char *method,
			struct blob_attr *message)
{
	void *features;
	void *platform;

	(void)object;
	(void)method;

	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());

	features = blobmsg_open_table(&reply, "features");
	blobmsg_add_u8(&reply, "read_only_inventory", true);
	blobmsg_add_u8(&reply, "native_protocol",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "discovery",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "persistent_identity",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "pairing",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "authenticated_control",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "transactions",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "one_wlan_sync",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "ownership_safe_release",
			active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "local_forget",
			 active_runtime && strcmp(active_runtime->role, "standalone"));
	blobmsg_add_u8(&reply, "durable_controller_operation",
			 active_runtime && !strcmp(active_runtime->role, "controller"));
	blobmsg_add_u8(&reply, "degraded_status", true);
	blobmsg_add_u8(&reply, "ubus_reconnect", true);
	blobmsg_add_u8(&reply, "roaming", false);
	blobmsg_add_u8(&reply, "wireless_backhaul", false);
	blobmsg_close_table(&reply, features);

	platform = blobmsg_open_table(&reply, "platform");
	blobmsg_add_u8(&reply, "ubus", true);
	blobmsg_add_u8(&reply, "uci", executable("/sbin/uci", "/usr/bin/uci"));
	blobmsg_add_u8(&reply, "hostapd", executable("/usr/sbin/hostapd", NULL));
	blobmsg_add_u8(&reply, "iw", executable("/usr/sbin/iw", "/usr/bin/iw"));
	blobmsg_close_table(&reply, platform);

	return ubus_send_reply(ctx, request, reply.head);
}

static int nodes(struct ubus_context *ctx, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	const struct wmcs_discovery_node *nodes = NULL;
	uint64_t now = wmcs_monotonic_ms();
	size_t count = 0;
	size_t i;
	void *array;

	(void)object;
	(void)method;

	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	array = blobmsg_open_array(&reply, "nodes");
	if (active_runtime)
		nodes = wmcs_discovery_nodes(active_runtime->discovery, &count);
	for (i = 0; nodes && i < count; i++) {
		char identifier[WMCS_DISCOVERY_NODE_ID_SIZE];
		uint64_t age = now >= nodes[i].last_seen_ms ? now - nodes[i].last_seen_ms : 0;
		void *entry;

		if (!wmcs_discovery_node_id(&nodes[i], identifier))
			continue;
		entry = blobmsg_open_table(&reply, NULL);

		blobmsg_add_string(&reply, "id", identifier);
		blobmsg_add_string(&reply, "role", "agent");
		blobmsg_add_string(&reply, "state", "discovered");
		blobmsg_add_string(&reply, "identity_trust", "ephemeral_untrusted");
		blobmsg_add_string(&reply, "transport", "native_udp_v0");
		blobmsg_add_string(&reply, "address", nodes[i].address);
		blobmsg_add_u64(&reply, "last_seen_age_ms", age);
		blobmsg_add_string(&reply, "reason", "native_v0_announcement");
		blobmsg_close_table(&reply, entry);
	}
	blobmsg_close_array(&reply, array);

	return ubus_send_reply(ctx, request, reply.head);
}

enum {
	DISCOVERY_DURATION,
	__DISCOVERY_MAX,
};

static const struct blobmsg_policy discovery_policy[__DISCOVERY_MAX] = {
	[DISCOVERY_DURATION] = {
		.name = "duration_seconds",
		.type = BLOBMSG_TYPE_INT32,
	},
};

static int discovery_start(struct ubus_context *ctx, struct ubus_object *object,
			   struct ubus_request_data *request, const char *method,
			   struct blob_attr *message)
{
	struct blob_attr *attributes[__DISCOVERY_MAX] = {0};
	uint32_t duration = 60;
	int result;

	(void)object;
	(void)method;

	if (!active_runtime || !active_runtime->discovery)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!discovery_request_valid(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (active_runtime->pairing &&
	    wmcs_pairing_active(active_runtime->pairing))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (active_runtime->control &&
	    wmcs_control_active(active_runtime->control))
		return UBUS_STATUS_INVALID_ARGUMENT;

	blobmsg_parse(discovery_policy, __DISCOVERY_MAX, attributes,
		      message ? blob_data(message) : NULL,
		      message ? blob_len(message) : 0);
	if (attributes[DISCOVERY_DURATION])
		duration = blobmsg_get_u32(attributes[DISCOVERY_DURATION]);

	result = wmcs_discovery_start(active_runtime->discovery, duration);
	if (result == -EINVAL)
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (result == -EOPNOTSUPP)
		return UBUS_STATUS_NOT_SUPPORTED;
	if (result == -EALREADY)
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (result)
		return UBUS_STATUS_UNKNOWN_ERROR;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", true);
	blobmsg_add_u32(&reply, "duration_seconds", duration);
	blobmsg_add_string(&reply, "interface",
			   wmcs_discovery_interface(active_runtime->discovery));

	return ubus_send_reply(ctx, request, reply.head);
}

static int discovery_stop(struct ubus_context *ctx, struct ubus_object *object,
			  struct ubus_request_data *request, const char *method,
			  struct blob_attr *message)
{
	(void)object;
	(void)method;

	if (!active_runtime || !active_runtime->discovery)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;

	wmcs_discovery_stop(active_runtime->discovery);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", false);

	return ubus_send_reply(ctx, request, reply.head);
}

static int identity_info(struct ubus_context *ctx, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message)
{
	char fingerprint[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	bool ready;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->identity)
		return UBUS_STATUS_UNKNOWN_ERROR;
	ready = wmcs_identity_ready(active_runtime->identity);
	if (ready && wmcs_identity_fingerprint(
			     wmcs_identity_public_key(active_runtime->identity),
			     fingerprint))
		return UBUS_STATUS_UNKNOWN_ERROR;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "ready", ready);
	blobmsg_add_string(&reply, "algorithm", "p256-ecdsa-sha256");
	if (ready)
		blobmsg_add_string(&reply, "fingerprint", fingerprint);
	blobmsg_add_u32(&reply, "paired_peer_count",
			(uint32_t)wmcs_identity_peer_count(active_runtime->identity));
	blobmsg_add_u32(&reply, "active_controller_count",
			(uint32_t)wmcs_identity_active_controller_count(
				active_runtime->identity));
	return ubus_send_reply(ctx, request, reply.head);
}

static const char *peer_role_name(enum wmcs_peer_role role)
{
	return role == WMCS_PEER_ROLE_CONTROLLER ? "controller" : "agent";
}

static const char *peer_state_name(enum wmcs_peer_state state)
{
	return state == WMCS_PEER_STATE_RELEASED ? "released" : "active";
}

static int peers(struct ubus_context *ctx, struct ubus_object *object,
		 struct ubus_request_data *request, const char *method,
		 struct blob_attr *message)
{
	struct wmcs_peer_info entries[WMCS_IDENTITY_MAX_PEERS];
	size_t count;
	size_t i;
	void *array;
	int result;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->identity)
		return UBUS_STATUS_UNKNOWN_ERROR;
	result = wmcs_identity_list_peers(active_runtime->identity, entries,
					  ARRAY_SIZE(entries), &count);
	if (result)
		return UBUS_STATUS_UNKNOWN_ERROR;
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	array = blobmsg_open_array(&reply, "peers");
	for (i = 0; i < count; i++) {
		void *entry = blobmsg_open_table(&reply, NULL);

		blobmsg_add_string(&reply, "peer_id", entries[i].peer_id);
		blobmsg_add_string(&reply, "role", peer_role_name(entries[i].role));
		blobmsg_add_string(&reply, "state", peer_state_name(entries[i].state));
		blobmsg_add_u64(&reply, "generation", entries[i].generation);
		blobmsg_close_table(&reply, entry);
	}
	blobmsg_close_array(&reply, array);
	wmcs_secure_zero(entries, sizeof(entries));
	return ubus_send_reply(ctx, request, reply.head);
}

enum {
	PAIRING_DURATION,
	PAIRING_CANDIDATE,
	__PAIRING_START_MAX,
};

static const struct blobmsg_policy pairing_start_policy[__PAIRING_START_MAX] = {
	[PAIRING_DURATION] = {
		.name = "duration_seconds",
		.type = BLOBMSG_TYPE_INT32,
	},
	[PAIRING_CANDIDATE] = {
		.name = "candidate_id",
		.type = BLOBMSG_TYPE_STRING,
	},
};

enum {
	PAIRING_SAS,
	__PAIRING_CONFIRM_MAX,
};

static const struct blobmsg_policy pairing_confirm_policy[__PAIRING_CONFIRM_MAX] = {
	[PAIRING_SAS] = {
		.name = "sas",
		.type = BLOBMSG_TYPE_STRING,
	},
};

static int pairing_result_status(int result)
{
	if (!result)
		return UBUS_STATUS_OK;
	if (result == -EINVAL || result == -EALREADY || result == -EEXIST)
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (result == -EBUSY || result == -ESTALE)
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (result == -EOPNOTSUPP)
		return UBUS_STATUS_NOT_SUPPORTED;
	if (result == -ENOENT)
		return UBUS_STATUS_NOT_FOUND;
	if (result == -EKEYREJECTED)
		return UBUS_STATUS_PERMISSION_DENIED;
	return UBUS_STATUS_UNKNOWN_ERROR;
}

static int pairing_status(struct ubus_context *ctx, struct ubus_object *object,
			  struct ubus_request_data *request, const char *method,
			  struct blob_attr *message)
{
	enum wmcs_pairing_state state;
	char fingerprint[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	bool identity_ready;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->pairing ||
	    !active_runtime->identity)
		return UBUS_STATUS_UNKNOWN_ERROR;
	state = wmcs_pairing_state(active_runtime->pairing);
	identity_ready = wmcs_identity_ready(active_runtime->identity);
	if (identity_ready && wmcs_identity_fingerprint(
				  wmcs_identity_public_key(active_runtime->identity),
				  fingerprint))
		return UBUS_STATUS_UNKNOWN_ERROR;

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active",
			wmcs_pairing_active(active_runtime->pairing));
	blobmsg_add_string(&reply, "state", wmcs_pairing_state_name(state));
	if (identity_ready)
		blobmsg_add_string(&reply, "local_fingerprint", fingerprint);
	if (state == WMCS_PAIRING_AWAITING_CONFIRMATION) {
		blobmsg_add_string(&reply, "sas",
				   wmcs_pairing_sas_value(active_runtime->pairing));
		blobmsg_add_string(&reply, "pending_peer_id",
				   wmcs_pairing_peer_id(active_runtime->pairing));
	}
	return ubus_send_reply(ctx, request, reply.head);
}

static int pairing_start(struct ubus_context *ctx, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message)
{
	struct blob_attr *attributes[__PAIRING_START_MAX] = {0};
	const char *candidate = NULL;
	char address[16];
	uint32_t duration = 120;
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->pairing ||
	    !active_runtime->discovery || !active_runtime->identity)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!pairing_start_request_valid(message) ||
	    wmcs_discovery_active(active_runtime->discovery) ||
	    (active_runtime->control &&
	     (wmcs_control_active(active_runtime->control) ||
	      wmcs_control_reconciliation_pending(
		      active_runtime->control))))
		return UBUS_STATUS_INVALID_ARGUMENT;
	blobmsg_parse(pairing_start_policy, __PAIRING_START_MAX, attributes,
		      message ? blob_data(message) : NULL,
		      message ? blob_len(message) : 0);
	if (attributes[PAIRING_DURATION])
		duration = blobmsg_get_u32(attributes[PAIRING_DURATION]);
	if (attributes[PAIRING_CANDIDATE])
		candidate = blobmsg_get_string(attributes[PAIRING_CANDIDATE]);

	if (!strcmp(active_runtime->role, "controller")) {
		if (!candidate)
			return UBUS_STATUS_INVALID_ARGUMENT;
		if (!wmcs_discovery_find_node(active_runtime->discovery, candidate,
					      address))
			return UBUS_STATUS_NOT_FOUND;
		result = wmcs_pairing_start_controller(active_runtime->pairing,
						       address, duration);
	} else if (!strcmp(active_runtime->role, "agent")) {
		if (candidate)
			return UBUS_STATUS_INVALID_ARGUMENT;
		result = wmcs_pairing_start_agent(active_runtime->pairing, duration);
	} else {
		return UBUS_STATUS_NOT_SUPPORTED;
	}
	if (result)
		return pairing_result_status(result);

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", true);
	blobmsg_add_string(&reply, "state", "exchanging");
	blobmsg_add_u32(&reply, "duration_seconds", duration);
	return ubus_send_reply(ctx, request, reply.head);
}

static int pairing_confirm(struct ubus_context *ctx, struct ubus_object *object,
			   struct ubus_request_data *request, const char *method,
			   struct blob_attr *message)
{
	struct blob_attr *attributes[__PAIRING_CONFIRM_MAX] = {0};
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!pairing_confirm_request_valid(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	blobmsg_parse(pairing_confirm_policy, __PAIRING_CONFIRM_MAX, attributes,
		      blob_data(message), blob_len(message));
	result = wmcs_pairing_confirm(active_runtime->pairing,
				      blobmsg_get_string(attributes[PAIRING_SAS]),
				      peer_id);
	if (result)
		return pairing_result_status(result);

	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "paired", true);
	blobmsg_add_string(&reply, "peer_id", peer_id);
	return ubus_send_reply(ctx, request, reply.head);
}

static int pairing_stop(struct ubus_context *ctx, struct ubus_object *object,
			struct ubus_request_data *request, const char *method,
			struct blob_attr *message)
{
	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	wmcs_pairing_stop(active_runtime->pairing);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", false);
	return ubus_send_reply(ctx, request, reply.head);
}

enum {
	CONTROL_DURATION,
	__CONTROL_LISTEN_MAX,
};

static const struct blobmsg_policy control_listen_policy[__CONTROL_LISTEN_MAX] = {
	[CONTROL_DURATION] = {
		.name = "duration_seconds",
		.type = BLOBMSG_TYPE_INT32,
	},
};

enum {
	SYNC_DURATION,
	SYNC_CANDIDATE,
	SYNC_PEER,
	SYNC_DRY_RUN,
	__SYNC_MAX,
};

static const struct blobmsg_policy wlan_sync_policy[__SYNC_MAX] = {
	[SYNC_DURATION] = {
		.name = "duration_seconds",
		.type = BLOBMSG_TYPE_INT32,
	},
	[SYNC_CANDIDATE] = {
		.name = "candidate_id",
		.type = BLOBMSG_TYPE_STRING,
	},
	[SYNC_PEER] = {
		.name = "peer_id",
		.type = BLOBMSG_TYPE_STRING,
	},
	[SYNC_DRY_RUN] = {
		.name = "dry_run",
		.type = BLOBMSG_TYPE_BOOL,
	},
};

static int control_result_status(int result)
{
	if (!result)
		return UBUS_STATUS_OK;
	if (result == -EINVAL || result == -EALREADY || result == -EBUSY ||
	    result == -ESTALE)
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (result == -EOPNOTSUPP)
		return UBUS_STATUS_NOT_SUPPORTED;
	if (result == -ENOENT || result == -ENOKEY)
		return UBUS_STATUS_NOT_FOUND;
	if (result == -EACCES || result == -EKEYREJECTED)
		return UBUS_STATUS_PERMISSION_DENIED;
	return UBUS_STATUS_UNKNOWN_ERROR;
}

static int control_listen(struct ubus_context *ctx, struct ubus_object *object,
			  struct ubus_request_data *request, const char *method,
			  struct blob_attr *message)
{
	struct blob_attr *attributes[__CONTROL_LISTEN_MAX] = {0};
	uint32_t duration = 30;
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->control ||
	    !active_runtime->discovery || !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!discovery_request_valid(message) ||
	    wmcs_discovery_active(active_runtime->discovery) ||
	    wmcs_pairing_active(active_runtime->pairing))
		return UBUS_STATUS_INVALID_ARGUMENT;
	blobmsg_parse(control_listen_policy, __CONTROL_LISTEN_MAX, attributes,
		      message ? blob_data(message) : NULL,
		      message ? blob_len(message) : 0);
	if (attributes[CONTROL_DURATION])
		duration = blobmsg_get_u32(attributes[CONTROL_DURATION]);
	result = wmcs_control_listen(active_runtime->control, duration);
	if (result)
		return control_result_status(result);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", true);
	blobmsg_add_string(&reply, "state", "listening");
	blobmsg_add_u32(&reply, "duration_seconds", duration);
	return ubus_send_reply(ctx, request, reply.head);
}

static int wlan_sync_start(struct ubus_context *ctx, struct ubus_object *object,
			   struct ubus_request_data *request, const char *method,
			   struct blob_attr *message)
{
	struct blob_attr *attributes[__SYNC_MAX] = {0};
	const char *candidate;
	const char *peer_id;
	char address[16];
	uint32_t duration = 30;
	bool dry_run = true;
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->control ||
	    !active_runtime->discovery || !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!wlan_sync_request_valid(message) ||
	    wmcs_discovery_active(active_runtime->discovery) ||
	    wmcs_pairing_active(active_runtime->pairing))
		return UBUS_STATUS_INVALID_ARGUMENT;
	blobmsg_parse(wlan_sync_policy, __SYNC_MAX, attributes,
		      blob_data(message), blob_len(message));
	if (attributes[SYNC_DURATION])
		duration = blobmsg_get_u32(attributes[SYNC_DURATION]);
	if (attributes[SYNC_DRY_RUN])
		dry_run = blobmsg_get_bool(attributes[SYNC_DRY_RUN]);
	candidate = blobmsg_get_string(attributes[SYNC_CANDIDATE]);
	peer_id = blobmsg_get_string(attributes[SYNC_PEER]);
	if (!wmcs_discovery_find_node(active_runtime->discovery, candidate,
				      address))
		return UBUS_STATUS_NOT_FOUND;
	result = wmcs_control_sync_start(active_runtime->control, address, peer_id,
					 dry_run, duration);
	if (result)
		return control_result_status(result);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", true);
	blobmsg_add_string(&reply, "state", "waiting_result");
	blobmsg_add_string(&reply, "operation", "wlan_sync");
	blobmsg_add_u8(&reply, "dry_run", dry_run);
	blobmsg_add_u32(&reply, "duration_seconds", duration);
	return ubus_send_reply(ctx, request, reply.head);
}

static int release_start(struct ubus_context *ctx, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message)
{
	struct blob_attr *attributes[__SYNC_MAX] = {0};
	const char *candidate;
	const char *peer_id;
	char address[16];
	uint32_t duration = 30;
	bool dry_run = true;
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->control ||
	    !active_runtime->discovery || !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!wlan_sync_request_valid(message) ||
	    wmcs_discovery_active(active_runtime->discovery) ||
	    wmcs_pairing_active(active_runtime->pairing))
		return UBUS_STATUS_INVALID_ARGUMENT;
	blobmsg_parse(wlan_sync_policy, __SYNC_MAX, attributes,
		      blob_data(message), blob_len(message));
	if (attributes[SYNC_DURATION])
		duration = blobmsg_get_u32(attributes[SYNC_DURATION]);
	if (attributes[SYNC_DRY_RUN])
		dry_run = blobmsg_get_bool(attributes[SYNC_DRY_RUN]);
	candidate = blobmsg_get_string(attributes[SYNC_CANDIDATE]);
	peer_id = blobmsg_get_string(attributes[SYNC_PEER]);
	if (!wmcs_discovery_find_node(active_runtime->discovery, candidate,
				      address))
		return UBUS_STATUS_NOT_FOUND;
	result = wmcs_control_release_start(active_runtime->control, address,
					    peer_id, dry_run, duration);
	if (result)
		return control_result_status(result);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", true);
	blobmsg_add_string(&reply, "state", "waiting_result");
	blobmsg_add_string(&reply, "operation", "release");
	blobmsg_add_u8(&reply, "dry_run", dry_run);
	blobmsg_add_u32(&reply, "duration_seconds", duration);
	return ubus_send_reply(ctx, request, reply.head);
}

static int wlan_sync_status(struct ubus_context *ctx, struct ubus_object *object,
			    struct ubus_request_data *request, const char *method,
			    struct blob_attr *message)
{
	struct wmcs_control *control;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->control)
		return UBUS_STATUS_UNKNOWN_ERROR;
	control = active_runtime->control;
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", wmcs_control_active(control));
	blobmsg_add_string(&reply, "state",
			   wmcs_control_state_name(wmcs_control_state(control)));
	blobmsg_add_string(&reply, "operation",
			   wmcs_control_operation_name(
				   wmcs_control_operation(control)));
	if (wmcs_control_peer_id(control)[0])
		blobmsg_add_string(&reply, "peer_id",
				   wmcs_control_peer_id(control));
	if (wmcs_control_sequence(control))
		blobmsg_add_u64(&reply, "sequence",
				wmcs_control_sequence(control));
	if (wmcs_control_operation(control) != WMCS_CONTROL_OPERATION_NONE)
		blobmsg_add_u8(&reply, "dry_run", control->request_dry_run);
	blobmsg_add_u8(&reply, "reconciliation_pending",
			 wmcs_control_reconciliation_pending(control));
	if (wmcs_control_state(control) == WMCS_CONTROL_COMPLETE) {
		blobmsg_add_string(&reply, "outcome",
				   wmcs_control_outcome_name(
					   wmcs_control_outcome(control)));
		blobmsg_add_string(&reply, "reason",
				   wmcs_control_reason_name(
					   wmcs_control_reason(control)));
	}
	return ubus_send_reply(ctx, request, reply.head);
}

enum {
	FORGET_PEER,
	__FORGET_MAX,
};

static const struct blobmsg_policy forget_policy[__FORGET_MAX] = {
	[FORGET_PEER] = {
		.name = "peer_id",
		.type = BLOBMSG_TYPE_STRING,
	},
};

static int forget_orphan(struct ubus_context *ctx, struct ubus_object *object,
			 struct ubus_request_data *request, const char *method,
			 struct blob_attr *message)
{
	struct blob_attr *attributes[__FORGET_MAX] = {0};
	const char *peer_id;
	int result;

	(void)object;
	(void)method;
	if (!active_runtime || !active_runtime->control ||
	    !active_runtime->identity || !active_runtime->discovery ||
	    !active_runtime->pairing)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	if (!forget_request_valid(message) ||
	    wmcs_discovery_active(active_runtime->discovery) ||
	    wmcs_pairing_active(active_runtime->pairing) ||
	    wmcs_control_active(active_runtime->control))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime->mutation_enabled)
		return UBUS_STATUS_PERMISSION_DENIED;
	blobmsg_parse(forget_policy, __FORGET_MAX, attributes,
		      blob_data(message), blob_len(message));
	peer_id = blobmsg_get_string(attributes[FORGET_PEER]);
	result = wmcs_control_forget_peer(active_runtime->control, peer_id);
	if (result)
		return control_result_status(result);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "forgotten", true);
	blobmsg_add_string(&reply, "peer_id", peer_id);
	return ubus_send_reply(ctx, request, reply.head);
}

static int wlan_sync_stop(struct ubus_context *ctx, struct ubus_object *object,
			  struct ubus_request_data *request, const char *method,
			  struct blob_attr *message)
{
	int result;

	(void)object;
	(void)method;
	if (!request_empty(message))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!active_runtime || !active_runtime->control)
		return UBUS_STATUS_UNKNOWN_ERROR;
	if (!runtime_mutation_allowed())
		return UBUS_STATUS_PERMISSION_DENIED;
	result = wmcs_control_stop(active_runtime->control);
	if (result)
		return control_result_status(result);
	blob_buf_init(&reply, 0);
	blobmsg_add_u32(&reply, "api_version", WMCS_API_VERSION);
	blobmsg_add_u64(&reply, "generation", current_generation());
	blobmsg_add_u8(&reply, "active", false);
	blobmsg_add_string(&reply, "state",
			   wmcs_control_state_name(wmcs_control_state(
				   active_runtime->control)));
	blobmsg_add_u8(&reply, "reconciliation_pending",
			 wmcs_control_reconciliation_pending(
				 active_runtime->control));
	return ubus_send_reply(ctx, request, reply.head);
}

static const struct ubus_method methods[] = {
	UBUS_METHOD_NOARG("status", status),
	UBUS_METHOD_NOARG("roaming_status", roaming_status),
	UBUS_METHOD_NOARG("capabilities", capabilities),
	UBUS_METHOD_NOARG("nodes", nodes),
	UBUS_METHOD_NOARG("identity", identity_info),
	UBUS_METHOD_NOARG("peers", peers),
	UBUS_METHOD("discovery_start", discovery_start, discovery_policy),
	UBUS_METHOD_NOARG("discovery_stop", discovery_stop),
	UBUS_METHOD("pairing_start", pairing_start, pairing_start_policy),
	UBUS_METHOD_NOARG("pairing_status", pairing_status),
	UBUS_METHOD("pairing_confirm", pairing_confirm, pairing_confirm_policy),
	UBUS_METHOD_NOARG("pairing_stop", pairing_stop),
	UBUS_METHOD("control_listen", control_listen, control_listen_policy),
	UBUS_METHOD("wlan_sync_start", wlan_sync_start, wlan_sync_policy),
	UBUS_METHOD_NOARG("wlan_sync_status", wlan_sync_status),
	UBUS_METHOD_NOARG("wlan_sync_stop", wlan_sync_stop),
	UBUS_METHOD("release_start", release_start, wlan_sync_policy),
	UBUS_METHOD_NOARG("release_status", wlan_sync_status),
	UBUS_METHOD_NOARG("release_stop", wlan_sync_stop),
	UBUS_METHOD("forget_orphan", forget_orphan, forget_policy),
};

static struct ubus_object_type object_type = UBUS_OBJECT_TYPE("wmcs", methods);

static struct ubus_object object = {
	.name = "wmcs",
	.type = &object_type,
	.methods = methods,
	.n_methods = ARRAY_SIZE(methods),
};

int wmcs_ubus_register(struct ubus_context *ctx, const struct wmcs_runtime *runtime)
{
	int error;

	active_runtime = runtime;
	if (object_registered && object.id)
		return 0;
	object_registered = false;
	error = ubus_add_object(ctx, &object);
	if (error) {
		fprintf(stderr, "wmcsd: cannot register ubus object: %s\n", ubus_strerror(error));
		active_runtime = NULL;
	} else {
		object_registered = true;
	}

	return error;
}

void wmcs_ubus_unregister(struct ubus_context *ctx)
{
	if (object_registered && ctx && ctx->sock.fd >= 0 && object.id)
		ubus_remove_object(ctx, &object);
	object_registered = false;
	active_runtime = NULL;
	blob_buf_free(&reply);
}
