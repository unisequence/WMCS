'use strict';
'require form';
'require poll';
'require rpc';
'require view';

const callStatus = rpc.declare({
	object: 'wmcs',
	method: 'status',
	expect: { '': {} }
});

const callRoamingStatus = rpc.declare({
	object: 'wmcs',
	method: 'roaming_status',
	expect: { '': {} }
});

const callCapabilities = rpc.declare({
	object: 'wmcs',
	method: 'capabilities',
	expect: { '': {} }
});

const callNodes = rpc.declare({
	object: 'wmcs',
	method: 'nodes',
	expect: { '': {} }
});

const callIdentity = rpc.declare({
	object: 'wmcs',
	method: 'identity',
	expect: { '': {} }
});

const callPeers = rpc.declare({
	object: 'wmcs',
	method: 'peers',
	expect: { '': {} }
});

const callPairingStatus = rpc.declare({
	object: 'wmcs',
	method: 'pairing_status',
	expect: { '': {} }
});

const callWlanSyncStatus = rpc.declare({
	object: 'wmcs',
	method: 'wlan_sync_status',
	expect: { '': {} }
});

const callReleaseStatus = rpc.declare({
	object: 'wmcs',
	method: 'release_status',
	expect: { '': {} }
});

function safeCall(call) {
	return L.resolveDefault(call(), {});
}

function readValue(object, key, fallback) {
	const value = object && object[key];

	return value === undefined || value === null || value === '' ?
		(fallback === undefined ? '-' : fallback) : value;
}

function asBoolean(value) {
	return value === true || value === 1 || value === '1';
}

function yesNo(value) {
	if (value === undefined || value === null || value === '')
		return '-';

	return asBoolean(value) ? _('Yes') : _('No');
}

function formatDbm(value) {
	return value === undefined || value === null || value === '' ?
		'-' : '%s %s'.format(value, _('dBm'));
}

function formatDuration(milliseconds) {
	const value = Number(milliseconds);

	if (!Number.isFinite(value) || value < 0)
		return '-';

	let seconds = Math.floor(value / 1000);
	const days = Math.floor(seconds / 86400);
	seconds -= days * 86400;
	const hours = Math.floor(seconds / 3600);
	seconds -= hours * 3600;
	const minutes = Math.floor(seconds / 60);
	seconds -= minutes * 60;
	const parts = [];

	if (days)
		parts.push('%dd'.format(days));
	if (hours || days)
		parts.push('%02dh'.format(hours));
	if (minutes || hours || days)
		parts.push('%02dm'.format(minutes));
	parts.push('%02ds'.format(seconds));

	return parts.join(' ');
}

function formatAge(milliseconds) {
	const value = Number(milliseconds);

	if (!Number.isFinite(value) || value < 0)
		return '-';

	return _('%s ago').format(formatDuration(value));
}

function shortId(value) {
	if (value === undefined || value === null || value === '')
		return '-';

	const text = String(value);

	if (text === '-' || text.length <= 16)
		return text;

	return '%s…%s'.format(text.substring(0, 8), text.substring(text.length - 6));
}

function setText(id, value, root) {
	const node = root ? root.querySelector('#%s'.format(id)) : document.getElementById(id);

	if (node)
		node.textContent = value === undefined || value === null || value === '' ? '-' : String(value);
}

function infoRow(label, value, valueId) {
	return E('div', { 'class': 'wmcs-kv-item' }, [
		E('div', { 'class': 'wmcs-kv-label' }, label),
		E('div', { 'class': 'wmcs-kv-value' }, valueId ? E('span', { 'id': valueId }, value) : value)
	]);
}

function statusSection(title, rows) {
	return E('section', { 'class': 'wmcs-card' }, [
		E('div', { 'class': 'wmcs-card-header' }, E('h3', {}, title)),
		E('div', { 'class': 'wmcs-kv' }, rows)
	]);
}

function tableSection(title, id, headers) {
	return E('section', { 'class': 'wmcs-card wmcs-table-card' }, [
		E('div', { 'class': 'wmcs-card-header' }, E('h3', {}, title)),
		E('div', { 'class': 'wmcs-table-scroll' }, E('table', { 'class': 'table wmcs-table' }, [
			E('thead', {}, E('tr', { 'class': 'tr table-titles' }, headers.map(function(header) {
				return E('th', { 'class': 'th' }, header);
			}))),
			E('tbody', { 'id': id })
		]))
	]);
}

