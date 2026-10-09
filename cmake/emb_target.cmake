# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Helpers for executables built against the kernel.

# emb_add_executable(<name> <sources>...)
#   Links the kernel, the port, and the board; on embedded targets also emits a .hex,
#   a map file, and a size report; registers the image for the footprint tracker.
function(emb_add_executable name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE emb_board emb_kernel emb_arch emb_api emb_flags)
  target_link_options(${name} PRIVATE "-Wl,-Map=$<TARGET_FILE:${name}>.map")
  if(EMB_BOARD_LINKER_SCRIPT)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${EMB_BOARD_LINKER_SCRIPT}")
  endif()
  if(NOT CONFIG_EMB_ARCH STREQUAL "native")
    set_target_properties(${name} PROPERTIES SUFFIX ".elf")
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom "$<TARGET_FILE:${name}>" "$<TARGET_FILE_DIR:${name}>/${name}.hex"
      COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${name}>"
      COMMENT "hex and size: ${name}")
  endif()
  set_property(GLOBAL APPEND PROPERTY EMB_IMAGES "$<TARGET_FILE:${name}>")
endfunction()

# emb_add_test(<name> <sources>...)  a conformance or unit test executable: run by ctest
# directly on the native port (SPEC-013 §11), under the board's QEMU machine elsewhere.
function(emb_add_test name)
  emb_add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE emb_test)
  if(NOT CONFIG_EMB_ARCH STREQUAL "native")
    # the image runs under the board's QEMU machine; the runner reads the console (SIM-002)
    if(EMB_QEMU)
      string(REPLACE ";" " " _qargs "${EMB_BOARD_QEMU_ARGS}")
      add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/qemu/run_qemu.py"
               --qemu "${EMB_QEMU}" --qemu-args "${_qargs}" --quiet "$<TARGET_FILE:${name}>")
      set_tests_properties(${name} PROPERTIES TIMEOUT 400)
    endif()
  else()
    add_test(NAME ${name} COMMAND ${name})
    # misuse tests fork (TEST-010); ThreadSanitizer refuses threads after a fork unless told
    set_tests_properties(${name} PROPERTIES TIMEOUT 300 ENVIRONMENT "TSAN_OPTIONS=die_after_fork=0")
  endif()
endfunction()
