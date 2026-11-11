# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - LP-core (ULP) image, part 9 (docs/09-strom-data.md).
# Built by sysbuild (app/sysbuild.cmake) for esp32c6_devkitc/esp32c6/lpcore,
# like Zephyr 4.4.0's samples/boards/espressif/ulp/lp_core/*.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a_lpcore LANGUAGES C)

target_sources(app PRIVATE src/main_ULP.c)

# lp_shared.h: memory layout and decision logic shared with the HP core
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
