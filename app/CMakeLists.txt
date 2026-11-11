# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - Project A (Industrial IoT, Micro Technic)
# HP-core application for ESP32-C6-DevKitC.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a LANGUAGES C)

# One .c/.h pair per module (docs/02-arkitektur.md).
target_sources(app PRIVATE
    src/main.c
    src/device_info.c
    src/timesync.c
    src/sensor.c
    src/modbus.c
    src/app_logic.c
    src/sparkplug.c
    src/mqtt_client.c
    src/tls_ca.c
    src/network.c
    src/power.c
)
target_sources_ifdef(CONFIG_APP_MQTT_SELFTEST app PRIVATE src/mqtt_selftest.c)

target_include_directories(app PRIVATE src)

# Mbed TLS user configuration (CONFIG_MBEDTLS_USER_CONFIG_FILE) must be on the
# include path of the Mbed TLS library, not only of the app.
zephyr_include_directories(config)

# Trusted root CAs for the MQTT broker certificate (public certificates,
# see app/ca/README.md). Order = order of the TLS security tags.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ca_certs.cmake)
zephyr_a_add_ca_certs(app
    ${CMAKE_CURRENT_SOURCE_DIR}/ca/isrg-root-ye.der
    ${CMAKE_CURRENT_SOURCE_DIR}/ca/isrg-root-yr.der
    ${CMAKE_CURRENT_SOURCE_DIR}/ca/isrg-root-x2.der
    ${CMAKE_CURRENT_SOURCE_DIR}/ca/isrg-root-x1.der
)
