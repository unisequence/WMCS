// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_WLAN_H
#define WMCS_WLAN_H

#include <stdbool.h>
#include <stdint.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "control_wire.h"
#include "identity.h"

#define WMCS_WLAN_SECTION "wmcs_home_5g"
#define WMCS_WLAN_VERIFY_TIMEOUT_MS 10000U
#define WMCS_WLAN_VERIFY_INTERVAL_MS 500U

enum wmcs_wlan_async_phase {
	WMCS_WLAN_ASYNC_IDLE = 0,
	WMCS_WLAN_ASYNC_RELOAD,
	WMCS_WLAN_ASYNC_RECONF,
	WMCS_WLAN_ASYNC_VERIFY,
	WMCS_WLAN_ASYNC_ROLLBACK_RELOAD,
	WMCS_WLAN_ASYNC_ROLLBACK_RECONF,
};

struct wmcs_wlan_async;

typedef void (*wmcs_wlan_async_complete_cb)(
	void *private, int status, enum wmcs_control_reason reason,
	bool backup_pending);

struct wmcs_wlan_async {
	struct ubus_context *ubus;
	char state_dir[WMCS_IDENTITY_STATE_DIR_SIZE];
	char target_radio[64];
	char owner_peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	uint64_t sequence;
	struct wmcs_wlan_request request;
	bool release;
	bool active;
	bool rollback_started;
	bool backup_pending;
	bool status_reply_seen;
	char status_ifname[IF_NAMESIZE];
	enum wmcs_wlan_async_phase phase;
	int original_status;
	enum wmcs_control_reason original_reason;
	uint64_t verify_deadline_ms;
	struct uloop_timeout timer;
	struct ubus_request ubus_request;
	bool ubus_pending;
	wmcs_wlan_async_complete_cb complete;
	void *private;
};

int wmcs_wlan_read_source(const char *source_iface, const char *source_radio,
			  struct wmcs_wlan_request *request);
int wmcs_wlan_check_target(const char *target_radio, const char *owner_peer_id,
			   const struct wmcs_wlan_request *request);
int wmcs_wlan_check_release(const char *target_radio,
			    const char *owner_peer_id,
			    bool *managed_present);
int wmcs_wlan_finish(const char *state_dir);
int wmcs_wlan_abort(struct ubus_context *ubus, const char *state_dir,
		    const char *target_radio);
int wmcs_wlan_recover(struct ubus_context *ubus, struct wmcs_identity *identity,
		      const char *state_dir, const char *target_radio);
int wmcs_wlan_peer_has_state(const char *state_dir, const char *peer_id,
			     bool *has_state);
int wmcs_wlan_resolve_hostapd(struct ubus_context *ctx,
			      const char *target_radio, const char *section,
			      char *object, size_t object_size);
int wmcs_wlan_async_init(struct wmcs_wlan_async *operation);
void wmcs_wlan_async_close(struct wmcs_wlan_async *operation);
int wmcs_wlan_async_start_apply(
	struct wmcs_wlan_async *operation, struct ubus_context *ubus,
	const char *state_dir, const char *target_radio, const char *owner_peer_id,
	uint64_t sequence, const struct wmcs_wlan_request *request,
	wmcs_wlan_async_complete_cb complete, void *private);
int wmcs_wlan_async_start_release(
	struct wmcs_wlan_async *operation, struct ubus_context *ubus,
	const char *state_dir, const char *target_radio, const char *owner_peer_id,
	uint64_t sequence, wmcs_wlan_async_complete_cb complete, void *private);
void wmcs_wlan_async_cancel(struct wmcs_wlan_async *operation);
bool wmcs_wlan_async_active(const struct wmcs_wlan_async *operation);

#endif
