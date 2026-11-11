# SPDX-License-Identifier: Apache-2.0
#
# Zephyr_A - integrity reference for the LP-core image (part 9).
#
# MCUboot verifies the signature of the HP image only. The LP image lives in
# its own flash partition (slot0_lpcore_partition) and is copied into the LP
# SRAM by Zephyr at boot without any check. To detect a modified LP image,
# the signed HP image carries the SHA-256 and size of the LP image it was
# built with; power.c hashes the partition at start-up and stops the LP core
# on a mismatch (docs/09-strom-data.md, section 4.4).
#
# zephyr_a_add_lp_image_hash(<target> <lp zephyr.bin>)

function(zephyr_a_add_lp_image_hash target lp_bin)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/lp_image_hash.c)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DIN=${lp_bin} -DOUT=${out}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lp_image_hash_gen.cmake
    DEPENDS ${lp_bin} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lp_image_hash_gen.cmake
    COMMENT "SHA-256 of the LP-core image"
    VERBATIM)
  # The LP image is built by sysbuild before this image; CMake only needs to
  # know that the file is produced outside this build.
  set_source_files_properties(${lp_bin} PROPERTIES GENERATED TRUE)
  target_sources(${target} PRIVATE ${out})
endfunction()