function disclosureSection(title, description, content) {
	return E('details', { 'class': 'wmcs-disclosure' }, [
		E('summary', { 'class': 'wmcs-disclosure-summary' }, [
			E('strong', {}, title),
			E('span', { 'class': 'wmcs-disclosure-hint' }, description)
		]),
		E('div', { 'class': 'wmcs-disclosure-body' }, content)
	]);
}

function ensureStyles() {
	if (document.querySelector('link[data-wmcs-style="overview"]'))
		return;

	document.head.appendChild(E('link', {
		'rel': 'stylesheet',
		'href': L.resource('wmcs/overview.css'),
		'data-wmcs-style': 'overview'
	}));
}

function tableCell(value) {
	return E('td', { 'class': 'td' }, value === undefined || value === null || value === '' ? '-' : String(value));
}

function clearNode(node) {
	while (node && node.firstChild)
		node.removeChild(node.firstChild);
}

function updateNodes(nodesReply, root) {
	const body = root ? root.querySelector('#wmcs-nodes') : document.getElementById('wmcs-nodes');
	const nodes = nodesReply && Array.isArray(nodesReply.nodes) ? nodesReply.nodes : [];

	if (!body)
		return;

	clearNode(body);

	if (!nodes.length) {
		body.appendChild(E('tr', { 'class': 'tr' }, E('td', {
			'class': 'td',
			'colspan': '7'
		}, _('No discovered nodes'))));
		return;
	}

	nodes.forEach(function(node) {
		body.appendChild(E('tr', { 'class': 'tr' }, [
			tableCell(shortId(node.id)),
			tableCell(readValue(node, 'role')),
			tableCell(readValue(node, 'state')),
			tableCell(readValue(node, 'identity_trust')),
			tableCell(readValue(node, 'transport')),
			tableCell(readValue(node, 'address')),
			tableCell(formatAge(node.last_seen_age_ms))
		]));
	});
}

function updatePeers(peersReply, root) {
	const body = root ? root.querySelector('#wmcs-peers') : document.getElementById('wmcs-peers');
	const peers = peersReply && Array.isArray(peersReply.peers) ? peersReply.peers : [];

	if (!body)
		return;

	clearNode(body);

	if (!peers.length) {
		body.appendChild(E('tr', { 'class': 'tr' }, E('td', {
			'class': 'td',
			'colspan': '4'
		}, _('No paired peers'))));
		return;
	}

	peers.forEach(function(peer) {
		body.appendChild(E('tr', { 'class': 'tr' }, [
			tableCell(shortId(peer.peer_id)),
			tableCell(readValue(peer, 'role')),
			tableCell(readValue(peer, 'state')),
			tableCell(readValue(peer, 'generation'))
		]));
	});
}

