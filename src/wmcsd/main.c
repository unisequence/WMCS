// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <net/if.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>

#include "wmcsd.h"

static void signal_handler(struct uloop_signal *signal)
{
	(void)signal;
	uloop_end();
}

static struct uloop_signal signals[] = {
	{
		.cb = signal_handler,
		.signo = SIGINT,
	},
	{
		.cb = signal_handler,
		.signo = SIGTERM,
	},
};

#define WMCS_UBUS_RECONNECT_MS 1000U

struct wmcs_ubus_lifecycle {
	struct ubus_context context;
	struct uloop_timeout reconnect;
	struct wmcs_runtime *runtime;
	struct wmcs_control *control;
	struct wmcs_roaming *roaming;
	bool initialized;
	bool connected;
	bool recovery_attempted;
	bool recovery_complete;
	bool roaming_started;
	bool stopping;
};

static void ubus_reconnect_expired(struct uloop_timeout *timeout);

static void runtime_set_degraded(struct wmcs_runtime *runtime,
				 const char *reason)
{
	if (!runtime)
		return;
	runtime->degraded = true;
	snprintf(runtime->degraded_reason, sizeof(runtime->degraded_reason),
		 "%s", reason && *reason ? reason : "unknown");
}

static void schedule_ubus_reconnect(struct wmcs_ubus_lifecycle *lifecycle,
					const char *reason)
{
	if (!lifecycle || lifecycle->stopping)
		return;
	lifecycle->connected = false;
	lifecycle->runtime->ubus_connected = false;
	runtime_set_degraded(lifecycle->runtime, reason);
	lifecycle->reconnect.cb = ubus_reconnect_expired;
	uloop_timeout_set(&lifecycle->reconnect, WMCS_UBUS_RECONNECT_MS);
}

static void ubus_connection_lost(struct ubus_context *ctx)
{
	struct wmcs_ubus_lifecycle *lifecycle = container_of(
		ctx, struct wmcs_ubus_lifecycle, context);

	wmcs_roaming_ubus_disconnected(lifecycle->roaming);
	schedule_ubus_reconnect(lifecycle, "ubus_disconnected");
}

static int connect_ubus(struct wmcs_ubus_lifecycle *lifecycle,
			bool startup_phase)
{
	int result;

	if (!lifecycle || lifecycle->stopping)
		return -EINVAL;
	if (lifecycle->initialized)
		result = ubus_reconnect(&lifecycle->context, NULL);
	else
		result = ubus_connect_ctx(&lifecycle->context, NULL);
	if (result) {
		schedule_ubus_reconnect(lifecycle, "ubus_unavailable");
		return result;
	}
	lifecycle->initialized = true;
	lifecycle->context.connection_lost = ubus_connection_lost;
	ubus_add_uloop(&lifecycle->context);
	lifecycle->connected = true;
	lifecycle->runtime->ubus_connected = true;

	if (!lifecycle->recovery_attempted && startup_phase) {
		lifecycle->recovery_attempted = true;
		result = wmcs_control_recover(lifecycle->control);
		if (result) {
			int error = result < 0 ? -result : EIO;

			fprintf(stderr,
				"wmcsd: startup recovery deferred: %s\n",
				strerror(error));
			runtime_set_degraded(lifecycle->runtime, "recovery_failed");
		} else {
			lifecycle->recovery_complete = true;
			lifecycle->runtime->degraded = false;
			lifecycle->runtime->degraded_reason[0] = '\0';
		}
	}
	if (wmcs_ubus_register(&lifecycle->context, lifecycle->runtime)) {
		schedule_ubus_reconnect(lifecycle, "ubus_api_unavailable");
		return -EIO;
	}
	if (!lifecycle->recovery_attempted)
		runtime_set_degraded(lifecycle->runtime, "startup_recovery_pending");
	else if (!lifecycle->recovery_complete)
		runtime_set_degraded(lifecycle->runtime, "recovery_failed");
	else {
		lifecycle->runtime->degraded = false;
		lifecycle->runtime->degraded_reason[0] = '\0';
	}
	if (!lifecycle->roaming_started) {
		wmcs_roaming_start(lifecycle->roaming);
		lifecycle->roaming_started = true;
	}
	return 0;
}

