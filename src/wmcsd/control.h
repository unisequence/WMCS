// SPDX-License-Identifier: Apache-2.0

#ifndef WMCS_CONTROL_H
#define WMCS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include <net/if.h>
#include <netinet/in.h>

#include <libubox/uloop.h>
#include <libubus.h>

#include "control_crypto.h"
#include "control_wire.h"
#include "discovery.h"
#include "identity.h"
#include "operation_store.h"
#include "wlan.h"

#define WMCS_CONTROL_MIN_SECONDS 5U
#define WMCS_CONTROL_MAX_SECONDS 300U
#define WMCS_CONTROL_NAME_SIZE 64U

enum wmcs_control_state {
	WMCS_CONTROL_IDLE = 0,
	WMCS_CONTROL_LISTENING,
	WMCS_CONTROL_WAITING_RESULT,
	WMCS_CONTROL_PROCESSING,
	WMCS_CONTROL_COMPLETE,
	WMCS_CONTROL_RECOVERY_PENDING,
	WMCS_CONTROL_FAILED,
	WMCS_CONTROL_TIMED_OUT,
};

enum wmcs_control_operation {
	WMCS_CONTROL_OPERATION_NONE = 0,
	WMCS_CONTROL_OPERATION_WLAN_SYNC,
	WMCS_CONTROL_OPERATION_RELEASE,
};

struct wmcs_control {
	const char *role;
	char interface[IF_NAMESIZE];
	char state_dir[WMCS_IDENTITY_STATE_DIR_SIZE];
	char source_iface[WMCS_CONTROL_NAME_SIZE];
	char source_radio[WMCS_CONTROL_NAME_SIZE];
	char target_radio[WMCS_CONTROL_NAME_SIZE];
	struct wmcs_identity *identity;
	struct wmcs_discovery *discovery;
	struct ubus_context *ubus;
	bool mutation_enabled;
	int fd;
	bool active;
	bool request_dry_run;
	enum wmcs_control_state state;
	enum wmcs_control_operation operation;
	enum wmcs_operation_phase journal_phase;
	struct uloop_fd socket_event;
	struct uloop_timeout deadline;
	struct uloop_timeout retry_timer;
	struct sockaddr_in peer_address;
	char peer_id[WMCS_IDENTITY_PEER_ID_SIZE + 1U];
	uint64_t sequence;
	struct wmcs_control_packet request;
	struct wmcs_control_packet response;
	uint8_t request_wire[WMCS_CONTROL_WIRE_SIZE];
	struct wmcs_wlan_result result;
	struct wmcs_wlan_async wlan;
};

int wmcs_control_init(struct wmcs_control *control, const char *role,
		      const char *interface, const char *state_dir,
		      const char *source_iface, const char *source_radio,
		      const char *target_radio, bool mutation_enabled,
		      struct wmcs_identity *identity,
		      struct wmcs_discovery *discovery,
		      struct ubus_context *ubus);
void wmcs_control_close(struct wmcs_control *control);
int wmcs_control_recover(struct wmcs_control *control);
int wmcs_control_listen(struct wmcs_control *control, uint32_t duration_seconds);
int wmcs_control_sync_start(struct wmcs_control *control, const char *address,
			    const char *peer_id, bool dry_run,
			    uint32_t duration_seconds);
int wmcs_control_release_start(struct wmcs_control *control, const char *address,
			       const char *peer_id, bool dry_run,
			       uint32_t duration_seconds);
int wmcs_control_stop(struct wmcs_control *control);
int wmcs_control_forget_peer(struct wmcs_control *control,
			     const char *peer_id);
bool wmcs_control_active(const struct wmcs_control *control);
bool wmcs_control_reconciliation_pending(const struct wmcs_control *control);
enum wmcs_control_state wmcs_control_state(const struct wmcs_control *control);
const char *wmcs_control_state_name(enum wmcs_control_state state);
enum wmcs_control_operation wmcs_control_operation(
	const struct wmcs_control *control);
const char *wmcs_control_operation_name(enum wmcs_control_operation operation);
const char *wmcs_control_outcome_name(enum wmcs_control_outcome outcome);
const char *wmcs_control_reason_name(enum wmcs_control_reason reason);
const char *wmcs_control_peer_id(const struct wmcs_control *control);
uint64_t wmcs_control_sequence(const struct wmcs_control *control);
enum wmcs_control_outcome wmcs_control_outcome(
	const struct wmcs_control *control);
enum wmcs_control_reason wmcs_control_reason(const struct wmcs_control *control);

#endif
