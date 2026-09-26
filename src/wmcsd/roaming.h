// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_ROAMING_H
#define WMCS_ROAMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <net/if.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "wlan.h"
#include "neighbor_sync.h"

#define WMCS_ROAMING_OBJECT_SIZE (IF_NAMESIZE + 16U)
#define WMCS_ROAMING_MAX_CLIENTS 32U
#define WMCS_ROAMING_MAX_NEIGHBORS 16U
#define WMCS_ROAMING_NEIGHBOR_SIZE 512U
#define WMCS_ROAMING_NEIGHBOR_BYTES_MAX 1024U
#define WMCS_ROAMING_BTM_RESPONSE_TIMEOUT_MS 5000U
#define WMCS_ROAMING_BEACON_TIMEOUT_MS 2500U
#define WMCS_ROAMING_TARGET_FRESH_MS 10000U
#define WMCS_ROAMING_SURVEY_RETRY_MS 30000U
#define WMCS_ROAMING_FORCE_DELAY_MS 10000U
#define WMCS_ROAMING_FORCE_COOLDOWN_MS 300000U
#define WMCS_ROAMING_FORCE_BAN_MS 5000U

struct wmcs_roaming_neighbor {
	char report[WMCS_ROAMING_NEIGHBOR_SIZE];
	char bssid[18];
	uint8_t op_class;
	uint8_t channel;
};

struct wmcs_roaming_survey {
	bool active;
	bool pending;
	char station[18];
	struct wmcs_roaming_neighbor candidates[WMCS_ROAMING_MAX_NEIGHBORS];
	size_t candidate_count;
	size_t next_candidate;
	size_t pending_candidate;
	uint64_t deadline_ms;
	bool best_signal_seen;
	int best_signal_dbm;
	size_t best_candidate;
	uint64_t best_observed_ms;
};

enum wmcs_roaming_state {
	WMCS_ROAMING_DISABLED = 0,
	WMCS_ROAMING_WAITING_HOSTAPD,
	WMCS_ROAMING_MONITORING,
};

struct wmcs_roaming_client {
	bool used;
	bool observed;
	bool assoc_seen;
	bool authorized_seen;
	bool signal_seen;
	bool btm;
	bool neighbor_report;
	uint8_t beacon_measurement_modes;
	bool weak;
	char address[18];
	int signal_dbm;
	uint64_t first_seen_ms;
	uint64_t weak_since_ms;
	unsigned int weak_samples;
	bool attempted;
	bool btm_pending;
	bool force_pending;
	uint8_t btm_dialog_token;
	uint64_t btm_deadline_ms;
	uint64_t force_due_ms;
	uint64_t measurement_retry_after_ms;
	char btm_target_bssid[18];
	char btm_source_bssid[18];
	uint8_t btm_target_op_class;
	uint8_t btm_target_channel;
};

struct wmcs_roaming_force_cooldown {
	char address[18];
	uint64_t until_ms;
};

struct wmcs_roaming {
	const char *role;
	const char *source_iface;
	const char *source_radio;
	struct ubus_context *ubus;
	struct ubus_subscriber subscriber;
	struct uloop_timeout poll_timer;
	struct wmcs_neighbor_sync neighbor_sync;
	char hostapd_object[WMCS_ROAMING_OBJECT_SIZE];
	int source_trigger_dbm;
	int improvement_margin_db;
	int force_trigger_dbm;
	bool enabled;
	bool force_after_timeout;
	bool initialized;
	bool hostapd_ready;
	bool subscriber_registered;
	bool hostapd_subscribed;
	bool response_monitor_active;
	bool source_identity_ready;
	char source_ssid[33];
	char source_bssid[18];
	uint32_t subscribed_hostapd_id;
	struct wmcs_roaming_client clients[WMCS_ROAMING_MAX_CLIENTS];
	struct wmcs_roaming_survey survey;
	size_t client_count;
	size_t neighbor_count;
	size_t observed_neighbor_records;
	bool neighbor_overflow;
	uint64_t samples;
	uint64_t gate_passes;
	uint64_t requests_sent;
	uint64_t request_failures;
	uint64_t fallback_btm_sent;
	uint64_t force_disconnects;
	uint64_t force_failures;
	uint64_t beacon_requests_sent;
	uint64_t beacon_request_failures;
	uint64_t beacon_reports_received;
	uint64_t beacon_report_timeouts;
	uint64_t btm_responses;
	uint64_t btm_accepted;
	uint64_t btm_rejected;
	uint64_t btm_response_timeouts;
	uint64_t last_action_ms;
	int last_source_signal_dbm;
	bool last_signal_seen;
	int last_target_signal_dbm;
	int last_target_margin_db;
	bool last_target_seen;
	uint64_t last_target_observed_ms;
	uint8_t next_dialog_token;
	uint8_t last_btm_status_code;
	bool last_btm_status_seen;
	char last_reason[48];
	struct wmcs_roaming_neighbor neighbors[WMCS_ROAMING_MAX_NEIGHBORS];
	struct wmcs_roaming_force_cooldown force_cooldowns[WMCS_ROAMING_MAX_CLIENTS];
};

int wmcs_roaming_init(struct wmcs_roaming *roaming, const char *role,
			      const char *interface, const char *source_iface,
			      const char *source_radio,
			      struct wmcs_identity *identity,
			      struct ubus_context *ubus, bool enabled,
			      bool neighbor_sync_enabled,
			      int source_trigger_dbm, int improvement_margin_db,
			      bool force_after_timeout, int force_trigger_dbm);
void wmcs_roaming_start(struct wmcs_roaming *roaming);
void wmcs_roaming_ubus_disconnected(struct wmcs_roaming *roaming);
void wmcs_roaming_close(struct wmcs_roaming *roaming);
bool wmcs_roaming_enabled(const struct wmcs_roaming *roaming);
bool wmcs_roaming_active(const struct wmcs_roaming *roaming);
enum wmcs_roaming_state wmcs_roaming_state(
	const struct wmcs_roaming *roaming);
const char *wmcs_roaming_state_name(enum wmcs_roaming_state state);
const char *wmcs_roaming_last_reason(const struct wmcs_roaming *roaming);

#endif
