# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# Helpers for executables built against the kernel.

# emb_add_executable(<name> <sources>...)
#   Links the kernel, the port, and the board; on embedded targets also emits a .hex,
#   a map file, and a size report; registers the image for the footprint tracker. Adds
#   run_<name>, which runs the image: on the host for native, under the board's QEMU
#   machine elsewhere when there is one (Ctrl-A X leaves QEMU, A91).
set(EMB_STACK_ROOTS "" CACHE FILEPATH "The application's stack-roots.json, for the stack check after the link (A125)")

function(emb_add_executable name)
  _emb_add_image(${name} ${ARGN})
  if(EMB_SYSTEM)
    # the system's configuration folder (A130): SYS_Initialize() and the system's objects, the
    # tasks and SYS_Tasks(), the application's interrupts, and definitions.h and configuration.h
    target_sources(${name} PRIVATE "${EMB_BOARD_DIR}/initialization.c" "${EMB_BOARD_DIR}/tasks.c")
    if(EXISTS "${EMB_BOARD_DIR}/interrupts.c")
      target_sources(${name} PRIVATE "${EMB_BOARD_DIR}/interrupts.c")
    endif()
    target_include_directories(${name} PRIVATE "${EMB_BOARD_DIR}" "${EMB_SYSTEM_DIR}/src" "${EMB_SYSTEM_DIR}")
    _emb_stack_check(${name})
  endif()
  if(CONFIG_EMB_ARCH STREQUAL "native")
    add_custom_target(run_${name} COMMAND "$<TARGET_FILE:${name}>" DEPENDS ${name} USES_TERMINAL
                      COMMENT "run ${name}")
  elseif(EMB_QEMU AND EMB_BOARD_QEMU_ARGS)
    add_custom_target(run_${name}
                      COMMAND "${EMB_QEMU}" ${EMB_BOARD_QEMU_ARGS} "$<TARGET_FILE:${name}>" -nographic
                      DEPENDS ${name} USES_TERMINAL
                      COMMENT "run ${name} under QEMU (Ctrl-A X to leave)")
  endif()
endfunction()

# A125: each thread's stack against the worst case tools/footprint/stack_bound.py computes from
# the linked image; a stack below it, or a bound it cannot compute, fails the build. The tool
# reads Arm images, from GCC's -fstack-usage and the disassembly: elsewhere the build says the
# check did not run.
function(_emb_stack_check name)
  if(NOT CONFIG_EMB_ARCH STREQUAL "cortex_m")
    message(STATUS "${name}: the stack check after the link runs on Cortex-M images only; not on ${CONFIG_EMB_ARCH}")
    return()
  endif()
  if(NOT EMB_STACK_ROOTS)
    # the check needs the application's roots: its interrupt levels and indirect calls
    message(WARNING "${name}: no stack check after the link: set EMB_STACK_ROOTS to the application's "
                    "stack-roots.json (tools/footprint/stack_bound.py)")
    return()
  endif()
  get_filename_component(_roots "${EMB_STACK_ROOTS}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/footprint/stack_bound.py"
            --elf "$<TARGET_FILE:${name}>" --build "${CMAKE_BINARY_DIR}" --roots "${_roots}"
            --system "${EMB_BOARD_DIR}/docs/system.json" --objdump "${CMAKE_OBJDUMP}" --nm "${CMAKE_NM}"
            --json "$<TARGET_FILE:${name}>.stack.json"
    COMMENT "stack check: ${name}'s threads against their worst case (A125)")
endfunction()

# The image without a run target: emb_add_executable() and emb_add_test() share it.
function(_emb_add_image name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE emb_subsys emb_board emb_kernel emb_arch emb_api emb_flags)
  if(APPLE)
    # Apple ld64 spells the map option differently from GNU ld
    target_link_options(${name} PRIVATE "-Wl,-map,$<TARGET_FILE:${name}>.map")
  else()
    target_link_options(${name} PRIVATE "-Wl,-Map=$<TARGET_FILE:${name}>.map")
  endif()
  if(EMB_BOARD_LINKER_SCRIPT)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${EMB_BOARD_LINKER_SCRIPT}")
  endif()
  if(NOT CONFIG_EMB_ARCH STREQUAL "native")
    set_target_properties(${name} PROPERTIES SUFFIX ".elf")
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom "$<TARGET_FILE:${name}>" "$<TARGET_FILE_DIR:${name}>/${name}.hex"
      COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${name}>"
      COMMENT "hex and size: ${name}")
    if(EMB_BOARD_FLASH_IMAGE STREQUAL "esp32c3")
      # what the board's ROM boots from flash offset 0 (tools/esp/esp_image.py)
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/esp/esp_image.py"
                "$<TARGET_FILE:${name}>" -o "$<TARGET_FILE_DIR:${name}>/${name}.bin"
        COMMENT "flash image: ${name}.bin")
    elseif(EMB_BOARD_FLASH_IMAGE STREQUAL "esp32")
      # the classic ESP32's ROM boots it from flash offset 0x1000, each segment at its run address
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/esp/esp_image.py"
                "$<TARGET_FILE:${name}>" -o "$<TARGET_FILE_DIR:${name}>/${name}.bin" --chip esp32
        COMMENT "flash image: ${name}.bin")
    elseif(EMB_BOARD_FLASH_IMAGE STREQUAL "uf2")
      # what the boot ROM's USB drive takes (tools/uf2/uf2.py, A79)
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/uf2/uf2.py"
                "$<TARGET_FILE:${name}>" -o "$<TARGET_FILE_DIR:${name}>/${name}.uf2" --family ${EMB_BOARD_UF2_FAMILY}
        COMMENT "flash image: ${name}.uf2")
    endif()
  endif()
  set_property(GLOBAL APPEND PROPERTY EMB_IMAGES "$<TARGET_FILE:${name}>")
