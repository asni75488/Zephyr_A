# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - Project A (Industrial IoT, Micro Technic)
# HP-core application for ESP32-C6-DevKitC.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a LANGUAGES C)

# One .c/.h pair per module. New modules (network, mqtt_client, sparkplug,
# modbus, ...) are added here in later parts.
target_sources(app PRIVATE
    src/main.c
)

target_include_directories(app PRIVATE src)
