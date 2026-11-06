# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
if(CONFIG_EMB_ARCH STREQUAL "avr")
  set(_platform platform_avr.c)
elseif(CONFIG_EMB_ARCH STREQUAL "native")
  set(_platform platform_native.c)
else()
  set(_platform platform_target.c)
endif()
add_library(emb_test STATIC emb_test.c ${_platform})
target_include_directories(emb_test PUBLIC .)
target_link_libraries(emb_test PUBLIC emb_api emb_arch emb_board PRIVATE emb_flags)

# Fault checks run on a board whose reset keeps RAM in this run (EMB_BOARD_RESETS_IN): the
# fault resets it and the next boot replays the case (A89). Native forks instead.
set(_emb_runner qemu)
if(EMB_TEST_EMULATOR STREQUAL "embsim" OR EMB_TEST_EMULATOR STREQUAL "hil")
  set(_emb_runner ${EMB_TEST_EMULATOR})
endif()
if(NOT CONFIG_EMB_ARCH STREQUAL "native" AND _emb_runner IN_LIST EMB_BOARD_RESETS_IN)
  target_compile_definitions(emb_test PUBLIC EMB_TEST_FAULT_BY_RESET=1)
endif()
