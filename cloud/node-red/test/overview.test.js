// Unit test of cloud/node-red/overview.js (no Node-RED, no npm).
//   node cloud/node-red/test/overview.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const body = fs.readFileSync(path.join(__dirname, '..', 'overview.js'), 'utf8');
const fn = new Function('msg', 'node', 'flow', 'env', body);
const store = {};
const flow = { get: (k) => store[k], set: (k, v) => { store[k] = v; } };
const node = { status: () => {} };
const run = (msg) => fn(msg, node, flow, {});
const rec = (type, edge, metrics, device) => ({ payload: { type, edge, device: device || null, received: Date.now(), metrics } });

// 1. NBIRTH: row with properties, config and a dropdown update
let out = run(rec('NBIRTH', 'esp32c6-a', [
    { name: 'Properties/FW Version', value: '0.10.0-del10' },
    { name: 'Properties/Reset Cause', value: 'task watchdog (mqtt)' },
    { name: 'Properties/Security', value: 'sb=0 fe=0 jtag=1 dl=1 rng=sar' },
    { name: 'Status/RSSI', value: -71 },
    { name: 'Config/Sample Interval', value: 10000 },
    { name: 'Config/Batch Size', value: 6 }]));
assert.strictEqual(out[0].payload.length, 1);
const row = out[0].payload[0];
assert.strictEqual(row.Node, 'esp32c6-a');
assert.match(row.Status, /^online/);
assert.strictEqual(row.Firmware, '0.10.0-del10');
assert.strictEqual(row.Reset, 'task watchdog (mqtt)');
assert.strictEqual(row['Interval [s]'], 10);
assert.strictEqual(row['RSSI [dBm]'], -71);
assert.strictEqual(row['Sidste data'], '-');
assert.deepStrictEqual(out[1].options.map((o) => o.value), ['', 'esp32c6-a']);

// 2. DDATA updates "last data", the node list did not change -> no dropdown msg
out = run(rec('DDATA', 'esp32c6-a', [{ name: 'Temperature/Die', value: 24.5 }], 'sensors'));
assert.match(out[0].payload[0]['Sidste data'], /UTC \(\d+ s siden\)$/);
assert.strictEqual(out[1], null);

// 3. Second node, then NDEATH of the first; queued command is shown
store.pendingNcmd = { 'esp32c6-a': [{ name: 'Node Control/Sleep', value: 600 }] };
run(rec('NBIRTH', 'esp32c6-b', []));
out = run(rec('NDEATH', 'esp32c6-a', [{ name: 'bdSeq', value: 3 }]));
assert.deepStrictEqual(out[0].payload.map((r) => r.Node), ['esp32c6-a', 'esp32c6-b']);
assert.match(out[0].payload[0].Status, /^offline/);
assert.strictEqual(out[0].payload[0]['Kommandoer i kø'], 'Sleep=600');

// 4. NDATA after a configuration change; refresh only re-renders
run(rec('NDATA', 'esp32c6-b', [{ name: 'Config/Batch Size', value: 12 }, { name: 'Status/MQTT Connects', value: 2 }]));
out = run({ topic: 'refresh' });
assert.strictEqual(out[0].payload[1].Batch, 12);
assert.strictEqual(out[0].payload[1]['MQTT-forbindelser'], 2);
assert.strictEqual(out[1], null);

console.log('overview.js: all tests passed');
