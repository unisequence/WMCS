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

#define WMCS_ROAMING_OBJECT_SIZE (IF_NAMESIZE + 16U)

enum wmcs_roaming_state {
	WMCS_ROAMING_DISABLED = 0,
	WMCS_ROAMING_WAITING_HOSTAPD,
	WMCS_ROAMING_MONITORING,
};

struct wmcs_roaming_client {
	bool used;
	bool observed;
	bool signal_seen;
	bool btm;
	bool neighbor_report;
	bool weak;
	char address[18];
	int signal_dbm;
	uint64_t first_seen_ms;
	uint64_t weak_since_ms;
	unsigned int weak_samples;
	bool attempted;
};

struct wmcs_roaming {
	const char *role;
	const char *source_iface;
	const char *source_radio;
	struct ubus_context *ubus;
	struct uloop_timeout poll_timer;
	char hostapd_object[WMCS_ROAMING_OBJECT_SIZE];
	int source_trigger_dbm;
	bool enabled;
	bool initialized;
	bool hostapd_ready;
	struct wmcs_roaming_client clients[32];
	size_t client_count;
	size_t neighbor_count;
	uint64_t samples;
	uint64_t gate_passes;
	uint64_t requests_sent;
	uint64_t request_failures;
	uint64_t last_action_ms;
	int last_source_signal_dbm;
	bool last_signal_seen;
	char last_reason[48];
	char neighbor[512];
};

int wmcs_roaming_init(struct wmcs_roaming *roaming, const char *role,
			      const char *source_iface, const char *source_radio,
			      struct ubus_context *ubus, bool enabled,
			      int source_trigger_dbm);
void wmcs_roaming_start(struct wmcs_roaming *roaming);
void wmcs_roaming_close(struct wmcs_roaming *roaming);
bool wmcs_roaming_enabled(const struct wmcs_roaming *roaming);
bool wmcs_roaming_active(const struct wmcs_roaming *roaming);
enum wmcs_roaming_state wmcs_roaming_state(
	const struct wmcs_roaming *roaming);
const char *wmcs_roaming_state_name(enum wmcs_roaming_state state);
const char *wmcs_roaming_last_reason(const struct wmcs_roaming *roaming);

#endif
