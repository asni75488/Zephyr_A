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
assert.match(log.pop(), /^warn:No edge node online/);

// 2. One online, one offline edge node -> only the online one is addressed
store.edgeNodes = { 'esp32c6-14c19fe0ab24': { group: 'ProjektA', online: true }, 'esp32c6-old': { group: 'ProjektA', online: false } };
let out = run({ payload: { name: 'Config/Sample Interval', type: 'Int64', value: 5000 } });
assert.strictEqual(out[0].length, 1);
assert.strictEqual(out[0][0].topic, 'spBv1.0/ProjektA/NCMD/esp32c6-14c19fe0ab24');
assert.ok(Buffer.isBuffer(out[0][0].payload));
assert.deepStrictEqual(encoded.metrics, [{ name: 'Config/Sample Interval', type: 'Int64', value: 5000 }]);
assert.ok(encoded.timestamp > 1767225600000, 'NCMD carries a UTC timestamp');

// 3. Several metrics and an explicit target
out = run({ edge: 'esp32c6-old', payload: { metrics: [{ name: 'Config/Batch Size', type: 'Int64', value: 1 }, { name: 'Node Control/Rebirth', type: 'Boolean', value: true }] } });
assert.strictEqual(out[0][0].topic, 'spBv1.0/ProjektA/NCMD/esp32c6-old');
assert.strictEqual(encoded.metrics.length, 2);

// 4. Invalid metric -> error, nothing sent
assert.strictEqual(run({ payload: { name: 'Config/Batch Size', value: 3 } }), null);
assert.match(log.pop(), /^error:NCMD metric needs name, type and value/);

console.log('ncmd.js: all tests passed');