static void ubus_reconnect_expired(struct uloop_timeout *timeout)
{
	struct wmcs_ubus_lifecycle *lifecycle = container_of(
		timeout, struct wmcs_ubus_lifecycle, reconnect);

	(void)connect_ubus(lifecycle, false);
}

static bool role_valid(const char *role)
{
	return !strcmp(role, "standalone") || !strcmp(role, "controller") ||
	       !strcmp(role, "agent");
}

static bool interface_valid(const char *interface)
{
	size_t i;
	size_t length = strlen(interface);

	if (!length || length >= IF_NAMESIZE)
		return false;

	for (i = 0; i < length; i++) {
		unsigned char character = (unsigned char)interface[i];

		if (!isalnum(character) && character != '.' && character != '_' &&
		    character != '-')
			return false;
	}

	return true;
}

static bool uci_name_valid(const char *name)
{
	size_t i;
	size_t length = strlen(name);

	if (!length || length >= WMCS_CONTROL_NAME_SIZE)
		return false;
	for (i = 0; i < length; i++) {
		unsigned char character = (unsigned char)name[i];

		if (!isalnum(character) && character != '_')
			return false;
	}
	return true;
}

static bool roaming_trigger_valid(const char *value, int *parsed)
{
	char *end = NULL;
	long number;

	if (!value || !*value)
		return false;

	errno = 0;
	number = strtol(value, &end, 10);
	if (errno == ERANGE || end == value || *end != '\0' ||
	    number < -95 || number > -50)
		return false;

	if (parsed)
		*parsed = (int)number;
	return true;
}

static bool roaming_margin_valid(const char *value, int *parsed)
{
	char *end = NULL;
	long number;

	if (!value || !*value)
		return false;

	errno = 0;
	number = strtol(value, &end, 10);
	if (errno == ERANGE || end == value || *end != '\0' ||
	    number < 1 || number > 20)
		return false;

	if (parsed)
		*parsed = (int)number;
	return true;
}

static bool roaming_force_trigger_valid(const char *value, int *parsed)
{
	char *end = NULL;
	long number;

	if (!value || !*value)
		return false;
	errno = 0;
	number = strtol(value, &end, 10);
	if (errno == ERANGE || end == value || *end != '\0' ||
	    number < -95 || number > -75)
		return false;
	if (parsed)
		*parsed = (int)number;
	return true;
}

