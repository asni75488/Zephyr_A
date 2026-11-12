# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - sysbuild configuration
#
# Images: MCUboot (board default), the HP-core application (app/) and, since
# part 9, the LP-core image (app/remote/, board SB_CONFIG_ULP_REMOTE_BOARD),
# following Zephyr 4.4.0's samples/boards/espressif/ulp/lp_core/*.
#
# The LP image is flashed to its own partition (slot0_lpcore_partition) and
# loaded into the LP SRAM by the HP image at boot.

ExternalZephyrProject_Add(
    APPLICATION zephyr_a_lpcore
    SOURCE_DIR ${APP_DIR}/remote
    BOARD ${SB_CONFIG_ULP_REMOTE_BOARD}
)

# The HP image embeds the SHA-256 of the LP image (app/cmake/lp_image.cmake),
# so the LP image must be built first.
add_dependencies(${DEFAULT_IMAGE} zephyr_a_lpcore)
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} zephyr_a_lpcore)
sysbuild_add_dependencies(FLASH zephyr_a_lpcore ${DEFAULT_IMAGE})

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  # MCUboot first
  sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} mcuboot)
endif()
