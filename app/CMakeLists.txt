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
    src/network.c
    src/power.c
)

target_include_directories(app PRIVATE src)
