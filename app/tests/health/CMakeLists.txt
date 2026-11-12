# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - unit test of the task watchdog module (app/src/health.c) on
# native_sim with Zephyr's task_wdt (simulated time, no hardware fallback).
#   west build -b native_sim/native/64 app/tests/health -t run

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a_health_test LANGUAGES C)

set(ZEPHYR_A_APP ${CMAKE_CURRENT_LIST_DIR}/../..)
target_sources(app PRIVATE src/main.c ${ZEPHYR_A_APP}/src/health.c)
target_include_directories(app PRIVATE ${ZEPHYR_A_APP}/src ${ZEPHYR_A_APP}/common)
