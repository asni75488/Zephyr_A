# Zephyr_A - sysbuild configuration
#
# Part 1: only the HP-core application (and MCUboot, the board default) are
# built. The LP-core image in remote/ is added in the LP-core part with
# ExternalZephyrProject_Add(), following Zephyr 4.4.0's
# samples/boards/espressif/ulp/lp_core/* samples.
#
# SPDX-License-Identifier: Apache-2.0
