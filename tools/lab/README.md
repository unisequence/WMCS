# Hardware lab tools

`read-only-smoke.sh` validates the first target daemon without enabling the
service or changing router configuration. It refuses to run when another
`wmcs` ubus object exists, launches one chosen binary, exercises all three v0
read methods, and compares SHA-256 hashes of the network, wireless, firewall,
and DHCP UCI exports before and after. Raw configuration and secrets are never
printed or stored.

Example after copying a test binary to a router's temporary filesystem:

```sh
/tmp/read-only-smoke.sh /tmp/wmcsd standalone
```

The caller remains responsible for removing temporary files. Package
installation and service enablement are intentionally outside this script.

## Native compatibility deployment

For a controlled lab target whose package-manager ABI differs from the build
host, `deploy-native-runtime.sh` installs the oldest supported aarch64 lab
binary with private runtime aliases:

```sh
tools/lab/deploy-native-runtime.sh root@192.168.1.2 /path/to/wmcsd
```

This follows the Freenetic native-runtime pattern: aliases live only under
`/usr/lib/wmcs`, the launcher prepends that directory to `LD_LIBRARY_PATH`, and
both staged and installed binaries must pass the read-only smoke test. It never
creates compatibility links in `/lib` or `/usr/lib`. The installer refuses to
replace existing WMCS paths and rolls back files it created if activation
fails. The resulting config remains disabled.

## Passive roaming observation

`observe-roaming.sh` polls the controller and agent hostapd objects during a
physical walk test. It aggregates station count, RSSI and client-advertised BTM
and neighbor-report capabilities on each router. Station MAC addresses and WLAN
credentials never cross SSH or enter the output.

Hostapd and the kernel may retain an authorized station entry after that client
has already reassociated to the other AP. The observer therefore also reports
kernel inactivity and a `recent_activity_count`. By default, an entry is recent
when its inactivity is at most 10000 ms; override the diagnostic threshold with
`WMCS_ROAM_RECENT_MS` (1000 to 60000). This field distinguishes a recently used
path from an obviously stale entry, but it is not proof that a quiet client has
disassociated. The raw hostapd count remains in the output for that reason.

The capability values are decoded from hostapd's raw public capability arrays:
Extended Capabilities bit 19 for BSS Transition Management and RRM Enabled
Capabilities bit 1 for Neighbor Report support. A value of `0` means the
client did not advertise that capability in the current association; `-` means
there was no associated client to inspect.

For a clean baseline, disconnect other clients, connect one phone to the shared
5 GHz WLAN, start a two-minute observation, and walk from one AP to the other
and back:

```sh
tools/lab/observe-roaming.sh root@192.168.1.1 root@192.168.1.2 120 \
  > roaming-baseline.tsv
```

The default hostapd object is `hostapd.phy1-ap0` on both devices. Override it
with `WMCS_CONTROLLER_AP` or `WMCS_AGENT_AP` when a platform assigns another
runtime interface. The observer is passive: it does not configure 802.11k/v/r,
send BSS transition requests, disconnect stations, or change UCI.

For a full-wpad two-node lab, `roaming-runtime.sh` enables or disables the
public hostapd 802.11k/v runtime flags and exchanges each AP's own Neighbor
Report through the workstation:

```sh
tools/lab/roaming-runtime.sh enable root@192.168.1.1 root@192.168.1.2
tools/lab/roaming-runtime.sh disable root@192.168.1.1 root@192.168.1.2
```

The tool requires the BTM and Neighbor Report methods on both APs, validates
the returned JSON without printing BSSIDs or SSIDs, expects exactly one remote
neighbor per AP, and proves the managed UCI digest did not change. A partial
enable is reverted at runtime. These settings are deliberately non-persistent
and disappear when wpad restarts; the tool is a capability experiment, not the
future owned WMCS configuration transaction. It never sends a BTM request.

For a measurement run, `capture-roaming-baseline.sh` combines that association
timeline with a 100 ms ICMP continuity probe from the development host to the
phone. It writes a new mode-`0700` evidence directory and refuses to overwrite
an existing path:

```sh
tools/lab/capture-roaming-baseline.sh \
  /home/uni/quarantine/wmcs-roaming-phone-a-run-01 192.168.1.123 120
```

The phone must answer LAN ICMP for the continuity result to be meaningful. The
capture contains the supplied IP address but no station MAC or WLAN credential.
An internal gap of one missing reply corresponds to approximately 100 ms; the
first and last losses still need the raw ping summary and association timeline
when classifying a transition.

For a controlled assisted run, `trigger-advisory-btm.sh` waits for one client
on a chosen source AP to remain at or below a source-signal threshold for a
bounded number of consecutive samples, then sends exactly one advisory BTM:

```sh
tools/lab/trigger-advisory-btm.sh \
  root@192.168.1.1 hostapd.phy1-ap0 -68 5 120
```

It requires exactly one recent source client, advertised client BTM support,
one Neighbor Report candidate, and the public hostapd BTM counters. The request
sets `disassociation_imminent=false` and a zero disassociation timer; it never
deauthenticates or disconnects the client. Station and BSS identifiers remain
inside the router-side process. Its TSV output contains only timestamps,
aggregate gate inputs, counter values, response status, and whether a returned
target matched the private candidate. Run it concurrently with the baseline
capture when measuring packet continuity. A gate timeout is a valid no-action
result, while a malformed or ambiguous preflight fails closed. A rejected
hostapd method call is emitted as `request_failed`; it is never inferred to be
a transmitted frame unless the public request counter advances exactly once.

Set `WMCS_BTM_DRY_RUN=1` to exercise the complete preflight and threshold gate
without calling the BTM method.
