# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Benchmarks (05 §4.4, R-003 §6): built for every port, published only from hardware runs.
emb_add_executable(bench_switch bench_switch.c)
if(CONFIG_EMB_ARCH_CORTEX_M OR CONFIG_EMB_ARCH_RISCV)
  emb_add_executable(bench_irq bench_irq.c) # the board's test line and a dynamic handler
endif()
# On a board (EMB_TEST_EMULATOR=hil) the benchmarks are ctest entries too, ending at BENCH_END
if(EMB_TEST_EMULATOR STREQUAL "hil" AND EMB_HIL_PORT)
  foreach(_bench bench_switch bench_irq)
    if(TARGET ${_bench})
      add_test(NAME ${_bench} COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/hil/run_hil.py"
               --hw-json "${EMB_BOARD_DIR}/hw.json" --port "${EMB_HIL_PORT}" --until BENCH_END
               --log "${CMAKE_CURRENT_BINARY_DIR}/${_bench}.console.log" $<TARGET_FILE:${_bench}>)
      set_tests_properties(${_bench} PROPERTIES RUN_SERIAL TRUE LABELS bench TIMEOUT 300)
    endif()
  endforeach()
endif()
