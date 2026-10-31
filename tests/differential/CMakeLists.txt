# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The differential runner (TEST-008): native only, needs the trace sink and every object.
if(CONFIG_EMB_ARCH_NATIVE AND CONFIG_EMB_TRACE AND CONFIG_EMB_SEM AND CONFIG_EMB_MUTEX AND CONFIG_EMB_NOTIFY)
  emb_add_executable(diff_runner diff_runner.c)
  target_link_libraries(diff_runner PRIVATE emb_kernel_private) # periodic idle timeout peek
  find_package(Python3 COMPONENTS Interpreter QUIET)
  # The model counts delivered ticks; a tickless quantum starts from the counter, so the
  # bridge skips every scenario there (bridge.applicable) and the checks are not registered.
  if(Python3_Interpreter_FOUND AND NOT (CONFIG_EMB_TIME_SLICING AND CONFIG_EMB_TICKLESS))
    add_test(NAME differential_bridge
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/model/bridge.py"
                     --runner "$<TARGET_FILE:diff_runner>" --runs 6 --seed 1)
    set_tests_properties(differential_bridge PROPERTIES TIMEOUT 600)
    add_test(NAME differential_timer_regressions
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/model/bridge.py"
                     --runner "$<TARGET_FILE:diff_runner>" --regressions)
    set_tests_properties(differential_timer_regressions PROPERTIES TIMEOUT 600)
    add_test(NAME differential_evidence
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/model/check_evidence.py"
                     --runner "$<TARGET_FILE:diff_runner>")
    set_tests_properties(differential_evidence PROPERTIES TIMEOUT 600)
  endif()
endif()
