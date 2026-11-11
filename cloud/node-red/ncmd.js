// Zephyr_A - Node-RED function "Sparkplug B encode NCMD" (part 7, queue part 9).
// Source of the function node in flows.json (kept here for review and the
// unit test in test/ncmd.test.js). External module: sparkplug-payload (Tahu).
//
// In:  msg.payload = one metric { name, type, value }
//                    or { metrics: [ { name, type, value }, ... ] }
//      msg.edge    = edge node id (optional; default: every known edge node,
//                    learned from NBIRTH/NDEATH by the decoder)
//   or a Sparkplug message from the MQTT-in node (msg.topic spBv1.0/...):
//      on an NBIRTH, the commands queued for that edge node are sent.
// Out: one MQTT message per edge node:
//      topic spBv1.0/<group>/NCMD/<edge>, payload = Sparkplug B protobuf
//
// Offline edge nodes (part 9): in the interval-uplink mode the node is online
// only for a minute every 30 min. A command for an offline node is queued
// (flow context "pendingNcmd", latest value per metric) and sent right after
// its next NBIRTH, while the node waits for commands (APP_UPLINK_LINGER_S).
// Sparkplug forbids retained NCMD, so the queue lives here.
//
// Commands the firmware understands (docs/07-sparkplug-tid.md, 09):
//   Node Control/Rebirth   Boolean true
//   Node Control/Reboot    Boolean true
//   Node Control/Sleep     Int64   60..86400 [s] deep sleep (LP core wakes it)
//   Config/Sample Interval Int64   5000..15000 [ms]
//   Config/Batch Size      Int64   1..12

const edges = flow.get('edgeNodes') || {};
const pending = flow.get('pendingNcmd') || {};

function encode(group, edge, metrics) {
    const payload = sparkplug.get('spBv1.0').encodePayload({ timestamp: Date.now(), metrics: metrics });
    return { topic: 'spBv1.0/' + group + '/NCMD/' + edge, payload: Buffer.from(payload) };
}

// NBIRTH from the MQTT-in node: deliver queued commands
if (typeof msg.topic === 'string' && msg.topic.startsWith('spBv1.0/')) {
    const t = msg.topic.split('/');
    if (t[2] !== 'NBIRTH' || !pending[t[3]]) {
        return null;
    }
    const metrics = pending[t[3]];
    delete pending[t[3]];
    flow.set('pendingNcmd', pending);
    node.status({ fill: 'green', shape: 'dot', text: 'queued command(s) sent to ' + t[3] });
    return [[encode(t[1], t[3], metrics)]];
}

const targets = msg.edge ? [msg.edge] : Object.keys(edges);
if (targets.length === 0) {
    node.warn('No edge node known yet (waiting for an NBIRTH)');
    return null;
}

const metrics = (msg.payload && Array.isArray(msg.payload.metrics)) ? msg.payload.metrics : [msg.payload];
for (const m of metrics) {
    if (!m || typeof m.name !== 'string' || typeof m.type !== 'string' || m.value === undefined) {
        node.error('NCMD metric needs name, type and value: ' + JSON.stringify(m));
        return null;
    }
}

const out = [];
const queued = [];
for (const e of targets) {
    if (edges[e] && edges[e].online) {
        out.push(encode(edges[e].group || 'ProjektA', e, metrics));
    } else {
        const q = (pending[e] || []).filter((p) => !metrics.some((m) => m.name === p.name));
        pending[e] = q.concat(metrics);
        queued.push(e);
    }
}
flow.set('pendingNcmd', pending);

const what = metrics.map((m) => m.name + '=' + m.value).join(', ');
node.status({
    fill: out.length ? 'blue' : 'yellow', shape: 'dot',
    text: what + (out.length ? ' -> ' + out.map((o) => o.topic.split('/')[3]).join(', ') : '') +
          (queued.length ? ' (queued for ' + queued.join(', ') + ')' : '')
});
return out.length ? [out] : null;
