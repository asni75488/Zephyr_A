// Zephyr_A - Node-RED function "Sparkplug B encode NCMD" (part 7).
// Source of the function node in flows.json (kept here for review and the
// unit test in test/ncmd.test.js). External module: sparkplug-payload (Tahu).
//
// In:  msg.payload = one metric { name, type, value }
//                    or { metrics: [ { name, type, value }, ... ] }
//      msg.edge    = edge node id (optional; default: every edge node that is
//                    online, learned from NBIRTH/NDEATH by the decoder)
// Out: one MQTT message per edge node:
//      topic spBv1.0/<group>/NCMD/<edge>, payload = Sparkplug B protobuf
//
// Commands the firmware understands (docs/07-sparkplug-tid.md):
//   Node Control/Rebirth   Boolean true
//   Node Control/Reboot    Boolean true
//   Config/Sample Interval Int64   5000..15000 [ms]
//   Config/Batch Size      Int64   1..12

const edges = flow.get('edgeNodes') || {};
const targets = msg.edge ? [msg.edge] : Object.keys(edges).filter((e) => edges[e].online);
if (targets.length === 0) {
    node.warn('No edge node online yet (waiting for an NBIRTH)');
    return null;
}

const metrics = (msg.payload && Array.isArray(msg.payload.metrics)) ? msg.payload.metrics : [msg.payload];
for (const m of metrics) {
    if (!m || typeof m.name !== 'string' || typeof m.type !== 'string' || m.value === undefined) {
        node.error('NCMD metric needs name, type and value: ' + JSON.stringify(m));
        return null;
    }
}

const payload = sparkplug.get('spBv1.0').encodePayload({ timestamp: Date.now(), metrics: metrics });
const out = targets.map((e) => ({
    topic: 'spBv1.0/' + ((edges[e] && edges[e].group) || 'ProjektA') + '/NCMD/' + e,
    payload: Buffer.from(payload)
}));
node.status({ fill: 'blue', shape: 'dot', text: metrics.map((m) => m.name + '=' + m.value).join(', ') + ' -> ' + targets.join(', ') });
return [out];