function updateRuntime(data, root) {
	const status = data.status || {};
	const identity = data.identity || {};
	const capabilities = data.capabilities || {};
	const features = capabilities.features || {};
	const platform = capabilities.platform || {};
	const pairing = data.pairing || {};
	const sync = data.sync || {};
	const release = data.release || {};
	const set = function(id, value) {
		setText(id, value, root);
	};

	set('wmcs-hero-state', readValue(status, 'state'));
	set('wmcs-hero-role', readValue(status, 'role'));
	set('wmcs-hero-daemon', readValue(status, 'daemon_version'));
	set('wmcs-status-daemon', readValue(status, 'daemon_version'));
	set('wmcs-status-state', readValue(status, 'state'));
	set('wmcs-status-role', readValue(status, 'role'));
	set('wmcs-status-mutation', yesNo(status.mutation_enabled));
	set('wmcs-status-mutation-available', yesNo(status.mutation_available));
	set('wmcs-status-ubus', yesNo(status.ubus_connected));
	set('wmcs-status-degraded', yesNo(status.degraded));
	set('wmcs-status-degraded-reason', readValue(status, 'degraded_reason'));
	set('wmcs-status-uptime', formatDuration(status.uptime_ms));
	set('wmcs-status-generation', readValue(status, 'generation'));
	set('wmcs-status-discovery', yesNo(status.discovery_active));
	set('wmcs-status-pairing', yesNo(status.pairing_active));
	set('wmcs-status-control', yesNo(status.control_active));
	set('wmcs-status-pairing-state', readValue(status, 'pairing_state'));
	set('wmcs-status-control-state', readValue(status, 'control_state'));
	set('wmcs-status-control-operation', readValue(status, 'control_operation'));
	set('wmcs-status-reconciliation', yesNo(status.control_reconciliation_pending));
	set('wmcs-status-identity', yesNo(identity.ready || status.identity_ready));
	set('wmcs-status-fingerprint', shortId(identity.fingerprint));
	set('wmcs-status-paired-count', readValue(status, 'paired_peer_count', identity.paired_peer_count));
	set('wmcs-status-controller-count', readValue(status, 'active_controller_count', identity.active_controller_count));

	set('wmcs-cap-native', yesNo(features.native_protocol));
	set('wmcs-cap-discovery', yesNo(features.discovery));
	set('wmcs-cap-pairing', yesNo(features.pairing));
	set('wmcs-cap-control', yesNo(features.authenticated_control));
	set('wmcs-cap-sync', yesNo(features.one_wlan_sync));
	set('wmcs-cap-release', yesNo(features.ownership_safe_release));
	set('wmcs-cap-ubus', yesNo(platform.ubus));
	set('wmcs-cap-uci', yesNo(platform.uci));
	set('wmcs-cap-hostapd', yesNo(platform.hostapd));
	set('wmcs-cap-iw', yesNo(platform.iw));

	set('wmcs-pairing-active', yesNo(pairing.active));
	set('wmcs-pairing-state', readValue(pairing, 'state'));
	set('wmcs-sync-state', readValue(sync, 'state'));
	set('wmcs-sync-operation', readValue(sync, 'operation'));
	set('wmcs-sync-reconciliation', yesNo(sync.reconciliation_pending));
	set('wmcs-release-state', readValue(release, 'state'));
	set('wmcs-release-operation', readValue(release, 'operation'));
	set('wmcs-release-reconciliation', yesNo(release.reconciliation_pending));
}

function updateRoaming(roaming, root) {
	const value = roaming || {};
	const set = function(id, current) {
		setText(id, current, root);
	};

	set('wmcs-roaming-mode', readValue(value, 'mode'));
	set('wmcs-roaming-enabled', yesNo(value.enabled));
	set('wmcs-roaming-active', yesNo(value.active));
	set('wmcs-roaming-state', readValue(value, 'state'));
	set('wmcs-roaming-threshold', formatDbm(value.source_trigger_dbm));
	set('wmcs-roaming-signal', formatDbm(value.last_source_signal_dbm));
	set('wmcs-roaming-clients', readValue(value, 'client_count'));
	set('wmcs-roaming-neighbors', readValue(value, 'neighbor_count'));
	set('wmcs-roaming-samples', readValue(value, 'samples'));
	set('wmcs-roaming-gates', readValue(value, 'gate_passes'));
	set('wmcs-roaming-requests', readValue(value, 'requests_sent'));
	set('wmcs-roaming-failures', readValue(value, 'request_failures'));
	set('wmcs-roaming-reason', readValue(value, 'last_reason'));
}

function loadRuntime() {
	return Promise.all([
		safeCall(callStatus),
		safeCall(callRoamingStatus),
		safeCall(callCapabilities),
		safeCall(callNodes),
		safeCall(callIdentity),
		safeCall(callPeers),
		safeCall(callPairingStatus),
		safeCall(callWlanSyncStatus),
		safeCall(callReleaseStatus)
	]).then(function(result) {
		return {
			status: result[0],
			roaming: result[1],
			capabilities: result[2],
			nodes: result[3],
			identity: result[4],
			peers: result[5],
			pairing: result[6],
			sync: result[7],
			release: result[8]
		};
	});
}

