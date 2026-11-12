// Unit test of cloud/node-red/decode.js without Node-RED and without npm:
// runs the function body exactly like Node-RED's function node does (as
// the body of a function with msg, node, flow, env, sparkplug), with a stub
// decoder that returns the object shape documented in Eclipse Tahu's
// sparkplug-payload (lib/sparkplugbpayload.ts: decodePayload/decodeMetric).
//   node cloud/node-red/test/decode.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const body = fs.readFileSync(path.join(__dirname, '..', 'decode.js'), 'utf8');
const fn = new Function('msg', 'node', 'flow', 'env', 'sparkplug', body);

// Long-like object as produced by protobufjs for uint64 fields
const L = (n) => ({ toNumber: () => n });

const store = {};
const flow = { get: (k) => store[k], set: (k, v) => { store[k] = v; } };
const statuses = [];
const node = { status: (s) => statuses.push(s), error: (e) => { throw new Error(e); } };
const env = { get: (k) => (k === 'ZEPHYR_A_DATA_DIR' ? '/home/admin/zephyr_a-data' : undefined) };
let next;
const sparkplug = { get: (ns) => { assert.strictEqual(ns, 'spBv1.0'); return { decodePayload: () => next }; } };
const run = (topic, decoded) => { next = decoded; return fn({ topic, payload: Buffer.alloc(0) }, node, flow, env, sparkplug); };

// 1. DBIRTH defines alias 1 = Temperature/Die
let out = run('spBv1.0/ProjektA/DBIRTH/esp32c6-test0001/sensors', {
    timestamp: L(1794300000000), seq: L(1),
    metrics: [{ name: 'Temperature/Die', alias: L(1), type: 'Float', value: 24.5, timestamp: L(1794300000000) }]
});
assert.strictEqual(out[0].payload.type, 'DBIRTH');
assert.strictEqual(out[1].length, 1);
assert.deepStrictEqual(store.aliases['ProjektA/esp32c6-test0001/sensors'], { 1: 'Temperature/Die' });

// 2. DDATA batch: 3 metrics with alias only and own timestamps (batching)
out = run('spBv1.0/ProjektA/DDATA/esp32c6-test0001/sensors', {
    timestamp: L(1794300060000), seq: L(2),
    metrics: [10000, 20000, 30000].map((dt, i) => ({ alias: L(1), type: 'Float', value: 24 + i, timestamp: L(1794300000000 + dt) }))
});
assert.strictEqual(out[1].length, 3, 'one chart point per metric');
assert.deepStrictEqual(out[1].map((m) => m.topic), ['Temperature/Die', 'Temperature/Die', 'Temperature/Die']);
assert.deepStrictEqual(out[1].map((m) => m.timestamp), [1794300010000, 1794300020000, 1794300030000]);
assert.deepStrictEqual(out[1].map((m) => m.payload), [24, 25, 26]);
assert.strictEqual(out[0].payload.seq, 2);
assert.match(out[0].filename, /^\/home\/admin\/zephyr_a-data\/sparkplug-\d{4}-\d{2}-\d{2}\.jsonl$/);
JSON.stringify(out[0].payload); // must be serialisable for the file node

// 3. NBIRTH: control metrics are logged but not charted
out = run('spBv1.0/ProjektA/NBIRTH/esp32c6-test0001', {
    timestamp: L(1794300000000), seq: L(0),
    metrics: [{ name: 'bdSeq', type: 'UInt64', value: L(0) }, { name: 'Node Control/Rebirth', type: 'Boolean', value: false }]
});
assert.strictEqual(out[1].length, 0);
assert.strictEqual(out[0].payload.metrics[0].value, 0, 'Long converted to number');

// 4. Unknown alias (DATA before BIRTH) is kept visible, metric without timestamp gets payload timestamp
out = run('spBv1.0/ProjektA/DDATA/esp32c6-other002/sensors', {
    timestamp: L(1794300099000), metrics: [{ alias: L(7), type: 'Int32', value: 5 }]
});
assert.strictEqual(out[1][0].topic, 'alias:7');
assert.strictEqual(out[1][0].timestamp, 1794300099000);

// 5. NDATA from the edge node: only Status/RSSI is charted, Config/Uptime are
//    logged; a message without any timestamp gets the receive time
const before = Date.now();
out = run('spBv1.0/ProjektA/NDATA/esp32c6-test0001', {
    seq: L(3), metrics: [{ alias: L(7), type: 'Int32', value: -71 }, { alias: L(8), type: 'Int64', value: L(600) },
        { name: 'Config/Sample Interval', type: 'Int64', value: L(5000) }]
});
assert.deepStrictEqual(out[1], [], 'alias unknown before NBIRTH: not charted');
assert.strictEqual(out[0].payload.metrics.length, 3, 'all metrics logged');
assert.ok(out[0].payload.timestamp >= before && out[0].payload.timestamp <= Date.now(), 'receive time used');
assert.strictEqual(out[0].payload.timestamp, out[0].payload.received);

// 5b. Same after an NBIRTH with names/aliases: RSSI resolves and is charted
run('spBv1.0/ProjektA/NBIRTH/esp32c6-test0001', {
    timestamp: L(1794300000000), seq: L(0),
    metrics: [{ name: 'bdSeq', type: 'Int64', value: L(1) }, { name: 'Status/RSSI', alias: L(7), type: 'Int32', value: -70 }]
});
assert.strictEqual(store.edgeNodes['esp32c6-test0001'].online, true, 'edge node registered for NCMD');
out = run('spBv1.0/ProjektA/NDATA/esp32c6-test0001', {
    timestamp: L(1794300600000), seq: L(1), metrics: [{ alias: L(7), type: 'Int32', value: -71 }, { alias: L(8), type: 'Int64', value: L(600) }]
});
assert.deepStrictEqual(out[1].map((m) => [m.topic, m.payload]), [['Status/RSSI', -71]]);

// 5c. DDATA with a bad-quality value (is_null -> null) is logged but not charted
out = run('spBv1.0/ProjektA/DDATA/esp32c6-test0001/sensors', {
    timestamp: L(1794300660000), seq: L(2),
    metrics: [{ alias: L(1), type: 'Float', value: null }, { alias: L(1), type: 'Float', value: 23.5 }]
});
assert.deepStrictEqual(out[1].map((m) => m.payload), [23.5]);
assert.strictEqual(out[0].payload.metrics[0].value, null);

// 5d. NDEATH marks the edge node offline
run('spBv1.0/ProjektA/NDEATH/esp32c6-test0001', { metrics: [{ name: 'bdSeq', type: 'Int64', value: L(1) }] });
assert.strictEqual(store.edgeNodes['esp32c6-test0001'].online, false);
assert.strictEqual(statuses[statuses.length - 1].fill, 'red');

// 6. Non-Sparkplug topic is ignored
assert.strictEqual(run('foo/bar', {}), null);

console.log('decode.js: all tests passed (' + statuses.length + ' status updates)');
