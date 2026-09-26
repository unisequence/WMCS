#!/bin/sh

set -eu

source_dir=src/wmcsd

for method in status roaming_status capabilities nodes identity peers; do
	if ! grep -q "UBUS_METHOD_NOARG(\"$method\"" "$source_dir/ubus_api.c"; then
		echo "Missing read-only ubus method: $method" >&2
		exit 1
	fi
done

for method in discovery_start discovery_stop pairing_start pairing_status \
	pairing_confirm pairing_stop control_listen wlan_sync_start wlan_sync_status \
	wlan_sync_stop release_start release_status release_stop forget_orphan; do
	if ! grep -q "UBUS_METHOD.*(\"$method\"" "$source_dir/ubus_api.c"; then
		echo "Missing bounded native API method: $method" >&2
		exit 1
	fi
done

for method in adopt release reconcile reset update steer; do
	if grep -q "UBUS_METHOD[^\n]*(\"$method\"" "$source_dir/ubus_api.c"; then
		echo "Mutation method entered the read-only daemon: $method" >&2
		exit 1
	fi
done

if grep -RInE '\b(system|popen|execl|execv|fork)\s*\(' "$source_dir"; then
	echo "Process or shell execution is forbidden in the read-only daemon" >&2
	exit 1
fi

if ! grep -q 'active_runtime->mutation_enabled' "$source_dir/ubus_api.c"; then
	echo "Status must report the configured mutation gate" >&2
	exit 1
fi

if ! grep -q 'PSA_ALG_GCM' "$source_dir/control_crypto.c"; then
	echo "Authenticated control must retain AES-GCM" >&2
	exit 1
fi

for domain in WMCS-RELEASE-C2A-V0 WMCS-RELEASE-A2C-V0; do
	if ! grep -q "$domain" "$source_dir/control_crypto.c"; then
		echo "Release must retain its operation-specific crypto domain: $domain" >&2
		exit 1
	fi
done

for token in 'ubus_subscribe(roaming->ubus' 'rrm_beacon_req' \
	'bss-transition-response' 'beacon-report' 'improvement_margin_db' \
	'wmcs_roaming_target_decide' 'wmcs_roaming_force_gate' \
	'force_cooldown_slot' '"del_client"'; do
	if ! grep -q "$token" "$source_dir/roaming.c"; then
		echo "Roaming adapter is missing bounded target-observation support: $token" >&2
		exit 1
	fi
done

if ! grep -Fq "option force_after_timeout '0'" package/openwrt/files/wmcs.config; then
	echo "Forced disassociation must be disabled in the packaged default" >&2
	exit 1
fi

for marker in wmcs_managed wmcs_owner; do
	if ! grep -q "\"$marker\"" "$source_dir/wlan.c"; then
		echo "Release must retain the ownership marker: $marker" >&2
		exit 1
	fi
done

if ! grep -q 'WMCS_PEER_STATE_RELEASED' "$source_dir/control.c"; then
	echo "Committed release must revoke peer mutation authority" >&2
	exit 1
fi

if ! grep -q 'return -EEXIST;' "$source_dir/identity.c"; then
	echo "Pairing must not overwrite an existing peer relationship" >&2
	exit 1
fi

if ! grep -q '#define WMCS_DISCOVERY_MAX_SECONDS 300U' "$source_dir/discovery.h"; then
	echo "Discovery window must retain its 300 second upper bound" >&2
	exit 1
fi

if ! grep -q '#define WMCS_PAIRING_MAX_SECONDS 300U' "$source_dir/pairing.h"; then
	echo "Pairing window must retain its 300 second upper bound" >&2
	exit 1
fi

if ! grep -q 'PSA_ALG_ECDSA(PSA_ALG_SHA_256)' "$source_dir/identity.c"; then
	echo "Pairing identity must use the frozen ECDSA/SHA-256 algorithm" >&2
	exit 1
fi

if grep -qE '\bnanosleep[[:space:]]*\(' "$source_dir/wlan.c"; then
	echo "WLAN mutation path must not sleep inside the daemon event loop" >&2
	exit 1
fi

for marker in wmcs_wlan_async_start_apply wmcs_wlan_async_start_release \
	WMCS_WLAN_ASYNC_VERIFY WMCS_WLAN_VERIFY_TIMEOUT_MS; do
	if ! grep -q "$marker" "$source_dir/wlan.c" "$source_dir/wlan.h" "$source_dir/control.c"; then
		echo "Nonblocking WLAN transaction marker is missing: $marker" >&2
		exit 1
	fi
done

if ! grep -q 'observed_neighbor_records !=' "$source_dir/roaming.c"; then
	echo "Steering must reject extra foreign hostapd neighbor records" >&2
	exit 1
fi

for marker in ubus_connection_lost ubus_reconnect WMCS_UBUS_RECONNECT_MS; do
	if ! grep -q "$marker" "$source_dir/main.c"; then
		echo "ubus lifecycle marker is missing: $marker" >&2
		exit 1
	fi
done

for marker in ubus_connected degraded mutation_available; do
	if ! grep -q "\"$marker\"" "$source_dir/ubus_api.c"; then
		echo "Degraded runtime status field is missing: $marker" >&2
		exit 1
	fi
done

echo "Read-only daemon contract: ok"
