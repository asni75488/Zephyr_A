/**
 * Zephyr_A - Node-RED settings overlay
 *
 * Installed to ~/.node-red/settings-zephyr_a.js by cloud/scripts/50-nodered.sh
 * and selected via ~/.node-red/environment:
 *     NODE_RED_OPTIONS=--settings /home/<user>/.node-red/settings-zephyr_a.js
 * (the nodered.service unit reads that file).
 *
 * It loads the default settings.js created by the Node-RED installer and
 * overrides only what this project needs, so Node-RED updates of settings.js
 * do not have to be merged by hand:
 *   - HTTPS with the Let's Encrypt certificate (copied by the certbot hook)
 *   - login for the editor (adminAuth) and for the dashboard (httpNodeAuth)
 *   - a fixed credentialSecret, so stored credentials (MQTT password) stay
 *     encrypted and readable after restarts
 *   - external modules in function nodes (sparkplug-payload decoder)
 *
 * Secrets (password hashes, credentialSecret) are read from
 * ~/.node-red/zephyr_a-secrets.json (chmod 600, created by the script,
 * never committed to git).
 */

const fs = require('fs');
const path = require('path');

const base = require('./settings.js');
const secrets = JSON.parse(fs.readFileSync(path.join(__dirname, 'zephyr_a-secrets.json'), 'utf8'));
const certDir = path.join(__dirname, 'certs');

module.exports = Object.assign({}, base, {
    // Listen on all interfaces; UFW allows 1880/tcp. HTTPS only.
    uiHost: '0.0.0.0',
    uiPort: 1880,
    https: function () {
        return {
            key: fs.readFileSync(path.join(certDir, 'privkey.pem')),
            cert: fs.readFileSync(path.join(certDir, 'fullchain.pem'))
        };
    },

    // Editor + admin API: login required.
    adminAuth: {
        type: 'credentials',
        users: [{
            username: secrets.adminUser,
            password: secrets.adminHash,
            permissions: '*'
        }]
    },

    // HTTP endpoints and Dashboard 2.0 (served via httpNode): basic auth.
    httpNodeAuth: {
        user: secrets.dashUser,
        pass: secrets.dashHash
    },

    // Encrypts flows_cred.json (e.g. the MQTT broker password).
    credentialSecret: secrets.credentialSecret,

    // Function nodes may use npm modules (Setup tab -> Modules).
    functionExternalModules: true,

    // No usage data is sent to the Node-RED project.
    telemetry: Object.assign({}, base.telemetry || {}, { enabled: false }),

    // Hide the welcome tour on first login.
    editorTheme: Object.assign({}, base.editorTheme || {}, { tours: false })
});
