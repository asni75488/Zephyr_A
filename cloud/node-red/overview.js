// Zephyr_A - Node-RED function "Edge node overview" (part 10, dashboard).
// Source of the function node in flows.json; unit test: test/overview.test.js.
//
// In:  the record of every Sparkplug message from "Sparkplug B decode"
//      (output 1: msg.payload = {type, edge, device, received, metrics[]})
//      or msg.topic === 'refresh' (inject every 30 s: ages, command queue)
// Out: [1] msg.payload = one row per edge node -> ui-table (action replace)
//      [2] msg.options  = edge nodes for the command target -> ui-dropdown
//          (only when the list of nodes changes)
//
// Shows per node what an operator needs at a glance: online/offline since,
// last data, firmware, reset cause, security posture, signal, reconnects,
// configuration and commands waiting for the node (interval mode, part 9).

const info = flow.get('nodeInfo') || {};
const pending = flow.get('pendingNcmd') || {};

const r = msg.payload;
if (msg.topic !== 'refresh' && r && r.edge && r.type) {
    const n = info[r.edge] || (info[r.edge] = { edge: r.edge });
    const byName = {};
    for (const m of (r.metrics || [])) {
        byName[m.name] = m.value;
    }
    const take = (metric, field, f) => {
        if (byName[metric] !== undefined && byName[metric] !== null) {
            n[field] = f ? f(byName[metric]) : byName[metric];
        }
    };
    if (r.type === 'NBIRTH') {
        n.online = true;
        n.since = r.received;
    } else if (r.type === 'NDEATH') {
        n.online = false;
        n.since = r.received;
    }
    if (r.type === 'DDATA' || r.type === 'NDATA') {
        n.lastData = r.received;
    }
    take('Properties/FW Version', 'fw');
    take('Properties/Reset Cause', 'reset');
    take('Properties/Security', 'security');
    take('Status/RSSI', 'rssi');
    take('Status/MQTT Connects', 'connects');
    take('Config/Sample Interval', 'interval', (v) => v / 1000);
    take('Config/Batch Size', 'batch');
    flow.set('nodeInfo', info);
}

const t = (ms) => (ms ? new Date(ms).toISOString().replace('T', ' ').slice(0, 19) + ' UTC' : '-');
const ago = (ms) => {
    if (!ms) return '';
    const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
    return s < 120 ? s + ' s' : (s < 7200 ? Math.round(s / 60) + ' min' : Math.round(s / 3600) + ' t');
};

const rows = Object.keys(info).sort().map((e) => {
    const n = info[e];
    const q = pending[e] || [];
    return {
        'Node': e,
        'Status': (n.online ? 'online' : 'offline') + (n.since ? ' i ' + ago(n.since) : ''),
        'Sidste data': n.lastData ? t(n.lastData) + ' (' + ago(n.lastData) + ' siden)' : '-',
        'Firmware': n.fw || '-',
        'Reset': n.reset || '-',
        'Sikkerhed': n.security || '-',
        'RSSI [dBm]': (n.rssi === undefined) ? '-' : n.rssi,
        'MQTT-forbindelser': (n.connects === undefined) ? '-' : n.connects,
        'Interval [s]': (n.interval === undefined) ? '-' : n.interval,
        'Batch': (n.batch === undefined) ? '-' : n.batch,
        'Kommandoer i kø': q.length ? q.map((m) => m.name.split('/').pop() + '=' + m.value).join(', ') : '-'
    };
});

const edgesKey = Object.keys(info).sort().join(',');
let options = null;
if (edgesKey !== flow.get('dashEdgesKey')) {
    flow.set('dashEdgesKey', edgesKey);
    options = {
        options: [{ label: 'Alle noder', value: '' }]
            .concat(Object.keys(info).sort().map((e) => ({ label: e, value: e }))),
        payload: flow.get('dashTarget') || ''
    };
}

node.status({ fill: 'blue', shape: 'dot', text: rows.length + ' node(s)' });
return [{ payload: rows }, options];