static int parse_args(int argc, char **argv, const char **role,
		      const char **interface, const char **state_dir,
		      const char **source_iface, const char **source_radio,
		      const char **target_radio, bool *mutation_enabled,
		      bool *roaming_enabled, int *roaming_source_trigger_dbm,
		      int *roaming_improvement_margin_db,
		      bool *neighbor_sync_enabled,
		      bool *roaming_force_after_timeout,
		      int *roaming_force_trigger_dbm)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("wmcsd %s\n", WMCS_VERSION);
			return 1;
		}

		if (!strcmp(argv[i], "--role")) {
			if (++i >= argc || !role_valid(argv[i])) {
				fprintf(stderr, "wmcsd: --role requires standalone, controller, or agent\n");
				return -1;
			}
			*role = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--interface")) {
			if (++i >= argc || !interface_valid(argv[i])) {
				fprintf(stderr, "wmcsd: --interface requires a valid interface name\n");
				return -1;
			}
			*interface = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--state-dir")) {
			if (++i >= argc || argv[i][0] != '/' ||
			    strlen(argv[i]) >= WMCS_IDENTITY_STATE_DIR_SIZE) {
				fprintf(stderr, "wmcsd: --state-dir requires a bounded absolute path\n");
				return -1;
			}
			*state_dir = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--home-source-iface")) {
			if (++i >= argc || !uci_name_valid(argv[i])) {
				fprintf(stderr, "wmcsd: --home-source-iface requires a UCI section name\n");
				return -1;
			}
			*source_iface = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--home-source-radio")) {
			if (++i >= argc || !uci_name_valid(argv[i])) {
				fprintf(stderr, "wmcsd: --home-source-radio requires a UCI section name\n");
				return -1;
			}
			*source_radio = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--home-target-radio")) {
			if (++i >= argc || !uci_name_valid(argv[i])) {
				fprintf(stderr, "wmcsd: --home-target-radio requires a UCI section name\n");
				return -1;
			}
			*target_radio = argv[i];
			continue;
		}

		if (!strcmp(argv[i], "--mutation-enabled")) {
			*mutation_enabled = true;
			continue;
		}

		if (!strcmp(argv[i], "--roaming-source-trigger-dbm")) {
			if (++i >= argc ||
			    !roaming_trigger_valid(argv[i], roaming_source_trigger_dbm)) {
				fprintf(stderr,
					"wmcsd: --roaming-source-trigger-dbm requires a value from -95 to -50\n");
				return -1;
			}
			continue;
		}

		if (!strcmp(argv[i], "--roaming-improvement-margin-db")) {
			if (++i >= argc ||
			    !roaming_margin_valid(argv[i], roaming_improvement_margin_db)) {
				fprintf(stderr,
					"wmcsd: --roaming-improvement-margin-db requires a value from 1 to 20\n");
				return -1;
			}
			continue;
		}

		if (!strcmp(argv[i], "--roaming-enabled")) {
			*roaming_enabled = true;
			continue;
		}

		if (!strcmp(argv[i], "--neighbor-sync-enabled")) {
			*neighbor_sync_enabled = true;
			continue;
		}

		if (!strcmp(argv[i], "--roaming-force-after-timeout")) {
			*roaming_force_after_timeout = true;
			continue;
		}

		if (!strcmp(argv[i], "--roaming-force-trigger-dbm")) {
			if (++i >= argc ||
			    !roaming_force_trigger_valid(argv[i],
						 roaming_force_trigger_dbm)) {
				fprintf(stderr,
					"wmcsd: --roaming-force-trigger-dbm requires a value from -95 to -75\n");
				return -1;
			}
			continue;
		}

		fprintf(stderr, "wmcsd: unknown argument: %s\n", argv[i]);
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct wmcs_runtime runtime = {
		.role = "standalone",
		.roaming_source_trigger_dbm = -68,
		.roaming_improvement_margin_db = 8,
		.roaming_force_trigger_dbm = -78,
	};
	struct wmcs_discovery discovery;
	struct wmcs_identity identity;
	struct wmcs_pairing pairing;
	struct wmcs_control control;
	struct wmcs_roaming roaming;
	struct wmcs_ubus_lifecycle ubus_lifecycle = {0};
	struct ubus_context *ctx = &ubus_lifecycle.context;
	const char *interface = "br-lan";
	const char *state_dir = "/etc/wmcs";
	const char *source_iface = "default_radio1";
	const char *source_radio = "radio1";
	const char *target_radio = "radio1";
	bool discovery_initialized = false;
	bool identity_initialized = false;
	bool pairing_initialized = false;
	bool control_initialized = false;
	bool roaming_initialized = false;
	unsigned int registered_signals = 0;
	int result = 1;
	int parsed;

	parsed = parse_args(argc, argv, &runtime.role, &interface, &state_dir,
			    &source_iface, &source_radio, &target_radio,
			    &runtime.mutation_enabled,
			    &runtime.roaming_enabled,
		    &runtime.roaming_source_trigger_dbm,
		    &runtime.roaming_improvement_margin_db,
		    &runtime.neighbor_sync_enabled,
		    &runtime.roaming_force_after_timeout,
			    &runtime.roaming_force_trigger_dbm);
	if (parsed)
		return parsed < 0 ? 2 : 0;

	if (uloop_init()) {
		fprintf(stderr, "wmcsd: cannot initialize event loop: %s\n", strerror(errno));
		return 1;
	}

	if (wmcs_identity_init(&identity, state_dir)) {
		fprintf(stderr, "wmcsd: cannot initialize protected identity state\n");
		goto out;
	}
	identity_initialized = true;
	runtime.identity = &identity;
	if (wmcs_discovery_init(&discovery, runtime.role, interface)) {
		fprintf(stderr, "wmcsd: cannot initialize discovery state\n");
		goto out;
	}
	discovery_initialized = true;
	runtime.discovery = &discovery;
	if (wmcs_pairing_init(&pairing, runtime.role, interface, &identity)) {
		fprintf(stderr, "wmcsd: cannot initialize pairing state\n");
		goto out;
	}
	pairing_initialized = true;
	runtime.pairing = &pairing;
	runtime.started_ms = wmcs_monotonic_ms();
	if (wmcs_control_init(&control, runtime.role, interface, state_dir,
			      source_iface, source_radio, target_radio,
			      runtime.mutation_enabled, &identity, &discovery, ctx)) {
		fprintf(stderr, "wmcsd: cannot initialize control state\n");
		goto out;
	}
	control_initialized = true;
	runtime.control = &control;
	if (wmcs_roaming_init(&roaming, runtime.role, interface, source_iface,
			      source_radio, &identity, ctx, runtime.roaming_enabled,
			      runtime.neighbor_sync_enabled,
			      runtime.roaming_source_trigger_dbm,
			      runtime.roaming_improvement_margin_db,
			      runtime.roaming_force_after_timeout,
			      runtime.roaming_force_trigger_dbm)) {
		fprintf(stderr, "wmcsd: cannot initialize roaming state\n");
		goto out;
	}
	roaming_initialized = true;
	runtime.roaming = &roaming;
	ubus_lifecycle.runtime = &runtime;
	ubus_lifecycle.control = &control;
	ubus_lifecycle.roaming = &roaming;
	ubus_lifecycle.reconnect.cb = ubus_reconnect_expired;
	runtime_set_degraded(&runtime, "ubus_unavailable");
	(void)connect_ubus(&ubus_lifecycle, true);

	for (registered_signals = 0; registered_signals < ARRAY_SIZE(signals);
	     registered_signals++) {
		if (uloop_signal_add(&signals[registered_signals])) {
			fprintf(stderr, "wmcsd: cannot register signal handler\n");
			goto out_unregister;
		}
	}

	fprintf(stderr, "wmcsd: API lifecycle started with role %s, mutation %s\n",
		runtime.role, runtime.mutation_enabled ? "enabled" : "disabled");
	uloop_run();
	result = 0;

	while (registered_signals > 0)
		uloop_signal_delete(&signals[--registered_signals]);

out_unregister:
	ubus_lifecycle.stopping = true;
	uloop_timeout_cancel(&ubus_lifecycle.reconnect);
	if (roaming_initialized) {
		wmcs_roaming_close(&roaming);
		roaming_initialized = false;
	}
	if (ubus_lifecycle.initialized)
		wmcs_ubus_unregister(ctx);
out:
	ubus_lifecycle.stopping = true;
	uloop_timeout_cancel(&ubus_lifecycle.reconnect);
	if (roaming_initialized)
		wmcs_roaming_close(&roaming);
	if (control_initialized)
		wmcs_control_close(&control);
	if (ubus_lifecycle.initialized)
		ubus_shutdown(ctx);
	if (pairing_initialized)
		wmcs_pairing_close(&pairing);
	if (discovery_initialized)
		wmcs_discovery_close(&discovery);
	if (identity_initialized)
		wmcs_identity_close(&identity);
	uloop_done();

	return result;
}
