# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - native_sim test of the MQTT/TLS client (mqtt_client.c) against a
# real Mosquitto broker on the host (Native Simulator offloaded sockets).
# Run with run.sh (it creates the test PKI and starts the broker).
#
# The TLS/MQTT Kconfig block is taken from app/prj.conf (between the
# ">>> mqtt-tls" and "<<< mqtt-tls" markers), so the test always uses exactly
# the firmware's TLS configuration.

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(ZEPHYR_A_APP ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)

file(READ ${ZEPHYR_A_APP}/prj.conf app_prj)
string(FIND "${app_prj}" "# >>> mqtt-tls" tls_begin)
string(FIND "${app_prj}" "# <<< mqtt-tls" tls_end)
if(tls_begin LESS 0 OR tls_end LESS 0)
  message(FATAL_ERROR "mqtt-tls markers not found in app/prj.conf")
endif()
math(EXPR tls_len "${tls_end} - ${tls_begin}")
string(SUBSTRING "${app_prj}" ${tls_begin} ${tls_len} tls_block)
set(tls_conf ${CMAKE_CURRENT_BINARY_DIR}/app-mqtt-tls.conf)
file(WRITE ${tls_conf} "${tls_block}")
list(PREPEND EXTRA_CONF_FILE ${tls_conf})

if(NOT DEFINED TEST_CA_DER)
  message(FATAL_ERROR "Pass -DTEST_CA_DER=<root.der>[;...] (run.sh does this)")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_a_mqtt_tls_test LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/stubs.c
    ${ZEPHYR_A_APP}/src/mqtt_client.c
    ${ZEPHYR_A_APP}/src/mqtt_selftest.c
    ${ZEPHYR_A_APP}/src/tls_ca.c
    ${ZEPHYR_A_APP}/src/sparkplug.c
)
target_include_directories(app PRIVATE ${ZEPHYR_A_APP}/src)
zephyr_include_directories(${ZEPHYR_A_APP}/config)

include(${ZEPHYR_A_APP}/cmake/ca_certs.cmake)
zephyr_a_add_ca_certs(app ${TEST_CA_DER})