function addCoreOptions(section) {
	let option;

	option = section.option(form.Flag, 'enabled', _('Enable WMCS daemon'));
	option.default = '0';
	option.rmempty = false;
	option.description = _('Controls whether the init script starts the daemon.');

	option = section.option(form.ListValue, 'role', _('Node role'));
	option.value('standalone', _('Standalone'));
	option.value('controller', _('Controller'));
	option.value('agent', _('Agent'));
	option.default = 'standalone';
	option.rmempty = false;
	option.description = _('Controller and agent roles enable the native WMCS protocol.');

	option = section.option(form.Flag, 'mutation_enabled', _('Enable configuration mutation'));
	option.default = '0';
	option.rmempty = false;
	option.description = _('Keep disabled while observing or pairing. Enabling it allows the bounded control and release paths.');

	option = section.option(form.Value, 'discovery_interface', _('Discovery interface'));
	option.default = 'br-lan';
	option.rmempty = false;
	option.description = _('Local interface used for native controller and agent discovery.');

	option = section.option(form.Value, 'home_source_iface', _('Home source interface'));
	option.default = 'default_radio1';
	option.rmempty = false;
	option.description = _('UCI wireless interface whose configuration is treated as the source.');

	option = section.option(form.Value, 'home_source_radio', _('Home source radio'));
	option.default = 'radio1';
	option.rmempty = false;

	option = section.option(form.Value, 'home_target_radio', _('Home target radio'));
	option.default = 'radio1';
	option.rmempty = false;
}

function addRoamingOptions(section) {
	let option;

	section.description = _('This policy is advisory only. The client remains in control of the final roam.');

	option = section.option(form.Flag, 'enabled', _('Enable advisory steering'));
	option.default = '0';
	option.rmempty = false;
	option.description = _('Enables the source-gated 802.11v advisory path. It does not force disassociation.');

	option = section.option(form.Value, 'source_trigger_dbm', _('Weak source signal threshold'));
	option.datatype = 'range(-95,-50)';
	option.default = '-68';
	option.rmempty = false;
	option.description = _('Trigger input in dBm. More negative values mean a weaker source signal.');
}

