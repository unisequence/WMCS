// SPDX-License-Identifier: Apache-2.0

#ifndef WMCSD_H
#define WMCSD_H

#include <stdbool.h>
#include <stdint.h>

#include <libubus.h>

#include "control.h"
#include "discovery.h"
#include "identity.h"
#include "pairing.h"
#include "roaming.h"

#define WMCS_API_VERSION 0

#ifndef WMCS_VERSION
#define WMCS_VERSION "0.2.0-dev"
#endif

struct wmcs_runtime {
	const char *role;
	bool mutation_enabled;
	bool roaming_enabled;
	bool neighbor_sync_enabled;
	bool roaming_force_after_timeout;
	bool ubus_connected;
	bool degraded;
	char degraded_reason[48];
	int roaming_source_trigger_dbm;
	int roaming_improvement_margin_db;
	int roaming_force_trigger_dbm;
	uint64_t started_ms;
	struct wmcs_control *control;
	struct wmcs_roaming *roaming;
	struct wmcs_discovery *discovery;
	struct wmcs_identity *identity;
	struct wmcs_pairing *pairing;
};

int wmcs_ubus_register(struct ubus_context *ctx, const struct wmcs_runtime *runtime);
void wmcs_ubus_unregister(struct ubus_context *ctx);

#endif
