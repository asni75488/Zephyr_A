# Security policy

Zephyr_A is a student project (UCL, Industrial IoT, Project A for Micro Technic):
firmware for an ESP32-C6 cellular edge node (Zephyr 4.4.0, MQTT/TLS, Sparkplug B)
and the scripts for its cloud server.

## Reporting a vulnerability

Please report security problems **privately** through GitHub:
**Security → Report a vulnerability** on this repository
(private vulnerability reporting). Do not open a public issue.

Please include the affected part (firmware, cloud scripts, Node-RED flow), the
version (`app/VERSION` or the `Properties/FW Version` metric) and how to
reproduce it. You will get an answer within 7 days. A fix is published as a
new signed firmware / script version, and the advisory is made public once the
fix is available (coordinated disclosure).

## Supported versions

Only the newest version on `main` is supported.

## Components

The software bill of materials of a firmware build is generated with
`scripts\build.ps1 -Sbom` (SPDX 2.3 in `build\spdx`). Main third-party
components: Zephyr RTOS 4.4.0, Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0,
MCUboot, nanopb 1.0.0, Espressif HAL; server: Ubuntu 24.04, Mosquitto 2.0,
Node-RED 5, Let's Encrypt certbot.

Security design and the mapping to IEC 62443-4-2 and the Cyber Resilience
Act: [docs/08-sikkerhed.md](docs/08-sikkerhed.md).
