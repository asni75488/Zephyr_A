#!/usr/bin/env node
/*
 * Zephyr_A - Sparkplug B test publisher (simulates the ESP32-C6 edge node).
 *
 * Connects like the device will in part 6/7: MQTT over TLS on port 8883,
 * certificate verified against the system CA store and the host name,
 * user name = edge node id, Last Will = NDEATH. Publishes NBIRTH + DBIRTH,
 * then collects a simulated temperature every SAMPLE_S seconds and sends a
 * batch of BATCH samples as one DDATA (aliases + own timestamps), exactly
 * like the firmware design (docs/02-arkitektur.md section 8).
 *
 * On the server:
 *   cd ~/Zephyr_A/cloud/tools && npm install
 *   MQTT_USER=esp32c6-test0001 node spb-test-publisher.js
 *   (asks for the password; or set MQTT_PASS)
 *
 * Environment:
 *   MQTT_HOST (zephyra.vps.webdock.cloud)  MQTT_PORT (8883)
 *   MQTT_USER (esp32c6-test0001)           MQTT_PASS (prompted if empty)
 *   GROUP (ProjektA)  DEVICE (sensors)  SAMPLE_S (10)  BATCH (6)  RUN_S (0 = forever)
 */
'use strict';

const mqtt = require('mqtt');
const readline = require('readline');
const sparkplug = require('sparkplug-payload').get('spBv1.0');

const cfg = {
    host: process.env.MQTT_HOST || 'zephyra.vps.webdock.cloud',
    port: Number(process.env.MQTT_PORT || 8883),
    user: process.env.MQTT_USER || 'esp32c6-test0001',
    pass: process.env.MQTT_PASS || '',
    group: process.env.GROUP || 'ProjektA',
    device: process.env.DEVICE || 'sensors',
    sampleS: Number(process.env.SAMPLE_S || 10),
    batch: Number(process.env.BATCH || 6),
    runS: Number(process.env.RUN_S || 0)
};

const edge = cfg.user; // edge node id == MQTT user name (ACL)
const topic = (type, dev) =>
    `spBv1.0/${cfg.group}/${type}/${edge}` + (dev ? `/${dev}` : '');
const ALIAS_TEMP = 1;

let bdSeq = 0;
let seq = 0;
const nextSeq = () => { const s = seq; seq = (seq + 1) % 256; return s; };
const encode = (obj) => Buffer.from(sparkplug.encodePayload(obj));

function askPassword() {
    return new Promise((resolve) => {
        const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
        rl.question(`Password for ${cfg.user}: `, (answer) => { rl.close(); resolve(answer); });
    });
}

async function main() {
    if (!cfg.pass) {
        cfg.pass = await askPassword();
    }

    const ndeath = encode({
        timestamp: Date.now(),
        metrics: [{ name: 'bdSeq', type: 'UInt64', value: bdSeq }]
    });

    console.log(`Connecting to mqtts://${cfg.host}:${cfg.port} as ${cfg.user} ...`);
    const client = mqtt.connect(`mqtts://${cfg.host}:${cfg.port}`, {
        username: cfg.user,
        password: cfg.pass,
        clientId: edge,
        protocolVersion: 4,
        clean: true,
        keepalive: 60,
        reconnectPeriod: 0,
        rejectUnauthorized: true, // verify the Let's Encrypt certificate + host name
        will: { topic: topic('NDEATH'), payload: ndeath, qos: 1, retain: false }
    });

    client.on('error', (err) => {
        console.error('MQTT error:', err.message);
        process.exit(1);
    });

    client.on('connect', () => {
        const sock = client.stream;
        const tls = sock && typeof sock.getProtocol === 'function' ? sock.getProtocol() : '?';
        const cipher = sock && typeof sock.getCipher === 'function' ? sock.getCipher().name : '?';
        console.log(`Connected (${tls}, ${cipher}), certificate verified`);

        seq = 0;
        const now = Date.now();
        client.subscribe(topic('NCMD'), { qos: 0 });
        client.publish(topic('NBIRTH'), encode({
            timestamp: now, seq: nextSeq(),
            metrics: [
                { name: 'bdSeq', type: 'UInt64', value: bdSeq },
                { name: 'Node Control/Rebirth', type: 'Boolean', value: false },
                { name: 'Properties/FW Version', type: 'String', value: 'test-publisher' }
            ]
        }));
        client.publish(topic('DBIRTH', cfg.device), encode({
            timestamp: now, seq: nextSeq(),
            metrics: [{ name: 'Temperature/Die', alias: ALIAS_TEMP, type: 'Float', value: 22.0, timestamp: now }]
        }));
        console.log(`NBIRTH + DBIRTH sent (bdSeq ${bdSeq})`);

        let samples = [];
        let t = 0;
        const timer = setInterval(() => {
            t++;
            samples.push({ ts: Date.now(), value: Math.round((22 + 2 * Math.sin(t / 6)) * 100) / 100 });
            if (samples.length >= cfg.batch) {
                const payload = encode({
                    timestamp: Date.now(), seq: nextSeq(),
                    metrics: samples.map((s) => ({ alias: ALIAS_TEMP, type: 'Float', value: s.value, timestamp: s.ts }))
                });
                client.publish(topic('DDATA', cfg.device), payload, { qos: 0 });
                console.log(`DDATA: ${samples.length} samples, ${payload.length} bytes protobuf, ` +
                    `values ${samples.map((s) => s.value).join(', ')}`);
                samples = [];
            }
        }, cfg.sampleS * 1000);

        if (cfg.runS > 0) {
            setTimeout(() => { clearInterval(timer); client.end(false, () => process.exit(0)); }, cfg.runS * 1000);
        }
    });

    client.on('message', (t, payload) => {
        try {
            console.log(`Received ${t}:`, JSON.stringify(sparkplug.decodePayload(payload)));
        } catch (e) {
            console.log(`Received ${t} (not Sparkplug): ${payload.toString()}`);
        }
    });

    process.on('SIGINT', () => {
        console.log('\nCtrl+C: clean disconnect (no NDEATH from the broker - send it ourselves)');
        client.publish(topic('NDEATH'), ndeath, { qos: 1 }, () => client.end(false, () => process.exit(0)));
    });
}

main();