return view.extend({
	load() {
		return loadRuntime();
	},

	render(data) {
		const m = new form.Map('wmcs', _('Configuration'), _('Native coordination remains separate from OpenWrt wireless configuration.'));
		const core = m.section(form.NamedSection, 'core', 'core', _('Daemon and node'));
		const policy = m.section(form.NamedSection, 'policy', 'roaming', _('Roaming policy'));
		const root = E('div', { 'class': 'wmcs-dashboard' }, [
			E('section', { 'class': 'wmcs-hero' }, [
				E('div', { 'class': 'wmcs-hero-copy' }, [
					E('div', { 'class': 'wmcs-eyebrow' }, _('Wireless mesh coordination')),
					E('h2', {}, _('WMCS')),
					E('p', {}, _('A bounded coordination layer for participating access points.'))
				]),
				E('div', { 'class': 'wmcs-hero-meta' }, [
					E('div', { 'class': 'wmcs-hero-label' }, _('Current state')),
					E('span', { 'id': 'wmcs-hero-state', 'class': 'wmcs-pill' }, '-'),
					E('div', { 'class': 'wmcs-hero-facts' }, [
						E('span', {}, [ _('Role'), ' ', E('strong', { 'id': 'wmcs-hero-role' }, '-') ]),
						E('span', {}, [ _('Daemon'), ' ', E('strong', { 'id': 'wmcs-hero-daemon' }, '-') ])
					])
				])
			]),
			E('div', { 'class': 'wmcs-grid wmcs-grid-two' }, [
				statusSection(_('Node overview'), [
					infoRow(_('Daemon version'), '-', 'wmcs-status-daemon'),
					infoRow(_('State'), '-', 'wmcs-status-state'),
					infoRow(_('Role'), '-', 'wmcs-status-role'),
					infoRow(_('Mutation gate'), '-', 'wmcs-status-mutation'),
					infoRow(_('Mutation available'), '-', 'wmcs-status-mutation-available'),
					infoRow(_('ubus connected'), '-', 'wmcs-status-ubus'),
					infoRow(_('Degraded'), '-', 'wmcs-status-degraded'),
					infoRow(_('Degraded reason'), '-', 'wmcs-status-degraded-reason'),
					infoRow(_('Uptime'), '-', 'wmcs-status-uptime'),
					infoRow(_('Generation'), '-', 'wmcs-status-generation'),
					infoRow(_('Identity ready'), '-', 'wmcs-status-identity'),
					infoRow(_('Paired peers'), '-', 'wmcs-status-paired-count'),
					infoRow(_('Active controllers'), '-', 'wmcs-status-controller-count')
				]),
				statusSection(_('Roaming'), [
					infoRow(_('Mode'), '-', 'wmcs-roaming-mode'),
					infoRow(_('Enabled by policy'), '-', 'wmcs-roaming-enabled'),
					infoRow(_('Active'), '-', 'wmcs-roaming-active'),
					infoRow(_('State'), '-', 'wmcs-roaming-state'),
					infoRow(_('Source threshold'), '-', 'wmcs-roaming-threshold'),
					infoRow(_('Last source signal'), '-', 'wmcs-roaming-signal'),
					infoRow(_('Observed clients'), '-', 'wmcs-roaming-clients'),
					infoRow(_('Neighbor reports'), '-', 'wmcs-roaming-neighbors'),
					infoRow(_('Advisory requests'), '-', 'wmcs-roaming-requests'),
					infoRow(_('Last reason'), '-', 'wmcs-roaming-reason')
				])
			]),
			tableSection(_('Discovered nodes'), 'wmcs-nodes', [
				_('Node'), _('Role'), _('State'), _('Trust'), _('Transport'), _('Address'), _('Last seen')
			]),
			tableSection(_('Paired peers'), 'wmcs-peers', [
				_('Peer'), _('Role'), _('State'), _('Generation')
			]),
			disclosureSection(_('Diagnostics'), _('Control plane, capabilities and counters'), [
				E('div', { 'class': 'wmcs-grid wmcs-grid-two' }, [
					statusSection(_('Control plane'), [
						infoRow(_('Discovery active'), '-', 'wmcs-status-discovery'),
						infoRow(_('Pairing active'), '-', 'wmcs-status-pairing'),
						infoRow(_('Pairing state'), '-', 'wmcs-status-pairing-state'),
						infoRow(_('Control active'), '-', 'wmcs-status-control'),
						infoRow(_('Control state'), '-', 'wmcs-status-control-state'),
						infoRow(_('Control operation'), '-', 'wmcs-status-control-operation'),
						infoRow(_('Reconciliation pending'), '-', 'wmcs-status-reconciliation'),
						infoRow(_('WLAN sync state'), '-', 'wmcs-sync-state'),
						infoRow(_('WLAN sync operation'), '-', 'wmcs-sync-operation'),
						infoRow(_('WLAN sync reconciliation'), '-', 'wmcs-sync-reconciliation'),
						infoRow(_('Release state'), '-', 'wmcs-release-state'),
						infoRow(_('Release operation'), '-', 'wmcs-release-operation'),
						infoRow(_('Release reconciliation'), '-', 'wmcs-release-reconciliation')
					]),
					statusSection(_('Read-only capabilities'), [
						infoRow(_('Native protocol'), '-', 'wmcs-cap-native'),
						infoRow(_('Discovery'), '-', 'wmcs-cap-discovery'),
						infoRow(_('Pairing'), '-', 'wmcs-cap-pairing'),
						infoRow(_('Authenticated control'), '-', 'wmcs-cap-control'),
						infoRow(_('One-WLAN sync'), '-', 'wmcs-cap-sync'),
						infoRow(_('Ownership-safe release'), '-', 'wmcs-cap-release'),
						infoRow(_('ubus'), '-', 'wmcs-cap-ubus'),
						infoRow(_('UCI'), '-', 'wmcs-cap-uci'),
						infoRow(_('hostapd'), '-', 'wmcs-cap-hostapd'),
						infoRow(_('iw'), '-', 'wmcs-cap-iw')
					])
				]),
				statusSection(_('Roaming telemetry'), [
					infoRow(_('Samples'), '-', 'wmcs-roaming-samples'),
					infoRow(_('Gate passes'), '-', 'wmcs-roaming-gates'),
					infoRow(_('Request failures'), '-', 'wmcs-roaming-failures'),
					infoRow(_('Identity fingerprint'), '-', 'wmcs-status-fingerprint')
				])
			]),
			E('div', { 'id': 'wmcs-config-form', 'class': 'wmcs-form-shell' })
		]);

		core.addremove = false;
		policy.addremove = false;
		addCoreOptions(core);
		addRoamingOptions(policy);

		poll.add(function() {
			return loadRuntime().then(function(runtime) {
				updateRuntime(runtime);
				updateRoaming(runtime.roaming);
				updateNodes(runtime.nodes);
				updatePeers(runtime.peers);
			});
		});

		return Promise.resolve(m.render()).then(function(map) {
			ensureStyles();
			root.querySelector('#wmcs-config-form').appendChild(map);
			updateRuntime(data, root);
			updateRoaming(data.roaming, root);
			updateNodes(data.nodes, root);
			updatePeers(data.peers, root);
			return root;
		});
	}
});
