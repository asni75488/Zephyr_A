# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - unit test of the LP-core decision logic (app/common/lp_shared.h)
# on the host (native_sim). The same inline functions run on the LP core.
#   west build -b native_sim/native/64 app/tests/lp_supervisor -t run

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a_lp_supervisor_test LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../common)
