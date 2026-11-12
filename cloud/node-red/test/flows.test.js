// Consistency test of cloud/node-red/flows.json (part 10): the function
// nodes contain exactly the reviewed and unit-tested .js files, every wire
// and every dashboard group reference points to an existing node.
//   node cloud/node-red/test/flows.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const dir = path.join(__dirname, '..');
const flows = JSON.parse(fs.readFileSync(path.join(dir, 'flows.json'), 'utf8'));
const byName = Object.fromEntries(flows.filter((n) => n.type === 'function').map((n) => [n.name, n]));
const ids = new Set(flows.map((n) => n.id));

const sources = {
    'Sparkplug B decode': 'decode.js',
    'Sparkplug B encode NCMD': 'ncmd.js',
    'Edge node overview': 'overview.js',
    'Dashboard command': 'dashcmd.js'
};
for (const [name, file] of Object.entries(sources)) {
    assert.ok(byName[name], 'function node "' + name + '" missing');
    assert.strictEqual(byName[name].func, fs.readFileSync(path.join(dir, file), 'utf8'),
        'flows.json "' + name + '" differs from ' + file);
}
for (const n of flows) {
    for (const out of (n.wires || [])) {
        for (const w of out) assert.ok(ids.has(w), n.id + ' wired to missing ' + w);
    }
    if (n.group) assert.ok(ids.has(n.group), n.id + ': missing ui-group ' + n.group);
    if (n.type === 'ui-group') assert.ok(ids.has(n.page), n.id + ': missing ui-page');
}
// Every dashboard control ends in the NCMD encoder
const cmd = byName['Dashboard command'];
for (const n of flows.filter((x) => ['ui-button', 'ui-slider', 'ui-dropdown'].includes(x.type))) {
    assert.deepStrictEqual(n.wires, [[cmd.id]], n.name + ' must feed "Dashboard command"');
}
assert.deepStrictEqual(cmd.wires, [[byName['Sparkplug B encode NCMD'].id]]);
assert.ok(flows.find((n) => n.type === 'mqtt in').wires[0].includes(byName['Sparkplug B encode NCMD'].id),
    'NBIRTH must reach the NCMD encoder (queued commands, part 9)');
console.log('flows.json: ' + flows.length + ' nodes consistent');
