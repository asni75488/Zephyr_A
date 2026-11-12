// Unit test of cloud/node-red/ncmd.js (like decode.test.js: no Node-RED, no npm).
//   node cloud/node-red/test/ncmd.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const body = fs.readFileSync(path.join(__dirname, '..', 'ncmd.js'), 'utf8');
const fn = new Function('msg', 'node', 'flow', 'env', 'sparkplug', body);

const store = {};
const flow = { get: (k) => store[k], set: (k, v) => { store[k] = v; } };
const log = [];
const node = { status: () => {}, warn: (w) => log.push('warn:' + w), error: (e) => log.push('error:' + e) };
let encoded;
const sparkplug = { get: (ns) => { assert.strictEqual(ns, 'spBv1.0'); return { encodePayload: (p) => { encoded = p; return new Uint8Array([1, 2, 3]); } }; } };
const run = (msg) => fn(msg, node, flow, {}, sparkplug);

// 1. No edge node known yet -> warning, nothing sent
assert.strictEqual(run({ payload: { name: 'Node Control/Rebirth', type: 'Boolean', value: true } }), null);
assert.match(log.pop(), /^warn:No edge node known/);

// 2. One online, one offline edge node -> online one gets it, offline one queues it
store.edgeNodes = { 'esp32c6-14c19fe0ab24': { group: 'ProjektA', online: true }, 'esp32c6-old': { group: 'ProjektA', online: false } };
let out = run({ payload: { name: 'Config/Sample Interval', type: 'Int64', value: 5000 } });
assert.strictEqual(out[0].length, 1);
assert.strictEqual(out[0][0].topic, 'spBv1.0/ProjektA/NCMD/esp32c6-14c19fe0ab24');
assert.ok(Buffer.isBuffer(out[0][0].payload));
assert.deepStrictEqual(encoded.metrics, [{ name: 'Config/Sample Interval', type: 'Int64', value: 5000 }]);
assert.ok(encoded.timestamp > 1767225600000, 'NCMD carries a UTC timestamp');
assert.deepStrictEqual(store.pendingNcmd['esp32c6-old'], [{ name: 'Config/Sample Interval', type: 'Int64', value: 5000 }]);

// 3. Several metrics for the offline node: queued, latest value per metric wins
out = run({ edge: 'esp32c6-old', payload: { metrics: [{ name: 'Config/Sample Interval', type: 'Int64', value: 15000 }, { name: 'Node Control/Sleep', type: 'Int64', value: 600 }] } });
assert.strictEqual(out, null);
assert.deepStrictEqual(store.pendingNcmd['esp32c6-old'].map((m) => m.name + '=' + m.value),
    ['Config/Sample Interval=15000', 'Node Control/Sleep=600']);

// 4. Other Sparkplug traffic does not release the queue; the node's NBIRTH does
assert.strictEqual(run({ topic: 'spBv1.0/ProjektA/DDATA/esp32c6-old/sensors', payload: Buffer.from([0]) }), null);
assert.strictEqual(run({ topic: 'spBv1.0/ProjektA/NBIRTH/esp32c6-14c19fe0ab24', payload: Buffer.from([0]) }), null);
out = run({ topic: 'spBv1.0/ProjektA/NBIRTH/esp32c6-old', payload: Buffer.from([0]) });
assert.strictEqual(out[0][0].topic, 'spBv1.0/ProjektA/NCMD/esp32c6-old');
assert.strictEqual(encoded.metrics.length, 2);
assert.strictEqual(store.pendingNcmd['esp32c6-old'], undefined, 'queue emptied');
assert.strictEqual(run({ topic: 'spBv1.0/ProjektA/NBIRTH/esp32c6-old', payload: Buffer.from([0]) }), null, 'sent once');

// 5. Invalid metric -> error, nothing sent
assert.strictEqual(run({ payload: { name: 'Config/Batch Size', value: 3 } }), null);
assert.match(log.pop(), /^error:NCMD metric needs name, type and value/);

console.log('ncmd.js: all tests passed');
