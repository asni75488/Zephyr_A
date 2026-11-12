// Unit test of cloud/node-red/dashcmd.js (no Node-RED, no npm).
//   node cloud/node-red/test/dashcmd.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const body = fs.readFileSync(path.join(__dirname, '..', 'dashcmd.js'), 'utf8');
const fn = new Function('msg', 'node', 'flow', 'env', body);
const store = {};
const flow = { get: (k) => store[k], set: (k, v) => { store[k] = v; } };
const log = [];
const node = { status: () => {}, warn: (w) => log.push('warn:' + w), error: (e) => log.push('error:' + e) };
const run = (msg) => fn(msg, node, flow, {});

// No target selected: command goes to all nodes (no msg.edge)
let out = run({ topic: 'command', payload: { name: 'Node Control/Rebirth', type: 'Boolean', value: true } });
assert.strictEqual(out.edge, undefined);
assert.strictEqual(out.payload.name, 'Node Control/Rebirth');

// Target from the dropdown
assert.strictEqual(run({ topic: 'target', payload: 'esp32c6-a' }), null);
out = run({ topic: 'interval', payload: 7 });
assert.deepStrictEqual(out, { payload: { name: 'Config/Sample Interval', type: 'Int64', value: 7000 }, edge: 'esp32c6-a' });
out = run({ topic: 'batch', payload: 12 });
assert.strictEqual(out.payload.value, 12);

// Limits as in the firmware
assert.strictEqual(run({ topic: 'interval', payload: 20 }), null);
assert.match(log.pop(), /^error:Config\/Sample Interval=20000 outside 5000\.\.15000/);
assert.strictEqual(run({ topic: 'command', payload: { name: 'Node Control/Sleep', type: 'Int64', value: 10 } }), null);
assert.match(log.pop(), /^error:Node Control\/Sleep=10 outside/);

// "All nodes" again
run({ topic: 'target', payload: '' });
out = run({ topic: 'command', payload: { name: 'Node Control/Sleep', type: 'Int64', value: 600 } });
assert.strictEqual(out.edge, undefined);

assert.strictEqual(run({ topic: 'foo', payload: 1 }), null);
assert.match(log.pop(), /^warn:Unknown dashboard input/);
console.log('dashcmd.js: all tests passed');
