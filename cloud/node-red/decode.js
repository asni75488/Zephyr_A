// Zephyr_A - Sparkplug B decoder (body of the Node-RED function node
// "Sparkplug B decode" in flows.json; kept here as a file so it can be
// reviewed and unit-tested: cloud/node-red/test/decode.test.js).
//
// In:  msg.topic   spBv1.0/<group>/<type>/<edge_node>[/<device>]
//      msg.payload Buffer (Sparkplug B protobuf)
// Out: [1] full record as JSON (data log file + debug)
//      [2] one message per numeric data metric (chart): topic=name,
//          payload=value, timestamp=metric timestamp (ms, UTC)
//      [3] status text for the dashboard
// "sparkplug" is the npm module sparkplug-payload (Setup -> Modules).

const parts = (msg.topic || '').split('/');
if (parts.length < 4 || parts[0] !== 'spBv1.0') {
    return null;
}
const [, group, type, edge, device] = parts;

let p;
try {
    p = sparkplug.get('spBv1.0').decodePayload(msg.payload);
} catch (err) {
    node.error('Sparkplug decode failed on ' + msg.topic + ': ' + err.message, msg);
    node.status({ fill: 'red', shape: 'ring', text: 'decode error ' + type });
    return null;
}

// 64-bit fields (timestamps, seq, bdSeq) are decoded as Long objects.
const num = (v) => {
    if (v === null || v === undefined) return v;
    if (typeof v === 'object' && typeof v.toNumber === 'function') return v.toNumber();
    if (typeof v === 'bigint') return Number(v);
    return v;
};

// Metric aliases: names are only sent in NBIRTH/DBIRTH; DATA uses aliases.
const key = group + '/' + edge + '/' + (device || '');
const aliases = flow.get('aliases') || {};
if (type === 'NBIRTH' || type === 'DBIRTH') {
    aliases[key] = {};
    for (const m of (p.metrics || [])) {
        if (m.alias !== undefined && m.alias !== null && m.name) {
            aliases[key][String(num(m.alias))] = m.name;
        }
    }
    flow.set('aliases', aliases);
}
const map = aliases[key] || {};

const ts = num(p.timestamp);
const metrics = (p.metrics || []).map((m) => {
    const alias = (m.alias !== undefined && m.alias !== null) ? num(m.alias) : undefined;
    return {
        name: m.name || map[String(alias)] || ('alias:' + alias),
        alias: alias,
        type: m.type,
        value: num(m.value),
        timestamp: (m.timestamp !== undefined && m.timestamp !== null) ? num(m.timestamp) : ts
    };
});

const record = {
    received: Date.now(),
    group: group,
    type: type,
    edge: edge,
    device: device || null,
    seq: num(p.seq),
    timestamp: ts,
    metrics: metrics
};

// Chart: numeric process values only (no control/bookkeeping metrics).
const isData = (type === 'DDATA' || type === 'NDATA' || type === 'DBIRTH');
const points = isData ? metrics
    .filter((m) => typeof m.value === 'number' &&
        !m.name.startsWith('Node Control/') && m.name !== 'bdSeq')
    .map((m) => ({ topic: m.name, payload: m.value, timestamp: m.timestamp })) : [];

const dataDir = env.get('ZEPHYR_A_DATA_DIR') || '/tmp';
const day = new Date().toISOString().slice(0, 10);

node.status({ fill: 'green', shape: 'dot', text: type + ' ' + edge + ' (' + metrics.length + ')' });

return [
    { payload: record, filename: dataDir + '/sparkplug-' + day + '.jsonl' },
    points,
    { payload: new Date(record.received).toISOString() + '  ' + type + '  ' + edge +
        (device ? '/' + device : '') + '  ' + metrics.length + ' metric(s)' }
];