endfunction()

# emb_add_test(<name> <sources>...)  a conformance or unit test executable: run by ctest
# directly on the native port (SPEC-013 §11), under the board's QEMU machine or on its
# EmbSIM board elsewhere.
function(emb_add_test name)
  # the suite's name for the framework's evidence line (docs/engineering/case-evidence.md)
  set(_suite "${CMAKE_CURRENT_BINARY_DIR}/${name}_suite.c")
  file(GENERATE OUTPUT "${_suite}"
       CONTENT "/* Generated by emb_add_test(). */\n#include <emb_test.h>\n\nEMB_FLASH_CONST char emb_test_suite[] = \"${name}\";\n")
  _emb_add_image(${name} ${ARGN} "${_suite}")
  target_link_libraries(${name} PRIVATE emb_test)
  if(NOT CONFIG_EMB_ARCH STREQUAL "native")
    # the image runs on the board's emulator; the runner reads the console (SIM-002)
    if(EMB_TEST_EMULATOR STREQUAL "hil")
      # a physical board (tools/hil/run_hil.py): flash, reset, read the console; one at a time.
      # Each run's record goes to EMB_HIL_ARCHIVE (default <build>/hil-archive, A89).
      # a suite that runs longer than the runner's two minutes names its own wait, in seconds,
      # as EMB_HIL_TIMEOUT_<name> before emb_add_test()
      set(_emb_hil_wait "")
      set(_emb_ctest_wait 400)
      if(DEFINED EMB_HIL_TIMEOUT_${name})
        set(_emb_hil_wait --timeout ${EMB_HIL_TIMEOUT_${name}})
        math(EXPR _emb_ctest_wait "${EMB_HIL_TIMEOUT_${name}} + 120")
      endif()
      add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/hil/run_hil.py"
               --hw-json "${EMB_BOARD_HW_JSON}" --port "${EMB_HIL_PORT}"
               --log "${CMAKE_CURRENT_BINARY_DIR}/${name}.console.log" --archive "${EMB_HIL_ARCHIVE}"
               ${_emb_hil_wait} "$<TARGET_FILE:${name}>")
      set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE TIMEOUT ${_emb_ctest_wait})
    elseif(EMB_TEST_EMULATOR STREQUAL "embsim")
      if(EMB_EMBSIM AND EMB_BOARD_EMBSIM_TARGET)
        # the ELF, or the flash image where the board boots one
        set(_image "$<TARGET_FILE:${name}>")
        if(EMB_BOARD_FLASH_IMAGE)
          set(_image "$<TARGET_FILE_DIR:${name}>/${name}.bin")
        endif()
        # the core clock the board's setup must leave the MCU at, where the board names one
        set(_clock)
        if(EMB_BOARD_EMBSIM_CORE_HZ)
          set(_clock --core-hz ${EMB_BOARD_EMBSIM_CORE_HZ})
        endif()
        add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/embsim/run_embsim.py"
                 --embsim "${EMB_EMBSIM}" --target "${EMB_BOARD_EMBSIM_TARGET}" ${_clock}
                 ${EMB_EMBSIM_ARGS_${name}} "${_image}")
        set_tests_properties(${name} PROPERTIES TIMEOUT 400)
      endif()
    elseif(EMB_QEMU)
      string(REPLACE ";" " " _qargs "${EMB_BOARD_QEMU_ARGS}")
      add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}" -I "${EMB_BASE}/tools/qemu/run_qemu.py"
               --qemu "${EMB_QEMU}" --qemu-args "${_qargs}" "$<TARGET_FILE:${name}>")
      set_tests_properties(${name} PROPERTIES TIMEOUT 400)
    endif()
  else()
    add_test(NAME ${name} COMMAND ${name})
    # misuse tests fork (TEST-010); ThreadSanitizer refuses threads after a fork unless told
    set_tests_properties(${name} PROPERTIES TIMEOUT 300 ENVIRONMENT "TSAN_OPTIONS=die_after_fork=0")
  endif()
endfunction()
