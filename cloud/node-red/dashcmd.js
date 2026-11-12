// Zephyr_A - Node-RED function "Dashboard command" (part 10).
// Source of the function node in flows.json; unit test: test/dashcmd.test.js.
//
// Turns dashboard widgets into input for "Sparkplug B encode NCMD":
//   msg.topic 'target'   (ui-dropdown)  remember the selected edge node
//   msg.topic 'interval' (ui-slider, s) -> Config/Sample Interval [ms]
//   msg.topic 'batch'    (ui-slider)    -> Config/Batch Size
//   msg.topic 'command'  (ui-button)    msg.payload = { name, type, value }
// The same limits as the firmware are checked here, so a wrong value is
// rejected with a visible message instead of an NCMD the device ignores.
// Out: msg.payload = metric, msg.edge = target ('' = all known nodes)

const LIMITS = {
    'Config/Sample Interval': [5000, 15000],
    'Config/Batch Size': [1, 12],
    'Node Control/Sleep': [60, 86400]
};

if (msg.topic === 'target') {
    flow.set('dashTarget', msg.payload || '');
    node.status({ fill: 'grey', shape: 'ring', text: 'target: ' + (msg.payload || 'all') });
    return null;
}

let metric;
if (msg.topic === 'interval') {
    metric = { name: 'Config/Sample Interval', type: 'Int64', value: Math.round(Number(msg.payload) * 1000) };
} else if (msg.topic === 'batch') {
    metric = { name: 'Config/Batch Size', type: 'Int64', value: Math.round(Number(msg.payload)) };
} else if (msg.topic === 'command' && msg.payload && typeof msg.payload.name === 'string') {
    metric = msg.payload;
} else {
    node.warn('Unknown dashboard input: ' + msg.topic);
    return null;
}

const lim = LIMITS[metric.name];
if (lim && !(metric.value >= lim[0] && metric.value <= lim[1])) {
    node.error(metric.name + '=' + metric.value + ' outside ' + lim[0] + '..' + lim[1]);
    return null;
}

const target = flow.get('dashTarget') || '';
node.status({ fill: 'blue', shape: 'dot', text: metric.name + '=' + metric.value + ' -> ' + (target || 'all') });
const out = { payload: metric };
if (target) out.edge = target;
return out;
